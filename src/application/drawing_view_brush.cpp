#include "autobbox/application/drawing_view_brush.h"

#include "autobbox/common/strings.h"
#include "autobbox/ui/message_dialog.h"

#include <ProArray.h>
#include <ProDrawing.h>
#include <ProDrawingView.h>
#include <ProGraphic.h>
#include <ProSelbuffer.h>
#include <ProSelection.h>
#include <ProToolkit.h>
#include <ProView.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cwctype>
#include <cstring>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace autobbox::application {

namespace {

constexpr double kMatrixEpsilon = 1.0e-5;

void LogLine(const Drawing3LogSink &log_sink, const char *fmt, ...)
{
    if (!log_sink || fmt == nullptr) {
        return;
    }

    char buffer[4096] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_sink(buffer);
}

void IdentityMatrix(ProMatrix matrix)
{
    std::memset(matrix, 0, sizeof(ProMatrix));
    matrix[0][0] = 1.0;
    matrix[1][1] = 1.0;
    matrix[2][2] = 1.0;
    matrix[3][3] = 1.0;
}

void CopyMatrix(const ProMatrix src, ProMatrix dst)
{
    std::memcpy(dst, src, sizeof(ProMatrix));
}

bool NormalizeViewMatrix(const ProMatrix in, ProMatrix out)
{
    ProMatrix orthonormal = {{0}};
    if (ProMatrixMakeOrthonormal(const_cast<double (*)[4]>(in), 1.0, orthonormal) == PRO_TK_NO_ERROR) {
        CopyMatrix(orthonormal, out);
    } else {
        CopyMatrix(in, out);
    }

    for (int r = 0; r < 3; ++r) {
        double len = 0.0;
        for (int c = 0; c < 3; ++c) {
            len += out[r][c] * out[r][c];
        }
        len = std::sqrt(len);
        if (len < 1.0e-9) {
            return false;
        }
        for (int c = 0; c < 3; ++c) {
            out[r][c] /= len;
        }
        out[r][3] = 0.0;
    }
    out[3][0] = 0.0;
    out[3][1] = 0.0;
    out[3][2] = 0.0;
    out[3][3] = 1.0;
    return true;
}

void FillFrontMatrix(ProMatrix matrix)
{
    IdentityMatrix(matrix);
}

void FillRightMatrix(ProMatrix matrix)
{
    std::memset(matrix, 0, sizeof(ProMatrix));
    matrix[0][2] = -1.0;
    matrix[1][1] = 1.0;
    matrix[2][0] = 1.0;
    matrix[3][3] = 1.0;
}

void FillLeftMatrix(ProMatrix matrix)
{
    std::memset(matrix, 0, sizeof(ProMatrix));
    matrix[0][2] = 1.0;
    matrix[1][1] = 1.0;
    matrix[2][0] = -1.0;
    matrix[3][3] = 1.0;
}

void FillTopMatrix(ProMatrix matrix)
{
    std::memset(matrix, 0, sizeof(ProMatrix));
    matrix[0][0] = 1.0;
    matrix[1][2] = 1.0;
    matrix[2][1] = -1.0;
    matrix[3][3] = 1.0;
}

void FillBottomMatrix(ProMatrix matrix)
{
    std::memset(matrix, 0, sizeof(ProMatrix));
    matrix[0][0] = 1.0;
    matrix[1][2] = -1.0;
    matrix[2][1] = 1.0;
    matrix[3][3] = 1.0;
}

void FillBackMatrix(ProMatrix matrix)
{
    std::memset(matrix, 0, sizeof(ProMatrix));
    matrix[0][0] = -1.0;
    matrix[1][1] = 1.0;
    matrix[2][2] = -1.0;
    matrix[3][3] = 1.0;
}

void FillIsoMatrix(ProMatrix matrix)
{
    matrix[0][0] = 0.707107;
    matrix[0][1] = -0.408103;
    matrix[0][2] = 0.577453;
    matrix[0][3] = 0.0;
    matrix[1][0] = -6.52932e-8;
    matrix[1][1] = 0.816642;
    matrix[1][2] = 0.577145;
    matrix[1][3] = 0.0;
    matrix[2][0] = -0.707107;
    matrix[2][1] = -0.408103;
    matrix[2][2] = 0.577453;
    matrix[2][3] = 0.0;
    matrix[3][0] = 0.0;
    matrix[3][1] = 0.0;
    matrix[3][2] = 0.0;
    matrix[3][3] = 1.0;
}

void Cross3(const double a[3], const double b[3], double out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

bool Normalize3(double v[3])
{
    const double len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len < 1.0e-9) {
        return false;
    }
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
    return true;
}

void FillSymmetricIsoMatrix(bool z_up, double first_sign, double second_sign, ProMatrix matrix)
{
    ProMatrix base = {{0}};
    FillIsoMatrix(base);
    std::memset(matrix, 0, sizeof(ProMatrix));

    // User requirement: "Z向上 右前" must be exactly the same as the Creo ISO
    // matrix used by the existing ISO command.  The other manual axonometric
    // directions are symmetric versions derived from that same matrix by
    // flipping model-axis columns.  For Y-up variants, swap the model Y/Z
    // columns first so the same base ISO orientation is expressed with +Y as
    // the named upward axis, then flip X/Z columns for the four quadrants.
    for (int r = 0; r < 4; ++r) {
        if (z_up) {
            matrix[r][0] = first_sign * base[r][0];
            matrix[r][1] = second_sign * base[r][1];
            matrix[r][2] = base[r][2];
        } else {
            matrix[r][0] = first_sign * base[r][0];
            matrix[r][1] = base[r][2];
            matrix[r][2] = second_sign * base[r][1];
        }
        matrix[r][3] = base[r][3];
    }
}

void FillPresetMatrix(DrawingViewBrushPreset preset, ProMatrix matrix)
{
    switch (preset) {
    case DrawingViewBrushPreset::Back:
        FillBackMatrix(matrix);
        break;
    case DrawingViewBrushPreset::Right:
        FillRightMatrix(matrix);
        break;
    case DrawingViewBrushPreset::Left:
        FillLeftMatrix(matrix);
        break;
    case DrawingViewBrushPreset::Top:
        FillTopMatrix(matrix);
        break;
    case DrawingViewBrushPreset::Bottom:
        FillBottomMatrix(matrix);
        break;
    case DrawingViewBrushPreset::Iso:
        FillIsoMatrix(matrix);
        break;
    case DrawingViewBrushPreset::IsoZUpNE:
        FillSymmetricIsoMatrix(true, 1.0, 1.0, matrix);
        break;
    case DrawingViewBrushPreset::IsoZUpNW:
        FillSymmetricIsoMatrix(true, -1.0, 1.0, matrix);
        break;
    case DrawingViewBrushPreset::IsoZUpSW:
        FillSymmetricIsoMatrix(true, -1.0, -1.0, matrix);
        break;
    case DrawingViewBrushPreset::IsoZUpSE:
        FillSymmetricIsoMatrix(true, 1.0, -1.0, matrix);
        break;
    case DrawingViewBrushPreset::IsoYUpNE:
        FillSymmetricIsoMatrix(false, 1.0, 1.0, matrix);
        break;
    case DrawingViewBrushPreset::IsoYUpNW:
        FillSymmetricIsoMatrix(false, -1.0, 1.0, matrix);
        break;
    case DrawingViewBrushPreset::IsoYUpSW:
        FillSymmetricIsoMatrix(false, -1.0, -1.0, matrix);
        break;
    case DrawingViewBrushPreset::IsoYUpSE:
        FillSymmetricIsoMatrix(false, 1.0, -1.0, matrix);
        break;
    case DrawingViewBrushPreset::Front:
    default:
        FillFrontMatrix(matrix);
        break;
    }
}

std::wstring PresetLabel(DrawingViewBrushPreset preset)
{
    switch (preset) {
    case DrawingViewBrushPreset::Back:
        return L"back / -X+Y";
    case DrawingViewBrushPreset::Right:
        return L"right / -Z+Y";
    case DrawingViewBrushPreset::Left:
        return L"left / +Z+Y";
    case DrawingViewBrushPreset::Top:
        return L"top / +X+Z";
    case DrawingViewBrushPreset::Bottom:
        return L"bottom / +X-Z";
    case DrawingViewBrushPreset::Iso:
        return L"iso";
    case DrawingViewBrushPreset::IsoZUpNE:
        return L"轴测 Z向上 右前 / Z-up +X+Y";
    case DrawingViewBrushPreset::IsoZUpNW:
        return L"轴测 Z向上 左前 / Z-up -X+Y";
    case DrawingViewBrushPreset::IsoZUpSW:
        return L"轴测 Z向上 左后 / Z-up -X-Y";
    case DrawingViewBrushPreset::IsoZUpSE:
        return L"轴测 Z向上 右后 / Z-up +X-Y";
    case DrawingViewBrushPreset::IsoYUpNE:
        return L"轴测 Y向上 右前 / Y-up +X+Z";
    case DrawingViewBrushPreset::IsoYUpNW:
        return L"轴测 Y向上 左前 / Y-up -X+Z";
    case DrawingViewBrushPreset::IsoYUpSW:
        return L"轴测 Y向上 左后 / Y-up -X-Z";
    case DrawingViewBrushPreset::IsoYUpSE:
        return L"轴测 Y向上 右后 / Y-up +X-Z";
    case DrawingViewBrushPreset::Front:
    default:
        return L"front / +X+Y";
    }
}

double MatrixRotationDistance(const ProMatrix lhs, const ProMatrix rhs)
{
    double total = 0.0;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            const double diff = lhs[r][c] - rhs[r][c];
            total += diff * diff;
        }
    }
    return std::sqrt(total);
}

