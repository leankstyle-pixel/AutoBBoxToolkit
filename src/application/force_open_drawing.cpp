#include "autobbox/application/force_open_drawing.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"

#include <ProAsmcomppath.h>
#include <ProMdl.h>
#include <ProModelitem.h>
#include <ProSelection.h>
#include <ProToolkit.h>
#include <ProWindows.h>

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace autobbox::application {

namespace {

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

void Log(const ForceOpenDrawingLogSink &log_sink, const std::string &line)
{
    if (log_sink) {
        log_sink(line);
    }
}

void LogStatus(const ForceOpenDrawingLogSink &log_sink, const char *label, ProError status)
{
    if (label == nullptr) {
        return;
    }
    Log(log_sink, std::string(label) + " status=" + std::to_string(static_cast<int>(status)));
}

std::wstring ToLower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        if (ch >= L'A' && ch <= L'Z') {
            return static_cast<wchar_t>(ch - L'A' + L'a');
        }
        return ch;
    });
    return value;
}

std::string ToLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return static_cast<char>(ch);
    });
    return value;
}

std::string TrimAscii(const std::string &value)
{
    size_t begin = 0;
    while (begin < value.size() && static_cast<unsigned char>(value[begin]) <= ' ') {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin && static_cast<unsigned char>(value[end - 1]) <= ' ') {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool IsPartOrAsmType(ProMdlType type)
{
    return type == PRO_MDL_PART || type == PRO_MDL_ASSEMBLY;
}

size_t LastSeparatorPos(const std::wstring &path)
{
    return path.find_last_of(L"\\/");
}

bool HasSchemePrefix(const std::wstring &path)
{
    const size_t sep = LastSeparatorPos(path);
    const size_t scheme = path.find(L"://");
    return scheme != std::wstring::npos && (sep == std::wstring::npos || scheme < sep);
}

bool IsLocalPath(const std::wstring &path)
{
    return !path.empty() && !HasSchemePrefix(path);
}

bool IsAllDigits(const std::wstring &value)
{
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](wchar_t ch) {
               return ch >= L'0' && ch <= L'9';
           });
}

bool TryParsePositiveInteger(const std::wstring &value, long long &number_out)
{
    number_out = -1;
    if (!IsAllDigits(value)) {
        return false;
    }

    long long value_number = 0;
    for (const wchar_t ch : value) {
        value_number = value_number * 10 + static_cast<long long>(ch - L'0');
    }
    number_out = value_number;
    return true;
}

std::wstring ResolveCreoVersionedFilePath(const std::wstring &path,
                                          const ForceOpenDrawingLogSink &log_sink)
{
    if (path.empty()) {
        return std::wstring();
    }
    if (autobbox::common::FileExistsW(path)) {
        return path;
    }

    std::error_code ec;
    const std::filesystem::path drawing_path(path);
    const std::filesystem::path parent = drawing_path.parent_path();
    const std::wstring base_name = drawing_path.filename().wstring();
    if (base_name.empty() || !std::filesystem::is_directory(parent, ec)) {
        return std::wstring();
    }

    const std::wstring versioned_prefix = ToLower(base_name + L".");
    long long best_version = -1;
    std::wstring best_path;
    for (const std::filesystem::directory_entry &entry :
         std::filesystem::directory_iterator(parent, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }

        const std::wstring file_name = entry.path().filename().wstring();
        const std::wstring lower_name = ToLower(file_name);
        if (lower_name.size() <= versioned_prefix.size() ||
            lower_name.compare(0, versioned_prefix.size(), versioned_prefix) != 0) {
            continue;
        }

        long long version = -1;
        if (TryParsePositiveInteger(lower_name.substr(versioned_prefix.size()), version) &&
            version > best_version) {
            best_version = version;
            best_path = entry.path().wstring();
        }
    }
    if (!best_path.empty()) {
        Log(log_sink,
            "ForceOpenDrawing resolved-versioned-drawing=" +
                autobbox::common::WToA(best_path.c_str()) +
                " version=" + std::to_string(best_version));
    }
    return best_path;
}

