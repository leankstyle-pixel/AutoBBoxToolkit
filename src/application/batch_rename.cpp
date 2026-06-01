#include "autobbox/application/batch_rename.h"

#include "autobbox/application/quick_rename.h"
#include "autobbox/application/target_collectors.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/creo/parameter_api.h"

#include <ProMdl.h>
#include <ProToolkit.h>

#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace autobbox::application {

namespace {

constexpr const wchar_t *kCommonNameParam = L"PTC_COMMON_NAME";

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

bool SameNameNoCase(const std::wstring &lhs, const std::wstring &rhs)
{
    return UppercaseAscii(lhs) == UppercaseAscii(rhs);
}

std::string FinalNameKey(ProMdlType type, const std::wstring &name)
{
    return std::to_string(static_cast<int>(type)) + ":" +
           autobbox::common::WToA(UppercaseAscii(name).c_str());
}

void LogLine(const BatchRenameLogSink &log_sink, const std::string &line)
{
    if (log_sink) {
        log_sink(line);
    }
}

std::wstring TypeLabel(ProMdlType type)
{
    switch (type) {
    case PRO_MDL_PART:
        return L"PRT";
    case PRO_MDL_ASSEMBLY:
        return L"ASM";
    default:
        return L"MDL";
    }
}

void SetIssue(core::BatchRenameCandidate &candidate,
              size_t row_index,
              const std::wstring &message,
              std::vector<core::BatchRenameValidationIssue> &issues)
{
    candidate.has_error = true;
    candidate.status_text = message;
    core::BatchRenameValidationIssue issue;
    issue.row_index = row_index;
    issue.message = message;
    issues.push_back(issue);
}

std::wstring ApplyStatusMessage(ProError status, bool common_name)
{
    if (!common_name) {
        return QuickRenameStatusMessage(status);
    }

    switch (status) {
    case PRO_TK_NO_ERROR:
        return L"PTC_COMMON_NAME 已更新";
    case PRO_TK_BAD_INPUTS:
        return L"PTC_COMMON_NAME 输入无效";
    case PRO_TK_NO_PERMISSION:
        return L"没有权限修改 PTC_COMMON_NAME";
    case PRO_TK_INVALID_TYPE:
        return L"PTC_COMMON_NAME 类型不是字符串";
    case PRO_TK_E_NOT_FOUND:
    case PRO_TK_GENERAL_ERROR:
        return L"写入 PTC_COMMON_NAME 失败";
    default:
        return L"写入 PTC_COMMON_NAME 失败，Creo 状态=" +
               std::to_wstring(static_cast<int>(status));
    }
}

std::wstring ToUpperForSearch(const std::wstring &value)
{
    std::wstring out = value;
    for (wchar_t &ch : out) {
        ch = static_cast<wchar_t>(std::towupper(ch));
    }
    return out;
}

std::wstring &ColumnValue(core::BatchRenameCandidate &candidate,
                          core::BatchRenameEditableColumn column)
{
    return column == core::BatchRenameEditableColumn::NewModelName
               ? candidate.new_model_name
               : candidate.new_common_name;
}

std::wstring ColumnValue(const core::BatchRenameCandidate &candidate,
                         core::BatchRenameEditableColumn column)
{
    return column == core::BatchRenameEditableColumn::NewModelName
               ? candidate.new_model_name
               : candidate.new_common_name;
}

void MarkTransformed(core::BatchRenameCandidate &candidate, const wchar_t *status_text)
{
    candidate.has_error = false;
    candidate.normalized_new_model_name.clear();
    candidate.status_text = status_text == nullptr ? L"" : status_text;
}

bool ReplaceModelParameterToken(std::wstring &pattern,
                                const core::BatchRenameCandidate &candidate,
                                const std::wstring &prefix)
{
    bool replaced = false;
    size_t pos = 0;
    while ((pos = pattern.find(prefix, pos)) != std::wstring::npos) {
        const size_t name_begin = pos + prefix.size();
        const size_t name_end = pattern.find(L'}', name_begin);
        if (name_end == std::wstring::npos) {
            break;
        }

        const std::wstring param_name = TrimWhitespace(pattern.substr(name_begin, name_end - name_begin));
        std::wstring param_value;
        if (!param_name.empty()) {
            autobbox::creo::ReadParamDisplayValueOnModel(candidate.mdl,
                                                         param_name.c_str(),
                                                         param_value);
        }

        pattern.replace(pos, name_end - pos + 1, param_value);
        pos += param_value.size();
        replaced = true;
    }
    return replaced;
}

bool ReplaceImplicitModelParameterTokens(std::wstring &pattern,
                                         const core::BatchRenameCandidate &candidate)
{
    bool replaced = false;
    size_t pos = 0;
    while ((pos = pattern.find(L'{', pos)) != std::wstring::npos) {
        const size_t name_begin = pos + 1;
        const size_t name_end = pattern.find(L'}', name_begin);
        if (name_end == std::wstring::npos) {
            break;
        }

        const std::wstring param_name = TrimWhitespace(pattern.substr(name_begin, name_end - name_begin));
        if (param_name.empty() ||
            param_name.find(L':') != std::wstring::npos ||
            param_name.find(L'{') != std::wstring::npos) {
            pos = name_end + 1;
            continue;
        }

        std::wstring param_value;
        autobbox::creo::ReadParamDisplayValueOnModel(candidate.mdl,
                                                     param_name.c_str(),
                                                     param_value);
        pattern.replace(pos, name_end - pos + 1, param_value);
        pos += param_value.size();
        replaced = true;
    }
    return replaced;
}

std::wstring ExpandTemplate(std::wstring pattern,
                            const core::BatchRenameCandidate &candidate,
                            core::BatchRenameEditableColumn target_column,
                            const std::wstring &source_value,
                            const std::wstring &match_value,
                            const std::wstring &sequence_value,
                            size_t row_index)
{
    const auto replace_token = [&](const wchar_t *token, const std::wstring &value) {
        if (token == nullptr) {
            return;
        }
        const std::wstring needle(token);
        size_t pos = 0;
        while ((pos = pattern.find(needle, pos)) != std::wstring::npos) {
            pattern.replace(pos, needle.size(), value);
            pos += value.size();
        }
    };

    replace_token(L"{match}", match_value);
    replace_token(L"{name}", source_value);
    replace_token(L"{model}", candidate.model_name);
    replace_token(L"{common}", candidate.new_common_name);
    replace_token(L"{row}", std::to_wstring(static_cast<unsigned long long>(row_index + 1)));
    replace_token(L"{num}", sequence_value);

    ReplaceModelParameterToken(pattern, candidate, L"{param:");
    ReplaceModelParameterToken(pattern, candidate, L"{\u53c2\u6570:");
    ReplaceModelParameterToken(pattern, candidate, L"{\u6a21\u578b\u53c2\u6570:");

    if (target_column == core::BatchRenameEditableColumn::CommonName) {
        replace_token(L"{target}", candidate.new_common_name);
    } else {
        replace_token(L"{target}", candidate.new_model_name);
    }

    ReplaceImplicitModelParameterTokens(pattern, candidate);

    return pattern;
}

bool ReplaceAllMatches(const core::BatchRenameCandidate &candidate,
                       core::BatchRenameEditableColumn column,
                       const core::BatchRenameReplaceSpec &spec,
                       size_t row_index,
                       std::wstring &result_out,
                       int &match_count)
{
    const std::wstring source_value = ColumnValue(candidate, column);
    const std::wstring find_text = spec.find_text;
    if (find_text.empty()) {
        result_out = source_value;
        match_count = 0;
        return false;
    }

    const std::wstring search_haystack =
        spec.case_sensitive ? source_value : ToUpperForSearch(source_value);
    const std::wstring search_needle =
        spec.case_sensitive ? find_text : ToUpperForSearch(find_text);

    size_t pos = 0;
    size_t search_pos = 0;
    match_count = 0;
    std::wstring output;
    while ((search_pos = search_haystack.find(search_needle, pos)) != std::wstring::npos) {
        output.append(source_value, pos, search_pos - pos);
        const std::wstring match_value = source_value.substr(search_pos, find_text.size());
        if (spec.mode == core::BatchRenameReplaceMode::Template) {
            output += ExpandTemplate(spec.replace_text,
                                     candidate,
                                     column,
                                     source_value,
                                     match_value,
                                     std::wstring(),
                                     row_index);
        } else {
            output += spec.replace_text;
        }
        pos = search_pos + find_text.size();
        ++match_count;
    }
    output.append(source_value, pos, source_value.size() - pos);
    result_out = match_count > 0 ? output : source_value;
    return true;
}

std::wstring FormatSequenceNumber(int value, int width);

std::wstring FormatSequenceNumber(int value, int width)
{
    std::wstringstream stream;
    if (width > 0) {
        stream << std::setw(width) << std::setfill(L'0');
    }
    stream << value;
    return stream.str();
}

} // namespace