std::wstring LabelForMatrix(const ProMatrix matrix)
{
    DrawingViewBrushPreset best = DrawingViewBrushPreset::Front;
    double best_dist = 1.0e18;
    for (DrawingViewBrushPreset preset : {
             DrawingViewBrushPreset::Front,
             DrawingViewBrushPreset::Back,
             DrawingViewBrushPreset::Right,
             DrawingViewBrushPreset::Left,
             DrawingViewBrushPreset::Top,
             DrawingViewBrushPreset::Bottom,
             DrawingViewBrushPreset::Iso,
             DrawingViewBrushPreset::IsoZUpNE,
             DrawingViewBrushPreset::IsoZUpNW,
             DrawingViewBrushPreset::IsoZUpSW,
             DrawingViewBrushPreset::IsoZUpSE,
             DrawingViewBrushPreset::IsoYUpNE,
             DrawingViewBrushPreset::IsoYUpNW,
             DrawingViewBrushPreset::IsoYUpSW,
             DrawingViewBrushPreset::IsoYUpSE
         }) {
        ProMatrix candidate = {{0}};
        FillPresetMatrix(preset, candidate);
        ProMatrix norm = {{0}};
        NormalizeViewMatrix(candidate, norm);
        const double dist = MatrixRotationDistance(matrix, norm);
        if (dist < best_dist) {
            best_dist = dist;
            best = preset;
        }
    }

    if (best_dist < 0.05) {
        return PresetLabel(best);
    }
    return L"custom / matrix";
}