bool EndsWithAsciiNoCase(const std::string &value, const char *suffix)
{
    if (suffix == nullptr) {
        return false;
    }
    const std::string suffix_text(suffix);
    if (value.size() < suffix_text.size()) {
        return false;
    }
    return ToLowerAscii(value.substr(value.size() - suffix_text.size())) ==
           ToLowerAscii(suffix_text);
}

std::string StemWithoutModelExtension(std::string file_name)
{
    const std::string lower = ToLowerAscii(file_name);
    const size_t asm_pos = lower.rfind(".asm");
    if (asm_pos != std::string::npos && asm_pos + 4 == lower.size()) {
        return file_name.substr(0, asm_pos);
    }
    const size_t prt_pos = lower.rfind(".prt");
    if (prt_pos != std::string::npos && prt_pos + 4 == lower.size()) {
        return file_name.substr(0, prt_pos);
    }
    return file_name;
}

std::wstring WidenAscii(const std::string &value)
{
    return std::wstring(value.begin(), value.end());
}

std::string NarrowAscii(const std::wstring &value)
{
    std::string out;
    out.reserve(value.size());
    for (wchar_t ch : value) {
        if (ch >= 0 && ch <= 0x7f) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('_');
        }
    }
    return out;
}

std::string FileNameWithoutVersionAscii(const std::wstring &path)
{
    std::wstring file_name = std::filesystem::path(path).filename().wstring();
    std::string ascii = NarrowAscii(file_name);
    const std::string lower = ToLowerAscii(ascii);
    for (const char *ext : {".asm", ".prt"}) {
        const size_t pos = lower.rfind(ext);
        if (pos != std::string::npos && pos + 4 <= lower.size()) {
            const std::string tail = lower.substr(pos + 4);
            if (tail.empty() || (tail.size() > 1 && tail[0] == '.' &&
                                 std::all_of(tail.begin() + 1, tail.end(), [](char ch) {
                                     return ch >= '0' && ch <= '9';
                                 }))) {
                return ascii.substr(0, pos + 4);
            }
        }
    }
    return ascii;
}

bool IsModelReferenceToken(const std::string &token)
{
    if (token.size() < 5) {
        return false;
    }
    return EndsWithAsciiNoCase(token, ".asm") || EndsWithAsciiNoCase(token, ".prt");
}

std::set<std::string> ExtractModelReferencesFromFile(const std::wstring &path,
                                                     const ForceOpenDrawingLogSink &log_sink)
{
    std::set<std::string> refs;
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) {
        Log(log_sink, "ForceOpenDrawing alias-scan open failed");
        return refs;
    }

    std::string token;
    char ch = '\0';
    auto flush_token = [&]() {
        if (IsModelReferenceToken(token)) {
            refs.insert(ToLowerAscii(token));
        }
        token.clear();
    };

    while (in.get(ch)) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        const bool model_char =
            (uch >= 'A' && uch <= 'Z') ||
            (uch >= 'a' && uch <= 'z') ||
            (uch >= '0' && uch <= '9') ||
            uch == '_' || uch == '-' || uch == '.';
        if (model_char) {
            token.push_back(static_cast<char>(uch));
            if (token.size() > 240) {
                flush_token();
            }
        } else if (!token.empty()) {
            flush_token();
        }
    }
    if (!token.empty()) {
        flush_token();
    }

    Log(log_sink, "ForceOpenDrawing alias-scan refs=" + std::to_string(refs.size()));
    return refs;
}

