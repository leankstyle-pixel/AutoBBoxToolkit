#include "autobbox/application/drawing_export.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"

#include <ProArray.h>
#include <ProDrawing.h>
#include <ProMdl.h>
#include <ProPDF.h>
#include <ProPrint.h>
#include <ProToolkit.h>
#include <ProUtil.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace autobbox::application {

namespace {

void LogLine(const DrawingExportLogSink &log_sink, const char *fmt, ...)
{
    if (!log_sink || fmt == nullptr) {
        return;
    }

    char buffer[2048] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_sink(buffer);
}

ProError CopyWideToFixed(const wchar_t *text, wchar_t *out, size_t out_size)
{
    if (text == nullptr || out == nullptr || out_size == 0 || std::wcslen(text) >= out_size) {
        return PRO_TK_BAD_INPUTS;
    }
#if defined(_MSC_VER)
    wcsncpy_s(out, out_size, text, _TRUNCATE);
#else
    std::wcsncpy(out, text, out_size - 1);
    out[out_size - 1] = L'\0';
#endif
    return PRO_TK_NO_ERROR;
}

bool WriteNoDxfDwgMappingFile(const std::wstring &path, const DrawingExportLogSink &log_sink)
{
    if (path.empty()) {
        return false;
    }

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        LogLine(log_sink,
                "drawing-export dxf-map no-map-file-create failed win32=%lu path=%s",
                static_cast<unsigned long>(GetLastError()),
                autobbox::common::WToA(path.c_str()).c_str());
        return false;
    }

    const char content[] =
        "! AutoBBox temporary DXF/DWG export mapping file.\r\n"
        "! Intentionally contains no map_color/map_layer/map_line_style entries.\r\n";
    DWORD written = 0;
    const BOOL ok = WriteFile(file, content, static_cast<DWORD>(sizeof(content) - 1), &written, nullptr);
    const DWORD write_error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(path.c_str());
        LogLine(log_sink,
                "drawing-export dxf-map no-map-file-write failed win32=%lu path=%s",
                static_cast<unsigned long>(write_error),
                autobbox::common::WToA(path.c_str()).c_str());
        return false;
    }
    return true;
}

struct ScopedDxfDwgMappingOption {
    ScopedDxfDwgMappingOption(bool apply_option,
                              bool enable_mapping,
                              const std::wstring &no_mapping_file_path,
                              const DrawingExportLogSink &log_sink)
        : log_sink(log_sink)
    {
        if (!apply_option) {
            return;
        }

        ProName option = {0};
        ProError st_option = CopyWideToFixed(kOptionName, option, PRO_NAME_SIZE);
        if (st_option != PRO_TK_NO_ERROR) {
            LogLine(log_sink, "drawing-export dxf-map option-name-invalid status=%d", static_cast<int>(st_option));
            return;
        }

        ProError st_get = ProConfigoptionGet(option, original_value);
        original_captured = (st_get == PRO_TK_NO_ERROR || st_get == PRO_TK_LINE_TOO_LONG);
        if (st_get == PRO_TK_LINE_TOO_LONG) {
            LogLine(log_sink, "drawing-export dxf-map original-truncated");
        }

        if (!enable_mapping && !original_captured) {
            LogLine(log_sink,
                    "drawing-export dxf-map requested=0 get_status=%d original_captured=0 action=already-unset",
                    static_cast<int>(st_get));
            return;
        }

        if (!enable_mapping) {
            if (!WriteNoDxfDwgMappingFile(no_mapping_file_path, log_sink)) {
                return;
            }
            no_mapping_file = no_mapping_file_path;
        }

        const wchar_t *target_value = enable_mapping ? kOfficialMappingPath : no_mapping_file.c_str();
        if (enable_mapping && !autobbox::common::FileExistsW(kOfficialMappingPath)) {
            LogLine(log_sink,
                    "drawing-export dxf-map official-file-missing path=%s",
                    autobbox::common::WToA(kOfficialMappingPath).c_str());
            return;
        }

        ProPath target = {0};
        ProError st_value = CopyWideToFixed(target_value, target, PRO_PATH_SIZE);
        if (st_value != PRO_TK_NO_ERROR) {
            LogLine(log_sink, "drawing-export dxf-map target-invalid status=%d", static_cast<int>(st_value));
            return;
        }

        const ProError st_set = ProConfigoptSet(option, target);
        active = (st_set == PRO_TK_NO_ERROR);
        LogLine(log_sink,
                "drawing-export dxf-map requested=%d get_status=%d set_status=%d original_captured=%d target=%s mode=%s",
                enable_mapping ? 1 : 0,
                static_cast<int>(st_get),
                static_cast<int>(st_set),
                original_captured ? 1 : 0,
                autobbox::common::WToA(target_value).c_str(),
                enable_mapping ? "official" : "no-map");
    }

