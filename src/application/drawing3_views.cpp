#include "autobbox/application/drawing3_views.h"

#include "autobbox/application/drawing_view_layout.h"
#include "autobbox/common/files.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/creo/parameter_api.h"
#include "autobbox/common/strings.h"

#include <ProArray.h>
#include <ProAsmcomp.h>
#include <ProAsmcomppath.h>
#include <ProDtlattach.h>
#include <ProDtlentity.h>
#include <ProDtlnote.h>
#include <ProDtlsymdef.h>
#include <ProDtlsyminst.h>
#include <ProDrawing.h>
#include <ProDrawingView.h>
#include <ProDwgtable.h>
#include <ProFeature.h>
#include <ProMdl.h>
#include <ProModelitem.h>
#include <ProParameter.h>
#include <ProReference.h>
#include <ProRule.h>
#include <ProSimprep.h>
#include <ProSolid.h>
#include <ProToolkit.h>
#include <ProUtil.h>
#include <ProView.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <cstdarg>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace autobbox::application {

namespace {

struct Drawing3CollectCtx {
    std::vector<ProMdl> targets;
    std::unordered_set<std::uintptr_t> seen;
    std::unordered_map<std::uintptr_t, int> occurrence_count_by_mdl;
};

struct Drawing3SimprepCollectCtx {
    std::vector<core::Dwg3SimprepOption> *options = nullptr;
    std::wstring active_user_rep_name;
};

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

template <size_t N>
void CopyWStr(wchar_t (&dest)[N], const wchar_t *src)
{
    if (N == 0) {
        return;
    }

    size_t i = 0;
    if (src != nullptr) {
        while (i + 1 < N && src[i] != L'\0') {
            dest[i] = src[i];
            ++i;
        }
    }
    dest[i] = L'\0';
}

const wchar_t *ModelTypeShortLabel(ProMdlType type)
{
    switch (type) {
    case PRO_MDL_PART:
        return L"PRT";
    case PRO_MDL_ASSEMBLY:
        return L"ASM";
    case PRO_MDL_DRAWING:
        return L"DRW";
    default:
        return L"MDL";
    }
}

std::string ModelTag(ProMdl mdl, const Drawing3ModelTagFormatter &format_model_tag)
{
    if (format_model_tag) {
        return format_model_tag(mdl);
    }
    return autobbox::creo::DefaultModelTag(mdl);
}

std::string SanitizeAsciiToken(const std::wstring &value, size_t max_len)
{
    std::string raw = autobbox::common::WToA(value.c_str());
    std::string out;
    out.reserve(raw.size());
    for (unsigned char ch : raw) {
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::toupper(ch)));
        } else if (ch == '_' || ch == '-') {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "MODEL";
    }
    if (out.size() > max_len) {
        out.resize(max_len);
    }
    return out;
}

std::wstring MakeDrawingViewName(const std::wstring &model_name, const char *suffix)
{
    const std::string core = SanitizeAsciiToken(model_name, 20);
    const std::string full = std::string("AB_") + core + "_" + (suffix == nullptr ? "VIEW" : suffix);
    return autobbox::common::AToW(full.c_str());
}

const wchar_t *ViewLabel(core::Dwg3ViewType type)
{
    switch (type) {
    case core::Dwg3ViewType::Front:
        return L"Front";
    case core::Dwg3ViewType::Right:
        return L"Right";
    case core::Dwg3ViewType::Left:
        return L"Left";
    case core::Dwg3ViewType::Top:
        return L"Top";
    case core::Dwg3ViewType::Bottom:
        return L"Bottom";
    case core::Dwg3ViewType::Back:
        return L"Back";
    case core::Dwg3ViewType::Iso:
        return L"Iso";
    default:
        return L"View";
    }
}

const char *ProjectionLabel(core::Dwg3ProjectionType projection_type)
{
    return projection_type == core::Dwg3ProjectionType::ThirdAngle ? "THIRD_ANGLE" : "FIRST_ANGLE";
}

std::wstring GetDrawingViewName(const core::Dwg3Candidate &cand, core::Dwg3ViewType type)
{
    switch (type) {
    case core::Dwg3ViewType::Front:
        return cand.front_view_name;
    case core::Dwg3ViewType::Right:
        return cand.right_view_name;
    case core::Dwg3ViewType::Left:
        return cand.left_view_name;
    case core::Dwg3ViewType::Top:
        return cand.top_view_name;
    case core::Dwg3ViewType::Bottom:
        return cand.bottom_view_name;
    case core::Dwg3ViewType::Back:
        return cand.back_view_name;
    case core::Dwg3ViewType::Iso:
        return cand.iso_view_name;
    default:
        return std::wstring();
    }
}

std::wstring MakeDrawingCandidateLabel(ProMdl mdl)
{
    std::wstring out = autobbox::creo::ModelName(mdl);
    out += L" (";
    out += ModelTypeShortLabel(autobbox::creo::ModelType(mdl));
    out += L")";
    return out;
}

bool TryReadModelParamDisplayValue(ProMdl mdl, const wchar_t *param_name, std::wstring &value_out);

core::Dwg3Candidate MakeDrawingCandidate(ProMdl mdl, int occurrence_count, size_t index)
{
    core::Dwg3Candidate cand;
    cand.mdl = mdl;
    cand.type = autobbox::creo::ModelType(mdl);
    cand.occurrence_count = std::max(1, occurrence_count);
    cand.model_name = autobbox::creo::ModelName(mdl);
    TryReadModelParamDisplayValue(mdl, L"PTC_COMMON_NAME", cand.common_name);
    cand.label = MakeDrawingCandidateLabel(mdl);
    cand.front_view_name = MakeDrawingViewName(cand.model_name, "FRONT");
    cand.right_view_name = MakeDrawingViewName(cand.model_name, "RIGHT");
    cand.left_view_name = MakeDrawingViewName(cand.model_name, "LEFT");
    cand.top_view_name = MakeDrawingViewName(cand.model_name, "TOP");
    cand.bottom_view_name = MakeDrawingViewName(cand.model_name, "BOTTOM");
    cand.back_view_name = MakeDrawingViewName(cand.model_name, "BACK");
    cand.iso_view_name = MakeDrawingViewName(cand.model_name, "ISO");
    char item_name[32] = {0};
    std::snprintf(item_name, sizeof(item_name), "mdl_%zu", index);
    cand.item_name = item_name;
    return cand;
}

bool TryReadSimprepName(ProSimprep *simprep, std::wstring &name_out)
{
    name_out.clear();
    if (simprep == nullptr) {
        return false;
    }

    ProSimprepdata *data = nullptr;
    if (ProSimprepdataGet(simprep, &data) != PRO_TK_NO_ERROR || data == nullptr) {
        return false;
    }

    ProName name = {0};
    const ProError st_name = ProSimprepdataNameGet(data, name);
    ProSimprepdataFree(&data);
    if (st_name != PRO_TK_NO_ERROR || name[0] == L'\0') {
        return false;
    }

    name_out.assign(name);
    return true;
}

ProError CollectDrawingSimprepVisitAction(ProGeomitem *handle, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || handle == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    Drawing3SimprepCollectCtx *ctx = reinterpret_cast<Drawing3SimprepCollectCtx *>(app_data);
    if (ctx->options == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    ProSimprep *simprep = reinterpret_cast<ProSimprep *>(handle);
    ProSimprepType type = PRO_SIMPREP_MASTER_REP;
    if (ProSimprepTypeGet(simprep, &type) != PRO_TK_NO_ERROR || type != PRO_SIMPREP_USER_DEFINED) {
        return PRO_TK_NO_ERROR;
    }

    std::wstring rep_name;
    if (!TryReadSimprepName(simprep, rep_name) || rep_name.empty()) {
        return PRO_TK_NO_ERROR;
    }

    core::Dwg3SimprepOption option;
    option.display_label = rep_name;
    option.rep_name = rep_name;
    option.use_current_active = false;
    option.is_active = (!ctx->active_user_rep_name.empty() && ctx->active_user_rep_name == rep_name);
    ctx->options->push_back(option);
    return PRO_TK_NO_ERROR;
}

bool TryGetActiveDrawingSimprep(ProSolid root_solid, ProSimprep &active_rep, ProSimprepType &active_type, std::wstring &active_name)
{
    active_rep = {};
    active_type = PRO_SIMPREP_MASTER_REP;
    active_name.clear();
    if (root_solid == nullptr ||
        autobbox::creo::ModelType(reinterpret_cast<ProMdl>(root_solid)) != PRO_MDL_ASSEMBLY) {
        return false;
    }

    if (ProSimprepActiveGet(root_solid, &active_rep) != PRO_TK_NO_ERROR) {
        return false;
    }

    if (ProSimprepTypeGet(&active_rep, &active_type) != PRO_TK_NO_ERROR) {
        active_type = PRO_SIMPREP_MASTER_REP;
    }
    if (active_type == PRO_SIMPREP_USER_DEFINED) {
        TryReadSimprepName(&active_rep, active_name);
    }
    return true;
}

std::vector<core::Dwg3Candidate> BuildDrawingCandidatesFromCollected(const Drawing3CollectCtx &ctx)
{
    std::vector<core::Dwg3Candidate> result;
    result.reserve(ctx.targets.size());
    for (size_t i = 0; i < ctx.targets.size(); ++i) {
        ProMdl mdl = ctx.targets[i];
        const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(mdl);
        const auto occ_it = ctx.occurrence_count_by_mdl.find(key);
        const int occurrence_count = (occ_it != ctx.occurrence_count_by_mdl.end()) ? occ_it->second : 1;
        result.push_back(MakeDrawingCandidate(mdl, occurrence_count, i));
    }
    return result;
}

std::wstring TrimWide(const std::wstring &value)
{
    size_t begin = 0;
    while (begin < value.size() && std::iswspace(value[begin])) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::iswspace(value[end - 1])) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::wstring UpperWide(const std::wstring &value)
{
    std::wstring out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towupper(ch));
    });
    return out;
}

std::wstring ModelDedupKey(ProMdl mdl)
{
    return std::wstring(ModelTypeShortLabel(autobbox::creo::ModelType(mdl))) +
           L"|" +
           UpperWide(autobbox::creo::ModelName(mdl));
}

bool ResolveBomCellModel(ProDwgtable *table, int column, int row, ProMdl &model_out)
{
    model_out = nullptr;
    if (table == nullptr || table->owner == nullptr) {
        return false;
    }

    ProAsmcomppath component_path;
    std::memset(&component_path, 0, sizeof(component_path));
    if (ProDwgtableCellComponentGet(table, column, row, &component_path) == PRO_TK_NO_ERROR) {
        ProMdl component_model = nullptr;
        if (ProAsmcomppathMdlGet(&component_path, &component_model) == PRO_TK_NO_ERROR &&
            component_model != nullptr &&
            autobbox::creo::IsPartOrAsm(component_model)) {
            model_out = component_model;
            return true;
        }
    }

    ProAssembly assembly = nullptr;
    ProMdl ref_model = nullptr;
    if (ProDwgtableCellRefmodelGet(table, column, row, &assembly, &ref_model) == PRO_TK_NO_ERROR &&
        ref_model != nullptr &&
        autobbox::creo::IsPartOrAsm(ref_model)) {
        model_out = ref_model;
        return true;
    }

    return false;
}