bool IsOrthographicPreset(DrawingViewBrushPreset preset)
{
    switch (preset) {
    case DrawingViewBrushPreset::Front:
    case DrawingViewBrushPreset::Back:
    case DrawingViewBrushPreset::Right:
    case DrawingViewBrushPreset::Left:
    case DrawingViewBrushPreset::Top:
    case DrawingViewBrushPreset::Bottom:
        return true;
    default:
        return false;
    }
}

bool MatrixIsCloseToOrthographic(const ProMatrix matrix)
{
    for (DrawingViewBrushPreset preset : {
             DrawingViewBrushPreset::Front,
             DrawingViewBrushPreset::Back,
             DrawingViewBrushPreset::Right,
             DrawingViewBrushPreset::Left,
             DrawingViewBrushPreset::Top,
             DrawingViewBrushPreset::Bottom
         }) {
        ProMatrix candidate = {{0}};
        ProMatrix normalized = {{0}};
        FillPresetMatrix(preset, candidate);
        NormalizeViewMatrix(candidate, normalized);
        if (MatrixRotationDistance(matrix, normalized) < 0.08) {
            return true;
        }
    }
    return false;
}

bool MatrixIsCloseToAxonometricPreset(const ProMatrix matrix)
{
    for (DrawingViewBrushPreset preset : {
             DrawingViewBrushPreset::Iso,
             DrawingViewBrushPreset::IsoZUpNE,
             DrawingViewBrushPreset::IsoZUpNW,
             DrawingViewBrushPreset::IsoZUpSW,
             DrawingViewBrushPreset::IsoZUpSE,
             DrawingViewBrushPreset::IsoYUpNE,
             DrawingViewBrushPreset::IsoYUpNW,
             DrawingViewBrushPreset::IsoYUpSW,
             DrawingViewBrushPreset::IsoYUpSE
         }) {
        ProMatrix candidate = {{0}};
        ProMatrix normalized = {{0}};
        FillPresetMatrix(preset, candidate);
        NormalizeViewMatrix(candidate, normalized);
        if (MatrixRotationDistance(matrix, normalized) < 0.35) {
            return true;
        }
    }
    return false;
}

bool MatrixLooksOblique3d(const ProMatrix matrix)
{
    int significant_depth_components = 0;
    for (int c = 0; c < 3; ++c) {
        if (std::fabs(matrix[2][c]) > 0.25) {
            ++significant_depth_components;
        }
    }
    if (significant_depth_components >= 2) {
        return true;
    }

    int mixed_screen_axes = 0;
    for (int r = 0; r < 3; ++r) {
        int significant = 0;
        for (int c = 0; c < 3; ++c) {
            if (std::fabs(matrix[r][c]) > 0.25) {
                ++significant;
            }
        }
        if (significant >= 2) {
            ++mixed_screen_axes;
        }
    }
    return mixed_screen_axes >= 2;
}

std::string ViewNameForLog(ProDrawing drawing, ProView view)
{
    ProName name = {0};
    if (ProDrawingViewNameGet(drawing, view, name) == PRO_TK_NO_ERROR && name[0] != L'\0') {
        return autobbox::common::WToA(name);
    }
    return std::string("<unnamed>");
}

std::wstring ViewNameWide(ProDrawing drawing, ProView view)
{
    ProName name = {0};
    if (ProDrawingViewNameGet(drawing, view, name) == PRO_TK_NO_ERROR && name[0] != L'\0') {
        return std::wstring(name);
    }
    return {};
}

std::wstring UpperWide(std::wstring value)
{
    for (wchar_t &ch : value) {
        ch = static_cast<wchar_t>(std::towupper(ch));
    }
    return value;
}

bool NameHintsAxonometric(const std::wstring &name)
{
    const std::wstring upper = UpperWide(name);
    return upper.find(L"ISO") != std::wstring::npos ||
           upper.find(L"ISOMETRIC") != std::wstring::npos ||
           upper.find(L"AXON") != std::wstring::npos ||
           upper.find(L"AXONOMETRIC") != std::wstring::npos ||
           upper.find(L"\u8f74\u6d4b") != std::wstring::npos ||
           upper.find(L"\u7b49\u8f74") != std::wstring::npos;
}

bool GetDrawingViewNormalizedMatrix(ProDrawing drawing, ProView view, ProMatrix matrix, const Drawing3LogSink &log_sink)
{
    ProSolid solid = nullptr;
    const ProError st_solid = ProDrawingViewSolidGet(drawing, view, &solid);
    if (st_solid != PRO_TK_NO_ERROR || solid == nullptr) {
        LogLine(log_sink,
                "view-brush axon-detect fail view=%s reason=solid status=%d",
                ViewNameForLog(drawing, view).c_str(),
                static_cast<int>(st_solid));
        return false;
    }

    ProMatrix raw_matrix = {{0}};
    const ProError st_matrix = ProViewMatrixGet(reinterpret_cast<ProMdl>(solid), view, raw_matrix);
    if (st_matrix != PRO_TK_NO_ERROR || !NormalizeViewMatrix(raw_matrix, matrix)) {
        LogLine(log_sink,
                "view-brush axon-detect fail view=%s reason=matrix status=%d",
                ViewNameForLog(drawing, view).c_str(),
                static_cast<int>(st_matrix));
        return false;
    }
    return true;
}

bool IsAxonometricDrawingView(ProDrawing drawing, ProView view, const Drawing3LogSink &log_sink)
{
    ProViewType type = PRO_VIEW_GENERAL;
    if (ProDrawingViewTypeGet(drawing, view, &type) != PRO_TK_NO_ERROR || type != PRO_VIEW_GENERAL) {
        return false;
    }

    ProMatrix matrix = {{0}};
    if (!GetDrawingViewNormalizedMatrix(drawing, view, matrix, log_sink)) {
        return false;
    }
    if (MatrixIsCloseToOrthographic(matrix)) {
        return false;
    }

    if (NameHintsAxonometric(ViewNameWide(drawing, view))) {
        return true;
    }

    return MatrixIsCloseToAxonometricPreset(matrix) || MatrixLooksOblique3d(matrix);
}