    ~ScopedDxfDwgMappingOption()
    {
        if (!active) {
            return;
        }

        ProName option = {0};
        ProError st_option = CopyWideToFixed(kOptionName, option, PRO_NAME_SIZE);
        ProPath restore_value = {0};
        ProError st_value = PRO_TK_NO_ERROR;
        if (st_option == PRO_TK_NO_ERROR) {
            st_value = original_captured
                           ? CopyWideToFixed(original_value, restore_value, PRO_PATH_SIZE)
                           : CopyWideToFixed(L"", restore_value, PRO_PATH_SIZE);
        }
        ProError st_restore = PRO_TK_GENERAL_ERROR;
        if (st_option == PRO_TK_NO_ERROR && st_value == PRO_TK_NO_ERROR) {
            st_restore = ProConfigoptSet(option, restore_value);
        }

        LogLine(log_sink,
                "drawing-export dxf-map restore original_captured=%d option_status=%d value_status=%d restore_status=%d",
                original_captured ? 1 : 0,
                static_cast<int>(st_option),
                static_cast<int>(st_value),
                static_cast<int>(st_restore));
        if (!no_mapping_file.empty()) {
            const BOOL deleted = DeleteFileW(no_mapping_file.c_str());
            LogLine(log_sink,
                    "drawing-export dxf-map no-map-file-delete deleted=%d path=%s",
                    deleted ? 1 : 0,
                    autobbox::common::WToA(no_mapping_file.c_str()).c_str());
        }
    }

    static constexpr const wchar_t *kOptionName = L"intf2d_out_dxf_mapping_file";
    static constexpr const wchar_t *kOfficialMappingPath =
        L"D:\\Program Files\\PTC\\Creo 10.0.8.0\\Common Files\\text\\intf_configs\\dxf_export.pro";

    const DrawingExportLogSink &log_sink;
    bool active = false;
    bool original_captured = false;
    ProPath original_value = {0};
    std::wstring no_mapping_file;
};

bool IsInvalidFileNameChar(wchar_t ch)
{
    if (ch < 32) {
        return true;
    }
    switch (ch) {
    case L'<':
    case L'>':
    case L':':
    case L'"':
    case L'/':
    case L'\\':
    case L'|':
    case L'?':
    case L'*':
        return true;
    default:
        return false;
    }
}

std::wstring TrimTrailingDotsAndSpaces(std::wstring text)
{
    while (!text.empty() && (text.back() == L'.' || text.back() == L' ' || text.back() == L'\t')) {
        text.pop_back();
    }
    while (!text.empty() && (text.front() == L' ' || text.front() == L'\t')) {
        text.erase(text.begin());
    }
    return text;
}

std::wstring SanitizeFileStem(std::wstring text, const wchar_t *fallback)
{
    for (wchar_t &ch : text) {
        if (IsInvalidFileNameChar(ch)) {
            ch = L'_';
        }
    }
    text = TrimTrailingDotsAndSpaces(text);
    if (text.empty()) {
        return fallback == nullptr ? L"export" : fallback;
    }
    return text;
}

std::wstring TwoDigit(int value)
{
    wchar_t buffer[8] = {0};
    std::swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%02d", value);
    return buffer;
}

bool ContainsNonAscii(const std::wstring &text)
{
    for (const wchar_t ch : text) {
        if (ch > 127) {
            return true;
        }
    }
    return false;
}

std::wstring AsciiSafeStem(std::wstring text, const wchar_t *fallback)
{
    for (wchar_t &ch : text) {
        if (ch > 127 || IsInvalidFileNameChar(ch) || ch == L' ') {
            ch = L'_';
            continue;
        }
        const bool keep =
            (ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') ||
            ch == L'_' || ch == L'-' || ch == L'.';
        if (!keep) {
            ch = L'_';
        }
    }
    return SanitizeFileStem(text, fallback);
}

std::wstring ExportTimestamp()
{
    SYSTEMTIME now = {};
    GetLocalTime(&now);
    wchar_t buffer[32] = {0};
    std::swprintf(buffer,
                  sizeof(buffer) / sizeof(buffer[0]),
                  L"%04d%02d%02d_%02d%02d%02d",
                  static_cast<int>(now.wYear),
                  static_cast<int>(now.wMonth),
                  static_cast<int>(now.wDay),
                  static_cast<int>(now.wHour),
                  static_cast<int>(now.wMinute),
                  static_cast<int>(now.wSecond));
    return buffer;
}

std::wstring UniqueOutputPath(const std::wstring &dir,
                              const std::wstring &stem,
                              const wchar_t *extension)
{
    const std::wstring ext = extension == nullptr ? L"" : extension;
    std::wstring path = autobbox::common::JoinPath(dir, (stem + ext).c_str());
    if (!autobbox::common::FileExistsW(path)) {
        return path;
    }

    const std::wstring timestamp = ExportTimestamp();
    for (int i = 0; i < 1000; ++i) {
        wchar_t suffix[32] = {0};
        if (i == 0) {
            std::swprintf(suffix, sizeof(suffix) / sizeof(suffix[0]), L"_%s", timestamp.c_str());
        } else {
            std::swprintf(suffix, sizeof(suffix) / sizeof(suffix[0]), L"_%s_%03d", timestamp.c_str(), i);
        }
        path = autobbox::common::JoinPath(dir, (stem + suffix + ext).c_str());
        if (!autobbox::common::FileExistsW(path)) {
            return path;
        }
    }

    return autobbox::common::JoinPath(dir, (stem + L"_" + timestamp + ext).c_str());
}

std::wstring ParentDirectory(const std::wstring &path)
{
    const std::wstring::size_type slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return std::wstring();
    }
    return path.substr(0, slash);
}