std::vector<core::Dwg3Candidate> BuildDrawingCandidatesFromRulePaths(ProAsmcomppath *paths, int path_count)
{
    Drawing3CollectCtx ctx;
    for (int i = 0; i < path_count; ++i) {
        ProMdl mdl = nullptr;
        if (ProAsmcomppathMdlGet(&paths[i], &mdl) != PRO_TK_NO_ERROR || mdl == nullptr) {
            continue;
        }

        const ProMdlType type = autobbox::creo::ModelType(mdl);
        if (type != PRO_MDL_PART && type != PRO_MDL_ASSEMBLY) {
            continue;
        }

        const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(mdl);
        ++ctx.occurrence_count_by_mdl[key];
        if (ctx.seen.insert(key).second) {
            ctx.targets.push_back(mdl);
        }
    }
    return BuildDrawingCandidatesFromCollected(ctx);
}

void CollectAllAssemblyCandidatesRecursive(ProSolid solid, Drawing3CollectCtx &ctx)
{
    if (solid == nullptr) {
        return;
    }

    struct VisitRuntime {
        Drawing3CollectCtx *ctx = nullptr;
    } runtime = { &ctx };

    auto visit = [](ProFeature *feature, ProError status, ProAppData app_data) -> ProError {
        if (status != PRO_TK_NO_ERROR || feature == nullptr || app_data == nullptr) {
            return PRO_TK_NO_ERROR;
        }

        VisitRuntime *runtime = reinterpret_cast<VisitRuntime *>(app_data);
        if (runtime->ctx == nullptr) {
            return PRO_TK_NO_ERROR;
        }

        ProBoolean is_component_like = PRO_B_FALSE;
        if (ProFeatureIsComponentLike(feature, &is_component_like) != PRO_TK_NO_ERROR ||
            is_component_like != PRO_B_TRUE) {
            return PRO_TK_NO_ERROR;
        }

        ProMdl mdl = nullptr;
        if (ProAsmcompMdlGet(reinterpret_cast<ProAsmcomp *>(feature), &mdl) != PRO_TK_NO_ERROR || mdl == nullptr) {
            return PRO_TK_NO_ERROR;
        }

        const ProMdlType mdl_type = autobbox::creo::ModelType(mdl);
        if (mdl_type != PRO_MDL_PART && mdl_type != PRO_MDL_ASSEMBLY) {
            return PRO_TK_NO_ERROR;
        }

        const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(mdl);
        ++runtime->ctx->occurrence_count_by_mdl[key];
        const bool first_seen = runtime->ctx->seen.insert(key).second;
        if (first_seen) {
            runtime->ctx->targets.push_back(mdl);
        }

        if (mdl_type == PRO_MDL_ASSEMBLY) {
            CollectAllAssemblyCandidatesRecursive(reinterpret_cast<ProSolid>(mdl), *runtime->ctx);
        }
        return PRO_TK_NO_ERROR;
    };

    ProSolidFeatVisit(solid, visit, nullptr, &runtime);
}

ProError CollectDrawingCandidateVisitAction(ProAsmcomppath *path,
                                            ProSolid handle,
                                            ProBoolean down,
                                            ProAppData app_data)
{
    if (down != PRO_B_TRUE || path == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }
    if (path->table_num < 1) {
        return PRO_TK_NO_ERROR;
    }

    Drawing3CollectCtx *ctx = reinterpret_cast<Drawing3CollectCtx *>(app_data);
    ProMdl mdl = reinterpret_cast<ProMdl>(handle);
    const ProMdlType type = autobbox::creo::ModelType(mdl);
    if (type != PRO_MDL_PART && type != PRO_MDL_ASSEMBLY) {
        return PRO_TK_NO_ERROR;
    }

    const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(mdl);
    ++ctx->occurrence_count_by_mdl[key];
    if (ctx->seen.insert(key).second) {
        ctx->targets.push_back(mdl);
    }
    return PRO_TK_NO_ERROR;
}

void FillFrontMatrix(ProMatrix matrix)
{
    std::memset(matrix, 0, sizeof(ProMatrix));
    matrix[0][0] = 1.0;
    matrix[1][1] = 1.0;
    matrix[2][2] = 1.0;
    matrix[3][3] = 1.0;
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

void FillViewMatrix(core::Dwg3ViewType type, ProMatrix matrix)
{
    switch (type) {
    case core::Dwg3ViewType::Front:
        FillFrontMatrix(matrix);
        break;
    case core::Dwg3ViewType::Right:
        FillRightMatrix(matrix);
        break;
    case core::Dwg3ViewType::Left:
        FillLeftMatrix(matrix);
        break;
    case core::Dwg3ViewType::Top:
        FillTopMatrix(matrix);
        break;
    case core::Dwg3ViewType::Bottom:
        FillBottomMatrix(matrix);
        break;
    case core::Dwg3ViewType::Back:
        FillBackMatrix(matrix);
        break;
    case core::Dwg3ViewType::Iso:
        FillIsoMatrix(matrix);
        break;
    default:
        FillFrontMatrix(matrix);
        break;
    }
}

bool ScreenToDrawingPoint(ProDrawing drawing,
                          int sheet,
                          const ProPoint3d screen_point,
                          ProPoint3d drawing_point)
{
    if (drawing == nullptr || screen_point == nullptr || drawing_point == nullptr) {
        return false;
    }

    ProName sheet_size = {0};
    ProMatrix trf = {{0}};
    if (ProDrawingSheetTrfGet(drawing, sheet, sheet_size, trf) != PRO_TK_NO_ERROR) {
        return false;
    }

    ProVector in_point = {screen_point[0], screen_point[1], screen_point[2]};
    ProVector out_point = {0.0, 0.0, 0.0};
    if (ProPntTrfEval(in_point, trf, out_point) != PRO_TK_NO_ERROR) {
        return false;
    }

    drawing_point[0] = out_point[0];
    drawing_point[1] = out_point[1];
    drawing_point[2] = out_point[2];
    return true;
}

bool BuildDrawingSheetLayout(ProDrawing drawing,
                             int sheet,
                             size_t selected_count,
                             const ProPoint3d start_point,
                             core::Dwg3SheetLayout &layout)
{
    (void)drawing;
    (void)sheet;
    if (selected_count == 0 || start_point == nullptr) {
        return false;
    }

    layout.width = 0.0;
    layout.height = 0.0;
    layout.start_x = start_point[0];
    layout.start_y = start_point[1];
    layout.cols = static_cast<int>(std::max<size_t>(1, selected_count));
    layout.rows = 1;
    layout.cell_w = 120.0;
    layout.cell_h = 80.0;
    return layout.cell_w > 0.0 && layout.cell_h > 0.0;
}

std::wstring BuildGroupTitle(size_t index, size_t total, const core::Dwg3Candidate &cand)
{
    (void)index;
    (void)total;
    return cand.model_name;
}

bool TryReadModelParamDisplayValue(ProMdl mdl, const wchar_t *param_name, std::wstring &value_out)
{
    value_out.clear();
    if (mdl == nullptr || param_name == nullptr || param_name[0] == L'\0') {
        return false;
    }

    ProModelitem owner;
    std::memset(&owner, 0, sizeof(owner));
    if (ProMdlToModelitem(mdl, &owner) != PRO_TK_NO_ERROR) {
        return false;
    }

    const std::wstring normalized = autobbox::creo::NormalizeParameterName(param_name);
    if (normalized.empty()) {
        return false;
    }

    ProName pname = {0};
    CopyWStr(pname, normalized.c_str());

    ProParameter param;
    std::memset(&param, 0, sizeof(param));
    if (ProParameterInit(&owner, pname, &param) == PRO_TK_NO_ERROR) {
        ProParamvalueType type = PRO_PARAM_NOT_SET;
        if (autobbox::creo::ReadParameterDisplayValue(&param, type, value_out) && !value_out.empty()) {
            return true;
        }
    }

    const core::BomModelSnapshot snapshot = autobbox::creo::CollectBomModelSnapshot(mdl);
    const auto it = snapshot.params.find(normalized);
    if (it == snapshot.params.end()) {
        return false;
    }

    value_out = it->second.display_value;
    return !value_out.empty();
}

std::wstring BuildGroupTitleWithQty(size_t index, size_t total, const core::Dwg3Candidate &cand)
{
    std::wstring qty_value;
    if (TryReadModelParamDisplayValue(cand.mdl, L"QTY", qty_value) && !qty_value.empty()) {
    } else {
        qty_value = std::to_wstring(std::max(1, cand.occurrence_count));
    }

    std::wstring out = BuildGroupTitle(index, total, cand);
    out += L" \u6570\u91CF: ";
    out += qty_value;
    return out;
}

std::string DescribeModelForLog(ProMdl mdl)
{
    if (mdl == nullptr) {
        return "<null>";
    }

    std::string name = autobbox::common::WToA(autobbox::creo::ModelName(mdl).c_str());
    if (name.empty()) {
        name = "<unnamed>";
    }
    return name;
}

std::string DescribeSelectionForLog(ProSelection selection)
{
    if (selection == nullptr) {
        return "<null>";
    }

    ProAsmcomppath comp_path = {};
    const ProError st_path = ProSelectionAsmcomppathGet(selection, &comp_path);

    ProModelitem model_item;
    std::memset(&model_item, 0, sizeof(model_item));
    const ProError st_item = ProSelectionModelitemGet(selection, &model_item);

    ProMdl item_owner = nullptr;
    const ProError st_item_owner =
        (st_item == PRO_TK_NO_ERROR) ? ProModelitemMdlGet(&model_item, &item_owner) : PRO_TK_E_NOT_FOUND;

    ProReference reference = nullptr;
    const ProError st_ref = ProSelectionToReference(selection, &reference);

    ProMdl ref_owner = nullptr;
    const ProError st_ref_owner =
        (st_ref == PRO_TK_NO_ERROR && reference != nullptr) ? ProReferenceOwnerGet(reference, &ref_owner) : PRO_TK_E_NOT_FOUND;

    ProView view = nullptr;
    const ProError st_view = ProSelectionViewGet(selection, &view);

    ProPoint3d point = {0.0, 0.0, 0.0};
    const ProError st_point = ProSelectionPoint3dGet(selection, point);

    char buffer[512] = {0};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "path_status=%d path_depth=%d item_status=%d item_owner_status=%d item_owner=%s ref_status=%d ref_owner_status=%d ref_owner=%s view_status=%d view=%s point_status=%d point=(%.3f,%.3f,%.3f)",
                  static_cast<int>(st_path),
                  (st_path == PRO_TK_NO_ERROR) ? comp_path.table_num : -1,
                  static_cast<int>(st_item),
                  static_cast<int>(st_item_owner),
                  DescribeModelForLog(item_owner).c_str(),
                  static_cast<int>(st_ref),
                  static_cast<int>(st_ref_owner),
                  DescribeModelForLog(ref_owner).c_str(),
                  static_cast<int>(st_view),
                  (st_view == PRO_TK_NO_ERROR && view != nullptr) ? "set" : "<null>",
                  static_cast<int>(st_point),
                  point[0],
                  point[1],
                  point[2]);

    if (reference != nullptr) {
        ProReferenceFree(reference);
    }
    return buffer;
}