enum class ReferenceConfirmResult {
    Accept,
    Retry,
    Cancel
};

ReferenceConfirmResult ConfirmReferenceOrientationEx(const DrawingViewBrushOrientation &orientation)
{
    ProUIMessageButton *buttons = nullptr;
    ProUIMessageButton choice = PRO_UI_MESSAGE_CANCEL;
    if (ProArrayAlloc(0, sizeof(ProUIMessageButton), 1, reinterpret_cast<ProArray *>(&buttons)) != PRO_TK_NO_ERROR ||
        buttons == nullptr) {
        return ReferenceConfirmResult::Cancel;
    }
    ProUIMessageButton ok_button = PRO_UI_MESSAGE_OK;
    ProUIMessageButton retry_button = PRO_UI_MESSAGE_RETRY;
    ProUIMessageButton cancel_button = PRO_UI_MESSAGE_CANCEL;
    ProArrayObjectAdd(reinterpret_cast<ProArray *>(&buttons), PRO_VALUE_UNUSED, 1, &ok_button);
    ProArrayObjectAdd(reinterpret_cast<ProArray *>(&buttons), PRO_VALUE_UNUSED, 1, &retry_button);
    ProArrayObjectAdd(reinterpret_cast<ProArray *>(&buttons), PRO_VALUE_UNUSED, 1, &cancel_button);

    std::wstring message = L"\u53c2\u8003\u89c6\u5411\uff1a";
    message += orientation.label.empty() ? L"custom / matrix" : orientation.label;
    message += L"\n\nOK = \u4f7f\u7528\u8be5\u89c6\u5411\nRetry = \u91cd\u65b0\u9009\u53c2\u8003\u89c6\u56fe\nCancel = \u9000\u51fa";

    ProUIMessageDialogDisplay(
        PROUIMESSAGE_INFO,
        const_cast<wchar_t *>(L"\u89c6\u56fe\u5237"),
        const_cast<wchar_t *>(message.c_str()),
        buttons,
        PRO_UI_MESSAGE_OK,
        &choice);
    ProArrayFree(reinterpret_cast<ProArray *>(&buttons));

    if (choice == PRO_UI_MESSAGE_OK) {
        return ReferenceConfirmResult::Accept;
    }
    if (choice == PRO_UI_MESSAGE_RETRY) {
        return ReferenceConfirmResult::Retry;
    }
    return ReferenceConfirmResult::Cancel;
}

bool ExtractViewFromSelection(ProSelection selection, ProView &view)
{
    view = nullptr;
    if (selection == nullptr) {
        return false;
    }
    return ProSelectionViewGet(selection, &view) == PRO_TK_NO_ERROR && view != nullptr;
}