std::wstring FileExtension(const std::wstring &path)
{
    const std::wstring::size_type slash = path.find_last_of(L"\\/");
    const std::wstring::size_type dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) {
        return std::wstring();
    }
    return path.substr(dot);
}

std::wstring FileStemOnly(const std::wstring &path)
{
    const std::wstring::size_type slash = path.find_last_of(L"\\/");
    const std::wstring::size_type begin = slash == std::wstring::npos ? 0 : slash + 1;
    const std::wstring::size_type dot = path.find_last_of(L'.');
    const std::wstring::size_type end =
        (dot == std::wstring::npos || dot < begin) ? path.size() : dot;
    return path.substr(begin, end - begin);
}

std::wstring TemporaryAsciiExportPath(const std::wstring &desired_path)
{
    const std::wstring dir = ParentDirectory(desired_path);
    const std::wstring ext = FileExtension(desired_path);
    const std::wstring stem = AsciiSafeStem(FileStemOnly(desired_path), L"export");
    return UniqueOutputPath(dir, stem + L"_ascii_tmp", ext.c_str());
}

ProError CopyToProPath(const std::wstring &path, ProPath out)
{
    if (out == nullptr || path.empty() || path.size() >= PRO_PATH_SIZE) {
        return PRO_TK_BAD_INPUTS;
    }
#if defined(_MSC_VER)
    wcsncpy_s(out, PRO_PATH_SIZE, path.c_str(), _TRUNCATE);
#else
    std::wcsncpy(out, path.c_str(), PRO_PATH_SIZE - 1);
    out[PRO_PATH_SIZE - 1] = L'\0';
#endif
    return PRO_TK_NO_ERROR;
}

std::wstring SheetStem(ProDrawing drawing, int sheet)
{
    ProName name = {0};
    if (drawing != nullptr &&
        ProDrawingSheetNameGet(drawing, sheet, name) == PRO_TK_NO_ERROR &&
        name[0] != L'\0') {
        return SanitizeFileStem(name, L"sheet");
    }

    wchar_t fallback[32] = {0};
    std::swprintf(fallback, sizeof(fallback) / sizeof(fallback[0]), L"sheet%02d", sheet);
    return fallback;
}

std::wstring JoinDrawingSheetStem(const std::wstring &drawing_stem,
                                  ProDrawing drawing,
                                  int sheet)
{
    return SanitizeFileStem(drawing_stem + L"_" + SheetStem(drawing, sheet), L"drawing_sheet");
}

ProError FillSelectedSheetArray(const std::vector<int> &sheets, int **sheets_out)
{
    if (sheets_out == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }
    *sheets_out = nullptr;
    if (sheets.empty()) {
        return PRO_TK_BAD_INPUTS;
    }

    ProError st = ProArrayAlloc(0, sizeof(int), 1, reinterpret_cast<ProArray *>(sheets_out));
    if (st != PRO_TK_NO_ERROR || *sheets_out == nullptr) {
        return st;
    }

    st = ProArrayObjectAdd(
        reinterpret_cast<ProArray *>(sheets_out),
        PRO_VALUE_UNUSED,
        static_cast<int>(sheets.size()),
        const_cast<int *>(sheets.data()));
    if (st != PRO_TK_NO_ERROR) {
        ProArrayFree(reinterpret_cast<ProArray *>(sheets_out));
        *sheets_out = nullptr;
    }
    return st;
}