std::vector<core::BatchRenameCandidate> CollectBatchRenameCandidates(
    const core::BatchRenameOptions &options)
{
    std::vector<core::BatchRenameCandidate> result;
    if (options.parts != PRO_B_TRUE && options.assemblies != PRO_B_TRUE) {
        return result;
    }

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR || current == nullptr ||
        autobbox::creo::ModelType(current) != PRO_MDL_ASSEMBLY) {
        return result;
    }

    std::vector<ProMdl> models = CollectTargetsFromCurrentModel(
        options.parts,
        options.assemblies,
        options.top_level_only);

    for (ProMdl mdl : models) {
        if (mdl == nullptr || mdl == current) {
            continue;
        }
        const ProMdlType type = autobbox::creo::ModelType(mdl);
        if ((type == PRO_MDL_PART && options.parts != PRO_B_TRUE) ||
            (type == PRO_MDL_ASSEMBLY && options.assemblies != PRO_B_TRUE) ||
            (type != PRO_MDL_PART && type != PRO_MDL_ASSEMBLY)) {
            continue;
        }

        core::BatchRenameCandidate candidate;
        candidate.mdl = mdl;
        candidate.type = type;
        candidate.row_name = "br_" + std::to_string(result.size());
        candidate.model_name = autobbox::creo::ModelName(mdl, L"");
        candidate.new_model_name = candidate.model_name;
        autobbox::creo::ReadStringParamOnModel(mdl, kCommonNameParam, candidate.common_name);
        candidate.new_common_name = candidate.common_name;
        candidate.status_text = TypeLabel(type);
        result.push_back(candidate);
    }

    std::sort(result.begin(), result.end(), [](const core::BatchRenameCandidate &lhs,
                                               const core::BatchRenameCandidate &rhs) {
        if (lhs.type != rhs.type) {
            return lhs.type < rhs.type;
        }
        return UppercaseAscii(lhs.model_name) < UppercaseAscii(rhs.model_name);
    });
    for (size_t i = 0; i < result.size(); ++i) {
        result[i].row_name = "br_" + std::to_string(i);
    }
    return result;
}