bool CaptureReferenceOnce(ProDrawing drawing,
                          int sheet,
                          DrawingViewBrushMode mode,
                          DrawingViewBrushOrientation &orientation,
                          bool &cancelled,
                          const Drawing3LogSink &log_sink)
{
    cancelled = false;
    autobbox::ui::ShowSimpleMessageDialog(
        PROUIMESSAGE_INFO,
        L"\u89c6\u56fe\u5237",
        mode == DrawingViewBrushMode::AxonometricView
            ? L"\u8bf7\u76f4\u63a5\u70b9\u9009\u53c2\u8003\u8f74\u6d4b\u56fe\uff0c\u7136\u540e\u5b8c\u6210\u9009\u62e9\u3002"
            : L"\u8bf7\u76f4\u63a5\u70b9\u9009\u53c2\u8003\u4e3b\u89c6\u56fe\uff0c\u7136\u540e\u5b8c\u6210\u9009\u62e9\u3002");

    // Official selection option evidence:
    // D:\Program Files\PTC\Creo 10.0.8.0\Common Files\protoolkit\protkdoc\manual0\selobj.html
    // lists Drawing view -> "dwg_view" -> PRO_VIEW.
    // Official sample evidence:
    // D:\Program Files\PTC\Creo 10.0.8.0\Common Files\protoolkit\protk_appls\pt_examples\pt_graphics\TestDrwView.c
    // uses ProSelect("dwg_view") followed by ProSelectionViewGet().
    char selection_filter[] = "dwg_view";
    ProSelection *selection_array = nullptr;
    int selection_count = 0;
    const ProError st_select =
        ProSelect(selection_filter, 1, nullptr, nullptr, nullptr, nullptr, &selection_array, &selection_count);
    LogLine(log_sink,
            "view-brush reference-select status=%d count=%d filter='%s'",
            static_cast<int>(st_select),
            selection_count,
            selection_filter);
    if (st_select == PRO_TK_USER_ABORT || selection_count == 0 || selection_array == nullptr) {
        cancelled = true;
        return false;
    }
    if (st_select != PRO_TK_NO_ERROR) {
        return false;
    }

    ProView ref_view = nullptr;
    const bool has_view = ExtractViewFromSelection(selection_array[0], ref_view);
    // Do not call ProSelectionarrayFree() for ProSelect() output. Existing
    // project pattern in quick_rename.cpp records that ProSelect returns static
    // selection storage that Creo reuses on later calls.
    if (!has_view || ref_view == nullptr) {
        LogLine(log_sink, "view-brush reference-select fail reason=no-view");
        return false;
    }

    int view_sheet = 0;
    const ProError st_sheet = ProDrawingViewSheetGet(drawing, ref_view, &view_sheet);
    if (st_sheet != PRO_TK_NO_ERROR || view_sheet != sheet) {
        LogLine(log_sink,
                "view-brush reference-select fail reason=sheet-mismatch status=%d view_sheet=%d current_sheet=%d",
                static_cast<int>(st_sheet),
                view_sheet,
                sheet);
        return false;
    }

    ProViewType type = PRO_VIEW_GENERAL;
    const ProError st_type = ProDrawingViewTypeGet(drawing, ref_view, &type);
    if (st_type != PRO_TK_NO_ERROR || type != PRO_VIEW_GENERAL) {
        LogLine(log_sink,
                "view-brush reference-select fail reason=not-general status=%d type=%d view=%s",
                static_cast<int>(st_type),
                static_cast<int>(type),
                ViewNameForLog(drawing, ref_view).c_str());
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u89c6\u56fe\u5237",
            L"\u53c2\u8003\u89c6\u56fe\u4e0d\u662f\u4e3b\u89c6\u56fe\uff08General View\uff09\uff0c\u8bf7\u91cd\u65b0\u9009\u62e9\u3002");
        return false;
    }

    const bool is_axon = IsAxonometricDrawingView(drawing, ref_view, log_sink);
    if (mode == DrawingViewBrushMode::MainView && is_axon) {
        LogLine(log_sink,
                "view-brush reference-select fail reason=axon-in-main-mode view=%s",
                ViewNameForLog(drawing, ref_view).c_str());
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u89c6\u56fe\u5237",
            L"\u4e3b\u89c6\u56fe\u6a21\u5f0f\u4e0b\u53c2\u8003\u89c6\u56fe\u4e0d\u80fd\u662f\u8f74\u6d4b\u56fe\uff0c\u8bf7\u91cd\u65b0\u9009\u62e9\u666e\u901a\u4e3b\u89c6\u56fe\u3002");
        return false;
    }
    if (mode == DrawingViewBrushMode::AxonometricView && !is_axon) {
        LogLine(log_sink,
                "view-brush reference-select fail reason=non-axon-in-axon-mode view=%s",
                ViewNameForLog(drawing, ref_view).c_str());
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u89c6\u56fe\u5237",
            L"\u8f74\u6d4b\u56fe\u6a21\u5f0f\u4e0b\u53c2\u8003\u89c6\u56fe\u5fc5\u987b\u662f\u8f74\u6d4b\u56fe\uff0c\u8bf7\u91cd\u65b0\u9009\u62e9\u3002");
        return false;
    }

    ProSolid solid = nullptr;
    const ProError st_solid = ProDrawingViewSolidGet(drawing, ref_view, &solid);
    if (st_solid != PRO_TK_NO_ERROR || solid == nullptr) {
        LogLine(log_sink,
                "view-brush reference-select fail reason=solid status=%d view=%s",
                static_cast<int>(st_solid),
                ViewNameForLog(drawing, ref_view).c_str());
        return false;
    }

    ProMatrix raw_matrix = {{0}};
    const ProError st_matrix = ProViewMatrixGet(reinterpret_cast<ProMdl>(solid), ref_view, raw_matrix);
    if (st_matrix != PRO_TK_NO_ERROR || !NormalizeViewMatrix(raw_matrix, orientation.matrix)) {
        LogLine(log_sink,
                "view-brush reference-select fail reason=matrix status=%d view=%s",
                static_cast<int>(st_matrix),
                ViewNameForLog(drawing, ref_view).c_str());
        return false;
    }

    orientation.label = LabelForMatrix(orientation.matrix);
    orientation.valid = true;
    LogLine(log_sink,
            "view-brush reference captured view=%s label=%s",
            ViewNameForLog(drawing, ref_view).c_str(),
            autobbox::common::WToA(orientation.label.c_str()).c_str());
    return true;
}

bool IsUserCancelled(ProError status, int selection_count, ProSelection *selection_array)
{
    return status == PRO_TK_USER_ABORT || selection_count == 0 || selection_array == nullptr;
}

DrawingViewBrushSummary SummaryFromTargets(const DrawingViewBrushTargetSet &targets)
{
    DrawingViewBrushSummary summary = {};
    summary.sheet = targets.sheet;
    summary.selected_total = targets.selected_total;
    summary.valid_views = targets.valid_views;
    summary.main_views_found = targets.main_views_found;
    summary.derived_resolved_to_main = targets.derived_resolved_to_main;
    summary.skipped_axonometric = targets.skipped_axonometric;
    summary.skipped_non_axonometric = targets.skipped_non_axonometric;
    summary.skipped_non_general = targets.skipped_non_general;
    summary.skipped_other_sheet = targets.skipped_other_sheet;
    summary.skipped_duplicate = targets.skipped_duplicate;
    summary.failed = targets.failed;
    summary.last_error = targets.last_error;
    return summary;
}

DrawingViewBrushTargetSet TargetsFromSummaryAndViews(const DrawingViewBrushSummary &summary,
                                                     std::vector<ProView> &&views)
{
    DrawingViewBrushTargetSet targets = {};
    targets.sheet = summary.sheet;
    targets.selected_total = summary.selected_total;
    targets.valid_views = summary.valid_views;
    targets.main_views_found = summary.main_views_found;
    targets.derived_resolved_to_main = summary.derived_resolved_to_main;
    targets.skipped_axonometric = summary.skipped_axonometric;
    targets.skipped_non_axonometric = summary.skipped_non_axonometric;
    targets.skipped_non_general = summary.skipped_non_general;
    targets.skipped_other_sheet = summary.skipped_other_sheet;
    targets.skipped_duplicate = summary.skipped_duplicate;
    targets.failed = summary.failed;
    targets.last_error = summary.last_error;
    targets.main_views = std::move(views);
    return targets;
}