ProError Export2dFileRaw(ProMdl model,
                         ProImportExportFile format,
                         const std::wstring &path,
                         Pro2dExportSheetOption sheet_option,
                         const std::vector<int> &selected_sheets,
                         int selected_sheet_for_log,
                         const DrawingExportLogSink &log_sink)
{
    ProPath pro_path = {0};
    ProError st = CopyToProPath(path, pro_path);
    if (st != PRO_TK_NO_ERROR) {
        LogLine(log_sink,
                "drawing-export 2d path-invalid status=%d path=%s",
                static_cast<int>(st),
                autobbox::common::WToA(path.c_str()).c_str());
        return st;
    }

    Pro2dExportdata data = nullptr;
    st = Pro2dExportdataAlloc(&data);
    if (st != PRO_TK_NO_ERROR || data == nullptr) {
        LogLine(log_sink, "drawing-export Pro2dExportdataAlloc status=%d", static_cast<int>(st));
        return st;
    }

    st = Pro2dExportdataSheetoptionSet(data, sheet_option);
    int *selected_sheets_array = nullptr;
    if (st == PRO_TK_NO_ERROR && sheet_option == PRO2DEXPORT_SELECTED) {
        st = FillSelectedSheetArray(selected_sheets, &selected_sheets_array);
        if (st == PRO_TK_NO_ERROR) {
            st = Pro2dExportdataSheetsSet(data, selected_sheets_array);
        }
    }

    if (st == PRO_TK_NO_ERROR) {
        st = Pro2dExport(format, pro_path, model, data);
    }

    if (selected_sheets_array != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&selected_sheets_array));
    }
    const ProError free_st = Pro2dExportdataFree(data);
    LogLine(log_sink,
            "drawing-export Pro2dExport format=%d sheet_option=%d selected_sheet=%d status=%d free_status=%d path=%s",
            static_cast<int>(format),
            static_cast<int>(sheet_option),
            selected_sheet_for_log,
            static_cast<int>(st),
            static_cast<int>(free_st),
            autobbox::common::WToA(path.c_str()).c_str());
    return st;
}

ProError Export2dFile(ProMdl model,
                      ProImportExportFile format,
                      const std::wstring &path,
                      Pro2dExportSheetOption sheet_option,
                      int selected_sheet,
                      const DrawingExportLogSink &log_sink)
{
    const std::vector<int> selected_sheets = selected_sheet > 0 ? std::vector<int>{selected_sheet} : std::vector<int>{};
    ProError st = Export2dFileRaw(model, format, path, sheet_option, selected_sheets, selected_sheet, log_sink);
    if (st == PRO_TK_NO_ERROR || !ContainsNonAscii(path) ||
        (format != PRO_DWG_FILE && format != PRO_DXF_FILE)) {
        return st;
    }

    const std::wstring temp_path = TemporaryAsciiExportPath(path);
    LogLine(log_sink,
            "drawing-export retry ascii-temp original=%s temp=%s",
            autobbox::common::WToA(path.c_str()).c_str(),
            autobbox::common::WToA(temp_path.c_str()).c_str());
    st = Export2dFileRaw(model, format, temp_path, sheet_option, selected_sheets, selected_sheet, log_sink);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }

    if (MoveFileExW(temp_path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) == FALSE) {
        const DWORD move_error = GetLastError();
        DeleteFileW(temp_path.c_str());
        LogLine(log_sink,
                "drawing-export ascii-temp-rename failed win32=%lu original=%s temp=%s",
                static_cast<unsigned long>(move_error),
                autobbox::common::WToA(path.c_str()).c_str(),
                autobbox::common::WToA(temp_path.c_str()).c_str());
        return PRO_TK_CANT_WRITE;
    }

    LogLine(log_sink,
            "drawing-export ascii-temp-rename success original=%s temp=%s",
            autobbox::common::WToA(path.c_str()).c_str(),
            autobbox::common::WToA(temp_path.c_str()).c_str());
    return PRO_TK_NO_ERROR;
}