ProView PickAnchorView(const core::Dwg3CreatedViews &views)
{
    const ProView front = CreatedViewSlot(views, core::Dwg3ViewType::Front);
    if (front != nullptr) {
        return front;
    }
    for (core::Dwg3ViewType type : AllDwg3ViewTypes()) {
        const ProView view = CreatedViewSlot(views, type);
        if (view != nullptr) {
            return view;
        }
    }
    return nullptr;
}

ProError CreateFreeDrawingNote(ProDrawing drawing,
                               ProView anchor_view,
                               const std::wstring &text,
                               const ProPoint3d screen_point,
                               double text_height,
                               ProBoolean text_height_in_model_units,
                               ProTextHrzJustification horz_justification,
                               ProVerticalJustification vert_justification)
{
    if (drawing == nullptr || text.empty()) {
        return PRO_TK_BAD_INPUTS;
    }

    ProDtlnotedata note_data = nullptr;
    ProDtlattach attachment = nullptr;
    ProSelection attachment_sel = nullptr;
    ProTextStyle text_style = nullptr;
    ProError st = ProDtlnotedataAlloc(reinterpret_cast<ProMdl>(drawing), &note_data);
    if (st != PRO_TK_NO_ERROR || note_data == nullptr) {
        return st;
    }

    auto cleanup = [&]() {
        if (note_data != nullptr) {
            ProDtlnotedataFree(note_data);
        }
        if (attachment != nullptr) {
            ProDtlattachFree(attachment);
        }
        if (attachment_sel != nullptr) {
            ProSelectionFree(&attachment_sel);
        }
        if (text_style != nullptr) {
            ProTextStyleFree(&text_style);
        }
    };

    ProDtlnoteline line = nullptr;
    st = ProDtlnotelineAlloc(&line);
    if (st != PRO_TK_NO_ERROR || line == nullptr) {
        cleanup();
        return st;
    }

    auto add_text_item = [&](const wchar_t *value) -> ProError {
        if (value == nullptr || value[0] == L'\0') {
            return PRO_TK_NO_ERROR;
        }

        ProDtlnotetext note_text = nullptr;
        ProError item_st = ProDtlnotetextAlloc(&note_text);
        if (item_st != PRO_TK_NO_ERROR || note_text == nullptr) {
            return item_st;
        }

        ProLine pro_text = {0};
        CopyWStr(pro_text, value);
        item_st = ProDtlnotetextStringSet(note_text, pro_text);
        if (item_st == PRO_TK_NO_ERROR) {
            item_st = ProDtlnotelineTextAdd(line, note_text);
        }
        return item_st;
    };

    st = add_text_item(text.c_str());
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }
    st = ProDtlnotedataLineAdd(note_data, line);
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }

    if (text_height > 0.0 ||
        horz_justification != PRO_TEXT_HRZJUST_LEFT ||
        vert_justification != PRO_VERTJUST_TOP) {
        st = ProTextStyleAlloc(&text_style);
        if (st != PRO_TK_NO_ERROR || text_style == nullptr) {
            cleanup();
            return st;
        }
        if (text_height > 0.0) {
            st = ProTextStyleHeightSet(text_style, text_height);
            if (st != PRO_TK_NO_ERROR) {
                cleanup();
                return st;
            }
            st = ProTextStyleHeightInModelUnitsSet(text_style, text_height_in_model_units);
            if (st != PRO_TK_NO_ERROR) {
                cleanup();
                return st;
            }
        }
        st = ProTextStyleJustificationSet(text_style, horz_justification);
        if (st != PRO_TK_NO_ERROR) {
            cleanup();
            return st;
        }
        st = ProTextStyleVertJustificationSet(text_style, vert_justification);
        if (st != PRO_TK_NO_ERROR) {
            cleanup();
            return st;
        }
        st = ProDtlnotedataTextStyleSet(note_data, text_style);
        if (st != PRO_TK_NO_ERROR) {
            cleanup();
            return st;
        }
    }

    auto attach_note = [&]() -> ProError {
        ProVector location = {screen_point[0], screen_point[1], 0.0};
        st = ProDtlattachAlloc(PRO_DTLATTACHTYPE_FREE, anchor_view, location, nullptr, &attachment);
        if (st != PRO_TK_NO_ERROR || attachment == nullptr) {
            return st;
        }
        return ProDtlnotedataAttachmentSet(note_data, attachment);
    };

    st = attach_note();
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }
    st = ProDtlnotedataDisplayedSet(note_data, PRO_B_TRUE);
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }

    ProDtlnote note;
    std::memset(&note, 0, sizeof(note));
    st = ProDtlnoteCreate(reinterpret_cast<ProMdl>(drawing), nullptr, note_data, &note);
    if (st == PRO_TK_NO_ERROR) {
        const ProError show_st = ProDtlnoteShow(&note);
        if (show_st != PRO_TK_NO_ERROR) {
            const ProError draw_st = ProDtlnoteDraw(&note);
            st = (draw_st == PRO_TK_NO_ERROR) ? PRO_TK_NO_ERROR : show_st;
        }
    }
    cleanup();
    return st;
}

ProError CreateParameterizedDrawingTitleNote(ProDrawing drawing,
                                             ProView anchor_view,
                                             const ProPoint3d screen_point,
                                             double text_height,
                                             ProBoolean text_height_in_model_units,
                                             ProTextHrzJustification horz_justification,
                                             ProVerticalJustification vert_justification,
                                             ProDtlnote *created_note_out)
{
    if (drawing == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }
    if (created_note_out != nullptr) {
        std::memset(created_note_out, 0, sizeof(*created_note_out));
    }

    ProDtlnotedata note_data = nullptr;
    ProDtlattach attachment = nullptr;
    ProSelection attachment_sel = nullptr;
    ProTextStyle text_style = nullptr;
    ProError st = ProDtlnotedataAlloc(reinterpret_cast<ProMdl>(drawing), &note_data);
    if (st != PRO_TK_NO_ERROR || note_data == nullptr) {
        return st;
    }

    auto cleanup = [&]() {
        if (note_data != nullptr) {
            ProDtlnotedataFree(note_data);
        }
        if (attachment != nullptr) {
            ProDtlattachFree(attachment);
        }
        if (attachment_sel != nullptr) {
            ProSelectionFree(&attachment_sel);
        }
        if (text_style != nullptr) {
            ProTextStyleFree(&text_style);
        }
    };

    ProDtlnoteline line = nullptr;
    st = ProDtlnotelineAlloc(&line);
    if (st != PRO_TK_NO_ERROR || line == nullptr) {
        cleanup();
        return st;
    }

    auto add_text_item = [&](const wchar_t *value) -> ProError {
        if (value == nullptr || value[0] == L'\0') {
            return PRO_TK_NO_ERROR;
        }

        ProDtlnotetext note_text = nullptr;
        ProError item_st = ProDtlnotetextAlloc(&note_text);
        if (item_st != PRO_TK_NO_ERROR || note_text == nullptr) {
            return item_st;
        }

        ProLine pro_text = {0};
        CopyWStr(pro_text, value);
        item_st = ProDtlnotetextStringSet(note_text, pro_text);
        if (item_st == PRO_TK_NO_ERROR) {
            item_st = ProDtlnotelineTextAdd(line, note_text);
        }
        return item_st;
    };
    st = add_text_item(L", QTY: ");
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }
    st = add_text_item(L", QTY: ");
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }
    st = add_text_item(L", QTY: ");
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }

    st = ProDtlnotedataLineAdd(note_data, line);
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }

    if (text_height > 0.0 ||
        horz_justification != PRO_TEXT_HRZJUST_LEFT ||
        vert_justification != PRO_VERTJUST_TOP) {
        st = ProTextStyleAlloc(&text_style);
        if (st != PRO_TK_NO_ERROR || text_style == nullptr) {
            cleanup();
            return st;
        }
        if (text_height > 0.0) {
            st = ProTextStyleHeightSet(text_style, text_height);
            if (st != PRO_TK_NO_ERROR) {
                cleanup();
                return st;
            }
            st = ProTextStyleHeightInModelUnitsSet(text_style, text_height_in_model_units);
            if (st != PRO_TK_NO_ERROR) {
                cleanup();
                return st;
            }
        }
        st = ProTextStyleJustificationSet(text_style, horz_justification);
        if (st != PRO_TK_NO_ERROR) {
            cleanup();
            return st;
        }
        st = ProTextStyleVertJustificationSet(text_style, vert_justification);
        if (st != PRO_TK_NO_ERROR) {
            cleanup();
            return st;
        }
        st = ProDtlnotedataTextStyleSet(note_data, text_style);
        if (st != PRO_TK_NO_ERROR) {
            cleanup();
            return st;
        }
    }

    if (anchor_view != nullptr) {
        st = ProSelectionAlloc(nullptr, nullptr, &attachment_sel);
        if (st == PRO_TK_NO_ERROR && attachment_sel != nullptr) {
            st = ProSelectionViewSet(anchor_view, &attachment_sel);
        }
        if (st == PRO_TK_NO_ERROR) {
            ProPoint3d attach_point = {screen_point[0], screen_point[1], 0.0};
            st = ProSelectionPoint3dSet(attach_point, &attachment_sel);
        }
        if (st == PRO_TK_NO_ERROR) {
            ProVector unused_location = {0.0, 0.0, 0.0};
            st = ProDtlattachAlloc(
                PRO_DTLATTACHTYPE_PARAMETRIC,
                nullptr,
                unused_location,
                attachment_sel,
                &attachment);
        }
        if (st == PRO_TK_NO_ERROR && attachment != nullptr) {
            st = ProDtlnotedataAttachmentSet(note_data, attachment);
            if (st == PRO_TK_NO_ERROR) {
                goto attachment_ready;
            }
        }
        if (attachment != nullptr) {
            ProDtlattachFree(attachment);
            attachment = nullptr;
        }
        if (attachment_sel != nullptr) {
            ProSelectionFree(&attachment_sel);
            attachment_sel = nullptr;
        }
    }

    ProVector location = {screen_point[0], screen_point[1], 0.0};
    st = ProDtlattachAlloc(PRO_DTLATTACHTYPE_FREE, anchor_view, location, nullptr, &attachment);
    if (st != PRO_TK_NO_ERROR || attachment == nullptr) {
        cleanup();
        return st;
    }
    st = ProDtlnotedataAttachmentSet(note_data, attachment);
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }
attachment_ready:
    st = ProDtlnotedataDisplayedSet(note_data, PRO_B_TRUE);
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }

    ProDtlnote note;
    std::memset(&note, 0, sizeof(note));
    st = ProDtlnoteCreate(reinterpret_cast<ProMdl>(drawing), nullptr, note_data, &note);
    if (st == PRO_TK_NO_ERROR) {
        const ProError show_st = ProDtlnoteShow(&note);
        if (show_st != PRO_TK_NO_ERROR) {
            const ProError draw_st = ProDtlnoteDraw(&note);
            st = (draw_st == PRO_TK_NO_ERROR) ? PRO_TK_NO_ERROR : show_st;
        }
        if (st == PRO_TK_NO_ERROR && created_note_out != nullptr) {
            *created_note_out = note;
        }
    }

    cleanup();
    return st;
}

