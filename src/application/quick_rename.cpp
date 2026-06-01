#include "autobbox/application/quick_rename.h"

#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"

#include <ProArray.h>
#include <ProAsmcomppath.h>
#include <ProAssembly.h>
#include <ProMdl.h>
#include <ProModelitem.h>
#include <ProSelbuffer.h>
#include <ProSelection.h>
#include <ProSizeConst.h>
#include <ProToolkit.h>

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

    const std::wstring tail = value.substr(value.size() - suffix_text.size());
    return UppercaseAscii(tail) == UppercaseAscii(suffix_text);
}

bool ContainsPathOrWildcardChar(const std::wstring &value)
{
    return value.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos;
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

bool IsPartOrAsmType(ProMdlType type)
{
    return type == PRO_MDL_PART || type == PRO_MDL_ASSEMBLY;
}

bool IsSameNameNoCase(const std::wstring &lhs, const std::wstring &rhs)
{
    return UppercaseAscii(lhs) == UppercaseAscii(rhs);
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

void LogLine(const std::function<void(const std::string &line)> &log_sink, const std::string &line)
{
    if (log_sink) {
        log_sink(line);
    }
}

bool FillComponentPlacement(ProAsmcomppath &comp_path,
                            core::QuickRenameTarget &target_out,
                            const std::function<void(const std::string &line)> &log_sink)
{
    if (comp_path.owner == nullptr || comp_path.table_num <= 0) {
        LogLine(log_sink, "quick-rename component-path unavailable");
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
                "quick-rename parent-path status=" +
                    std::to_string(static_cast<int>(parent_status)));
        if (parent_status != PRO_TK_NO_ERROR || parent_mdl == nullptr) {
            return false;
        }
    }

    if (autobbox::creo::ModelType(parent_mdl) != PRO_MDL_ASSEMBLY) {
        LogLine(log_sink, "quick-rename parent-path not-assembly");
        return false;
    }

    target_out.has_component_path = true;
    target_out.component_path = comp_path;
    target_out.parent_assembly = ProMdlToAssembly(parent_mdl);
    target_out.component_id = comp_path.comp_id_table[comp_path.table_num - 1];
    LogLine(log_sink,
            "quick-rename component-path parent=" +
                autobbox::creo::DefaultModelTag(parent_mdl) +
                " comp_id=" + std::to_string(target_out.component_id));
    return true;
}

bool TargetFromSelection(ProSelection selection,
                         core::QuickRenameTarget &target_out,
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
        LogLine(log_sink, "quick-rename selection source=asmcomppath");
    }

    if (mdl == nullptr) {
        ProModelitem item = {};
        if (ProSelectionModelitemGet(selection, &item) == PRO_TK_NO_ERROR) {
            ProMdl owner = nullptr;
            if (ProModelitemMdlGet(&item, &owner) == PRO_TK_NO_ERROR && owner != nullptr) {
                mdl = owner;
                LogLine(log_sink, "quick-rename selection source=modelitem-owner");
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

    target_out.mdl = mdl;
    target_out.type = type;
    target_out.old_name = autobbox::creo::ModelName(mdl, L"");
    if (path_status == PRO_TK_NO_ERROR) {
        FillComponentPlacement(comp_path, target_out, log_sink);
    }
    return !target_out.old_name.empty();
}

bool TryTargetFromSelectionBuffer(core::QuickRenameTarget &target_out,
                                  bool &has_ambiguous_selection,
                                  const std::function<void(const std::string &line)> &log_sink)
{
    has_ambiguous_selection = false;

    ProSelection *buffer = nullptr;
    const ProError buffer_status = ProSelbufferSelectionsGet(&buffer);
    if (buffer_status != PRO_TK_NO_ERROR || buffer == nullptr) {
        LogLine(log_sink, "quick-rename selbuffer status=" + std::to_string(static_cast<int>(buffer_status)));
        return false;
    }

    int count = 0;
    ProArraySizeGet(reinterpret_cast<ProArray>(buffer), &count);
    LogLine(log_sink, "quick-rename selbuffer count=" + std::to_string(count));

    int valid_count = 0;
    core::QuickRenameTarget first_valid = {};
    for (int i = 0; i < count; ++i) {
        core::QuickRenameTarget candidate = {};
        if (TargetFromSelection(buffer[i], candidate, log_sink)) {
            ++valid_count;
            if (valid_count == 1) {
                first_valid = candidate;
            }
        }
    }

    ProSelectionarrayFree(buffer);
    if (valid_count == 1) {
        target_out = first_valid;
        return true;
    }

    has_ambiguous_selection = valid_count > 1;
    return false;
}

bool SelectTargetInteractively(core::QuickRenameTarget &target_out,
                               bool &cancelled,
                               std::wstring &error_text,
                               const std::function<void(const std::string &line)> &log_sink)
{
    cancelled = false;
    error_text.clear();

    char select_filter[] = "prt_or_asm";
    ProSelection *selection = nullptr;
    int selection_count = 0;
    const ProError select_status =
        ProSelect(select_filter, 1, nullptr, nullptr, nullptr, nullptr, &selection, &selection_count);
    LogLine(log_sink,
            "quick-rename proselect status=" + std::to_string(static_cast<int>(select_status)) +
                " count=" + std::to_string(selection_count));

    if (select_status == PRO_TK_USER_ABORT ||
        selection_count == 0 ||
        selection == nullptr) {
        cancelled = true;
        return false;
    }

    if (select_status != PRO_TK_NO_ERROR) {
        error_text = L"\u9009\u62e9\u6a21\u578b\u5931\u8d25\u3002";
        return false;
    }

    // ProSelect returns static selection storage; Creo reuses it on later calls.
    const bool ok = TargetFromSelection(selection[0], target_out, log_sink);
    if (!ok) {
        error_text = L"\u8bf7\u9009\u62e9\u88c5\u914d\u4e2d\u7684\u96f6\u4ef6\u6216\u5b50\u88c5\u914d\u6a21\u578b\u3002";
    }
    return ok;
}

} // namespace

bool ResolveQuickRenameTarget(core::QuickRenameTarget &target_out,
                              bool &cancelled,
                              std::wstring &error_text,
                              const std::function<void(const std::string &line)> &log_sink)
{
    target_out = {};
    cancelled = false;
    error_text.clear();

    bool has_ambiguous_selection = false;
    if (TryTargetFromSelectionBuffer(target_out, has_ambiguous_selection, log_sink)) {
        return true;
    }

    if (has_ambiguous_selection) {
        error_text = L"\u5f53\u524d\u9009\u4e2d\u4e86\u591a\u4e2a\u53ef\u91cd\u547d\u540d\u6a21\u578b\uff0c\u8bf7\u53ea\u4fdd\u7559\u4e00\u4e2a\u9009\u4e2d\u9879\u3002";
        return false;
    }

    return SelectTargetInteractively(target_out, cancelled, error_text, log_sink);
}

core::QuickRenameValidationResult ValidateQuickRenameName(
    const core::QuickRenameTarget &target,
    const std::wstring &input_name,
    bool allow_existing_same_type)
{
    core::QuickRenameValidationResult result = {};
    std::wstring name = TrimWhitespace(input_name);

    if (name.empty()) {
        result.error_text = L"\u65b0\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002";
        return result;
    }

    const wchar_t *expected_ext = ExpectedExtension(target.type);
    const bool has_prt_ext = EndsWithAsciiNoCase(name, L".prt");
    const bool has_asm_ext = EndsWithAsciiNoCase(name, L".asm");
    if (has_prt_ext || has_asm_ext) {
        if (!EndsWithAsciiNoCase(name, expected_ext)) {
            result.error_text = L"\u8f93\u5165\u7684\u6269\u5c55\u540d\u4e0e\u6a21\u578b\u7c7b\u578b\u4e0d\u5339\u914d\u3002";
            return result;
        }
        name = name.substr(0, name.size() - std::wcslen(expected_ext));
        name = TrimWhitespace(name);
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
    if (IsSameNameNoCase(name, target.old_name)) {
        result.ok = true;
        result.unchanged = true;
        result.normalized_name = name;
        return result;
    }

    ProMdl existing = nullptr;
    ProMdlName pro_name = {0};
    CopyToProMdlName(name, pro_name);
    const ProError init_status =
        ProMdlnameInit(pro_name, autobbox::creo::ToMdlFileType(target.type), &existing);
    if (init_status == PRO_TK_NO_ERROR && existing != nullptr && existing != target.mdl) {
        result.existing_name_conflict = true;
        result.existing_mdl = existing;
        if (!allow_existing_same_type) {
            result.error_text = L"\u540c\u540d\u6a21\u578b\u5df2\u5728\u4f1a\u8bdd\u4e2d\u5b58\u5728\uff0c\u8bf7\u6362\u4e00\u4e2a\u540d\u79f0\u3002";
            return result;
        }
    }

    result.ok = true;
    result.normalized_name = name;
    return result;
}

ProError RenameModelInSession(const core::QuickRenameTarget &target,
                              const std::wstring &new_name)
{
    if (target.mdl == nullptr || new_name.empty()) {
        return PRO_TK_BAD_INPUTS;
    }

    ProMdlName pro_name = {0};
    CopyToProMdlName(new_name, pro_name);
    return ProMdlnameRename(target.mdl, pro_name);
}

ProError CloneModelInSession(const core::QuickRenameTarget &target,
                             const std::wstring &new_name,
                             ProMdl *new_mdl)
{
    if (target.mdl == nullptr || new_name.empty()) {
        return PRO_TK_BAD_INPUTS;
    }

    ProMdlName pro_name = {0};
    CopyToProMdlName(new_name, pro_name);
    return ProMdlnameCopy(target.mdl, pro_name, new_mdl);
}

ProError ReplaceModelInAssembly(const core::QuickRenameTarget &target,
                                const std::wstring &replacement_name,
                                ProMdl *replacement_mdl)
{
    if (replacement_mdl != nullptr) {
        *replacement_mdl = nullptr;
    }
    if (target.mdl == nullptr ||
        target.parent_assembly == nullptr ||
        target.component_id <= 0 ||
        replacement_name.empty()) {
        return PRO_TK_BAD_INPUTS;
    }

    ProMdl replacement = nullptr;
    ProMdlName pro_name = {0};
    CopyToProMdlName(replacement_name, pro_name);
    ProError status = ProMdlnameInit(
        pro_name,
        autobbox::creo::ToMdlFileType(target.type),
        &replacement);

    if (status == PRO_TK_E_NOT_FOUND || replacement == nullptr) {
        ProFamilyMdlName family_name = {0};
        CopyToProFamilyMdlName(replacement_name, family_name);
        status = ProMdlnameRetrieve(
            family_name,
            autobbox::creo::ToMdlFileType(target.type),
            &replacement);
    }

    if (status != PRO_TK_NO_ERROR || replacement == nullptr) {
        return status;
    }
    status = ReplaceLoadedModelInAssembly(target, replacement);

    if (status == PRO_TK_NO_ERROR && replacement_mdl != nullptr) {
        *replacement_mdl = replacement;
    }
    return status;
}

ProError ReplaceLoadedModelInAssembly(const core::QuickRenameTarget &target,
                                      ProMdl replacement_mdl)
{
    if (target.mdl == nullptr ||
        target.parent_assembly == nullptr ||
        target.component_id <= 0 ||
        replacement_mdl == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }
    if (replacement_mdl == target.mdl) {
        return PRO_TK_NO_CHANGE;
    }
    if (autobbox::creo::ModelType(replacement_mdl) != target.type) {
        return PRO_TK_INVALID_TYPE;
    }

    int *component_ids = nullptr;
    ProError status = ProArrayAlloc(1, sizeof(int), 1, reinterpret_cast<ProArray *>(&component_ids));
    if (status != PRO_TK_NO_ERROR || component_ids == nullptr) {
        return status == PRO_TK_NO_ERROR ? PRO_TK_OUT_OF_MEMORY : status;
    }

    component_ids[0] = target.component_id;
    status = ProAssemblyAutointerchange(target.parent_assembly, component_ids, replacement_mdl);
    ProArrayFree(reinterpret_cast<ProArray *>(&component_ids));
    return status;
}

std::wstring QuickRenameStatusMessage(ProError status)
{
    switch (status) {
    case PRO_TK_NO_ERROR:
        return L"\u64cd\u4f5c\u5b8c\u6210\u3002";
    case PRO_TK_BAD_INPUTS:
        return L"\u65b0\u540d\u79f0\u4e0d\u7b26\u5408 Creo \u6a21\u578b\u547d\u540d\u89c4\u5219\u3002";
    case PRO_TK_NO_PERMISSION:
        return L"\u6ca1\u6709\u6743\u9650\u91cd\u547d\u540d\u8be5\u6a21\u578b\uff0c\u8bf7\u68c0\u67e5\u5de5\u4f5c\u533a\u6216\u6587\u4ef6\u6743\u9650\u3002";
    case PRO_TK_UNSUPPORTED:
        return L"\u8be5\u6a21\u578b\u7c7b\u578b\u4e0d\u652f\u6301\u91cd\u547d\u540d\u3002";
    case PRO_TK_BAD_CONTEXT:
        return L"\u5f53\u524d Creo \u72b6\u6001\u4e0d\u5141\u8bb8\u91cd\u547d\u540d\u3002";
    default:
        return L"\u91cd\u547d\u540d\u5931\u8d25\uff0cCreo \u8fd4\u56de\u72b6\u6001\uff1a" +
               std::to_wstring(static_cast<int>(status));
    }
}

std::wstring QuickCloneStatusMessage(ProError status)
{
    switch (status) {
    case PRO_TK_NO_ERROR:
        return L"\u64cd\u4f5c\u5b8c\u6210\u3002";
    case PRO_TK_BAD_INPUTS:
        return L"\u65b0\u540d\u79f0\u4e0d\u7b26\u5408 Creo \u6a21\u578b\u547d\u540d\u89c4\u5219\u3002";
    case PRO_TK_NO_PERMISSION:
        return L"\u6ca1\u6709\u6743\u9650\u514b\u9686\u8be5\u6a21\u578b\uff0c\u8bf7\u68c0\u67e5\u5de5\u4f5c\u533a\u6216\u6587\u4ef6\u6743\u9650\u3002";
    case PRO_TK_UNSUPPORTED:
        return L"\u8be5\u6a21\u578b\u7c7b\u578b\u4e0d\u652f\u6301\u514b\u9686\u3002";
    default:
        return L"\u514b\u9686\u5931\u8d25\uff0cCreo \u8fd4\u56de\u72b6\u6001\uff1a" +
               std::to_wstring(static_cast<int>(status));
    }
}

std::wstring QuickReplaceStatusMessage(ProError status)
{
    switch (status) {
    case PRO_TK_NO_ERROR:
        return L"\u64cd\u4f5c\u5b8c\u6210\u3002";
    case PRO_TK_NO_CHANGE:
        return L"\u66ff\u6362\u6a21\u578b\u4e0e\u5f53\u524d\u5b9e\u4f8b\u76f8\u540c\uff0c\u65e0\u9700\u66ff\u6362\u3002";
    case PRO_TK_BAD_INPUTS:
        return L"\u65e0\u6cd5\u83b7\u53d6\u5f53\u524d\u88c5\u914d\u5b9e\u4f8b\u6216\u66ff\u6362\u540d\u79f0\u4e0d\u7b26\u5408 Creo \u89c4\u5219\u3002";
    case PRO_TK_E_NOT_FOUND:
        return L"\u672a\u5728\u4f1a\u8bdd\u6216\u5f53\u524d\u76ee\u5f55\u627e\u5230\u540c\u540d\u540c\u7c7b\u578b\u66ff\u6362\u6a21\u578b\u3002";
    case PRO_TK_INVALID_TYPE:
        return L"\u66ff\u6362\u6a21\u578b\u7684\u7c7b\u578b\u4e0e\u5f53\u524d\u5b9e\u4f8b\u4e0d\u4e00\u81f4\u3002";
    case PRO_TK_BAD_CONTEXT:
        return L"\u5f53\u524d Creo \u72b6\u6001\u4e0d\u5141\u8bb8\u539f\u5730\u66ff\u6362\u3002";
    case PRO_TK_NO_PERMISSION:
        return L"\u6ca1\u6709\u6743\u9650\u8bfb\u53d6\u6216\u66ff\u6362\u8be5\u6a21\u578b\u3002";
    case PRO_TK_GENERAL_ERROR:
        return L"\u539f\u5730\u66ff\u6362\u5931\u8d25\uff1b\u8bf7\u786e\u8ba4\u66ff\u6362\u6a21\u578b\u4e0e\u5f53\u524d\u5b9e\u4f8b\u6709\u4e92\u6362\u7ec4\u3001\u65cf\u8868\u6216\u53ef\u6620\u5c04\u7684\u5168\u5c40\u53c2\u7167\u3002";
    default:
        return L"\u539f\u5730\u66ff\u6362\u5931\u8d25\uff0cCreo \u8fd4\u56de\u72b6\u6001\uff1a" +
               std::to_wstring(static_cast<int>(status));
    }
}

} // namespace autobbox::application