ProError Export2dSelectedSheetsFile(ProMdl model,
                                    ProImportExportFile format,
                                    const std::wstring &path,
                                    const std::vector<int> &selected_sheets,
                                    const DrawingExportLogSink &log_sink)
{
    const int first_sheet = selected_sheets.empty() ? 0 : selected_sheets.front();
    ProError st = Export2dFileRaw(
        model,
        format,
        path,
        PRO2DEXPORT_SELECTED,
        selected_sheets,
        first_sheet,
        log_sink);
    if (st == PRO_TK_NO_ERROR || !ContainsNonAscii(path) ||
        (format != PRO_DWG_FILE && format != PRO_DXF_FILE)) {
        return st;
    }

    const std::wstring temp_path = TemporaryAsciiExportPath(path);
    LogLine(log_sink,
            "drawing-export selected retry ascii-temp original=%s temp=%s sheets=%d",
            autobbox::common::WToA(path.c_str()).c_str(),
            autobbox::common::WToA(temp_path.c_str()).c_str(),
            static_cast<int>(selected_sheets.size()));
    st = Export2dFileRaw(
        model,
        format,
        temp_path,
        PRO2DEXPORT_SELECTED,
        selected_sheets,
        first_sheet,
        log_sink);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }

    if (MoveFileExW(temp_path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) == FALSE) {
        const DWORD move_error = GetLastError();
        DeleteFileW(temp_path.c_str());
        LogLine(log_sink,
                "drawing-export selected ascii-temp-rename failed win32=%lu original=%s temp=%s",
                static_cast<unsigned long>(move_error),
                autobbox::common::WToA(path.c_str()).c_str(),
                autobbox::common::WToA(temp_path.c_str()).c_str());
        return PRO_TK_CANT_WRITE;
    }

    LogLine(log_sink,
            "drawing-export selected ascii-temp-rename success original=%s temp=%s",
            autobbox::common::WToA(path.c_str()).c_str(),
            autobbox::common::WToA(temp_path.c_str()).c_str());
    return PRO_TK_NO_ERROR;
}

ProError ExportCurrentSheet2dRaw(ProMdl model,
                                 ProImportExportFile format,
                                 const std::wstring &path,
                                 const DrawingExportLogSink &log_sink)
{
    ProPath pro_path = {0};
    const ProError st_path = CopyToProPath(path, pro_path);
    if (st_path != PRO_TK_NO_ERROR) {
        LogLine(log_sink,
                "drawing-export 2d-current path-invalid status=%d path=%s",
                static_cast<int>(st_path),
                autobbox::common::WToA(path.c_str()).c_str());
        return st_path;
    }

    const ProError st = Pro2dExport(format, pro_path, model, nullptr);
    LogLine(log_sink,
            "drawing-export Pro2dExport-current format=%d status=%d path=%s",
            static_cast<int>(format),
            static_cast<int>(st),
            autobbox::common::WToA(path.c_str()).c_str());
    return st;
}

ProError ExportCurrentSheet2dFile(ProMdl model,
                                  ProImportExportFile format,
                                  const std::wstring &path,
                                  const DrawingExportLogSink &log_sink)
{
    ProError st = ExportCurrentSheet2dRaw(model, format, path, log_sink);
    if (st == PRO_TK_NO_ERROR || !ContainsNonAscii(path) ||
        (format != PRO_DWG_FILE && format != PRO_DXF_FILE)) {
        return st;
    }

    const std::wstring temp_path = TemporaryAsciiExportPath(path);
    LogLine(log_sink,
            "drawing-export current retry ascii-temp original=%s temp=%s",
            autobbox::common::WToA(path.c_str()).c_str(),
            autobbox::common::WToA(temp_path.c_str()).c_str());
    st = ExportCurrentSheet2dRaw(model, format, temp_path, log_sink);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }

    if (MoveFileExW(temp_path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) == FALSE) {
        const DWORD move_error = GetLastError();
        DeleteFileW(temp_path.c_str());
        LogLine(log_sink,
                "drawing-export current ascii-temp-rename failed win32=%lu original=%s temp=%s",
                static_cast<unsigned long>(move_error),
                autobbox::common::WToA(path.c_str()).c_str(),
                autobbox::common::WToA(temp_path.c_str()).c_str());
        return PRO_TK_CANT_WRITE;
    }

    LogLine(log_sink,
            "drawing-export current ascii-temp-rename success original=%s temp=%s",
            autobbox::common::WToA(path.c_str()).c_str(),
            autobbox::common::WToA(temp_path.c_str()).c_str());
    return PRO_TK_NO_ERROR;
}

ProError ExportPerSheet2dFile(ProDrawing drawing,
                              ProImportExportFile format,
                              const std::wstring &path,
                              int sheet,
                              const DrawingExportLogSink &log_sink)
{
    if (drawing == nullptr || sheet < 1) {
        return PRO_TK_BAD_INPUTS;
    }

    const ProError st_set = ProDrawingCurrentSheetSet(drawing, sheet);
    if (st_set != PRO_TK_NO_ERROR) {
        LogLine(log_sink,
                "drawing-export set-current-sheet status=%d sheet=%d path=%s",
                static_cast<int>(st_set),
                sheet,
                autobbox::common::WToA(path.c_str()).c_str());
        return st_set;
    }

    return ExportCurrentSheet2dFile(reinterpret_cast<ProMdl>(drawing), format, path, log_sink);
}