void LogParameterizedTitleBinding(ProDrawing drawing,
                                  ProView anchor_view,
                                  ProDtlnote *note,
                                  const core::Dwg3Candidate &cand,
                                  const Drawing3ModelTagFormatter &format_model_tag,
                                  const Drawing3LogSink &log_sink)
{
    if (drawing == nullptr || note == nullptr || log_sink == nullptr) {
        return;
    }

    ProSolid view_solid = nullptr;
    ProMdl model_name_ref = nullptr;
    ProMdl qty_ref = nullptr;
    ProDtlnotedata note_data = nullptr;
    ProDtlattach attachment = nullptr;
    ProSelection attach_selection = nullptr;
    ProDtlattachType attach_type = PRO_DTLATTACHTYPE_UNIMPLEMENTED;
    ProView attach_view = nullptr;
    ProVector attach_location = {0.0, 0.0, 0.0};

    const ProError st_view_model =
        (anchor_view != nullptr) ? ProDrawingViewSolidGet(drawing, anchor_view, &view_solid) : PRO_TK_E_NOT_FOUND;
    const ProError st_model_name = ProDtlnoteModelrefGet(note, nullptr, 0, 0, &model_name_ref);
    const ProError st_qty = ProDtlnoteModelrefGet(note, nullptr, 0, 2, &qty_ref);
    const ProError st_note_data = ProDtlnoteDataGet(note, nullptr, PRODISPMODE_SYMBOLIC, &note_data);
    const ProError st_attachment =
        (st_note_data == PRO_TK_NO_ERROR && note_data != nullptr) ? ProDtlnotedataAttachmentGet(note_data, &attachment) : PRO_TK_E_NOT_FOUND;
    const ProError st_attach_get =
        (st_attachment == PRO_TK_NO_ERROR && attachment != nullptr)
            ? ProDtlattachGet(attachment, &attach_type, &attach_view, attach_location, &attach_selection)
            : PRO_TK_E_NOT_FOUND;

    const std::string cand_model = DescribeModelForLog(cand.mdl);
    const std::string view_model = DescribeModelForLog(reinterpret_cast<ProMdl>(view_solid));
    const std::string model_name_model = DescribeModelForLog(model_name_ref);
    const std::string qty_model = DescribeModelForLog(qty_ref);
    const std::string attach_sel = DescribeSelectionForLog(attach_selection);

    LogLine(log_sink,
            "INFO %s title-bind cand_model=%s view_model_status=%d view_model=%s text0_status=%d text0_model=%s text2_status=%d text2_model=%s note_data_status=%d attach_status=%d attach_get_status=%d attach_type=%d attach_view=%s attach_sel=%s",
            ModelTag(cand.mdl, format_model_tag).c_str(),
            cand_model.c_str(),
            static_cast<int>(st_view_model),
            view_model.c_str(),
            static_cast<int>(st_model_name),
            model_name_model.c_str(),
            static_cast<int>(st_qty),
            qty_model.c_str(),
            static_cast<int>(st_note_data),
            static_cast<int>(st_attachment),
            static_cast<int>(st_attach_get),
            static_cast<int>(attach_type),
            (attach_view != nullptr) ? "set" : "<null>",
            attach_sel.c_str());

    if (attach_selection != nullptr) {
        ProSelectionFree(&attach_selection);
    }
    if (attachment != nullptr) {
        ProDtlattachFree(attachment);
    }
    if (note_data != nullptr) {
        ProDtlnotedataFree(note_data);
    }
}

ProError CreateDraftLineEntity(ProDrawing drawing,
                               ProView anchor_view,
                               double x1,
                               double y1,
                               double x2,
                               double y2)
{
    if (drawing == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    ProDtlentitydata entdata = nullptr;
    ProCurvedata *curve = nullptr;
    ProError st = ProDtlentitydataAlloc(reinterpret_cast<ProMdl>(drawing), &entdata);
    if (st != PRO_TK_NO_ERROR || entdata == nullptr) {
        return st;
    }

    auto cleanup = [&]() {
        if (curve != nullptr) {
            ProCurvedataFree(curve);
        }
        if (entdata != nullptr) {
            ProDtlentitydataFree(entdata);
        }
    };

    st = ProCurvedataAlloc(&curve);
    if (st != PRO_TK_NO_ERROR || curve == nullptr) {
        cleanup();
        return st;
    }

    curve->line.type = PRO_ENT_LINE;
    curve->line.end1[0] = x1;
    curve->line.end1[1] = y1;
    curve->line.end1[2] = 0.0;
    curve->line.end2[0] = x2;
    curve->line.end2[1] = y2;
    curve->line.end2[2] = 0.0;

    st = ProDtlentitydataCurveSet(entdata, curve);
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }
    if (anchor_view != nullptr) {
        st = ProDtlentitydataViewSet(entdata, anchor_view);
        if (st != PRO_TK_NO_ERROR) {
            cleanup();
            return st;
        }
    }

    ProDtlentity entity;
    std::memset(&entity, 0, sizeof(entity));
    st = ProDtlentityCreate(reinterpret_cast<ProMdl>(drawing), nullptr, entdata, &entity);
    if (st == PRO_TK_NO_ERROR) {
        ProDtlentityDraw(&entity);
    }
    cleanup();
    return st;
}

ProError CreateGroupFrame(ProDrawing drawing,
                          ProView anchor_view,
                          const core::Dwg3GroupOutline &screen_outline,
                          double pad_x,
                          double pad_y)
{
    const double left = screen_outline.min_x - pad_x;
    const double right = screen_outline.max_x + pad_x;
    const double top = screen_outline.max_y + pad_y;
    const double bottom = screen_outline.min_y - pad_y;

    ProError st = CreateDraftLineEntity(drawing, anchor_view, left, top, right, top);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }
    st = CreateDraftLineEntity(drawing, anchor_view, right, top, right, bottom);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }
    st = CreateDraftLineEntity(drawing, anchor_view, right, bottom, left, bottom);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }
    return CreateDraftLineEntity(drawing, anchor_view, left, bottom, left, top);
}

std::wstring ParentPath(std::wstring path)
{
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return std::wstring();
    }
    return path.substr(0, pos);
}

std::vector<std::wstring> ResolveFrameSymbolDirectories()
{
    std::vector<std::wstring> dirs;
    auto add_dir = [&](const std::wstring &dir) {
        if (!dir.empty() &&
            std::find(dirs.begin(), dirs.end(), dir) == dirs.end()) {
            dirs.push_back(dir);
        }
    };

    const std::wstring text_root = autobbox::common::ResolveToolkitTextRoot();
    if (!text_root.empty()) {
        add_dir(autobbox::common::JoinPath(autobbox::common::JoinPath(text_root, L"lib"), L"formatSymbol"));
        add_dir(autobbox::common::JoinPath(autobbox::common::JoinPath(ParentPath(text_root), L"lib"), L"formatSymbol"));
        add_dir(autobbox::common::JoinPath(autobbox::common::JoinPath(ParentPath(ParentPath(text_root)), L"lib"), L"formatSymbol"));
    }

    const std::wstring cwd = autobbox::common::CurrentWorkingDirectoryW();
    if (!cwd.empty()) {
        add_dir(autobbox::common::JoinPath(autobbox::common::JoinPath(cwd, L"lib"), L"formatSymbol"));
    }
    return dirs;
}

std::wstring VersionedSymbolFileName(const core::Dwg3FrameOptions &frame_options)
{
    if (frame_options.symbol_file_name.empty()) {
        return std::wstring();
    }
    std::wstring name = frame_options.symbol_file_name;
    if (frame_options.symbol_version != PRO_VALUE_UNUSED && frame_options.symbol_version >= 0) {
        name += L".";
        name += std::to_wstring(frame_options.symbol_version);
    }
    return name;
}

std::wstring SymbolRetrieveName(const std::wstring &symbol_file_name)
{
    std::wstring name = symbol_file_name;

    const size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        name = name.substr(slash + 1);
    }

    const size_t version_sep = name.find_last_of(L'.');
    if (version_sep != std::wstring::npos) {
        bool version_suffix = version_sep + 1 < name.size();
        for (size_t i = version_sep + 1; i < name.size(); ++i) {
            if (!std::iswdigit(name[i])) {
                version_suffix = false;
                break;
            }
        }
        if (version_suffix) {
            name = name.substr(0, version_sep);
        }
    }

    const std::wstring sym_ext = L".sym";
    if (name.size() > sym_ext.size()) {
        const size_t ext_pos = name.size() - sym_ext.size();
        bool has_sym_ext = true;
        for (size_t i = 0; i < sym_ext.size(); ++i) {
            if (std::towlower(name[ext_pos + i]) != sym_ext[i]) {
                has_sym_ext = false;
                break;
            }
        }
        if (has_sym_ext) {
            name.resize(ext_pos);
        }
    }

    return name;
}

ProError RetrieveFrameSymbolDefinition(ProDrawing drawing,
                                       const core::Dwg3FrameOptions &frame_options,
                                       ProDtlsymdef &symbol_definition,
                                       const Drawing3LogSink &log_sink)
{
    symbol_definition = {};
    if (drawing == nullptr || frame_options.symbol_file_name.empty()) {
        return PRO_TK_BAD_INPUTS;
    }

    ProName filename = {0};
    const std::wstring retrieve_name = SymbolRetrieveName(frame_options.symbol_file_name);
    CopyWStr(filename, retrieve_name.c_str());
    const int version = frame_options.symbol_version >= 0 ? frame_options.symbol_version : PRO_VALUE_UNUSED;
    const std::wstring versioned_file = VersionedSymbolFileName(frame_options);

    for (const std::wstring &dir : ResolveFrameSymbolDirectories()) {
        const std::wstring candidate = autobbox::common::JoinPath(dir, versioned_file.c_str());
        if (!autobbox::common::FileExistsW(candidate)) {
            LogLine(log_sink,
                    "Drawing3 frame-symbol probe exists=0 path=%s",
                    autobbox::common::WToA(candidate.c_str()).c_str());
            continue;
        }

        ProPath filepath = {0};
        CopyWStr(filepath, dir.c_str());
        ProError st = ProDrawingDtlsymdefRetrieve(
            reinterpret_cast<ProMdl>(drawing),
            filepath,
            filename,
            version,
            PRO_B_TRUE,
            &symbol_definition);
        LogLine(log_sink,
                "Drawing3 frame-symbol retrieve status=%d dir=%s file=%s retrieve_name=%s version=%d",
                static_cast<int>(st),
                autobbox::common::WToA(dir.c_str()).c_str(),
                autobbox::common::WToA(frame_options.symbol_file_name.c_str()).c_str(),
                autobbox::common::WToA(retrieve_name.c_str()).c_str(),
                version);
        if (st == PRO_TK_NO_ERROR) {
            return st;
        }

        ProName option = {0};
        ProPath option_value = {0};
        CopyWStr(option, L"pro_symbol_dir");
        CopyWStr(option_value, dir.c_str());
        const ProError st_config = ProConfigoptSet(option, option_value);
        st = ProDrawingDtlsymdefRetrieve(
            reinterpret_cast<ProMdl>(drawing),
            nullptr,
            filename,
            version,
            PRO_B_TRUE,
            &symbol_definition);
        LogLine(log_sink,
                "Drawing3 frame-symbol retrieve-via-config config_status=%d status=%d",
                static_cast<int>(st_config),
                static_cast<int>(st));
        if (st == PRO_TK_NO_ERROR) {
            return st;
        }
    }
    return PRO_TK_E_NOT_FOUND;
}