bool BatchRenameCandidateHasChanges(const core::BatchRenameCandidate &candidate)
{
    const std::wstring new_name = TrimWhitespace(candidate.new_model_name);
    const bool rename_changed = !SameNameNoCase(new_name, candidate.model_name);
    const bool common_changed = candidate.new_common_name != candidate.common_name;
    return rename_changed || common_changed;
}

bool ValidateBatchRenameCandidates(
    std::vector<core::BatchRenameCandidate> &candidates,
    std::vector<core::BatchRenameValidationIssue> &issues)
{
    issues.clear();
    std::unordered_map<std::string, size_t> final_name_owner;

    for (size_t i = 0; i < candidates.size(); ++i) {
        core::BatchRenameCandidate &candidate = candidates[i];
        if (!candidate.selected) {
            candidate.has_error = false;
            candidate.status_text = L"\u672a\u52fe\u9009";
            candidate.normalized_new_model_name.clear();
            continue;
        }
        candidate.has_error = false;
        candidate.status_text.clear();
        candidate.normalized_new_model_name.clear();

        if (candidate.mdl == nullptr || candidate.model_name.empty()) {
            SetIssue(candidate, i, L"模型句柄无效", issues);
            continue;
        }

        core::QuickRenameTarget target;
        target.mdl = candidate.mdl;
        target.type = candidate.type;
        target.old_name = candidate.model_name;

        const core::QuickRenameValidationResult validation =
            ValidateQuickRenameName(target, candidate.new_model_name);
        if (!validation.ok) {
            SetIssue(candidate, i, validation.error_text.empty() ? L"模型名无效" : validation.error_text, issues);
            continue;
        }

        candidate.normalized_new_model_name = validation.normalized_name;
        if (candidate.normalized_new_model_name.empty()) {
            candidate.normalized_new_model_name = candidate.model_name;
        }

        const std::string key = FinalNameKey(candidate.type, candidate.normalized_new_model_name);
        const auto inserted = final_name_owner.emplace(key, i);
        if (!inserted.second) {
            SetIssue(candidate, i, L"目标模型名在本批次中重复", issues);
            core::BatchRenameCandidate &first = candidates[inserted.first->second];
            if (!first.has_error) {
                SetIssue(first, inserted.first->second, L"目标模型名在本批次中重复", issues);
            }
            continue;
        }

        if (!BatchRenameCandidateHasChanges(candidate)) {
            candidate.status_text = L"未改动";
        } else {
            candidate.status_text = L"待应用";
        }
    }

    return issues.empty();
}