ProError ExportPdfFile(ProMdl model,
                       const std::wstring &path,
                       const DrawingExportLogSink &log_sink)
{
    ProPath pro_path = {0};
    ProError st = CopyToProPath(path, pro_path);
    if (st != PRO_TK_NO_ERROR) {
        LogLine(log_sink,
                "drawing-export pdf path-invalid status=%d path=%s",
                static_cast<int>(st),
                autobbox::common::WToA(path.c_str()).c_str());
        return st;
    }

    ProPDFOptions options = nullptr;
    st = ProPDFoptionsAlloc(&options);
    if (st != PRO_TK_NO_ERROR || options == nullptr) {
        LogLine(log_sink, "drawing-export ProPDFoptionsAlloc status=%d", static_cast<int>(st));
        return st;
    }

    ProError prop_st = ProPDFoptionsIntpropertySet(options, PRO_PDFOPT_EXPORT_MODE, PRO_PDF_2D_DRAWING);
    if (prop_st == PRO_TK_NO_ERROR) {
        prop_st = ProPDFoptionsIntpropertySet(options, PRO_PDFOPT_SHEETS, PRINT_ALL_SHEETS);
    }
    ProPDFoptionsBoolpropertySet(options, PRO_PDFOPT_LAUNCH_VIEWER, PRO_B_FALSE);

    if (prop_st == PRO_TK_NO_ERROR) {
        st = ProPDFExport(model, pro_path, options);
    } else {
        st = prop_st;
    }

    const ProError free_st = ProPDFoptionsFree(options);
    LogLine(log_sink,
            "drawing-export ProPDFExport status=%d options_status=%d free_status=%d path=%s",
            static_cast<int>(st),
            static_cast<int>(prop_st),
            static_cast<int>(free_st),
            autobbox::common::WToA(path.c_str()).c_str());
    return st;
}

void RecordExport(core::DrawingExportResult &result, const std::wstring &path, int sheet, ProError st)
{
    core::DrawingExportedFile file = {};
    file.path = path;
    file.sheet = sheet;
    file.status = st;
    result.files.push_back(file);
    ++result.exports_attempted;
    if (st == PRO_TK_NO_ERROR) {
        ++result.exports_succeeded;
    } else if (result.status == PRO_TK_NO_ERROR || result.status == PRO_TK_GENERAL_ERROR) {
        result.status = st;
    }
}

std::vector<int> NormalizeSelectedSheets(const core::DrawingExportRequest &request, int sheets_total)
{
    std::vector<int> sheets;
    for (const int sheet : request.selected_sheets) {
        if (sheet >= 1 && sheet <= sheets_total &&
            std::find(sheets.begin(), sheets.end(), sheet) == sheets.end()) {
            sheets.push_back(sheet);
        }
    }
    if (sheets.empty()) {
        const int fallback = request.selected_sheet >= 1 && request.selected_sheet <= sheets_total
                                 ? request.selected_sheet
                                 : 1;
        sheets.push_back(fallback);
    }
    std::sort(sheets.begin(), sheets.end());
    return sheets;
}

bool ContainsAllSheets(const std::vector<int> &selected_sheets, int sheets_total)
{
    if (static_cast<int>(selected_sheets.size()) != sheets_total) {
        return false;
    }
    for (int sheet = 1; sheet <= sheets_total; ++sheet) {
        if (selected_sheets[static_cast<size_t>(sheet - 1)] != sheet) {
            return false;
        }
    }
    return true;
}

} // namespace

const wchar_t *DrawingExportFormatExtension(core::DrawingExportFormat format)
{
    switch (format) {
    case core::DrawingExportFormat::Dwg:
        return L".dwg";
    case core::DrawingExportFormat::Pdf:
        return L".pdf";
    case core::DrawingExportFormat::Dxf:
        return L".dxf";
    default:
        return L".out";
    }
}

