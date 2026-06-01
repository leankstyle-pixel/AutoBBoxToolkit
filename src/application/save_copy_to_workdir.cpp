#include "autobbox/application/save_copy_to_workdir.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"

#include <ProArray.h>
#include <ProAsmcomp.h>
#include <ProAsmcomppath.h>
#include <ProAssembly.h>
#include <ProMdl.h>
#include <ProModelitem.h>
#include <ProObjects.h>
#include <ProSelbuffer.h>
#include <ProSelection.h>
#include <ProSizeConst.h>
#include <ProToolkit.h>
#include <ProUtil.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <string>

namespace autobbox::application {

namespace {

std::wstring TrimWhitespace(const std::wstring &value)
{
    size_t begin = 0;
    while (begin < value.size() && std::iswspace(value[begin]) != 0) {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin && std::iswspace(value[end - 1]) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::wstring UppercaseAscii(const std::wstring &value)
{
    std::wstring out = value;
    for (wchar_t &ch : out) {
        if (ch >= L'a' && ch <= L'z') {
            ch = static_cast<wchar_t>(ch - L'a' + L'A');
        }
    }
    return out;
}

bool EndsWithAsciiNoCase(const std::wstring &value, const wchar_t *suffix)
{
    if (suffix == nullptr) {
        return false;
    }
    const std::wstring suffix_text(suffix);
    if (value.size() < suffix_text.size()) {
        return false;
    }
    return UppercaseAscii(value.substr(value.size() - suffix_text.size())) ==
           UppercaseAscii(suffix_text);
}

bool ContainsPathOrWildcardChar(const std::wstring &value)
{
    return value.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos;
}

bool IsPartOrAsmType(ProMdlType type)
{
    return type == PRO_MDL_PART || type == PRO_MDL_ASSEMBLY;
}

const wchar_t *ExpectedExtension(ProMdlType type)
{
    switch (type) {
    case PRO_MDL_PART:
        return L".prt";
    case PRO_MDL_ASSEMBLY:
        return L".asm";
    default:
        return L"";
    }
}

ProMdlfileType ToFileType(ProMdlType type)
{
    return autobbox::creo::ToMdlFileType(type);
}

void CopyToProMdlName(const std::wstring &name, ProMdlName pro_name)
{
    if (pro_name != nullptr) {
        pro_name[0] = L'\0';
        wcsncpy_s(pro_name, PRO_MDLNAME_SIZE, name.c_str(), _TRUNCATE);
    }
}

void CopyToProFamilyMdlName(const std::wstring &name, ProFamilyMdlName pro_name)
{
    if (pro_name != nullptr) {
        pro_name[0] = L'\0';
        wcsncpy_s(pro_name, PRO_FAMILY_MDLNAME_SIZE, name.c_str(), _TRUNCATE);
    }
}

void LogLine(const std::function<void(const std::string &line)> &log_sink,
             const std::string &line)
{
    if (log_sink) {
        log_sink(line);
    }
}

std::wstring JoinTargetPath(const std::wstring &directory,
                            const std::wstring &name,
                            const wchar_t *extension)
{
    std::wstring file_name = name;
    if (extension != nullptr) {
        file_name += extension;
    }

    std::wstring out = directory;
    if (!out.empty()) {
        const wchar_t last = out.back();
        if (last != L'\\' && last != L'/') {
            out.push_back(L'\\');
        }
    }
    out += file_name;
    return out;
}

bool FillComponentPlacement(ProAsmcomppath &comp_path,
                            core::QuickRenameTarget &target_out,
                            const std::function<void(const std::string &line)> &log_sink)
{
    if (comp_path.owner == nullptr || comp_path.table_num <= 0) {
        LogLine(log_sink, "save-copy source component-path unavailable");
        return false;
    }

    ProMdl parent_mdl = nullptr;
    if (comp_path.table_num == 1) {
        parent_mdl = ProSolidToMdl(comp_path.owner);
    } else {
        ProAsmcomppath parent_path = comp_path;
        --parent_path.table_num;
        const ProError parent_status = ProAsmcomppathMdlGet(&parent_path, &parent_mdl);
        LogLine(log_sink,
                "save-copy parent-path status=" +
                    std::to_string(static_cast<int>(parent_status)));
        if (parent_status != PRO_TK_NO_ERROR || parent_mdl == nullptr) {
            return false;
        }
    }

    if (autobbox::creo::ModelType(parent_mdl) != PRO_MDL_ASSEMBLY) {
        return false;
    }

    target_out.has_component_path = true;
    target_out.component_path = comp_path;
    target_out.parent_assembly = ProMdlToAssembly(parent_mdl);
    target_out.component_id = comp_path.comp_id_table[comp_path.table_num - 1];
    return target_out.component_id > 0;
}

bool SourceFromSelection(ProSelection selection,
                         SaveCopyToWorkdirSource &source_out,
                         const std::function<void(const std::string &line)> &log_sink)
{
    if (selection == nullptr) {
        return false;
    }

    ProMdl mdl = nullptr;
    ProAsmcomppath comp_path = {};
    const ProError path_status = ProSelectionAsmcomppathGet(selection, &comp_path);
    if (path_status == PRO_TK_NO_ERROR &&
        ProAsmcomppathMdlGet(&comp_path, &mdl) == PRO_TK_NO_ERROR &&
        mdl != nullptr) {
        LogLine(log_sink, "save-copy selection source=asmcomppath");
    }

    if (mdl == nullptr) {
        ProModelitem item = {};
        if (ProSelectionModelitemGet(selection, &item) == PRO_TK_NO_ERROR) {
            ProMdl owner = nullptr;
            if (ProModelitemMdlGet(&item, &owner) == PRO_TK_NO_ERROR && owner != nullptr) {
                mdl = owner;
                LogLine(log_sink, "save-copy selection source=modelitem-owner");
            }
        }
    }

    if (mdl == nullptr) {
        return false;
    }

    const ProMdlType type = autobbox::creo::ModelType(mdl);
    if (!IsPartOrAsmType(type)) {
        return false;
    }

    source_out = {};
    source_out.mdl = mdl;
    source_out.type = type;
    source_out.name = autobbox::creo::ModelName(mdl, L"");

    core::QuickRenameTarget replacement_target = {};
    replacement_target.mdl = mdl;
    replacement_target.type = type;
    replacement_target.old_name = source_out.name;
    if (path_status == PRO_TK_NO_ERROR &&
        FillComponentPlacement(comp_path, replacement_target, log_sink)) {
        source_out.can_replace_component = true;
        source_out.replacement_target = replacement_target;
    }

    return !source_out.name.empty();
}

bool TrySourceFromSelectionBuffer(SaveCopyToWorkdirSource &source_out,
                                  bool &has_ambiguous_selection,
                                  const std::function<void(const std::string &line)> &log_sink)
{
    has_ambiguous_selection = false;

    ProSelection *buffer = nullptr;
    const ProError buffer_status = ProSelbufferSelectionsGet(&buffer);
    if (buffer_status != PRO_TK_NO_ERROR || buffer == nullptr) {
        LogLine(log_sink, "save-copy selbuffer status=" + std::to_string(static_cast<int>(buffer_status)));
        return false;
    }

    int count = 0;
    ProArraySizeGet(reinterpret_cast<ProArray>(buffer), &count);
    LogLine(log_sink, "save-copy selbuffer count=" + std::to_string(count));

    int valid_count = 0;
    SaveCopyToWorkdirSource first_valid = {};
    for (int i = 0; i < count; ++i) {
        SaveCopyToWorkdirSource candidate = {};
        if (SourceFromSelection(buffer[i], candidate, log_sink)) {
            ++valid_count;
            if (valid_count == 1) {
                first_valid = candidate;
            }
        }
    }

    ProSelectionarrayFree(buffer);
    if (valid_count == 1) {
        source_out = first_valid;
        return true;
    }

    has_ambiguous_selection = valid_count > 1;
    return false;
}

bool TrySourceFromCurrentModel(SaveCopyToWorkdirSource &source_out,
                               const std::function<void(const std::string &line)> &log_sink)
{
    ProMdl current = nullptr;
    const ProError status = ProMdlCurrentGet(&current);
    LogLine(log_sink, "save-copy current status=" + std::to_string(static_cast<int>(status)));
    if (status != PRO_TK_NO_ERROR || current == nullptr) {
        return false;
    }

    const ProMdlType type = autobbox::creo::ModelType(current);
    if (!IsPartOrAsmType(type)) {
        return false;
    }

    source_out = {};
    source_out.mdl = current;
    source_out.type = type;
    source_out.name = autobbox::creo::ModelName(current, L"");
    return !source_out.name.empty();
}

ProMdlType FileTypeFromExtension(const std::wstring &extension)
{
    std::wstring ext = UppercaseAscii(extension);
    if (!ext.empty() && ext[0] != L'.') {
        ext.insert(ext.begin(), L'.');
    }
    if (ext == L".PRT") {
        return PRO_MDL_PART;
    }
    if (ext == L".ASM") {
        return PRO_MDL_ASSEMBLY;
    }
    return PRO_MDL_UNUSED;
}

bool LoadSourceFromPickedFile(SaveCopyToWorkdirSource &source_out,
                              bool &cancelled,
                              std::wstring &error_text,
                              const std::function<void(const std::string &line)> &log_sink)
{
    cancelled = false;
    error_text.clear();

    ProMdlfileType *file_types = nullptr;
    ProError status = ProArrayAlloc(
        2,
        sizeof(ProMdlfileType),
        1,
        reinterpret_cast<ProArray *>(&file_types));
    if (status != PRO_TK_NO_ERROR || file_types == nullptr) {
        error_text = L"\u65e0\u6cd5\u521b\u5efa\u6587\u4ef6\u7c7b\u578b\u8fc7\u6ee4\u5668\u3002";
        return false;
    }
    file_types[0] = PRO_MDLFILE_PART;
    file_types[1] = PRO_MDLFILE_ASSEMBLY;

    ProPath default_path = {0};
    ProDirectoryCurrentGet(default_path);

    ProName dialog_label = {0};
    wcsncpy_s(dialog_label, L"\u9009\u62e9\u8981\u7ec4\u88c5\u526f\u672c\u7684\u6a21\u578b", _TRUNCATE);
    ProMdlFileName pre_selected = {0};
    ProPath selected_file = {0};
    status = ProFileMdlfiletypeOpen(
        dialog_label,
        file_types,
        nullptr,
        nullptr,
        default_path[0] == L'\0' ? nullptr : default_path,
        pre_selected,
        selected_file);
    ProArrayFree(reinterpret_cast<ProArray *>(&file_types));

    LogLine(log_sink,
            "save-copy file-open status=" +
                std::to_string(static_cast<int>(status)) +
                " path=" + autobbox::common::WToA(selected_file));

    if (status == PRO_TK_USER_ABORT) {
        cancelled = true;
        return false;
    }
    if (status != PRO_TK_NO_ERROR || selected_file[0] == L'\0') {
        error_text = L"\u9009\u62e9\u6a21\u578b\u6587\u4ef6\u5931\u8d25\u3002";
        return false;
    }

    ProPath parsed_dir = {0};
    ProMdlName parsed_name = {0};
    ProMdlExtension parsed_ext = {0};
    status = ProFileMdlnameParse(selected_file, parsed_dir, parsed_name, parsed_ext, nullptr);
    if (status != PRO_TK_NO_ERROR || parsed_name[0] == L'\0') {
        error_text = L"\u89e3\u6790\u6a21\u578b\u6587\u4ef6\u540d\u5931\u8d25\u3002";
        return false;
    }

    const ProMdlType type = FileTypeFromExtension(parsed_ext);
    if (!IsPartOrAsmType(type)) {
        error_text = L"\u8bf7\u9009\u62e9 .prt \u6216 .asm \u6a21\u578b\u6587\u4ef6\u3002";
        return false;
    }

    ProMdl loaded = nullptr;
    status = ProMdlFiletypeLoad(
        selected_file,
        ToFileType(type),
        PRO_B_FALSE,
        &loaded);
    LogLine(log_sink,
            "save-copy file-load status=" + std::to_string(static_cast<int>(status)));
    if (status != PRO_TK_NO_ERROR || loaded == nullptr) {
        error_text = L"\u88c5\u5165\u6240\u9009\u6a21\u578b\u5931\u8d25\uff0cCreo \u8fd4\u56de\u72b6\u6001\uff1a" +
                     std::to_wstring(static_cast<int>(status));
        return false;
    }

    source_out = {};
    source_out.mdl = loaded;
    source_out.type = type;
    source_out.name = autobbox::creo::ModelName(loaded, parsed_name);
    source_out.source_path = selected_file;
    source_out.from_file_picker = true;
    return !source_out.name.empty();
}

} // namespace

bool ResolveSaveCopyToWorkdirSource(
    SaveCopyToWorkdirSource &source_out,
    bool &cancelled,
    std::wstring &error_text,
    const std::function<void(const std::string &line)> &log_sink)
{
    source_out = {};
    cancelled = false;
    error_text.clear();

    // This command is intentionally file-first: it should behave like Creo's
    // official File Open dialog for selecting a model from any directory, then
    // retrieve it in the background for Save Copy. Do not require the user to
    // pre-open or pre-select the source model.
    return LoadSourceFromPickedFile(source_out, cancelled, error_text, log_sink);
}

SaveCopyToWorkdirValidationResult ValidateSaveCopyToWorkdirName(
    const SaveCopyToWorkdirSource &source,
    const std::wstring &input_name,
    const std::wstring &target_directory)
{
    SaveCopyToWorkdirValidationResult result = {};
    if (source.mdl == nullptr || !IsPartOrAsmType(source.type)) {
        result.error_text = L"\u672a\u83b7\u53d6\u5230\u53ef\u7ec4\u88c5\u526f\u672c\u7684\u96f6\u4ef6\u6216\u88c5\u914d\u6a21\u578b\u3002";
        return result;
    }
    if (target_directory.empty()) {
        result.error_text = L"\u672a\u83b7\u53d6\u5230 Creo \u5f53\u524d\u5de5\u4f5c\u76ee\u5f55\u3002";
        return result;
    }

    std::wstring name = TrimWhitespace(input_name);
    if (name.empty()) {
        result.error_text = L"\u65b0\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002";
        return result;
    }

    const wchar_t *expected_ext = ExpectedExtension(source.type);
    const bool has_prt_ext = EndsWithAsciiNoCase(name, L".prt");
    const bool has_asm_ext = EndsWithAsciiNoCase(name, L".asm");
    if (has_prt_ext || has_asm_ext) {
        if (!EndsWithAsciiNoCase(name, expected_ext)) {
            result.error_text = L"\u8f93\u5165\u7684\u6269\u5c55\u540d\u4e0e\u6a21\u578b\u7c7b\u578b\u4e0d\u5339\u914d\u3002";
            return result;
        }
        name = TrimWhitespace(name.substr(0, name.size() - std::wcslen(expected_ext)));
    }

    if (name.empty()) {
        result.error_text = L"\u65b0\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002";
        return result;
    }
    if (ContainsPathOrWildcardChar(name)) {
        result.error_text = L"\u65b0\u540d\u79f0\u4e0d\u80fd\u5305\u542b\u8def\u5f84\u5206\u9694\u7b26\u6216\u6587\u4ef6\u540d\u975e\u6cd5\u5b57\u7b26\u3002";
        return result;
    }
    if (name.size() >= PRO_NAME_SIZE) {
        result.error_text = L"\u65b0\u540d\u79f0\u8fc7\u957f\uff0c\u8bf7\u4f7f\u7528 31 \u4e2a\u5b57\u7b26\u4ee5\u5185\u7684\u540d\u79f0\u3002";
        return result;
    }
    if (UppercaseAscii(name) == UppercaseAscii(source.name)) {
        result.unchanged = true;
        result.normalized_name = name;
        result.error_text = L"\u526f\u672c\u540d\u79f0\u4e0d\u80fd\u4e0e\u6e90\u6a21\u578b\u540d\u79f0\u76f8\u540c\u3002";
        return result;
    }

    result.normalized_name = name;
    result.target_path = JoinTargetPath(target_directory, name, expected_ext);
    if (autobbox::common::FileExistsW(result.target_path)) {
        result.target_file_exists = true;
        result.error_text = L"\u5f53\u524d\u5de5\u4f5c\u76ee\u5f55\u5df2\u5b58\u5728\u76ee\u6807\u6587\u4ef6\uff1a\n" +
                            result.target_path +
                            L"\n\u8bf7\u6362\u4e00\u4e2a\u540d\u79f0\uff0c\u672c\u547d\u4ee4\u4e0d\u8986\u76d6\u73b0\u6709\u6587\u4ef6\u3002";
        return result;
    }

    ProMdl existing = nullptr;
    ProMdlName pro_name = {0};
    CopyToProMdlName(name, pro_name);
    const ProError init_status = ProMdlnameInit(pro_name, ToFileType(source.type), &existing);
    if (init_status == PRO_TK_NO_ERROR && existing != nullptr && existing != source.mdl) {
        result.session_name_conflict = true;
        result.existing_mdl = existing;
        result.error_text = L"\u540c\u540d\u540c\u7c7b\u578b\u6a21\u578b\u5df2\u5728\u4f1a\u8bdd\u4e2d\u5b58\u5728\uff0c\u8bf7\u6362\u4e00\u4e2a\u540d\u79f0\u3002";
        return result;
    }

    result.ok = true;
    return result;
}

ProError SaveModelCopyToWorkdir(const SaveCopyToWorkdirSource &source,
                                const std::wstring &new_name,
                                ProMdl *copied_mdl)
{
    if (copied_mdl != nullptr) {
        *copied_mdl = nullptr;
    }
    if (source.mdl == nullptr || new_name.empty()) {
        return PRO_TK_BAD_INPUTS;
    }

    ProMdlName pro_name = {0};
    CopyToProMdlName(new_name, pro_name);
    ProMdl copied = nullptr;
    ProError status = ProMdlnameCopy(source.mdl, pro_name, &copied);
    if (status != PRO_TK_NO_ERROR || copied == nullptr) {
        return status;
    }

    if (copied_mdl != nullptr) {
        *copied_mdl = copied;
    }
    return ProMdlSave(copied);
}

ProError AssembleSavedCopyToAssembly(ProAssembly target_assembly,
                                     ProMdl copied_mdl,
                                     ProAsmcomp *assembled_component,
                                     ProError *constraint_ui_status)
{
    if (assembled_component != nullptr) {
        *assembled_component = {};
    }
    if (constraint_ui_status != nullptr) {
        *constraint_ui_status = PRO_TK_GENERAL_ERROR;
    }
    if (target_assembly == nullptr || copied_mdl == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    const ProMdlType copied_type = autobbox::creo::ModelType(copied_mdl);
    if (!IsPartOrAsmType(copied_type)) {
        return PRO_TK_INVALID_TYPE;
    }

    ProMatrix initial_position = {
        {1.0, 0.0, 0.0, 0.0},
        {0.0, 1.0, 0.0, 0.0},
        {0.0, 0.0, 1.0, 0.0},
        {0.0, 0.0, 0.0, 1.0},
    };
    ProAsmcomp assembled = {};
    const ProError assemble_status = ProAsmcompAssemble(
        target_assembly,
        ProMdlToSolid(copied_mdl),
        initial_position,
        &assembled);
    if (assemble_status != PRO_TK_NO_ERROR) {
        return assemble_status;
    }

    if (assembled_component != nullptr) {
        *assembled_component = assembled;
    }

    const ProError constraint_status = ProAsmcompConstrRedefUI(&assembled);
    if (constraint_ui_status != nullptr) {
        *constraint_ui_status = constraint_status;
    }
    return PRO_TK_NO_ERROR;
}

std::wstring SaveCopyToWorkdirStatusMessage(ProError status)
{
    switch (status) {
    case PRO_TK_NO_ERROR:
        return L"\u64cd\u4f5c\u5b8c\u6210\u3002";
    case PRO_TK_BAD_INPUTS:
        return L"\u8f93\u5165\u53c2\u6570\u6216\u65b0\u540d\u79f0\u4e0d\u7b26\u5408 Creo \u89c4\u5219\u3002";
    case PRO_TK_NO_PERMISSION:
        return L"\u6ca1\u6709\u6743\u9650\u590d\u5236\u6216\u4fdd\u5b58\u8be5\u6a21\u578b\uff0c\u8bf7\u68c0\u67e5\u5de5\u4f5c\u76ee\u5f55\u6216\u6587\u4ef6\u6743\u9650\u3002";
    case PRO_TK_UNSUPPORTED:
        return L"\u8be5\u6a21\u578b\u7c7b\u578b\u4e0d\u652f\u6301\u53e6\u5b58\u526f\u672c\u3002";
    case PRO_TK_CANT_WRITE:
        return L"\u5199\u5165\u76ee\u6807\u4f4d\u7f6e\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u51b2\u7a81\u3001\u6743\u9650\u6216\u5de5\u4f5c\u76ee\u5f55\u72b6\u6001\u3002";
    case PRO_TK_BAD_CONTEXT:
        return L"\u5f53\u524d Creo \u72b6\u6001\u4e0d\u5141\u8bb8\u4fdd\u5b58\u6a21\u578b\u3002";
    default:
        return L"\u53e6\u5b58\u526f\u672c\u5931\u8d25\uff0cCreo \u8fd4\u56de\u72b6\u6001\uff1a" +
               std::to_wstring(static_cast<int>(status));
    }
}

std::wstring AssembleSavedCopyToAssemblyStatusMessage(ProError status)
{
    switch (status) {
    case PRO_TK_NO_ERROR:
        return L"\u64cd\u4f5c\u5b8c\u6210\u3002";
    case PRO_TK_BAD_INPUTS:
        return L"\u672a\u627e\u5230\u5f53\u524d\u88c5\u914d\u6216\u526f\u672c\u6a21\u578b\u53e5\u67c4\u65e0\u6548\u3002";
    case PRO_TK_INVALID_TYPE:
        return L"\u526f\u672c\u6a21\u578b\u4e0d\u662f\u53ef\u88c5\u914d\u7684\u96f6\u4ef6\u6216\u88c5\u914d\u3002";
    case PRO_TK_UNSUPPORTED:
        return L"\u8be5\u6a21\u578b\u4e0d\u652f\u6301\u901a\u8fc7 Toolkit \u88c5\u914d\u5230\u5f53\u524d\u88c5\u914d\u3002";
    case PRO_TK_GENERAL_ERROR:
        return L"\u521b\u5efa\u88c5\u914d\u7ec4\u4ef6\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u5f53\u524d\u88c5\u914d\u72b6\u6001\u548c\u6a21\u578b\u5faa\u73af\u5f15\u7528\u3002";
    case PRO_TK_BAD_CONTEXT:
        return L"\u5f53\u524d Creo \u72b6\u6001\u4e0d\u5141\u8bb8\u7ec4\u88c5\u6216\u91cd\u5b9a\u4e49\u7ec4\u4ef6\u7ea6\u675f\u3002";
    case PRO_TK_USER_ABORT:
        return L"\u7528\u6237\u53d6\u6d88\u4e86 Creo \u7ec4\u4ef6\u7ea6\u675f\u5b9a\u4e49\u3002";
    default:
        return L"\u7ec4\u88c5\u526f\u672c\u6216\u5b9a\u4e49\u7ea6\u675f\u5931\u8d25\uff0cCreo \u8fd4\u56de\u72b6\u6001\uff1a" +
               std::to_wstring(static_cast<int>(status));
    }
}

} // namespace autobbox::application