void ClearBatchRenameCandidates(
    std::vector<core::BatchRenameCandidate> &candidates,
    const core::BatchRenameClearSpec &spec,
    core::BatchRenameTransformSummary &summary)
{
    summary = {};
    for (core::BatchRenameCandidate &candidate : candidates) {
        if (!candidate.selected) {
            continue;
        }
        bool changed = false;
        if (spec.clear_new_model_name && !candidate.new_model_name.empty()) {
            candidate.new_model_name.clear();
            changed = true;
            ++summary.touched_rows;
        }
        if (spec.clear_common_name && !candidate.new_common_name.empty()) {
            candidate.new_common_name.clear();
            changed = true;
            ++summary.touched_rows;
        }
        if (changed) {
            MarkTransformed(candidate, L"\u5df2\u6e05\u7a7a\uff0c\u5f85\u9a8c\u8bc1");
            ++summary.changed_rows;
        }
    }
    summary.summary_text = summary.changed_rows > 0
                               ? (L"\u5df2\u6e05\u7a7a " +
                                  std::to_wstring(summary.changed_rows) +
                                  L" \u884c")
                               : L"\u6ca1\u6709\u53ef\u6e05\u7a7a\u7684\u5185\u5bb9\u3002";
}

bool ReplaceBatchRenameCandidates(
    std::vector<core::BatchRenameCandidate> &candidates,
    const core::BatchRenameReplaceSpec &spec,
    core::BatchRenameTransformSummary &summary,
    std::wstring &error_out)
{
    summary = {};
    error_out.clear();

    if (spec.find_text.empty()) {
        if (spec.mode != core::BatchRenameReplaceMode::Template) {
            error_out = L"\u8bf7\u8f93\u5165\u8981\u67e5\u627e\u7684\u6587\u672c\u3002";
            return false;
        }
    }

    for (size_t i = 0; i < candidates.size(); ++i) {
        core::BatchRenameCandidate &candidate = candidates[i];
        if (!candidate.selected) {
            continue;
        }
        std::wstring replaced_value;
        int match_count = 0;
        if (spec.mode == core::BatchRenameReplaceMode::Template) {
            const std::wstring source_value = ColumnValue(candidate, spec.target_column);
            replaced_value = ExpandTemplate(spec.replace_text,
                                            candidate,
                                            spec.target_column,
                                            source_value,
                                            std::wstring(),
                                            std::wstring(),
                                            i);
            match_count = 1;
        } else {
            if (!ReplaceAllMatches(candidate,
                                   spec.target_column,
                                   spec,
                                   i,
                                   replaced_value,
                                   match_count)) {
                continue;
            }
            if (match_count <= 0) {
                continue;
            }
        }
        ++summary.touched_rows;
        std::wstring &target_value = ColumnValue(candidate, spec.target_column);
        if (target_value != replaced_value) {
            target_value = replaced_value;
            MarkTransformed(candidate, L"\u5df2\u66ff\u6362\uff0c\u5f85\u9a8c\u8bc1");
            ++summary.changed_rows;
        }
    }

    summary.summary_text = summary.changed_rows > 0
                               ? (L"\u5df2\u66ff\u6362 " +
                                  std::to_wstring(summary.changed_rows) +
                                  L" \u884c")
                               : (spec.mode == core::BatchRenameReplaceMode::Template
                                      ? L"\u672a\u751f\u6210\u65b0\u7684\u6a21\u677f\u66ff\u6362\u7ed3\u679c\u3002"
                                      : L"\u672a\u627e\u5230\u53ef\u66ff\u6362\u7684\u6587\u672c\u3002");
    return true;
}