ProError CollectDrawingExportSheets(
    ProMdl drawing_model,
    std::vector<core::DrawingExportSheetChoice> &sheets_out,
    int &current_sheet_out,
    const DrawingExportLogSink &log_sink)
{
    sheets_out.clear();
    current_sheet_out = 1;

    if (drawing_model == nullptr || autobbox::creo::ModelType(drawing_model) != PRO_MDL_DRAWING) {
        LogLine(log_sink, "drawing-export collect-sheets fail reason=model-not-drawing");
        return PRO_TK_BAD_INPUTS;
    }

    const ProDrawing drawing = reinterpret_cast<ProDrawing>(drawing_model);
    int sheets = 0;
    ProError st = ProDrawingSheetsCount(drawing, &sheets);
    if (st != PRO_TK_NO_ERROR || sheets < 1) {
        LogLine(log_sink,
                "drawing-export collect-sheets fail reason=sheets-count status=%d sheets=%d",
                static_cast<int>(st),
                sheets);
        return st != PRO_TK_NO_ERROR ? st : PRO_TK_GENERAL_ERROR;
    }

    st = ProDrawingCurrentSheetGet(drawing, &current_sheet_out);
    if (st != PRO_TK_NO_ERROR || current_sheet_out < 1 || current_sheet_out > sheets) {
        LogLine(log_sink,
                "drawing-export collect-sheets current-sheet status=%d current=%d fallback=1",
                static_cast<int>(st),
                current_sheet_out);
        current_sheet_out = 1;
    }

    sheets_out.reserve(static_cast<size_t>(sheets));
    for (int sheet = 1; sheet <= sheets; ++sheet) {
        core::DrawingExportSheetChoice choice = {};
        choice.sheet = sheet;
        ProName sheet_name = {0};
        if (ProDrawingSheetNameGet(drawing, sheet, sheet_name) == PRO_TK_NO_ERROR && sheet_name[0] != L'\0') {
            choice.name = sheet_name;
        }

        wchar_t label[PRO_NAME_SIZE + 32] = {0};
        if (!choice.name.empty()) {
            std::swprintf(label,
                          sizeof(label) / sizeof(label[0]),
                          L"%02d - %s",
                          sheet,
                          choice.name.c_str());
        } else {
            std::swprintf(label, sizeof(label) / sizeof(label[0]), L"%02d - sheet%02d", sheet, sheet);
        }
        choice.display_name = label;
        sheets_out.push_back(choice);
    }

    LogLine(log_sink,
            "drawing-export collect-sheets count=%d current=%d",
            static_cast<int>(sheets_out.size()),
            current_sheet_out);
    return PRO_TK_NO_ERROR;
}

const char *DrawingExportFormatLogName(core::DrawingExportFormat format)
{
    switch (format) {
    case core::DrawingExportFormat::Dwg:
        return "dwg";
    case core::DrawingExportFormat::Pdf:
        return "pdf";
    case core::DrawingExportFormat::Dxf:
        return "dxf";
    default:
        return "unknown";
    }
}