ProError CreateGroupFrameSymbol(ProDrawing drawing,
                                ProView anchor_view,
                                const core::Dwg3GroupOutline &frame_outline,
                                const core::Dwg3FrameOptions &frame_options,
                                const Drawing3LogSink &log_sink)
{
    if (drawing == nullptr || frame_options.mode != core::Dwg3FrameMode::Symbol) {
        return PRO_TK_BAD_INPUTS;
    }

    ProDtlsymdef symbol_definition = {};
    ProError st = RetrieveFrameSymbolDefinition(drawing, frame_options, symbol_definition, log_sink);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }

    ProDtlsyminstdata data = nullptr;
    ProDtlattach attachment = nullptr;
    auto cleanup = [&]() {
        if (attachment != nullptr) {
            ProDtlattachFree(attachment);
            attachment = nullptr;
        }
        if (data != nullptr) {
            ProDtlsyminstdataFree(data);
            data = nullptr;
        }
    };

    st = ProDtlsyminstdataAlloc(reinterpret_cast<ProMdl>(drawing), &data);
    if (st != PRO_TK_NO_ERROR || data == nullptr) {
        cleanup();
        return st;
    }
    st = ProDtlsyminstdataDefSet(data, &symbol_definition);
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }
    st = ProDtlsyminstdataAttachtypeSet(data, PROSYMDEFATTACHTYPE_FREE);
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }

    const double height = frame_outline.max_y - frame_outline.min_y;
    if (height > 0.0 && std::isfinite(height)) {
        const ProError st_height = ProDtlsyminstdataScaledheightSet(data, height);
        LogLine(log_sink,
                "Drawing3 frame-symbol scaled-height status=%d height=%.3f",
                static_cast<int>(st_height),
                height);
    }
    (void)ProDtlsyminstdataAngleSet(data, 0.0);
    (void)ProDtlsyminstdataGroupoptionsSet(data, PRO_DTLSYMINST_GROUP_ALL, nullptr);

    ProVector location = {frame_outline.min_x, frame_outline.min_y, 0.0};
    st = ProDtlattachAlloc(PRO_DTLATTACHTYPE_FREE, anchor_view, location, nullptr, &attachment);
    if (st != PRO_TK_NO_ERROR || attachment == nullptr) {
        cleanup();
        return st;
    }
    st = ProDtlsyminstdataAttachmentSet(data, attachment);
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }

    ProDtlsyminst symbol_instance = {};
    st = ProDtlsyminstCreate(reinterpret_cast<ProMdl>(drawing), data, &symbol_instance);
    if (st == PRO_TK_NO_ERROR) {
        const ProError show_st = ProDtlsyminstShow(&symbol_instance);
        if (show_st != PRO_TK_NO_ERROR) {
            const ProError draw_st = ProDtlsyminstDraw(&symbol_instance);
            st = (draw_st == PRO_TK_NO_ERROR) ? PRO_TK_NO_ERROR : show_st;
        }
    }
    cleanup();
    return st;
}

void TryAnnotateGroup(ProDrawing drawing,
                      const core::Dwg3Candidate &cand,
                      size_t index,
                      size_t total,
                      double page_scale,
                      const core::Dwg3CreatedViews &views,
                      const core::Dwg3GroupOutline &screen_outline,
                      const core::Dwg3FrameOptions &frame_options,
                      const Drawing3ModelTagFormatter &format_model_tag,
                      const Drawing3LogSink &log_sink)
{
    if (drawing == nullptr) {
        return;
    }

    const double factor = std::max(1.0, page_scale / 0.015);
    const core::Dwg3GroupOutline frame_outline = ExpandDecoratedOutline(screen_outline, page_scale, false);

    const ProView anchor_view = PickAnchorView(views);
    if (anchor_view != nullptr) {
        ProError st_frame = PRO_TK_NO_ERROR;
        if (frame_options.mode == core::Dwg3FrameMode::Symbol) {
            st_frame = CreateGroupFrameSymbol(drawing, anchor_view, frame_outline, frame_options, log_sink);
            if (st_frame != PRO_TK_NO_ERROR) {
                LogLine(log_sink,
                        "WARN %s frame-symbol status=%d fallback=auto symbol=%s",
                        ModelTag(cand.mdl, format_model_tag).c_str(),
                        static_cast<int>(st_frame),
                        autobbox::common::WToA(frame_options.symbol_file_name.c_str()).c_str());
                st_frame = CreateGroupFrame(
                    drawing,
                    anchor_view,
                    screen_outline,
                    frame_outline.min_x < screen_outline.min_x ? (screen_outline.min_x - frame_outline.min_x) : 0.0,
                    frame_outline.min_y < screen_outline.min_y ? (screen_outline.min_y - frame_outline.min_y) : 0.0);
            }
        } else {
            st_frame = CreateGroupFrame(
                drawing,
                anchor_view,
                screen_outline,
                frame_outline.min_x < screen_outline.min_x ? (screen_outline.min_x - frame_outline.min_x) : 0.0,
                frame_outline.min_y < screen_outline.min_y ? (screen_outline.min_y - frame_outline.min_y) : 0.0);
        }
        if (st_frame != PRO_TK_NO_ERROR) {
            LogLine(log_sink,
                    "WARN %s frame status=%d",
                    ModelTag(cand.mdl, format_model_tag).c_str(),
                    static_cast<int>(st_frame));
        }
        double view_scale = page_scale;
        if (ProDrawingViewScaleGet(drawing, anchor_view, &view_scale) != PRO_TK_NO_ERROR ||
            !(view_scale > 0.0) ||
            !std::isfinite(view_scale)) {
            view_scale = (page_scale > 0.0 && std::isfinite(page_scale)) ? page_scale : 1.0;
        }
        const double title_inset_x = 6.0 * factor;
        const double title_inset_y = 5.0 * factor;
        const double safe_view_scale =
            (view_scale > 0.0 && std::isfinite(view_scale)) ? view_scale : 1.0;
        const double target_sheet_text_height = std::clamp(
            2.2 * factor * std::sqrt(safe_view_scale),
            1.6 * factor,
            3.0 * factor);
        const ProPoint3d title_pos = {
            frame_outline.max_x - title_inset_x,
            frame_outline.max_y - title_inset_y,
            0.0};
        const std::wstring title = BuildGroupTitleWithQty(index, total, cand);
        // Create the title note relative to the main/anchor view so later
        // ProDrawingViewMove-based arrange operations carry it with the view.
        ProError st_title = CreateFreeDrawingNote(
            drawing,
            anchor_view,
            title,
            title_pos,
            0.0,
            PRO_B_FALSE,
            PRO_TEXT_HRZJUST_RIGHT,
            PRO_VERTJUST_TOP);
        if (st_title != PRO_TK_NO_ERROR) {
            st_title = CreateFreeDrawingNote(
                drawing,
                nullptr,
                title,
                title_pos,
                0.0,
                PRO_B_FALSE,
                PRO_TEXT_HRZJUST_RIGHT,
                PRO_VERTJUST_TOP);
        }
        if (st_title != PRO_TK_NO_ERROR) {
            st_title = CreateFreeDrawingNote(
                drawing,
                anchor_view,
                title,
                title_pos,
                target_sheet_text_height,
                PRO_B_FALSE,
                PRO_TEXT_HRZJUST_RIGHT,
                PRO_VERTJUST_TOP);
        }
        if (st_title != PRO_TK_NO_ERROR) {
            st_title = CreateFreeDrawingNote(
                drawing,
                nullptr,
                title,
                title_pos,
                target_sheet_text_height,
                PRO_B_FALSE,
                PRO_TEXT_HRZJUST_RIGHT,
                PRO_VERTJUST_TOP);
        }
        if (st_title != PRO_TK_NO_ERROR) {
            LogLine(log_sink,
                    "WARN %s title-note status=%d",
                    ModelTag(cand.mdl, format_model_tag).c_str(),
                    static_cast<int>(st_title));
        }
    }
}

void DeleteDrawingViewGroup(ProDrawing drawing, core::Dwg3CreatedViews &views)
{
    if (drawing == nullptr) {
        return;
    }
    for (size_t i = views.items.size(); i > 0; --i) {
        ProView &view = views.items[i - 1];
        if (view != nullptr) {
            ProDrawingViewDelete(drawing, view, PRO_B_TRUE);
            view = nullptr;
        }
    }
}

std::unordered_map<std::wstring, ProView> CollectCurrentSheetViewsByName(ProDrawing drawing, int sheet)
{
    std::unordered_map<std::wstring, ProView> views_by_name;
    ProView *views = nullptr;
    const ProError st = ProDrawingViewsCollect(drawing, &views);
    if (!(st == PRO_TK_NO_ERROR || st == PRO_TK_E_NOT_FOUND) || views == nullptr) {
        return views_by_name;
    }

    int count = 0;
    ProArraySizeGet((ProArray)views, &count);
    for (int i = 0; i < count; ++i) {
        int view_sheet = 0;
        if (ProDrawingViewSheetGet(drawing, views[i], &view_sheet) != PRO_TK_NO_ERROR || view_sheet != sheet) {
            continue;
        }
        ProName name = {0};
        if (ProDrawingViewNameGet(drawing, views[i], name) == PRO_TK_NO_ERROR && name[0] != L'\0') {
            views_by_name[std::wstring(name)] = views[i];
        }
    }

    ProArrayFree((ProArray *)&views);
    return views_by_name;
}

ProError SetDrawingViewName(ProDrawing drawing, ProView view, const std::wstring &name)
{
    if (drawing == nullptr || view == nullptr || name.empty()) {
        return PRO_TK_BAD_INPUTS;
    }
    ProName pname = {0};
    CopyWStr(pname, name.c_str());
    return ProDrawingViewNameSet(drawing, view, pname);
}