bool ResolveMainDrawingView(ProDrawing drawing,
                            ProView selected_view,
                            ProView &main_view,
                            bool &resolved_from_derived,
                            const Drawing3LogSink &log_sink)
{
    main_view = nullptr;
    resolved_from_derived = false;
    if (drawing == nullptr || selected_view == nullptr) {
        return false;
    }

    ProView current = selected_view;
    for (int depth = 0; depth < 16 && current != nullptr; ++depth) {
        ProViewType type = PRO_VIEW_GENERAL;
        const ProError st_type = ProDrawingViewTypeGet(drawing, current, &type);
        if (st_type != PRO_TK_NO_ERROR) {
            LogLine(log_sink,
                    "view-brush resolve-main fail view=%s reason=type status=%d depth=%d",
                    ViewNameForLog(drawing, current).c_str(),
                    static_cast<int>(st_type),
                    depth);
            return false;
        }

        if (type == PRO_VIEW_GENERAL) {
            main_view = current;
            resolved_from_derived = (current != selected_view);
            return true;
        }

        ProView parent = nullptr;
        const ProError st_parent = ProDrawingViewParentGet(drawing, current, &parent);
        if (st_parent != PRO_TK_NO_ERROR || parent == nullptr) {
            LogLine(log_sink,
                    "view-brush resolve-main fail view=%s reason=no-general-parent status=%d type=%d depth=%d",
                    ViewNameForLog(drawing, current).c_str(),
                    static_cast<int>(st_parent),
                    static_cast<int>(type),
                    depth);
            return false;
        }
        current = parent;
    }

    LogLine(log_sink,
            "view-brush resolve-main fail view=%s reason=parent-depth-limit",
            ViewNameForLog(drawing, selected_view).c_str());
    return false;
}

void CollectMainViewsFromSelections(ProDrawing drawing,
                                    int sheet,
                                    DrawingViewBrushMode mode,
                                    ProSelection *selection_array,
                                    int selection_count,
                                    DrawingViewBrushSummary &summary,
                                    std::vector<ProView> &main_views,
                                    const Drawing3LogSink &log_sink)
{
    std::unordered_set<std::uintptr_t> seen_selection_views;
    std::unordered_set<std::uintptr_t> seen_main_views;
    main_views.clear();
    main_views.reserve(static_cast<size_t>(std::max(0, selection_count)));

    for (int i = 0; i < selection_count; ++i) {
        ProView selected_view = nullptr;
        if (!ExtractViewFromSelection(selection_array[i], selected_view) || selected_view == nullptr) {
            LogLine(log_sink, "view-brush skip selection=%d reason=no-view", i);
            continue;
        }

        const std::uintptr_t selected_key = reinterpret_cast<std::uintptr_t>(selected_view);
        if (!seen_selection_views.insert(selected_key).second) {
            ++summary.skipped_duplicate;
            LogLine(log_sink, "view-brush skip selection=%d reason=duplicate-selected-view", i);
            continue;
        }

        int view_sheet = 0;
        const ProError st_sheet = ProDrawingViewSheetGet(drawing, selected_view, &view_sheet);
        if (st_sheet != PRO_TK_NO_ERROR || view_sheet != sheet) {
            ++summary.skipped_other_sheet;
            LogLine(log_sink,
                    "view-brush skip view=%s reason=sheet status=%d view_sheet=%d current_sheet=%d",
                    ViewNameForLog(drawing, selected_view).c_str(),
                    static_cast<int>(st_sheet),
                    view_sheet,
                    sheet);
            continue;
        }
        ++summary.valid_views;

        if (mode == DrawingViewBrushMode::AxonometricView) {
            ProViewType selected_type = PRO_VIEW_GENERAL;
            const ProError st_type = ProDrawingViewTypeGet(drawing, selected_view, &selected_type);
            if (st_type != PRO_TK_NO_ERROR || selected_type != PRO_VIEW_GENERAL) {
                ++summary.skipped_non_general;
                LogLine(log_sink,
                        "view-brush skip view=%s reason=axon-mode-not-general status=%d type=%d",
                        ViewNameForLog(drawing, selected_view).c_str(),
                        static_cast<int>(st_type),
                        static_cast<int>(selected_type));
                continue;
            }
            if (!IsAxonometricDrawingView(drawing, selected_view, log_sink)) {
                ++summary.skipped_non_axonometric;
                LogLine(log_sink,
                        "view-brush skip view=%s reason=not-axonometric",
                        ViewNameForLog(drawing, selected_view).c_str());
                continue;
            }

            const std::uintptr_t target_key = reinterpret_cast<std::uintptr_t>(selected_view);
            if (!seen_main_views.insert(target_key).second) {
                ++summary.skipped_duplicate;
                LogLine(log_sink, "view-brush skip view=%s reason=duplicate-axon-view",
                        ViewNameForLog(drawing, selected_view).c_str());
                continue;
            }
            ++summary.main_views_found;
            main_views.push_back(selected_view);
            continue;
        }

        ProView main_view = nullptr;
        bool resolved_from_derived = false;
        if (!ResolveMainDrawingView(drawing, selected_view, main_view, resolved_from_derived, log_sink) ||
            main_view == nullptr) {
            ++summary.skipped_non_general;
            continue;
        }

        if (IsAxonometricDrawingView(drawing, main_view, log_sink)) {
            ++summary.skipped_axonometric;
            LogLine(log_sink,
                    "view-brush skip view=%s reason=main-mode-axonometric main=%s",
                    ViewNameForLog(drawing, selected_view).c_str(),
                    ViewNameForLog(drawing, main_view).c_str());
            continue;
        }

        const std::uintptr_t main_key = reinterpret_cast<std::uintptr_t>(main_view);
        if (!seen_main_views.insert(main_key).second) {
            ++summary.skipped_duplicate;
            LogLine(log_sink,
                    "view-brush skip view=%s reason=duplicate-main-view main=%s",
                    ViewNameForLog(drawing, selected_view).c_str(),
                    ViewNameForLog(drawing, main_view).c_str());
            continue;
        }

        if (resolved_from_derived) {
            ++summary.derived_resolved_to_main;
            LogLine(log_sink,
                    "view-brush target view=%s resolved-main=%s",
                    ViewNameForLog(drawing, selected_view).c_str(),
                    ViewNameForLog(drawing, main_view).c_str());
        }

        ++summary.main_views_found;
        main_views.push_back(main_view);
    }
}