core::DrawingExportResult ExecuteDrawingExportTask(
    ProMdl drawing_model,
    const core::DrawingExportRequest &request,
    const DrawingExportLogSink &log_sink)
{
    core::DrawingExportResult result = {};
    result.status = PRO_TK_GENERAL_ERROR;

    if (drawing_model == nullptr || autobbox::creo::ModelType(drawing_model) != PRO_MDL_DRAWING) {
        result.status = PRO_TK_BAD_INPUTS;
        LogLine(log_sink, "drawing-export fail reason=model-not-drawing");
        return result;
    }

    const std::wstring cwd = autobbox::common::CurrentWorkingDirectoryW();
    if (cwd.empty()) {
        result.status = PRO_TK_CANT_ACCESS;
        LogLine(log_sink, "drawing-export fail reason=cwd-empty");
        return result;
    }

    result.output_dir = autobbox::common::JoinPath(cwd, L"export");
    if (!autobbox::common::EnsureDirectoryW(result.output_dir)) {
        result.status = PRO_TK_CANT_WRITE;
        LogLine(log_sink,
                "drawing-export fail reason=ensure-export-dir path=%s",
                autobbox::common::WToA(result.output_dir.c_str()).c_str());
        return result;
    }

    const ProDrawing drawing = reinterpret_cast<ProDrawing>(drawing_model);
    int sheets = 0;
    ProError st = ProDrawingSheetsCount(drawing, &sheets);
    if (st != PRO_TK_NO_ERROR || sheets < 1) {
        result.status = st != PRO_TK_NO_ERROR ? st : PRO_TK_GENERAL_ERROR;
        LogLine(log_sink,
                "drawing-export fail reason=sheets-count status=%d sheets=%d",
                static_cast<int>(st),
                sheets);
        return result;
    }
    result.sheets_total = sheets;

    const std::wstring drawing_stem =
        SanitizeFileStem(autobbox::creo::ModelName(drawing_model, L"drawing"), L"drawing");
    LogLine(log_sink,
            "drawing-export begin format=%s dwg_mode=%d color_map=%d drawing=%s sheets=%d dir=%s",
            DrawingExportFormatLogName(request.format),
            static_cast<int>(request.dwg_mode),
            request.enable_official_color_mapping ? 1 : 0,
            autobbox::common::WToA(drawing_stem.c_str()).c_str(),
            sheets,
            autobbox::common::WToA(result.output_dir.c_str()).c_str());

    const bool dxf_dwg_mapping_applicable =
        request.format == core::DrawingExportFormat::Dwg ||
        request.format == core::DrawingExportFormat::Dxf;
    ScopedDxfDwgMappingOption dxf_dwg_mapping_option(
        dxf_dwg_mapping_applicable,
        request.enable_official_color_mapping,
        UniqueOutputPath(result.output_dir, drawing_stem + L"_no_dxf_dwg_mapping", L".pro"),
        log_sink);

    if (request.format == core::DrawingExportFormat::Pdf) {
        const std::wstring path =
            UniqueOutputPath(result.output_dir, drawing_stem, DrawingExportFormatExtension(request.format));
        st = ExportPdfFile(drawing_model, path, log_sink);
        RecordExport(result, path, 0, st);
    } else if (request.format == core::DrawingExportFormat::Dwg) {
        const std::vector<int> selected_sheets = NormalizeSelectedSheets(request, sheets);
        LogLine(log_sink,
                "drawing-export dwg selected_count=%d mode=%d",
                static_cast<int>(selected_sheets.size()),
                static_cast<int>(request.dwg_mode));
        if (selected_sheets.size() == 1 || request.dwg_mode == core::DwgExportMode::PerSheetFiles) {
            int original_sheet = 0;
            const ProError st_current_sheet = ProDrawingCurrentSheetGet(drawing, &original_sheet);
            if (st_current_sheet != PRO_TK_NO_ERROR) {
                result.status = st_current_sheet;
                LogLine(log_sink,
                        "drawing-export fail reason=get-current-sheet dwg-selected status=%d",
                        static_cast<int>(st_current_sheet));
                return result;
            }
            for (const int sheet : selected_sheets) {
                const std::wstring stem = JoinDrawingSheetStem(drawing_stem, drawing, sheet);
                const std::wstring path =
                    UniqueOutputPath(result.output_dir, stem, DrawingExportFormatExtension(request.format));
                st = ExportPerSheet2dFile(drawing, PRO_DWG_FILE, path, sheet, log_sink);
                RecordExport(result, path, sheet, st);
            }
            const ProError st_restore_sheet = ProDrawingCurrentSheetSet(drawing, original_sheet);
            LogLine(log_sink,
                    "drawing-export restore-current-sheet dwg-selected status=%d sheet=%d",
                    static_cast<int>(st_restore_sheet),
                    original_sheet);
            if (st_restore_sheet != PRO_TK_NO_ERROR &&
                (result.status == PRO_TK_NO_ERROR || result.status == PRO_TK_GENERAL_ERROR)) {
                result.status = st_restore_sheet;
            }
        } else {
            const std::wstring stem = ContainsAllSheets(selected_sheets, sheets)
                                          ? drawing_stem
                                          : SanitizeFileStem(drawing_stem + L"_selected", L"drawing_selected");
            const std::wstring path =
                UniqueOutputPath(result.output_dir, stem, DrawingExportFormatExtension(request.format));
            st = Export2dSelectedSheetsFile(drawing_model, PRO_DWG_FILE, path, selected_sheets, log_sink);
            RecordExport(result, path, 0, st);
        }
    } else {
        const ProImportExportFile format =
            request.format == core::DrawingExportFormat::Dwg ? PRO_DWG_FILE : PRO_DXF_FILE;
        int original_sheet = 0;
        const ProError st_current_sheet = ProDrawingCurrentSheetGet(drawing, &original_sheet);
        if (st_current_sheet != PRO_TK_NO_ERROR) {
            result.status = st_current_sheet;
            LogLine(log_sink,
                    "drawing-export fail reason=get-current-sheet status=%d",
                    static_cast<int>(st_current_sheet));
            return result;
        }
        for (int sheet = 1; sheet <= sheets; ++sheet) {
            const std::wstring stem = JoinDrawingSheetStem(drawing_stem, drawing, sheet);
            const std::wstring path =
                UniqueOutputPath(result.output_dir, stem, DrawingExportFormatExtension(request.format));
            st = ExportPerSheet2dFile(drawing, format, path, sheet, log_sink);
            RecordExport(result, path, sheet, st);
        }
        const ProError st_restore_sheet = ProDrawingCurrentSheetSet(drawing, original_sheet);
        LogLine(log_sink,
                "drawing-export restore-current-sheet status=%d sheet=%d",
                static_cast<int>(st_restore_sheet),
                original_sheet);
        if (st_restore_sheet != PRO_TK_NO_ERROR &&
            (result.status == PRO_TK_NO_ERROR || result.status == PRO_TK_GENERAL_ERROR)) {
            result.status = st_restore_sheet;
        }
    }

    result.success = result.exports_attempted > 0 && result.exports_succeeded == result.exports_attempted;
    if (result.success) {
        result.status = PRO_TK_NO_ERROR;
    }
    LogLine(log_sink,
            "drawing-export end success=%d attempted=%d succeeded=%d status=%d",
            result.success ? 1 : 0,
            result.exports_attempted,
            result.exports_succeeded,
            static_cast<int>(result.status));
    return result;
}

} // namespace autobbox::application