core::Dwg3ViewMask CollectExistingCandidateViewMask(const std::unordered_map<std::wstring, ProView> &existing_views,
                                                    const core::Dwg3Candidate &cand,
                                                    core::Dwg3ViewMask requested_mask,
                                                    core::Dwg3CreatedViews &views)
{
    views = {};
    core::Dwg3ViewMask existing_mask = 0;
    for (core::Dwg3ViewType type : AllDwg3ViewTypes()) {
        const core::Dwg3ViewMask bit = core::Dwg3ViewBit(type);
        if ((requested_mask & bit) == 0) {
            continue;
        }
        const auto it = existing_views.find(GetDrawingViewName(cand, type));
        if (it != existing_views.end()) {
            CreatedViewSlot(views, type) = it->second;
            existing_mask |= bit;
        }
    }
    return existing_mask;
}

bool ResolveBaseOriginFromExisting(ProDrawing drawing,
                                   const core::Dwg3CreatedViews &existing_views,
                                   const core::Dwg3Spacing &spacing,
                                   core::Dwg3ProjectionType projection_type,
                                   ProPoint3d base_origin)
{
    for (core::Dwg3ViewType type : AllDwg3ViewTypes()) {
        const ProView view = CreatedViewSlot(existing_views, type);
        if (view == nullptr) {
            continue;
        }

        ProPoint3d origin = {0.0, 0.0, 0.0};
        if (ProDrawingViewOriginGet(drawing, view, origin, nullptr) != PRO_TK_NO_ERROR) {
            continue;
        }

        ProPoint3d offset = {0.0, 0.0, 0.0};
        GetViewOriginOffset(spacing, type, projection_type, offset);
        base_origin[0] = origin[0] - offset[0];
        base_origin[1] = origin[1] - offset[1];
        base_origin[2] = 0.0;
        return true;
    }
    return false;
}

bool IsProjectedView(core::Dwg3ViewType type)
{
    switch (type) {
    case core::Dwg3ViewType::Right:
    case core::Dwg3ViewType::Left:
    case core::Dwg3ViewType::Top:
    case core::Dwg3ViewType::Bottom:
    case core::Dwg3ViewType::Back:
        return true;
    default:
        return false;
    }
}

core::Dwg3ViewMask ExpandProjectedParentMask(core::Dwg3ViewMask mask, core::Dwg3ViewMask existing_mask)
{
    core::Dwg3ViewMask expanded = mask;
    const core::Dwg3ViewMask front_bit = core::Dwg3ViewBit(core::Dwg3ViewType::Front);
    const core::Dwg3ViewMask top_bit = core::Dwg3ViewBit(core::Dwg3ViewType::Top);
    const core::Dwg3ViewMask projected_from_front =
        core::Dwg3ViewBit(core::Dwg3ViewType::Right) |
        core::Dwg3ViewBit(core::Dwg3ViewType::Left) |
        core::Dwg3ViewBit(core::Dwg3ViewType::Top) |
        core::Dwg3ViewBit(core::Dwg3ViewType::Bottom);

    if ((expanded & projected_from_front) != 0 && (expanded & front_bit) == 0 && (existing_mask & front_bit) == 0) {
        expanded |= front_bit;
    }

    if ((expanded & core::Dwg3ViewBit(core::Dwg3ViewType::Back)) != 0 &&
        (expanded & top_bit) == 0 &&
        (existing_mask & top_bit) == 0) {
        expanded |= top_bit;
        if ((expanded & front_bit) == 0 && (existing_mask & front_bit) == 0) {
            expanded |= front_bit;
        }
    }

    return expanded;
}

ProError CreateSingleDrawingView(ProDrawing drawing,
                                 int sheet,
                                 const core::Dwg3Candidate &cand,
                                 core::Dwg3ViewType type,
                                 const ProPoint3d location,
                                 double scale,
                                 ProView &view)
{
    view = nullptr;
    if (drawing == nullptr || cand.mdl == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    ProMatrix orientation = {{0}};
    FillViewMatrix(type, orientation);
    ProError st = ProDrawingGeneralviewCreate(
        drawing,
        reinterpret_cast<ProSolid>(cand.mdl),
        sheet,
        PRO_B_FALSE,
        const_cast<double *>(location),
        scale,
        orientation,
        &view);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }

    st = SetDrawingViewName(drawing, view, GetDrawingViewName(cand, type));
    if (st != PRO_TK_NO_ERROR) {
        ProDrawingViewDelete(drawing, view, PRO_B_TRUE);
        view = nullptr;
    }
    return st;
}

ProError CreateProjectedDrawingView(ProDrawing drawing,
                                    const core::Dwg3Candidate &cand,
                                    core::Dwg3ViewType type,
                                    ProView parent_view,
                                    const ProPoint3d location,
                                    ProView &view)
{
    view = nullptr;
    if (drawing == nullptr || cand.mdl == nullptr || parent_view == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    ProError st = ProDrawingProjectedviewCreate(
        drawing,
        parent_view,
        PRO_B_FALSE,
        const_cast<double *>(location),
        &view);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }

    st = SetDrawingViewName(drawing, view, GetDrawingViewName(cand, type));
    if (st != PRO_TK_NO_ERROR) {
        ProDrawingViewDelete(drawing, view, PRO_B_TRUE);
        view = nullptr;
    }
    return st;
}

ProView ResolveProjectedParentView(core::Dwg3ViewType type,
                                   const core::Dwg3CreatedViews &created,
                                   const core::Dwg3CreatedViews &existing)
{
    auto pick = [&](core::Dwg3ViewType parent_type) -> ProView {
        ProView view = CreatedViewSlot(created, parent_type);
        if (view != nullptr) {
            return view;
        }
        return CreatedViewSlot(existing, parent_type);
    };

    switch (type) {
    case core::Dwg3ViewType::Right:
    case core::Dwg3ViewType::Left:
    case core::Dwg3ViewType::Top:
    case core::Dwg3ViewType::Bottom:
        return pick(core::Dwg3ViewType::Front);
    case core::Dwg3ViewType::Back:
        return pick(core::Dwg3ViewType::Top);
    default:
        return nullptr;
    }
}

ProError CreateDrawingViewsForMask(ProDrawing drawing,
                                   int sheet,
                                   const core::Dwg3Candidate &cand,
                                   core::Dwg3ViewMask create_mask,
                                   const core::Dwg3CreatedViews &existing_views,
                                   const ProPoint3d temp_base_origin,
                                   const core::Dwg3Spacing &spacing,
                                   core::Dwg3ProjectionType projection_type,
                                   double scale,
                                   core::Dwg3CreatedViews &created)
{
    created = {};
    if (drawing == nullptr || cand.mdl == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    ProError st = ProDrawingSolidAdd(drawing, reinterpret_cast<ProSolid>(cand.mdl));
    if (!(st == PRO_TK_NO_ERROR || st == PRO_TK_E_IN_USE)) {
        return st;
    }

    for (core::Dwg3ViewType type : AllDwg3ViewTypes()) {
        const core::Dwg3ViewMask bit = core::Dwg3ViewBit(type);
        if ((create_mask & bit) == 0) {
            continue;
        }

        ProPoint3d origin = {0.0, 0.0, 0.0};
        MakeViewOrigin(temp_base_origin, spacing, type, projection_type, origin);
        if (IsProjectedView(type)) {
            ProView parent_view = ResolveProjectedParentView(type, created, existing_views);
            if (parent_view == nullptr) {
                DeleteDrawingViewGroup(drawing, created);
                return PRO_TK_BAD_INPUTS;
            }
            st = CreateProjectedDrawingView(drawing, cand, type, parent_view, origin, CreatedViewSlot(created, type));
        } else {
            st = CreateSingleDrawingView(drawing, sheet, cand, type, origin, scale, CreatedViewSlot(created, type));
        }
        if (st != PRO_TK_NO_ERROR) {
            DeleteDrawingViewGroup(drawing, created);
            return st;
        }
    }

    return PRO_TK_NO_ERROR;
}

ProError SetDrawingViewOriginsForMask(ProDrawing drawing,
                                      core::Dwg3CreatedViews &views,
                                      core::Dwg3ViewMask mask,
                                      const ProPoint3d base_origin,
                                      const core::Dwg3Spacing &spacing,
                                      core::Dwg3ProjectionType projection_type)
{
    for (core::Dwg3ViewType type : AllDwg3ViewTypes()) {
        const core::Dwg3ViewMask bit = core::Dwg3ViewBit(type);
        ProView view = CreatedViewSlot(views, type);
        if (view == nullptr || (mask & bit) == 0) {
            continue;
        }

        ProPoint3d origin = {0.0, 0.0, 0.0};
        MakeViewOrigin(base_origin, spacing, type, projection_type, origin);
        const ProError st = ProDrawingViewOriginSet(drawing, view, origin, nullptr);
        if (st != PRO_TK_NO_ERROR) {
            return st;
        }
    }
    return PRO_TK_NO_ERROR;
}

} // namespace

std::vector<core::Dwg3SimprepOption> CollectDrawingViewSimprepOptions(ProSolid root_solid)
{
    std::vector<core::Dwg3SimprepOption> options;
    if (root_solid == nullptr) {
        return options;
    }

    ProSimprep active_rep = {};
    ProSimprepType active_type = PRO_SIMPREP_MASTER_REP;
    std::wstring active_name;
    const bool has_active_rep = TryGetActiveDrawingSimprep(root_solid, active_rep, active_type, active_name);

    core::Dwg3SimprepOption master_option;
    master_option.display_label = L"Master Rep";
    master_option.use_master_rep = true;
    master_option.is_active = (!has_active_rep || active_type == PRO_SIMPREP_MASTER_REP);
    options.push_back(master_option);

    if (autobbox::creo::ModelType(reinterpret_cast<ProMdl>(root_solid)) != PRO_MDL_ASSEMBLY) {
        return options;
    }

    Drawing3SimprepCollectCtx ctx;
    ctx.options = &options;
    if (active_type == PRO_SIMPREP_USER_DEFINED) {
        ctx.active_user_rep_name = active_name;
    }

    const ProError st = ProSolidSimprepVisit(
        root_solid,
        nullptr,
        CollectDrawingSimprepVisitAction,
        &ctx);
    if (st != PRO_TK_NO_ERROR && st != PRO_TK_E_NOT_FOUND) {
        return options;
    }

    bool has_active_option = false;
    for (const core::Dwg3SimprepOption &option : options) {
        if (option.is_active) {
            has_active_option = true;
            break;
        }
    }

    if (!has_active_option && !options.empty()) {
        options.front().is_active = true;
    }

    return options;
}

bool ActivateDrawingViewSimprepByName(ProSolid root_solid, const std::wstring &rep_name)
{
    if (root_solid == nullptr) {
        return false;
    }
    if (autobbox::creo::ModelType(reinterpret_cast<ProMdl>(root_solid)) != PRO_MDL_ASSEMBLY) {
        return true;
    }
    if (rep_name.empty()) {
        return false;
    }

    ProName rep_name_w = {0};
    CopyWStr(rep_name_w, rep_name.c_str());

    ProSimprep simprep = {};
    const ProError st_init = ProSimprepInit(rep_name_w, PRO_VALUE_UNUSED, root_solid, &simprep);
    if (st_init != PRO_TK_NO_ERROR) {
        return false;
    }

    return ProSimprepActivate(root_solid, &simprep) == PRO_TK_NO_ERROR;
}