std::wstring GuessAliasReplacement(const std::filesystem::path &drawing_dir,
                                   const std::string &missing_ref,
                                   const std::wstring &selected_model_path,
                                   const ForceOpenDrawingLogSink &log_sink)
{
    const std::string missing_lower = ToLowerAscii(missing_ref);
    const std::string missing_stem = StemWithoutModelExtension(missing_lower);
    const std::string selected_name = ToLowerAscii(FileNameWithoutVersionAscii(selected_model_path));

    std::vector<std::string> candidate_names;
    const std::string sub_prefix = "fg744_sub_asm_mm_";
    if (missing_stem.rfind(sub_prefix, 0) == 0) {
        const std::string number = missing_stem.substr(sub_prefix.size());
        if (!number.empty()) {
            if (EndsWithAsciiNoCase(missing_lower, ".asm")) {
                candidate_names.push_back("fg744_assembly_csys_" + number + ".asm");
            }
            candidate_names.push_back("fg744_part_csys_" + number + ".prt");
        }
    }

    for (const std::string &candidate_name : candidate_names) {
        if (candidate_name.empty()) {
            continue;
        }
        const std::wstring candidate_path =
            (drawing_dir / std::filesystem::path(WidenAscii(candidate_name))).wstring();
        const std::wstring resolved = ResolveCreoVersionedFilePath(candidate_path, log_sink);
        if (!resolved.empty()) {
            return resolved;
        }
    }
    return std::wstring();
}

int CreateMissingModelAliasesForDrawing(const std::wstring &drawing_path,
                                        const std::wstring &selected_model_path,
                                        const ForceOpenDrawingLogSink &log_sink)
{
    const std::filesystem::path drawing_fs_path(drawing_path);
    const std::filesystem::path drawing_dir = drawing_fs_path.parent_path();
    if (drawing_dir.empty()) {
        return 0;
    }

    const std::set<std::string> refs = ExtractModelReferencesFromFile(drawing_path, log_sink);
    int created_count = 0;
    for (const std::string &ref : refs) {
        const std::wstring target_path =
            (drawing_dir / std::filesystem::path(WidenAscii(ref))).wstring();
        if (!ResolveCreoVersionedFilePath(target_path, log_sink).empty()) {
            continue;
        }

        const std::wstring replacement =
            GuessAliasReplacement(drawing_dir, ref, selected_model_path, log_sink);
        if (replacement.empty()) {
            Log(log_sink, "ForceOpenDrawing alias missing no-replacement=" + ref);
            continue;
        }

        std::error_code ec;
        std::filesystem::copy_file(
            std::filesystem::path(replacement),
            std::filesystem::path(target_path),
            std::filesystem::copy_options::skip_existing,
            ec);
        if (ec) {
            Log(log_sink,
                "ForceOpenDrawing alias copy failed missing=" + ref +
                    " replacement=" + autobbox::common::WToA(replacement.c_str()) +
                    " ec=" + ec.message());
            continue;
        }

        ++created_count;
        Log(log_sink,
            "ForceOpenDrawing alias created missing=" + ref +
                " alias=" + autobbox::common::WToA(target_path.c_str()) +
                " replacement=" + autobbox::common::WToA(replacement.c_str()));
    }

    Log(log_sink, "ForceOpenDrawing alias created-count=" + std::to_string(created_count));
    return created_count;
}

ProType ObjectWindowTypeForModel(ProMdlType type)
{
    switch (type) {
    case PRO_MDL_PART:
        return PRO_PART;
    case PRO_MDL_ASSEMBLY:
        return PRO_ASSEMBLY;
    case PRO_MDL_DRAWING:
        return PRO_DRAWING;
    default:
        return PRO_TYPE_UNUSED;
    }
}