void BrushMainViews(ProDrawing drawing,
                    int sheet,
                    const DrawingViewBrushOrientation &orientation,
                    const std::vector<ProView> &main_views,
                    DrawingViewBrushSummary &summary,
                    const Drawing3LogSink &log_sink)
{
    for (ProView view : main_views) {
        ProSolid solid = nullptr;
        const ProError st_solid = ProDrawingViewSolidGet(drawing, view, &solid);
        if (st_solid != PRO_TK_NO_ERROR || solid == nullptr) {
            ++summary.failed;
            summary.last_error = st_solid;
            LogLine(log_sink,
                    "view-brush fail view=%s reason=solid status=%d",
                    ViewNameForLog(drawing, view).c_str(),
                    static_cast<int>(st_solid));
            continue;
        }

        ProMatrix matrix = {{0}};
        CopyMatrix(orientation.matrix, matrix);
        const ProError st_set = ProViewMatrixSet(reinterpret_cast<ProMdl>(solid), view, matrix);
        if (st_set != PRO_TK_NO_ERROR) {
            ++summary.failed;
            summary.last_error = st_set;
            LogLine(log_sink,
                    "view-brush fail view=%s reason=matrix-set status=%d",
                    ViewNameForLog(drawing, view).c_str(),
                    static_cast<int>(st_set));
            continue;
        }

        ++summary.brushed;
        LogLine(log_sink,
                "view-brush brushed view=%s label=%s",
                ViewNameForLog(drawing, view).c_str(),
                autobbox::common::WToA(orientation.label.c_str()).c_str());
    }

    if (summary.brushed > 0) {
        // Official ProDrawing.h documents that ProDwgSheetRegenerate regenerates
        // the displayed sheet. Regenerating the sheet lets projected/derived
        // child views follow the updated general-view orientation relationship.
        const ProError st_regen = ProDwgSheetRegenerate(drawing, sheet);
        summary.regenerate_error = st_regen;
        summary.sheet_regenerated = (st_regen == PRO_TK_NO_ERROR);
        LogLine(log_sink,
                "view-brush sheet-regenerate status=%d sheet=%d",
                static_cast<int>(st_regen),
                sheet);
        if (st_regen != PRO_TK_NO_ERROR) {
            summary.last_error = st_regen;
        }
    }
}

} // namespace

DrawingViewBrushOrientation MakeDrawingViewBrushPresetOrientation(DrawingViewBrushPreset preset)
{
    DrawingViewBrushOrientation orientation = {};
    ProMatrix matrix = {{0}};
    FillPresetMatrix(preset, matrix);
    if (NormalizeViewMatrix(matrix, orientation.matrix)) {
        orientation.valid = true;
        orientation.label = PresetLabel(preset);
    }
    return orientation;
}

bool CaptureReferenceDrawingViewBrushOrientation(ProDrawing drawing,
                                                 int sheet,
                                                 DrawingViewBrushMode mode,
                                                 DrawingViewBrushOrientation &orientation,
                                                 bool &cancelled,
                                                 const Drawing3LogSink &log_sink)
{
    cancelled = false;
    while (true) {
        orientation = {};
        bool step_cancelled = false;
        if (!CaptureReferenceOnce(drawing, sheet, mode, orientation, step_cancelled, log_sink)) {
            if (step_cancelled) {
                cancelled = true;
                return false;
            }
            continue;
        }

        const ReferenceConfirmResult confirm = ConfirmReferenceOrientationEx(orientation);
        if (confirm == ReferenceConfirmResult::Accept) {
            return true;
        }
        if (confirm == ReferenceConfirmResult::Cancel) {
            cancelled = true;
            return false;
        }
    }
}