std::vector<core::Dwg3Candidate> CollectDrawingViewCandidates(ProMdl root)
{
    std::vector<core::Dwg3Candidate> result;
    if (root == nullptr) {
        return result;
    }

    const ProMdlType root_type = autobbox::creo::ModelType(root);
    if (root_type == PRO_MDL_PART) {
        result.push_back(MakeDrawingCandidate(root, 1, 0));
        return result;
    }

    if (root_type != PRO_MDL_ASSEMBLY) {
        return result;
    }

    Drawing3CollectCtx ctx;
    ProSolidDispCompVisit(reinterpret_cast<ProSolid>(root), CollectDrawingCandidateVisitAction, nullptr, &ctx);
    return BuildDrawingCandidatesFromCollected(ctx);
}

std::vector<core::Dwg3Candidate> CollectDrawingViewCandidatesForSimprep(
    ProSolid root_solid,
    const core::Dwg3SimprepOption *option)
{
    if (root_solid == nullptr) {
        return {};
    }

    const ProMdl root = reinterpret_cast<ProMdl>(root_solid);
    const ProMdlType root_type = autobbox::creo::ModelType(root);
    if (root_type != PRO_MDL_ASSEMBLY) {
        return CollectDrawingViewCandidates(root);
    }

    if (option == nullptr || option->use_current_active) {
        return CollectDrawingViewCandidates(root);
    }

    if (option->use_master_rep) {
        Drawing3CollectCtx ctx;
        CollectAllAssemblyCandidatesRecursive(root_solid, ctx);
        return BuildDrawingCandidatesFromCollected(ctx);
    }

    if (option->rep_name.empty()) {
        return {};
    }

    ProName rep_name = {0};
    CopyWStr(rep_name, option->rep_name.c_str());
    ProSimprep simprep = {};
    if (ProSimprepInit(rep_name, PRO_VALUE_UNUSED, root_solid, &simprep) != PRO_TK_NO_ERROR) {
        return CollectDrawingViewCandidates(root);
    }

    ProRule rule = {};
    if (ProRuleInitRep(&simprep, &rule) != PRO_TK_NO_ERROR) {
        return CollectDrawingViewCandidates(root);
    }

    ProAsmcomppath *comp_paths = nullptr;
    int path_count = 0;
    const ProError st_eval = ProRuleEval(root_solid, &rule, &comp_paths, &path_count);
    if (st_eval != PRO_TK_NO_ERROR || comp_paths == nullptr || path_count <= 0) {
        if (comp_paths != nullptr) {
            ProArrayFree(reinterpret_cast<ProArray *>(&comp_paths));
        }
        return {};
    }

    std::vector<core::Dwg3Candidate> result = BuildDrawingCandidatesFromRulePaths(comp_paths, path_count);
    ProArrayFree(reinterpret_cast<ProArray *>(&comp_paths));
    return result;
}

std::vector<core::Dwg3Candidate> CollectDrawingViewCandidatesFromBomTable(
    ProDwgtable table,
    int segment)
{
    return CollectDrawingViewCandidatesFromBomTable(table, segment, Drawing3LogSink{});
}

std::vector<core::Dwg3Candidate> CollectDrawingViewCandidatesFromBomTable(
    ProDwgtable table,
    int segment,
    const Drawing3LogSink &log_sink)
{
    std::vector<core::Dwg3Candidate> result;
    if (table.owner == nullptr) {
        LogLine(log_sink, "Drawing3 bom collect skip=null-table");
        return result;
    }

    int first_row = 0;
    int last_row = 0;
    int first_col = 0;
    int last_col = 0;
    const int extents_segment = (segment <= 0) ? PRO_VALUE_UNUSED : segment;
    ProError st_extents =
        ProDwgtableSegExtentsGet(&table, extents_segment, &first_row, &last_row, &first_col, &last_col);
    LogLine(log_sink,
            "Drawing3 bom extents status=%d selected_segment=%d extents_segment=%d rows=%d-%d cols=%d-%d",
            static_cast<int>(st_extents),
            segment,
            extents_segment,
            first_row,
            last_row,
            first_col,
            last_col);
    if (st_extents != PRO_TK_NO_ERROR && extents_segment != PRO_VALUE_UNUSED) {
        st_extents = ProDwgtableSegExtentsGet(
            &table,
            PRO_VALUE_UNUSED,
            &first_row,
            &last_row,
            &first_col,
            &last_col);
        LogLine(log_sink,
                "Drawing3 bom extents fallback status=%d rows=%d-%d cols=%d-%d",
                static_cast<int>(st_extents),
                first_row,
                last_row,
                first_col,
                last_col);
    }
    if (st_extents != PRO_TK_NO_ERROR) {
        int row_count = 0;
        int column_count = 0;
        if (ProDwgtableRowsCount(&table, &row_count) == PRO_TK_NO_ERROR &&
            ProDwgtableColumnsCount(&table, &column_count) == PRO_TK_NO_ERROR &&
            row_count > 0 &&
            column_count > 0) {
            first_row = 1;
            last_row = row_count;
            first_col = 1;
            last_col = column_count;
            st_extents = PRO_TK_NO_ERROR;
        }
        LogLine(log_sink,
                "Drawing3 bom full-table fallback status=%d rows=%d cols=%d",
                static_cast<int>(st_extents),
                row_count,
                column_count);
    }
    if (st_extents != PRO_TK_NO_ERROR || first_row <= 0 || last_row < first_row || first_col <= 0 || last_col < first_col) {
        LogLine(log_sink,
                "Drawing3 bom collect skip=bad-extents status=%d rows=%d-%d cols=%d-%d",
                static_cast<int>(st_extents),
                first_row,
                last_row,
                first_col,
                last_col);
        return result;
    }

    std::unordered_map<std::wstring, size_t> index_by_key;
    std::vector<ProMdl> ordered_models;
    std::vector<int> occurrence_counts;

    for (int row = first_row; row <= last_row; ++row) {
        ProMdl row_model = nullptr;
        for (int col = first_col; col <= last_col; ++col) {
            if (ResolveBomCellModel(&table, col, row, row_model)) {
                break;
            }
        }
        if (row_model == nullptr) {
            continue;
        }

        const std::wstring key = ModelDedupKey(row_model);
        const auto found = index_by_key.find(key);
        if (found == index_by_key.end()) {
            index_by_key[key] = ordered_models.size();
            ordered_models.push_back(row_model);
            occurrence_counts.push_back(1);
        } else {
            ++occurrence_counts[found->second];
        }
    }

    result.reserve(ordered_models.size());
    for (size_t i = 0; i < ordered_models.size(); ++i) {
        result.push_back(MakeDrawingCandidate(ordered_models[i], occurrence_counts[i], i));
    }
    LogLine(log_sink,
            "Drawing3 bom collect done rows=%d-%d cols=%d-%d unique=%d",
            first_row,
            last_row,
            first_col,
            last_col,
            static_cast<int>(result.size()));
    return result;
}

core::Dwg3ProjectionType ResolveDrawingProjectionType(ProDrawing drawing)
{
    if (drawing == nullptr) {
        return core::Dwg3ProjectionType::FirstAngle;
    }

    ProName option = {0};
    CopyWStr(option, L"projection_type");
    ProLine value = {0};
    const ProError st = ProMdlDetailOptionGet(reinterpret_cast<ProMdl>(drawing), option, value);
    if (st != PRO_TK_NO_ERROR || value[0] == L'\0') {
        return core::Dwg3ProjectionType::FirstAngle;
    }

    const std::wstring normalized = UpperWide(TrimWide(value));
    if (normalized == L"THIRD_ANGLE") {
        return core::Dwg3ProjectionType::ThirdAngle;
    }
    return core::Dwg3ProjectionType::FirstAngle;
}

std::wstring JoinDwg3ViewLabels(core::Dwg3ViewMask mask)
{
    std::wstring out;
    for (core::Dwg3ViewType type : AllDwg3ViewTypes()) {
        if ((mask & core::Dwg3ViewBit(type)) == 0) {
            continue;
        }
        if (!out.empty()) {
            out += L"+";
        }
        out += ViewLabel(type);
    }
    if (out.empty()) {
        out = L"<none>";
    }
    return out;
}