ProError DisplayModelInOwnWindow(ProMdl model,
                                 ProMdlType type,
                                 ProError &display_status,
                                 const ForceOpenDrawingLogSink &log_sink)
{
    display_status = PRO_TK_GENERAL_ERROR;
    if (model == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    ProMdlName model_name = {0};
    const ProError name_status = ProMdlNameGet(model, model_name);
    LogStatus(log_sink, "ForceOpenDrawing window-name", name_status);
    if (name_status == PRO_TK_NO_ERROR && model_name[0] != L'\0') {
        int window_id = PRO_VALUE_UNUSED;
        const ProType window_type = ObjectWindowTypeForModel(type);
        const ProError window_status =
            ProObjectwindowMdlnameCreate(model_name, window_type, &window_id);
        LogStatus(log_sink, "ForceOpenDrawing object-window-create", window_status);
        Log(log_sink, "ForceOpenDrawing object-window-id=" + std::to_string(window_id));
        if (window_status == PRO_TK_NO_ERROR && window_id != PRO_VALUE_UNUSED) {
            const ProError activate_status = ProWindowActivate(window_id);
            LogStatus(log_sink, "ForceOpenDrawing object-window-activate", activate_status);
            display_status = ProMdlDisplay(model);
            return display_status;
        }
    }

    display_status = ProMdlDisplay(model);
    return display_status;
}

bool HasDirectoryPart(const std::wstring &path)
{
    return LastSeparatorPos(path) != std::wstring::npos;
}

size_t FindModelExtensionPos(const std::wstring &path)
{
    const size_t sep = LastSeparatorPos(path);
    const size_t search_from = (sep == std::wstring::npos) ? 0 : sep + 1;
    const std::wstring lower = ToLower(path);

    const size_t prt = lower.find(L".prt", search_from);
    const size_t asm_pos = lower.find(L".asm", search_from);

    size_t best = std::wstring::npos;
    if (prt != std::wstring::npos) {
        best = prt;
    }
    if (asm_pos != std::wstring::npos && (best == std::wstring::npos || asm_pos < best)) {
        best = asm_pos;
    }
    return best;
}

std::wstring BuildDrawingPath(const std::wstring &model_path_or_name)
{
    if (model_path_or_name.empty()) {
        return std::wstring();
    }

    const size_t model_ext = FindModelExtensionPos(model_path_or_name);
    if (model_ext != std::wstring::npos) {
        return model_path_or_name.substr(0, model_ext) + L".drw";
    }

    const size_t sep = LastSeparatorPos(model_path_or_name);
    const size_t dot = model_path_or_name.find_last_of(L'.');
    if (dot != std::wstring::npos && (sep == std::wstring::npos || dot > sep)) {
        return model_path_or_name.substr(0, dot) + L".drw";
    }
    return model_path_or_name + L".drw";
}

std::wstring ModelExtension(ProMdlType type)
{
    const wchar_t *ext = nullptr;
    if (autobbox::creo::MdlTypeToExt(type, &ext) && ext != nullptr) {
        return std::wstring(ext);
    }
    return std::wstring();
}

std::wstring ModelOrigin(ProMdl mdl, const ForceOpenDrawingLogSink &log_sink)
{
    ProPath origin = {0};
    const ProError status = ProMdlOriginGet(mdl, origin);
    LogStatus(log_sink, "ForceOpenDrawing model-origin", status);
    if (status == PRO_TK_NO_ERROR && origin[0] != L'\0') {
        return std::wstring(origin);
    }
    return std::wstring();
}

std::wstring ModelNameWithExt(ProMdl mdl, ProMdlType type)
{
    ProMdlFileName display_name = {0};
    if (mdl != nullptr && ProMdlDisplaynameGet(mdl, PRO_B_TRUE, display_name) == PRO_TK_NO_ERROR &&
        display_name[0] != L'\0') {
        return std::wstring(display_name);
    }

    std::wstring name = autobbox::creo::ModelName(mdl, L"");
    const std::wstring ext = ModelExtension(type);
    if (!name.empty() && !ext.empty() && FindModelExtensionPos(name) == std::wstring::npos) {
        name += ext;
    }
    return name;
}

std::wstring ResolveDrawingPathForModel(ProMdl mdl,
                                        ProMdlType type,
                                        std::wstring &model_reference_out,
                                        const ForceOpenDrawingLogSink &log_sink)
{
    const std::wstring origin = ModelOrigin(mdl, log_sink);
    const std::wstring display_name = ModelNameWithExt(mdl, type);

    std::wstring source = !origin.empty() ? origin : display_name;
    model_reference_out = !origin.empty() ? origin : display_name;

    std::wstring drawing_path = BuildDrawingPath(source);
    if (drawing_path.empty()) {
        return drawing_path;
    }

    if (IsLocalPath(drawing_path) && !HasDirectoryPart(drawing_path)) {
        const std::wstring current_dir = autobbox::common::CurrentWorkingDirectoryW();
        if (!current_dir.empty()) {
            drawing_path = autobbox::common::JoinPath(current_dir, drawing_path.c_str());
        }
    }
    return drawing_path;
}

bool ResolveModelFromSelection(ProSelection selection,
                               ProMdl &mdl_out,
                               ProMdlType &type_out,
                               const ForceOpenDrawingLogSink &log_sink)
{
    mdl_out = nullptr;
    type_out = PRO_MDL_UNUSED;
    if (selection == nullptr) {
        return false;
    }

    ProMdl mdl = nullptr;
    ProAsmcomppath comp_path = {};
    const ProError path_status = ProSelectionAsmcomppathGet(selection, &comp_path);
    LogStatus(log_sink, "ForceOpenDrawing selection-asmcomppath", path_status);
    if (path_status == PRO_TK_NO_ERROR &&
        ProAsmcomppathMdlGet(&comp_path, &mdl) == PRO_TK_NO_ERROR &&
        mdl != nullptr) {
        Log(log_sink, "ForceOpenDrawing selection source=asmcomppath");
    }

    if (mdl == nullptr) {
        ProModelitem item = {};
        const ProError item_status = ProSelectionModelitemGet(selection, &item);
        LogStatus(log_sink, "ForceOpenDrawing selection-modelitem", item_status);
        if (item_status == PRO_TK_NO_ERROR) {
            ProMdl owner = nullptr;
            const ProError owner_status = ProModelitemMdlGet(&item, &owner);
            LogStatus(log_sink, "ForceOpenDrawing selection-owner", owner_status);
            if (owner_status == PRO_TK_NO_ERROR && owner != nullptr) {
                mdl = owner;
                Log(log_sink, "ForceOpenDrawing selection source=modelitem-owner");
            }
        }
    }

    if (mdl == nullptr) {
        return false;
    }

    const ProMdlType type = autobbox::creo::ModelType(mdl);
    if (!IsPartOrAsmType(type)) {
        Log(log_sink,
            "ForceOpenDrawing selection invalid type=" + std::to_string(static_cast<int>(type)));
        return false;
    }

    mdl_out = mdl;
    type_out = type;
    return true;
}

ProError SelectModelFromTree(ProMdl &mdl_out,
                             ProMdlType &type_out,
                             const ForceOpenDrawingLogSink &log_sink)
{
    mdl_out = nullptr;
    type_out = PRO_MDL_UNUSED;

    char select_filter[] = "prt_or_asm";
    ProSelection *selection = nullptr;
    int selection_count = 0;
    const ProError select_status =
        ProSelect(select_filter, 1, nullptr, nullptr, nullptr, nullptr, &selection, &selection_count);
    Log(log_sink,
        "ForceOpenDrawing proselect status=" + std::to_string(static_cast<int>(select_status)) +
            " count=" + std::to_string(selection_count));

    if (select_status != PRO_TK_NO_ERROR) {
        return select_status;
    }
    if (selection == nullptr || selection_count <= 0) {
        return PRO_TK_USER_ABORT;
    }
    if (!ResolveModelFromSelection(selection[0], mdl_out, type_out, log_sink)) {
        return PRO_TK_INVALID_TYPE;
    }
    return PRO_TK_NO_ERROR;
}

ProError LoadAndDisplayDrawing(const std::wstring &path,
                               ProMdl *model_out,
                               ProError &display_status,
                               const ForceOpenDrawingLogSink &log_sink)
{
    display_status = PRO_TK_GENERAL_ERROR;
    if (model_out != nullptr) {
        *model_out = nullptr;
    }
    if (path.empty()) {
        return PRO_TK_BAD_INPUTS;
    }

    ProPath pro_path = {0};
    CopyWStr(pro_path, path.c_str());
    ProMdl model = nullptr;
    const ProError load_status = ProMdlFiletypeLoad(pro_path, PRO_MDLFILE_DRAWING, PRO_B_FALSE, &model);
    if (load_status != PRO_TK_NO_ERROR || model == nullptr) {
        return load_status;
    }

    display_status = DisplayModelInOwnWindow(model, PRO_MDL_DRAWING, display_status, log_sink);
    if (model_out != nullptr) {
        *model_out = model;
    }
    return load_status;
}

} // namespace