DrawingViewBrushSummary ExecuteDrawingViewBrushTask(ProDrawing drawing,
                                                    int sheet,
                                                    const DrawingViewBrushOrientation &orientation,
                                                    const Drawing3LogSink &log_sink)
{
    DrawingViewBrushSummary summary = {};
    summary.sheet = sheet;
    if (drawing == nullptr || !orientation.valid) {
        summary.failed = 1;
        summary.last_error = PRO_TK_BAD_INPUTS;
        return summary;
    }

    std::wstring prompt = L"\u8bf7\u70b9\u9009\u6216\u6846\u9009\u9700\u8981\u5237\u65b0\u7684\u5de5\u7a0b\u56fe\u89c6\u56fe\u3002\n\n";
    prompt += L"\u7a0b\u5e8f\u5c06\u81ea\u52a8\u8bc6\u522b\u5e76\u53ea\u5904\u7406\u4e3b\u89c6\u56fe\u3002\n";
    prompt += L"\u76ee\u6807\u89c6\u5411\uff1a";
    prompt += orientation.label.empty() ? L"custom / matrix" : orientation.label;
    autobbox::ui::ShowSimpleMessageDialog(
        PROUIMESSAGE_INFO,
        L"\u89c6\u56fe\u5237",
        prompt.c_str());

    // Select drawing views directly; using geometry filters here makes it hard
    // to select/box-select whole views and can leave Creo in an unstable
    // selection state when the command immediately edits drawing views.
    char selection_filter[] = "dwg_view";
    ProSelection *selection_array = nullptr;
    int selection_count = 0;
    const ProError st_select =
        ProSelect(selection_filter, -1, nullptr, nullptr, nullptr, nullptr, &selection_array, &selection_count);
    summary.selected_total = selection_count;
    LogLine(log_sink,
            "view-brush target-select status=%d count=%d filter='%s'",
            static_cast<int>(st_select),
            selection_count,
            selection_filter);
    if (IsUserCancelled(st_select, selection_count, selection_array)) {
        return summary;
    }
    if (st_select != PRO_TK_NO_ERROR) {
        summary.failed = 1;
        summary.last_error = st_select;
        return summary;
    }

    std::vector<ProView> views;
    CollectMainViewsFromSelections(
        drawing, sheet, DrawingViewBrushMode::MainView, selection_array, selection_count, summary, views, log_sink);
    // ProSelect() output is static Creo-owned selection storage; do not free.

    BrushMainViews(drawing, sheet, orientation, views, summary, log_sink);

    LogLine(log_sink,
            "view-brush summary selected=%d valid=%d target=%d derived_to_main=%d brushed=%d skipped_axon=%d skipped_non_axon=%d skipped_non_general=%d skipped_sheet=%d skipped_duplicate=%d failed=%d regen=%d regen_error=%d last_error=%d",
            summary.selected_total,
            summary.valid_views,
            summary.main_views_found,
            summary.derived_resolved_to_main,
            summary.brushed,
            summary.skipped_axonometric,
            summary.skipped_non_axonometric,
            summary.skipped_non_general,
            summary.skipped_other_sheet,
            summary.skipped_duplicate,
            summary.failed,
            summary.sheet_regenerated ? 1 : 0,
            static_cast<int>(summary.regenerate_error),
            static_cast<int>(summary.last_error));
    return summary;
}

DrawingViewBrushSummary ExecuteDrawingViewBrushFromSelectionBuffer(
    ProDrawing drawing,
    int sheet,
    const DrawingViewBrushOrientation &orientation,
    const Drawing3LogSink &log_sink)
{
    const DrawingViewBrushTargetSet targets =
        CaptureDrawingViewBrushTargetsFromSelectionBuffer(drawing, sheet, DrawingViewBrushMode::MainView, log_sink);
    return ExecuteDrawingViewBrushForTargets(drawing, sheet, orientation, targets, log_sink);
}

DrawingViewBrushTargetSet CaptureDrawingViewBrushTargetsFromSelectionBuffer(
    ProDrawing drawing,
    int sheet,
    DrawingViewBrushMode mode,
    const Drawing3LogSink &log_sink)
{
    DrawingViewBrushSummary summary = {};
    summary.sheet = sheet;
    if (drawing == nullptr) {
        summary.failed = 1;
        summary.last_error = PRO_TK_BAD_INPUTS;
        return TargetsFromSummaryAndViews(summary, {});
    }

    ProSelection *buffer = nullptr;
    const ProError st_buffer = ProSelbufferSelectionsGet(&buffer);
    if (st_buffer == PRO_TK_E_NOT_FOUND || st_buffer == PRO_TK_CANT_ACCESS) {
        LogLine(log_sink,
                "view-brush selection-buffer empty status=%d",
                static_cast<int>(st_buffer));
        return TargetsFromSummaryAndViews(summary, {});
    }
    if (st_buffer != PRO_TK_NO_ERROR || buffer == nullptr) {
        summary.failed = 1;
        summary.last_error = st_buffer;
        LogLine(log_sink,
                "view-brush selection-buffer fail status=%d",
                static_cast<int>(st_buffer));
        return TargetsFromSummaryAndViews(summary, {});
    }

    int selection_count = 0;
    ProArraySizeGet(reinterpret_cast<ProArray>(buffer), &selection_count);
    summary.selected_total = selection_count;
    LogLine(log_sink,
            "view-brush selection-buffer status=%d count=%d",
            static_cast<int>(st_buffer),
            selection_count);

    std::vector<ProView> views;
    CollectMainViewsFromSelections(drawing, sheet, mode, buffer, selection_count, summary, views, log_sink);
    ProSelectionarrayFree(buffer);

    return TargetsFromSummaryAndViews(summary, std::move(views));
}

DrawingViewBrushSummary ExecuteDrawingViewBrushForTargets(
    ProDrawing drawing,
    int sheet,
    const DrawingViewBrushOrientation &orientation,
    const DrawingViewBrushTargetSet &targets,
    const Drawing3LogSink &log_sink)
{
    DrawingViewBrushSummary summary = SummaryFromTargets(targets);
    summary.sheet = sheet;
    if (drawing == nullptr || !orientation.valid) {
        summary.failed += 1;
        summary.last_error = PRO_TK_BAD_INPUTS;
        return summary;
    }

    BrushMainViews(drawing, sheet, orientation, targets.main_views, summary, log_sink);

    LogLine(log_sink,
            "view-brush summary selected=%d valid=%d target=%d derived_to_main=%d brushed=%d skipped_axon=%d skipped_non_axon=%d skipped_non_general=%d skipped_sheet=%d skipped_duplicate=%d failed=%d regen=%d regen_error=%d last_error=%d",
            summary.selected_total,
            summary.valid_views,
            summary.main_views_found,
            summary.derived_resolved_to_main,
            summary.brushed,
            summary.skipped_axonometric,
            summary.skipped_non_axonometric,
            summary.skipped_non_general,
            summary.skipped_other_sheet,
            summary.skipped_duplicate,
            summary.failed,
            summary.sheet_regenerated ? 1 : 0,
            static_cast<int>(summary.regenerate_error),
            static_cast<int>(summary.last_error));
    return summary;
}

} // namespace autobbox::application