void ExecuteDrawing3ViewsTask(ProDrawing drawing,
                              int sheet,
                              int candidates_total,
                              const std::vector<core::Dwg3Candidate> &selected,
                              core::Dwg3ViewMask selected_views,
                              bool quick_mode,
                              const core::Dwg3FrameOptions &frame_options,
                              const ProPoint3d start_point_screen,
                              const Drawing3ModelTagFormatter &format_model_tag,
                              const Drawing3LogSink &log_sink)
{
    if (drawing == nullptr) {
        LogLine(log_sink, "FAIL drawing3 reason=null-drawing");
        return;
    }
    if (selected.empty()) {
        LogLine(log_sink, "Drawing3 no models selected");
        return;
    }
    if (selected_views == 0) {
        LogLine(log_sink, "Drawing3 no views selected");
        return;
    }

    ProPoint3d start_point_draw = {0.0, 0.0, 0.0};
    if (!ScreenToDrawingPoint(drawing, sheet, start_point_screen, start_point_draw)) {
        LogLine(log_sink, "FAIL drawing3 reason=start-point-transform");
        return;
    }

    core::Dwg3SheetLayout layout;
    if (!BuildDrawingSheetLayout(drawing, sheet, selected.size(), start_point_draw, layout)) {
        LogLine(log_sink, "FAIL drawing3 reason=sheet-layout");
        return;
    }

    LogLine(log_sink,
            "Drawing3 layout sheet=%d start_screen_x=%.3f start_screen_y=%.3f start_draw_x=%.3f start_draw_y=%.3f width=%.3f height=%.3f cols=%d rows=%d cell_w=%.3f cell_h=%.3f",
            sheet,
            start_point_screen[0],
            start_point_screen[1],
            layout.start_x,
            layout.start_y,
            layout.width,
            layout.height,
            layout.cols,
            layout.rows,
            layout.cell_w,
            layout.cell_h);

    const double page_scale = ComputeDrawingGroupScale(drawing, sheet);
    const core::Dwg3Spacing spacing = ComputeDrawingSpacing(page_scale);
    const core::Dwg3ProjectionType projection_type = ResolveDrawingProjectionType(drawing);
    LogLine(log_sink, "Drawing3 page_scale=%.6f", page_scale);
    LogLine(log_sink, "Drawing3 projection_type=%s", ProjectionLabel(projection_type));
    LogLine(log_sink,
            "Drawing3 frame_mode=%s symbol=%s version=%d",
            frame_options.mode == core::Dwg3FrameMode::Symbol ? "symbol" : "auto",
            autobbox::common::WToA(frame_options.symbol_file_name.c_str()).c_str(),
            frame_options.symbol_version);
    LogLine(log_sink,
            "Drawing3 spacing side_dx=%.3f iso_dx=%.3f vertical_dy=%.3f gap_x=%.3f",
            spacing.side_dx,
            spacing.iso_dx,
            spacing.vertical_dy,
            spacing.gap_x);

    std::unordered_map<std::wstring, ProView> existing_views = CollectCurrentSheetViewsByName(drawing, sheet);
    int ok_count = 0;
    int fail_count = 0;
    int skip_count = 0;
    std::vector<core::Dwg3PendingDecoration> pending_decorations;
    pending_decorations.reserve(selected.size());
    double next_left_screen = start_point_screen[0];
    const double anchor_top_screen = start_point_screen[1];
    const double temp_factor = std::max(1.0, page_scale / 0.015);
    const ProPoint3d temp_base = {60.0 * temp_factor, 180.0 * temp_factor, 0.0};

    for (size_t i = 0; i < selected.size(); ++i) {
        const core::Dwg3Candidate &cand = selected[i];
        core::Dwg3CreatedViews existing_group = {};
        const core::Dwg3ViewMask existing_mask =
            CollectExistingCandidateViewMask(existing_views, cand, selected_views, existing_group);
        core::Dwg3ViewMask create_mask = selected_views & ~existing_mask;
        const core::Dwg3ViewMask requested_create_mask = create_mask;
        create_mask = ExpandProjectedParentMask(create_mask, existing_mask);
        if (create_mask != requested_create_mask) {
            LogLine(log_sink,
                    "INFO %s auto-parent requested=%s expanded=%s existing=%s",
                    ModelTag(cand.mdl, format_model_tag).c_str(),
                    autobbox::common::WToA(JoinDwg3ViewLabels(requested_create_mask).c_str()).c_str(),
                    autobbox::common::WToA(JoinDwg3ViewLabels(create_mask).c_str()).c_str(),
                    autobbox::common::WToA(JoinDwg3ViewLabels(existing_mask).c_str()).c_str());
        }

        ProPoint3d base_origin = {layout.start_x, layout.start_y, 0.0};
        if (existing_mask != 0 &&
            !ResolveBaseOriginFromExisting(drawing, existing_group, spacing, projection_type, base_origin)) {
            LogLine(log_sink,
                    "WARN %s resolve-existing-anchor failed fallback=start views=%s",
                    ModelTag(cand.mdl, format_model_tag).c_str(),
                    autobbox::common::WToA(JoinDwg3ViewLabels(existing_mask).c_str()).c_str());
        }

        core::Dwg3CreatedViews created_group = {};
        if (create_mask != 0) {
            ProError st = CreateDrawingViewsForMask(
                drawing,
                sheet,
                cand,
                create_mask,
                existing_group,
                temp_base,
                spacing,
                projection_type,
                page_scale,
                created_group);
            if (st != PRO_TK_NO_ERROR) {
                DeleteDrawingViewGroup(drawing, created_group);
                ++fail_count;
                LogLine(log_sink,
                        "FAIL %s reason=create-dwgviews status=%d views=%s",
                        ModelTag(cand.mdl, format_model_tag).c_str(),
                        static_cast<int>(st),
                        autobbox::common::WToA(JoinDwg3ViewLabels(create_mask).c_str()).c_str());
                continue;
            }

            const core::Dwg3ViewMask pack_mask = create_mask & ~core::Dwg3ViewBit(core::Dwg3ViewType::Front);
            if (existing_mask == 0) {
                if (pack_mask != 0) {
                    st = PackCreatedViewsByOutline(drawing, created_group, pack_mask, page_scale, projection_type);
                    if (st != PRO_TK_NO_ERROR) {
                        DeleteDrawingViewGroup(drawing, created_group);
                        ++fail_count;
                        LogLine(log_sink,
                                "FAIL %s reason=pack-new-group status=%d views=%s",
                                ModelTag(cand.mdl, format_model_tag).c_str(),
                                static_cast<int>(st),
                                autobbox::common::WToA(JoinDwg3ViewLabels(create_mask).c_str()).c_str());
                        continue;
                    }
                }

                core::Dwg3GroupOutline temp_outline = {};
                st = GetDrawingViewGroupOutline(drawing, created_group, temp_outline);
                if (st != PRO_TK_NO_ERROR) {
                    DeleteDrawingViewGroup(drawing, created_group);
                    ++fail_count;
                    LogLine(log_sink,
                            "FAIL %s reason=temp-outline status=%d views=%s",
                            ModelTag(cand.mdl, format_model_tag).c_str(),
                            static_cast<int>(st),
                            autobbox::common::WToA(JoinDwg3ViewLabels(create_mask).c_str()).c_str());
                    continue;
                }

                const core::Dwg3GroupOutline temp_decorated = ExpandDecoratedOutline(temp_outline, page_scale, false);
                const double dx = next_left_screen - temp_decorated.min_x;
                const double dy = anchor_top_screen - temp_decorated.max_y;
                st = MoveCreatedViewsByScreenDelta(drawing, created_group, create_mask, dx, dy);
                if (st != PRO_TK_NO_ERROR) {
                    DeleteDrawingViewGroup(drawing, created_group);
                    ++fail_count;
                    LogLine(log_sink,
                            "FAIL %s reason=move-screen-delta status=%d views=%s dx=%.3f dy=%.3f",
                            ModelTag(cand.mdl, format_model_tag).c_str(),
                            static_cast<int>(st),
                            autobbox::common::WToA(JoinDwg3ViewLabels(create_mask).c_str()).c_str(),
                            dx,
                            dy);
                    continue;
                }
            } else {
                st = SetDrawingViewOriginsForMask(drawing, created_group, create_mask, base_origin, spacing, projection_type);
                if (st != PRO_TK_NO_ERROR) {
                    DeleteDrawingViewGroup(drawing, created_group);
                    ++fail_count;
                    LogLine(log_sink,
                            "FAIL %s reason=set-dwgview-origin status=%d views=%s",
                            ModelTag(cand.mdl, format_model_tag).c_str(),
                            static_cast<int>(st),
                            autobbox::common::WToA(JoinDwg3ViewLabels(create_mask).c_str()).c_str());
                    continue;
                }

                if (pack_mask != 0) {
                    core::Dwg3CreatedViews pack_group = existing_group;
                    for (core::Dwg3ViewType type : AllDwg3ViewTypes()) {
                        ProView created_view = CreatedViewSlot(created_group, type);
                        if (created_view != nullptr) {
                            CreatedViewSlot(pack_group, type) = created_view;
                        }
                    }
                    st = PackCreatedViewsByOutline(drawing, pack_group, pack_mask, page_scale, projection_type);
                    if (st != PRO_TK_NO_ERROR) {
                        DeleteDrawingViewGroup(drawing, created_group);
                        ++fail_count;
                        LogLine(log_sink,
                                "FAIL %s reason=pack-existing-group status=%d views=%s",
                                ModelTag(cand.mdl, format_model_tag).c_str(),
                                static_cast<int>(st),
                                autobbox::common::WToA(JoinDwg3ViewLabels(create_mask).c_str()).c_str());
                        continue;
                    }
                }
            }
        }

        core::Dwg3CreatedViews combined_group = existing_group;
        for (core::Dwg3ViewType type : AllDwg3ViewTypes()) {
            ProView created_view = CreatedViewSlot(created_group, type);
            if (created_view != nullptr) {
                CreatedViewSlot(combined_group, type) = created_view;
                existing_views[GetDrawingViewName(cand, type)] = created_view;
            }
        }

        core::Dwg3GroupOutline placed = {};
        const ProError st_outline = GetDrawingViewGroupOutline(drawing, combined_group, placed);
        const core::Dwg3GroupOutline decorated_outline =
            (st_outline == PRO_TK_NO_ERROR)
                ? ExpandDecoratedOutline(placed, page_scale, false)
                : placed;

        if (create_mask == 0) {
            if (st_outline == PRO_TK_NO_ERROR) {
                next_left_screen = decorated_outline.max_x + spacing.gap_x;
            }
            ++skip_count;
            LogLine(log_sink,
                    "SKIP %s reason=existing-selected-views views=%s left=%.3f top=%.3f w=%.3f h=%.3f",
                    ModelTag(cand.mdl, format_model_tag).c_str(),
                    autobbox::common::WToA(JoinDwg3ViewLabels(selected_views).c_str()).c_str(),
                    placed.min_x,
                    placed.max_y,
                    placed.max_x - placed.min_x,
                    placed.max_y - placed.min_y);
            continue;
        }

        if (existing_mask == 0 && st_outline == PRO_TK_NO_ERROR) {
            if (quick_mode) {
                core::Dwg3PendingDecoration pending;
                pending.cand = cand;
                pending.index = i;
                pending.total = selected.size();
                pending.views = combined_group;
                pending.outline = placed;
                pending_decorations.push_back(pending);
            } else {
                TryAnnotateGroup(
                    drawing,
                    cand,
                    i,
                    selected.size(),
                    page_scale,
                    combined_group,
                    placed,
                    frame_options,
                    format_model_tag,
                    log_sink);
            }
        }

        if (st_outline == PRO_TK_NO_ERROR) {
            next_left_screen = decorated_outline.max_x + spacing.gap_x;
        }

        ++ok_count;
        LogLine(log_sink,
                "OK   %s created=%s existing=%s scale=%.4f left=%.3f top=%.3f w=%.3f h=%.3f",
                ModelTag(cand.mdl, format_model_tag).c_str(),
                autobbox::common::WToA(JoinDwg3ViewLabels(create_mask).c_str()).c_str(),
                autobbox::common::WToA(JoinDwg3ViewLabels(existing_mask).c_str()).c_str(),
                page_scale,
                placed.min_x,
                placed.max_y,
                placed.max_x - placed.min_x,
                placed.max_y - placed.min_y);
    }

    if (quick_mode && !pending_decorations.empty()) {
        LogLine(log_sink, "Drawing3 quick-annotate start count=%d", static_cast<int>(pending_decorations.size()));
        for (const core::Dwg3PendingDecoration &pending : pending_decorations) {
            TryAnnotateGroup(
                drawing,
                pending.cand,
                pending.index,
                pending.total,
                page_scale,
                pending.views,
                pending.outline,
                frame_options,
                format_model_tag,
                log_sink);
        }
        LogLine(log_sink, "Drawing3 quick-annotate done count=%d", static_cast<int>(pending_decorations.size()));
    }

    const int uncreated_count = skip_count + fail_count;

    LogLine(log_sink,
            "Summary mode=dwg3 sheet=%d targets=%d selected=%d views=%s ok=%d skip=%d fail=%d uncreated=%d pending=%d",
            sheet,
            candidates_total,
            static_cast<int>(selected.size()),
            autobbox::common::WToA(JoinDwg3ViewLabels(selected_views).c_str()).c_str(),
            ok_count,
            skip_count,
            fail_count,
            uncreated_count,
            static_cast<int>(pending_decorations.size()));

    LogLine(log_sink,
            "Summary detail mode=dwg3 created=%d uncreated=%d existing-only=%d failed=%d",
            ok_count,
            uncreated_count,
            skip_count,
            fail_count);
}

} // namespace autobbox::application