ForceOpenDrawingResult ExecuteForceOpenDrawingTask(const ForceOpenDrawingLogSink &log_sink)
{
    ForceOpenDrawingResult result = {};
    Log(log_sink, "ForceOpenDrawing begin");

    ProMdl selected_model = nullptr;
    ProMdlType selected_type = PRO_MDL_UNUSED;
    result.selection_status = SelectModelFromTree(selected_model, selected_type, log_sink);
    LogStatus(log_sink, "ForceOpenDrawing select-model", result.selection_status);
    if (result.selection_status == PRO_TK_USER_ABORT) {
        result.cancelled = true;
        result.status = PRO_TK_USER_ABORT;
        Log(log_sink, "ForceOpenDrawing cancelled by user");
        return result;
    }
    if (result.selection_status != PRO_TK_NO_ERROR || selected_model == nullptr) {
        result.status = result.selection_status;
        return result;
    }

    result.selected_model_name = ModelNameWithExt(selected_model, selected_type);
    std::wstring model_reference;
    result.drawing_path = ResolveDrawingPathForModel(selected_model, selected_type, model_reference, log_sink);
    result.selected_model_path = model_reference;

    Log(log_sink, "ForceOpenDrawing selected-model=" + autobbox::common::WToA(result.selected_model_path.c_str()));
    Log(log_sink, "ForceOpenDrawing selected-name=" + autobbox::common::WToA(result.selected_model_name.c_str()));
    Log(log_sink, "ForceOpenDrawing derived-drawing=" + autobbox::common::WToA(result.drawing_path.c_str()));

    result.model_display_status = PRO_TK_NO_ERROR;
    Log(log_sink, "ForceOpenDrawing model-display skipped; selected model remains in its existing window");

    if (IsLocalPath(result.drawing_path)) {
        const std::wstring existing_drawing_path =
            ResolveCreoVersionedFilePath(result.drawing_path, log_sink);
        if (existing_drawing_path.empty()) {
            result.drawing_path_local_missing = true;
            result.drawing_load_status = PRO_TK_E_NOT_FOUND;
            result.status = PRO_TK_E_NOT_FOUND;
            Log(log_sink, "ForceOpenDrawing drawing local file missing");
            return result;
        }
        result.drawing_path = existing_drawing_path;
        result.alias_created_count = CreateMissingModelAliasesForDrawing(
            result.drawing_path,
            result.selected_model_path,
            log_sink);
    }

    ProMdl drawing = nullptr;
    result.drawing_load_status = LoadAndDisplayDrawing(
        result.drawing_path,
        &drawing,
        result.drawing_display_status,
        log_sink);
    LogStatus(log_sink, "ForceOpenDrawing drawing-load", result.drawing_load_status);
    LogStatus(log_sink, "ForceOpenDrawing drawing-display", result.drawing_display_status);
    if (result.drawing_load_status != PRO_TK_NO_ERROR) {
        result.status = result.drawing_load_status;
        return result;
    }
    if (result.drawing_display_status != PRO_TK_NO_ERROR) {
        result.status = result.drawing_display_status;
        return result;
    }

    result.status = PRO_TK_NO_ERROR;
    Log(log_sink, "ForceOpenDrawing success");
    return result;
}

} // namespace autobbox::application