bool SequenceBatchRenameCandidates(
    std::vector<core::BatchRenameCandidate> &candidates,
    const core::BatchRenameSequenceSpec &spec,
    core::BatchRenameTransformSummary &summary,
    std::wstring &error_out)
{
    summary = {};
    error_out.clear();

    if (TrimWhitespace(spec.template_text).empty()) {
        error_out = L"\u8bf7\u8f93\u5165\u5e8f\u53f7\u6a21\u677f\u3002";
        return false;
    }
    if (spec.step == 0) {
        error_out = L"\u6b65\u957f\u4e0d\u80fd\u4e3a 0\u3002";
        return false;
    }
    if (spec.width < 0) {
        error_out = L"\u4f4d\u6570\u4e0d\u80fd\u4e3a\u8d1f\u6570\u3002";
        return false;
    }

    for (size_t i = 0; i < candidates.size(); ++i) {
        core::BatchRenameCandidate &candidate = candidates[i];
        if (!candidate.selected) {
            continue;
        }
        const int seq_value = spec.start + static_cast<int>(i) * spec.step;
        const std::wstring source_value = ColumnValue(candidate, spec.target_column);
        const std::wstring generated = ExpandTemplate(spec.template_text,
                                                      candidate,
                                                      spec.target_column,
                                                      source_value,
                                                      std::wstring(),
                                                      FormatSequenceNumber(seq_value, spec.width),
                                                      i);
        ++summary.touched_rows;
        std::wstring &target_value = ColumnValue(candidate, spec.target_column);
        if (target_value != generated) {
            target_value = generated;
            MarkTransformed(candidate, L"\u5df2\u6dfb\u52a0\u5e8f\u53f7\uff0c\u5f85\u9a8c\u8bc1");
            ++summary.changed_rows;
        }
    }

    summary.summary_text = summary.changed_rows > 0
                               ? (L"\u5df2\u4e3a " +
                                  std::to_wstring(summary.changed_rows) +
                                  L" \u884c\u751f\u6210\u5e8f\u53f7")
                               : L"\u672a\u751f\u6210\u65b0\u7684\u5e8f\u53f7\u53d8\u66f4\u3002";
    return true;
}

ProError ApplyBatchRenameCandidates(
    std::vector<core::BatchRenameCandidate> &candidates,
    core::BatchRenameApplySummary &summary,
    const BatchRenameLogSink &log_sink)
{
    summary = {};
    std::vector<core::BatchRenameValidationIssue> issues;
    if (!ValidateBatchRenameCandidates(candidates, issues)) {
        summary.failed = static_cast<int>(issues.size());
        summary.summary_text = L"存在校验错误，未执行写入";
        LogLine(log_sink, "batch-rename validate failed issues=" + std::to_string(issues.size()));
        return PRO_TK_BAD_INPUTS;
    }

    ProError overall = PRO_TK_NO_ERROR;
    for (core::BatchRenameCandidate &candidate : candidates) {
        if (!candidate.selected) {
            candidate.has_error = false;
            candidate.status_text = L"\u672a\u52fe\u9009";
            ++summary.skipped;
            continue;
        }
        const bool rename_changed = !SameNameNoCase(candidate.normalized_new_model_name, candidate.model_name);
        const bool common_changed = candidate.new_common_name != candidate.common_name;
        if (!rename_changed && !common_changed) {
            candidate.status_text = L"未改动";
            ++summary.skipped;
            continue;
        }

        ++summary.changed_rows;
        bool row_failed = false;
        bool row_renamed = false;
        bool row_common_updated = false;

        if (rename_changed) {
            core::QuickRenameTarget target;
            target.mdl = candidate.mdl;
            target.type = candidate.type;
            target.old_name = candidate.model_name;
            const ProError rename_status = RenameModelInSession(target, candidate.normalized_new_model_name);
            LogLine(log_sink,
                    "batch-rename rename old=" + autobbox::common::WToA(candidate.model_name.c_str()) +
                        " new=" + autobbox::common::WToA(candidate.normalized_new_model_name.c_str()) +
                        " status=" + std::to_string(static_cast<int>(rename_status)));
            if (rename_status != PRO_TK_NO_ERROR) {
                candidate.has_error = true;
                candidate.status_text = ApplyStatusMessage(rename_status, false);
                ++summary.failed;
                overall = rename_status;
                continue;
            }
            candidate.model_name = candidate.normalized_new_model_name;
            candidate.new_model_name = candidate.normalized_new_model_name;
            row_renamed = true;
            ++summary.renamed;
        }

        if (common_changed) {
            const ProError common_status = autobbox::creo::SetStringParamOnModel(
                candidate.mdl,
                kCommonNameParam,
                candidate.new_common_name);
            LogLine(log_sink,
                    "batch-rename common model=" + autobbox::common::WToA(candidate.model_name.c_str()) +
                        " value=" + autobbox::common::WToA(candidate.new_common_name.c_str()) +
                        " status=" + std::to_string(static_cast<int>(common_status)));
            if (common_status != PRO_TK_NO_ERROR) {
                candidate.has_error = true;
                candidate.status_text = (row_renamed ? L"已重命名；" : L"") + ApplyStatusMessage(common_status, true);
                ++summary.failed;
                overall = common_status;
                row_failed = true;
            } else {
                candidate.common_name = candidate.new_common_name;
                row_common_updated = true;
                ++summary.common_updated;
            }
        }

        if (!row_failed) {
            candidate.has_error = false;
            if (row_renamed && row_common_updated) {
                candidate.status_text = L"重命名和参数已更新";
            } else if (row_renamed) {
                candidate.status_text = L"已重命名";
            } else {
                candidate.status_text = L"PTC_COMMON_NAME 已更新";
            }
        }
    }

    summary.summary_text = L"改动行 " + std::to_wstring(summary.changed_rows) +
                           L"，重命名 " + std::to_wstring(summary.renamed) +
                           L"，更新 PTC_COMMON_NAME " + std::to_wstring(summary.common_updated) +
                           L"，失败 " + std::to_wstring(summary.failed) +
                           L"，未改动 " + std::to_wstring(summary.skipped) + L" 行";
    return overall;
}

} // namespace autobbox::application
