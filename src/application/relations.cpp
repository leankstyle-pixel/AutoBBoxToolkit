#include "autobbox/application/relations.h"

#include "autobbox/creo/model_info.h"
#include "autobbox/common/strings.h"

#include <ProArray.h>
#include <ProModelitem.h>
#include <ProParameter.h>
#include <ProParamval.h>
#include <ProRelSet.h>
#include <ProSolid.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace autobbox::application {

namespace {

struct MainRelsetFindCtx {
    ProRelset relset = {};
    bool found = false;
};

struct RelationCleanResult {
    std::vector<std::wstring> lines;
    int total_before = 0;
    int total_after = 0;
    int single_before = 0;
    int single_after = 0;
    int removed_duplicates = 0;
};

struct RelationApplyDiagnostics {
    ProError set_status = PRO_TK_NO_ERROR;
    ProError regen_status = PRO_TK_NO_ERROR;
    ProError solid_regen_status = PRO_TK_NO_ERROR;
    ProError rollback_set_status = PRO_TK_NO_ERROR;
    ProError rollback_regen_status = PRO_TK_NO_ERROR;
    ProError delete_created_status = PRO_TK_NO_ERROR;
};

enum class RelationParamIssueKind {
    MissingParameter,
    TypeMismatch
};

struct RelationParamIssue {
    RelationParamIssueKind kind = RelationParamIssueKind::MissingParameter;
    std::wstring token;
    std::wstring line_text;
    int line_index = 0;
    ProParamvalueType expected_type = PRO_PARAM_NOT_SET;
    ProParamvalueType actual_type = PRO_PARAM_NOT_SET;
};

struct RelationDuplicateInfo {
    bool duplicated = false;
    bool duplicated_in_input = false;
    int input_line_index = 0;
    int existing_line_index = 0;
    std::wstring line_text;
};

void LogLine(const RelationLogSink &log_sink, const char *fmt, ...)
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

std::wstring TrimWhitespace(const std::wstring &value)
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

std::wstring UppercaseAscii(const std::wstring &value)
{
    std::wstring out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towupper(ch));
    });
    return out;
}

std::vector<std::wstring> SplitMultilineText(const std::wstring &text)
{
    std::vector<std::wstring> lines;
    if (text.empty()) {
        return lines;
    }

    std::wstring normalized;
    normalized.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (ch == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') {
                ++i;
            }
            normalized.push_back(L'\n');
        } else {
            normalized.push_back(ch);
        }
    }

    size_t start = 0;
    while (start <= normalized.size()) {
        const size_t pos = normalized.find(L'\n', start);
        if (pos == std::wstring::npos) {
            lines.push_back(normalized.substr(start));
            break;
        }
        lines.push_back(normalized.substr(start, pos - start));
        start = pos + 1;
        if (start == normalized.size()) {
            lines.push_back(std::wstring());
            break;
        }
    }

    return lines;
}

bool HasNonWhitespaceLine(const std::vector<std::wstring> &lines)
{
    for (const std::wstring &line : lines) {
        if (!TrimWhitespace(line).empty()) {
            return true;
        }
    }
    return false;
}

std::wstring JoinRelationLinesForLog(const std::vector<std::wstring> &lines)
{
    std::wstring joined;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            joined += L"\n";
        }
        joined += lines[i];
    }
    return joined;
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

bool IsPartOrAsm(ProMdl mdl)
{
    return autobbox::creo::IsPartOrAsm(mdl);
}

ProModelitem ModelAsModelitem(ProMdl mdl)
{
    ProModelitem item = {};
    if (mdl != nullptr) {
        ProMdlToModelitem(mdl, &item);
    }
    return item;
}

std::string ModelTag(ProMdl mdl, const RelationModelTagFormatter &format_model_tag)
{
    if (format_model_tag) {
        return format_model_tag(mdl);
    }
    return autobbox::creo::DefaultModelTag(mdl);
}

bool StartsWithToken(const std::wstring &value, const wchar_t *token)
{
    if (token == nullptr) {
        return false;
    }
    const size_t token_len = std::wcslen(token);
    if (value.size() < token_len) {
        return false;
    }
    if (value.compare(0, token_len, token) != 0) {
        return false;
    }
    if (value.size() == token_len) {
        return true;
    }
    const wchar_t next = value[token_len];
    return std::iswspace(next) || next == L'(' || next == L'_';
}

bool IsRelationCommentLine(const std::wstring &trimmed)
{
    if (trimmed.empty()) {
        return false;
    }
    return trimmed.rfind(L"/*", 0) == 0 ||
           trimmed.rfind(L"//", 0) == 0 ||
           trimmed.rfind(L"!", 0) == 0 ||
           trimmed.rfind(L"#", 0) == 0;
}

bool IsRelationIfLine(const std::wstring &upper_trimmed)
{
    return StartsWithToken(upper_trimmed, L"IF");
}

bool IsRelationElseLine(const std::wstring &upper_trimmed)
{
    return StartsWithToken(upper_trimmed, L"ELSE") ||
           upper_trimmed.rfind(L"ELSEIF", 0) == 0 ||
           upper_trimmed.rfind(L"ELSE_IF", 0) == 0;
}

bool IsRelationEndifLine(const std::wstring &upper_trimmed)
{
    return StartsWithToken(upper_trimmed, L"ENDIF");
}

bool IsRelationDuplicateCandidate(const std::wstring &line)
{
    const std::wstring trimmed = TrimWhitespace(line);
    if (trimmed.empty() || IsRelationCommentLine(trimmed)) {
        return false;
    }

    const std::wstring upper_trimmed = UppercaseAscii(trimmed);
    return !IsRelationIfLine(upper_trimmed) &&
           !IsRelationElseLine(upper_trimmed) &&
           !IsRelationEndifLine(upper_trimmed);
}

std::wstring NormalizeRelationLineForDuplicate(const std::wstring &line)
{
    return TrimWhitespace(line);
}

const wchar_t *ParamTypeLabel(ProParamvalueType type)
{
    switch (type) {
    case PRO_PARAM_STRING:
        return L"STRING";
    case PRO_PARAM_DOUBLE:
        return L"DOUBLE";
    case PRO_PARAM_INTEGER:
        return L"INTEGER";
    case PRO_PARAM_BOOLEAN:
        return L"BOOLEAN";
    case PRO_PARAM_NOTE_ID:
        return L"NOTE_ID";
    case PRO_PARAM_VOID:
        return L"VOID";
    case PRO_PARAM_NOT_SET:
        return L"NOT_SET";
    default:
        return L"UNKNOWN";
    }
}

const char *RelationParamIssueKindLabel(RelationParamIssueKind kind)
{
    switch (kind) {
    case RelationParamIssueKind::MissingParameter:
        return "missing-param";
    case RelationParamIssueKind::TypeMismatch:
        return "type-mismatch";
    default:
        return "param-issue";
    }
}

void LogRelationLinesBlock(const RelationLogSink &log_sink,
                           const RelationModelTagFormatter &format_model_tag,
                           const char *prefix,
                           ProMdl mdl,
                           const std::vector<std::wstring> &lines)
{
    if (prefix == nullptr) {
        prefix = "relation-text";
    }

    const std::string mdl_tag = ModelTag(mdl, format_model_tag);
    LogLine(log_sink,
            "%s %s line_count=%d",
            prefix,
            mdl_tag.c_str(),
            static_cast<int>(lines.size()));
    for (size_t i = 0; i < lines.size(); ++i) {
        LogLine(log_sink,
                "%s %s line[%zu]=%s",
                prefix,
                mdl_tag.c_str(),
                i + 1,
                autobbox::common::WToA(lines[i].c_str()).c_str());
    }
}

bool GetParamType(ProMdl mdl, const std::wstring &param_name, ProParamvalueType &type)
{
    type = PRO_PARAM_NOT_SET;
    if (mdl == nullptr || param_name.empty()) {
        return false;
    }

    ProParameter param;
    ProModelitem owner = ModelAsModelitem(mdl);
    ProName pname = {0};
    CopyWStr(pname, param_name.c_str());
    if (ProParameterInit(&owner, pname, &param) != PRO_TK_NO_ERROR) {
        return false;
    }

    ProParamvalue value;
    std::memset(&value, 0, sizeof(value));
    if (ProParameterValueWithUnitsGet(&param, &value, nullptr) != PRO_TK_NO_ERROR) {
        return false;
    }

    return ProParamvalueTypeGet(&value, &type) == PRO_TK_NO_ERROR;
}

bool IsParamLikeIdentifier(const std::wstring &token)
{
    if (token.empty()) {
        return false;
    }
    const wchar_t first = token[0];
    if (!(std::iswalpha(first) || first == L'_')) {
        return false;
    }
    bool has_alpha = false;
    for (wchar_t ch : token) {
        if (std::iswalpha(ch)) {
            has_alpha = true;
            if (std::iswlower(ch)) {
                return false;
            }
        } else if (!(std::iswdigit(ch) || ch == L'_')) {
            return false;
        }
    }
    return has_alpha;
}

bool IsRelationKeywordToken(const std::wstring &token_upper)
{
    static const std::unordered_set<std::wstring> keywords = {
        L"IF", L"ELSE", L"ELSEIF", L"ELSE_IF", L"ENDIF",
        L"AND", L"OR", L"NOT", L"TRUE", L"FALSE",
        L"YES", L"NO"
    };
    return keywords.find(token_upper) != keywords.end();
}

std::wstring StripQuotedText(const std::wstring &line, bool &had_string_literal)
{
    had_string_literal = false;
    std::wstring out;
    out.reserve(line.size());
    bool in_quote = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const wchar_t ch = line[i];
        if (ch == L'"') {
            had_string_literal = true;
            in_quote = !in_quote;
            out.push_back(L' ');
            continue;
        }
        out.push_back(in_quote ? L' ' : ch);
    }
    return out;
}

std::vector<std::wstring> ExtractIdentifierTokens(const std::wstring &text)
{
    std::vector<std::wstring> tokens;
    size_t i = 0;
    while (i < text.size()) {
        const wchar_t ch = text[i];
        if (std::iswalpha(ch) || ch == L'_') {
            size_t j = i + 1;
            while (j < text.size()) {
                const wchar_t cj = text[j];
                if (!(std::iswalpha(cj) || std::iswdigit(cj) || cj == L'_')) {
                    break;
                }
                ++j;
            }
            tokens.push_back(text.substr(i, j - i));
            i = j;
            continue;
        }
        ++i;
    }
    return tokens;
}

bool PrecheckAddRelationsOnModel(ProMdl mdl,
                                 const std::vector<std::wstring> &append_lines,
                                 std::vector<RelationParamIssue> &issues)
{
    issues.clear();

    for (size_t i = 0; i < append_lines.size(); ++i) {
        const std::wstring &line = append_lines[i];
        const std::wstring trimmed = TrimWhitespace(line);
        const std::wstring upper_trimmed = UppercaseAscii(trimmed);
        if (trimmed.empty() ||
            IsRelationCommentLine(trimmed) ||
            IsRelationIfLine(upper_trimmed) ||
            IsRelationElseLine(upper_trimmed) ||
            IsRelationEndifLine(upper_trimmed)) {
            continue;
        }

        bool has_string_literal = false;
        const std::wstring no_strings = StripQuotedText(line, has_string_literal);
        const size_t eq = no_strings.find(L'=');
        if (eq == std::wstring::npos || eq == 0 || eq + 1 >= no_strings.size()) {
            continue;
        }

        const std::vector<std::wstring> lhs_tokens = ExtractIdentifierTokens(no_strings.substr(0, eq));
        if (lhs_tokens.empty()) {
            continue;
        }
        const std::wstring lhs_token = UppercaseAscii(lhs_tokens.back());
        if (!IsParamLikeIdentifier(lhs_token) || IsRelationKeywordToken(lhs_token)) {
            continue;
        }

        ProParamvalueType lhs_type = PRO_PARAM_NOT_SET;
        if (!GetParamType(mdl, lhs_token, lhs_type)) {
            RelationParamIssue issue;
            issue.kind = RelationParamIssueKind::MissingParameter;
            issue.token = lhs_token;
            issue.line_text = line;
            issue.line_index = static_cast<int>(i + 1);
            issues.push_back(issue);
        }

        ProParamvalueType expected_rhs_type = lhs_type;
        if (has_string_literal && expected_rhs_type == PRO_PARAM_NOT_SET) {
            expected_rhs_type = PRO_PARAM_STRING;
        }

        std::unordered_set<std::wstring> seen_rhs;
        const std::vector<std::wstring> rhs_tokens = ExtractIdentifierTokens(no_strings.substr(eq + 1));
        for (const std::wstring &rhs_raw : rhs_tokens) {
            const std::wstring rhs_token = UppercaseAscii(rhs_raw);
            if (!IsParamLikeIdentifier(rhs_token) ||
                IsRelationKeywordToken(rhs_token) ||
                rhs_token == lhs_token ||
                !seen_rhs.insert(rhs_token).second) {
                continue;
            }

            ProParamvalueType rhs_type = PRO_PARAM_NOT_SET;
            if (!GetParamType(mdl, rhs_token, rhs_type)) {
                RelationParamIssue issue;
                issue.kind = RelationParamIssueKind::MissingParameter;
                issue.token = rhs_token;
                issue.line_text = line;
                issue.line_index = static_cast<int>(i + 1);
                issue.expected_type = expected_rhs_type;
                issues.push_back(issue);
                continue;
            }

            if (expected_rhs_type != PRO_PARAM_NOT_SET &&
                rhs_type != PRO_PARAM_NOT_SET &&
                rhs_type != expected_rhs_type) {
                RelationParamIssue issue;
                issue.kind = RelationParamIssueKind::TypeMismatch;
                issue.token = rhs_token;
                issue.line_text = line;
                issue.line_index = static_cast<int>(i + 1);
                issue.expected_type = expected_rhs_type;
                issue.actual_type = rhs_type;
                issues.push_back(issue);
            }
        }

        if (has_string_literal &&
            lhs_type != PRO_PARAM_NOT_SET &&
            lhs_type != PRO_PARAM_STRING) {
            RelationParamIssue issue;
            issue.kind = RelationParamIssueKind::TypeMismatch;
            issue.token = lhs_token;
            issue.line_text = line;
            issue.line_index = static_cast<int>(i + 1);
            issue.expected_type = PRO_PARAM_STRING;
            issue.actual_type = lhs_type;
            issues.push_back(issue);
        }
    }

    return issues.empty();
}

void LogRelationParamIssues(const RelationLogSink &log_sink,
                            const RelationModelTagFormatter &format_model_tag,
                            ProMdl mdl,
                            const std::vector<RelationParamIssue> &issues)
{
    const std::string mdl_tag = ModelTag(mdl, format_model_tag);
    for (const RelationParamIssue &issue : issues) {
        LogLine(log_sink,
                "PRECHECK %s kind=%s line=%d token=%s expected=%s actual=%s text=%s",
                mdl_tag.c_str(),
                RelationParamIssueKindLabel(issue.kind),
                issue.line_index,
                autobbox::common::WToA(issue.token.c_str()).c_str(),
                autobbox::common::WToA(ParamTypeLabel(issue.expected_type)).c_str(),
                autobbox::common::WToA(ParamTypeLabel(issue.actual_type)).c_str(),
                autobbox::common::WToA(issue.line_text.c_str()).c_str());
    }
}

ProError FindMainRelsetVisitAction(ProRelset *relset, ProAppData app_data)
{
    if (relset == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    MainRelsetFindCtx *ctx = reinterpret_cast<MainRelsetFindCtx *>(app_data);
    ProModelitem owner = {};
    const ProError status = ProRelsetToModelitem(relset, &owner);
    if (status != PRO_TK_NO_ERROR) {
        return PRO_TK_NO_ERROR;
    }

    if ((owner.type == PRO_PART || owner.type == PRO_ASSEMBLY) &&
        owner.id != PRO_RELSET_POST_REGEN_ID) {
        ctx->relset = *relset;
        ctx->found = true;
        return PRO_TK_E_FOUND;
    }

    return PRO_TK_NO_ERROR;
}

bool FindMainRelset(ProMdl mdl, ProRelset &relset)
{
    std::memset(&relset, 0, sizeof(relset));
    if (!IsPartOrAsm(mdl)) {
        return false;
    }

    MainRelsetFindCtx ctx;
    ProSolidRelsetVisit(mdl, FindMainRelsetVisitAction, &ctx);
    if (ctx.found) {
        relset = ctx.relset;
        return true;
    }

    ProModelitem owner = ModelAsModelitem(mdl);
    return ProModelitemToRelset(&owner, &relset) == PRO_TK_NO_ERROR;
}

ProError GetOrCreateMainRelset(ProMdl mdl, bool create_if_missing, ProRelset &relset, bool &created)
{
    created = false;
    if (FindMainRelset(mdl, relset)) {
        return PRO_TK_NO_ERROR;
    }

    if (!create_if_missing) {
        return PRO_TK_E_NOT_FOUND;
    }

    ProModelitem owner = ModelAsModelitem(mdl);
    ProError status = ProRelsetCreate(&owner, &relset);
    if (status == PRO_TK_E_FOUND) {
        status = ProModelitemToRelset(&owner, &relset);
    } else if (status == PRO_TK_NO_ERROR) {
        created = true;
    }
    return status;
}

ProError GetRelsetLines(ProRelset &relset, std::vector<std::wstring> &lines)
{
    lines.clear();
    ProWstring *raw_lines = nullptr;
    ProError status = ProArrayAlloc(0, sizeof(ProWstring), 1, (ProArray *)&raw_lines);
    if (status != PRO_TK_NO_ERROR) {
        return status;
    }

    status = ProRelsetRelationsGet(&relset, &raw_lines);
    if (status != PRO_TK_NO_ERROR) {
        ProArrayFree((ProArray *)&raw_lines);
        return status;
    }

    int count = 0;
    status = ProArraySizeGet((ProArray)raw_lines, &count);
    if (status == PRO_TK_NO_ERROR && count > 0) {
        lines.reserve(count);
        for (int i = 0; i < count; ++i) {
            lines.emplace_back(raw_lines[i] == nullptr ? L"" : raw_lines[i]);
        }
    }

    ProArrayFree((ProArray *)&raw_lines);
    return status == PRO_TK_NO_ERROR ? PRO_TK_NO_ERROR : status;
}

ProError BuildRelsetLineArray(const std::vector<std::wstring> &lines, ProWstring **out_array)
{
    if (out_array == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }
    *out_array = nullptr;
    ProError status = ProArrayAlloc(0, sizeof(ProWstring), 1, (ProArray *)out_array);
    if (status != PRO_TK_NO_ERROR) {
        return status;
    }

    for (const std::wstring &line : lines) {
        ProWstring one = const_cast<wchar_t *>(line.c_str());
        status = ProArrayObjectAdd((ProArray *)out_array, PRO_VALUE_UNUSED, 1, &one);
        if (status != PRO_TK_NO_ERROR) {
            ProArrayFree((ProArray *)out_array);
            *out_array = nullptr;
            return status;
        }
    }

    return PRO_TK_NO_ERROR;
}

ProError SetRelsetLines(ProRelset &relset, const std::vector<std::wstring> &lines)
{
    ProWstring *raw_lines = nullptr;
    ProError status = BuildRelsetLineArray(lines, &raw_lines);
    if (status != PRO_TK_NO_ERROR) {
        return status;
    }

    status = ProRelsetRelationsSet(&relset, raw_lines, static_cast<int>(lines.size()));
    ProArrayFree((ProArray *)&raw_lines);
    return status;
}

ProError ApplyRelsetLinesWithRollback(ProRelset &relset,
                                      ProMdl mdl,
                                      const std::vector<std::wstring> &old_lines,
                                      const std::vector<std::wstring> &new_lines,
                                      bool created_relset,
                                      const RelationLogSink &log_sink,
                                      RelationApplyDiagnostics *diag = nullptr)
{
    if (diag != nullptr) {
        *diag = RelationApplyDiagnostics{};
    }

    ProError status = SetRelsetLines(relset, new_lines);
    if (diag != nullptr) {
        diag->set_status = status;
    }
    if (status != PRO_TK_NO_ERROR) {
        return status;
    }

    status = ProRelsetRegenerate(&relset);
    if (diag != nullptr) {
        diag->regen_status = status;
    }
    if (status == PRO_TK_NO_ERROR) {
        status = ProSolidRegenerate(reinterpret_cast<ProSolid>(mdl), PRO_REGEN_NO_FLAGS);
        if (diag != nullptr) {
            diag->solid_regen_status = status;
        }
        if (status == PRO_TK_NO_ERROR) {
            return status;
        }
    }

    if (created_relset && old_lines.empty()) {
        const ProError delete_status = ProRelsetDelete(&relset);
        if (diag != nullptr) {
            diag->delete_created_status = delete_status;
        }
    } else {
        const ProError restore_set = SetRelsetLines(relset, old_lines);
        const ProError restore_regen =
            (restore_set == PRO_TK_NO_ERROR) ? ProRelsetRegenerate(&relset) : restore_set;
        if (restore_regen == PRO_TK_NO_ERROR) {
            ProSolidRegenerate(reinterpret_cast<ProSolid>(mdl), PRO_REGEN_NO_FLAGS);
        }
        if (diag != nullptr) {
            diag->rollback_set_status = restore_set;
            diag->rollback_regen_status = restore_regen;
        }
        LogLine(log_sink,
                "WARN relset-rollback status=%d restore_set=%d restore_regen=%d",
                static_cast<int>(status),
                static_cast<int>(restore_set),
                static_cast<int>(restore_regen));
    }

    return status;
}

RelationCleanResult DeduplicateRelationLines(const std::vector<std::wstring> &input_lines)
{
    RelationCleanResult result;
    result.total_before = static_cast<int>(input_lines.size());
    result.lines.reserve(input_lines.size());

    std::unordered_set<std::wstring> seen_single_lines;
    int block_depth = 0;

    for (const std::wstring &line : input_lines) {
        const std::wstring trimmed = TrimWhitespace(line);
        const std::wstring upper_trimmed = UppercaseAscii(trimmed);

        const bool is_blank = trimmed.empty();
        const bool is_comment = IsRelationCommentLine(trimmed);
        const bool is_if = IsRelationIfLine(upper_trimmed);
        const bool is_else = IsRelationElseLine(upper_trimmed);
        const bool is_endif = IsRelationEndifLine(upper_trimmed);
        const bool is_control = is_if || is_else || is_endif;

        if (is_blank || is_comment || is_control || block_depth > 0) {
            result.lines.push_back(line);
            if (is_if) {
                ++block_depth;
            } else if (is_endif && block_depth > 0) {
                --block_depth;
            }
            continue;
        }

        ++result.single_before;
        if (seen_single_lines.insert(line).second) {
            result.lines.push_back(line);
            ++result.single_after;
        } else {
            ++result.removed_duplicates;
        }
    }

    result.total_after = static_cast<int>(result.lines.size());
    return result;
}

ProError CleanMainRelationsOnModel(ProMdl mdl,
                                   const RelationLogSink &log_sink,
                                   RelationCleanResult &result)
{
    result = RelationCleanResult{};
    ProRelset relset = {};
    if (!FindMainRelset(mdl, relset)) {
        return PRO_TK_E_NOT_FOUND;
    }

    std::vector<std::wstring> old_lines;
    ProError status = GetRelsetLines(relset, old_lines);
    if (status != PRO_TK_NO_ERROR) {
        return status;
    }

    result = DeduplicateRelationLines(old_lines);
    if (result.removed_duplicates <= 0) {
        return PRO_TK_NO_CHANGE;
    }

    return ApplyRelsetLinesWithRollback(relset, mdl, old_lines, result.lines, false, log_sink);
}

bool FindDuplicateRelationForAppend(const std::vector<std::wstring> &old_lines,
                                    const std::vector<std::wstring> &append_lines,
                                    RelationDuplicateInfo &duplicate)
{
    duplicate = RelationDuplicateInfo{};

    std::unordered_map<std::wstring, int> existing_lines;
    existing_lines.reserve(old_lines.size());
    for (size_t i = 0; i < old_lines.size(); ++i) {
        if (!IsRelationDuplicateCandidate(old_lines[i])) {
            continue;
        }
        const std::wstring key = NormalizeRelationLineForDuplicate(old_lines[i]);
        existing_lines.emplace(key, static_cast<int>(i + 1));
    }

    std::unordered_map<std::wstring, int> input_lines;
    input_lines.reserve(append_lines.size());
    for (size_t i = 0; i < append_lines.size(); ++i) {
        if (!IsRelationDuplicateCandidate(append_lines[i])) {
            continue;
        }
        const std::wstring key = NormalizeRelationLineForDuplicate(append_lines[i]);

        const auto input_it = input_lines.find(key);
        if (input_it != input_lines.end()) {
            duplicate.duplicated = true;
            duplicate.duplicated_in_input = true;
            duplicate.input_line_index = static_cast<int>(i + 1);
            duplicate.existing_line_index = input_it->second;
            duplicate.line_text = append_lines[i];
            return true;
        }
        input_lines.emplace(key, static_cast<int>(i + 1));

        const auto existing_it = existing_lines.find(key);
        if (existing_it != existing_lines.end()) {
            duplicate.duplicated = true;
            duplicate.duplicated_in_input = false;
            duplicate.input_line_index = static_cast<int>(i + 1);
            duplicate.existing_line_index = existing_it->second;
            duplicate.line_text = append_lines[i];
            return true;
        }
    }

    return false;
}

ProError AppendMainRelationsOnModel(ProMdl mdl,
                                    const std::vector<std::wstring> &append_lines,
                                    const RelationLogSink &log_sink,
                                    bool &created_relset,
                                    int &existing_line_count,
                                    int &final_line_count,
                                    bool &skipped_duplicate,
                                    RelationDuplicateInfo &duplicate_info,
                                    RelationApplyDiagnostics *diag = nullptr)
{
    created_relset = false;
    existing_line_count = 0;
    final_line_count = 0;
    skipped_duplicate = false;
    duplicate_info = RelationDuplicateInfo{};

    std::vector<std::wstring> old_lines;
    ProRelset relset = {};
    const bool has_existing_relset = FindMainRelset(mdl, relset);
    if (has_existing_relset) {
        ProError status = GetRelsetLines(relset, old_lines);
        if (status != PRO_TK_NO_ERROR) {
            return status;
        }
    }

    existing_line_count = static_cast<int>(old_lines.size());
    if (FindDuplicateRelationForAppend(old_lines, append_lines, duplicate_info)) {
        skipped_duplicate = true;
        final_line_count = existing_line_count;
        return PRO_TK_NO_CHANGE;
    }

    if (!has_existing_relset) {
        ProModelitem owner = ModelAsModelitem(mdl);
        ProError status = ProRelsetCreate(&owner, &relset);
        if (status == PRO_TK_E_FOUND) {
            status = ProModelitemToRelset(&owner, &relset);
            if (status == PRO_TK_NO_ERROR) {
                const ProError get_status = GetRelsetLines(relset, old_lines);
                if (get_status != PRO_TK_NO_ERROR) {
                    return get_status;
                }
                existing_line_count = static_cast<int>(old_lines.size());
                if (FindDuplicateRelationForAppend(old_lines, append_lines, duplicate_info)) {
                    skipped_duplicate = true;
                    final_line_count = existing_line_count;
                    return PRO_TK_NO_CHANGE;
                }
            }
        } else if (status == PRO_TK_NO_ERROR) {
            created_relset = true;
        }
        if (status != PRO_TK_NO_ERROR) {
            return status;
        }
    }

    std::vector<std::wstring> new_lines = old_lines;
    new_lines.insert(new_lines.end(), append_lines.begin(), append_lines.end());
    final_line_count = static_cast<int>(new_lines.size());
    return ApplyRelsetLinesWithRollback(relset, mdl, old_lines, new_lines, created_relset, log_sink, diag);
}

} // namespace

void ExecuteCleanRelationsTask(const std::vector<ProMdl> &models,
                               const RelationModelTagFormatter &format_model_tag,
                               const RelationLogSink &log_sink)
{
    int ok_count = 0;
    int fail_count = 0;
    int skip_no_relset = 0;
    int changed_count = 0;
    int unchanged_count = 0;

    for (ProMdl mdl : models) {
        RelationCleanResult clean_result;
        const ProError status = CleanMainRelationsOnModel(mdl, log_sink, clean_result);
        const std::string mdl_tag = ModelTag(mdl, format_model_tag);
        if (status == PRO_TK_E_NOT_FOUND) {
            ++skip_no_relset;
            LogLine(log_sink, "SKIP %s reason=no-main-relset", mdl_tag.c_str());
            continue;
        }
        if (status == PRO_TK_NO_CHANGE) {
            ++ok_count;
            ++unchanged_count;
            LogLine(log_sink,
                    "OK   %s clean-rel unchanged total_before=%d total_after=%d single_before=%d single_after=%d removed=%d",
                    mdl_tag.c_str(),
                    clean_result.total_before,
                    clean_result.total_after,
                    clean_result.single_before,
                    clean_result.single_after,
                    clean_result.removed_duplicates);
            continue;
        }
        if (status != PRO_TK_NO_ERROR) {
            ++fail_count;
            LogLine(log_sink,
                    "FAIL %s reason=clean-main-relset status=%d",
                    mdl_tag.c_str(),
                    static_cast<int>(status));
            continue;
        }

        ++ok_count;
        ++changed_count;
        LogLine(log_sink,
                "OK   %s clean-rel changed total_before=%d total_after=%d single_before=%d single_after=%d removed=%d",
                mdl_tag.c_str(),
                clean_result.total_before,
                clean_result.total_after,
                clean_result.single_before,
                clean_result.single_after,
                clean_result.removed_duplicates);
    }

    LogLine(log_sink,
            "Summary mode=clean-relations targets=%d ok=%d fail=%d changed=%d unchanged=%d skip_no_relset=%d",
            static_cast<int>(models.size()),
            ok_count,
            fail_count,
            changed_count,
            unchanged_count,
            skip_no_relset);
}

void ExecuteAddRelationsTask(const std::vector<ProMdl> &models,
                             const std::wstring &raw_text,
                             const RelationModelTagFormatter &format_model_tag,
                             const RelationLogSink &log_sink)
{
    const std::vector<std::wstring> append_lines = SplitMultilineText(raw_text);
    if (!HasNonWhitespaceLine(append_lines)) {
        LogLine(log_sink, "Add relations skipped: no input text");
        LogLine(log_sink,
                "Summary mode=add-relations targets=%d ok=%d fail=%d skip_duplicate=%d created_main_relset=%d input_lines=%d",
                static_cast<int>(models.size()),
                0,
                0,
                0,
                0,
                static_cast<int>(append_lines.size()));
        LogLine(log_sink, "Add relations skip_duplicate=%d", 0);
        return;
    }

    LogLine(log_sink, "Add relations input_lines=%d", static_cast<int>(append_lines.size()));

    int ok_count = 0;
    int fail_count = 0;
    int created_relset_count = 0;
    int skip_duplicate_count = 0;

    for (ProMdl mdl : models) {
        const std::string mdl_tag = ModelTag(mdl, format_model_tag);

        bool created_relset = false;
        int existing_line_count = 0;
        int final_line_count = 0;
        bool skipped_duplicate = false;
        RelationDuplicateInfo duplicate_info;
        RelationApplyDiagnostics diag;
        const ProError status = AppendMainRelationsOnModel(
            mdl,
            append_lines,
            log_sink,
            created_relset,
            existing_line_count,
            final_line_count,
            skipped_duplicate,
            duplicate_info,
            &diag);
        if (status == PRO_TK_NO_CHANGE && skipped_duplicate) {
            ++skip_duplicate_count;
            LogLine(log_sink,
                    "SKIP %s reason=duplicate-relation source=%s input_line=%d existing_line=%d existing_lines=%d text=%s",
                    mdl_tag.c_str(),
                    duplicate_info.duplicated_in_input ? "input" : "model",
                    duplicate_info.input_line_index,
                    duplicate_info.existing_line_index,
                    existing_line_count,
                    autobbox::common::WToA(duplicate_info.line_text.c_str()).c_str());
            continue;
        }
        if (status != PRO_TK_NO_ERROR) {
            ++fail_count;
            const char *stage = "unknown";
            int stage_status = static_cast<int>(status);
            if (diag.set_status != PRO_TK_NO_ERROR) {
                stage = "relations-set";
                stage_status = static_cast<int>(diag.set_status);
            } else if (diag.regen_status != PRO_TK_NO_ERROR) {
                stage = "regenerate";
                stage_status = static_cast<int>(diag.regen_status);
            } else if (diag.solid_regen_status != PRO_TK_NO_ERROR) {
                stage = "solid-regenerate";
                stage_status = static_cast<int>(diag.solid_regen_status);
            }
            LogLine(log_sink,
                    "FAIL %s reason=append-main-relset stage=%s status=%d set_status=%d regen_status=%d solid_regen_status=%d created_main=%d existing_lines=%d final_lines=%d rollback_set=%d rollback_regen=%d delete_created=%d raw=%s",
                    mdl_tag.c_str(),
                    stage,
                    stage_status,
                    static_cast<int>(diag.set_status),
                    static_cast<int>(diag.regen_status),
                    static_cast<int>(diag.solid_regen_status),
                    created_relset ? 1 : 0,
                    existing_line_count,
                    final_line_count,
                    static_cast<int>(diag.rollback_set_status),
                    static_cast<int>(diag.rollback_regen_status),
                    static_cast<int>(diag.delete_created_status),
                    autobbox::common::WToA(JoinRelationLinesForLog(append_lines).c_str()).c_str());
            LogRelationLinesBlock(log_sink, format_model_tag, "FAILREL", mdl, append_lines);
            continue;
        }

        ++ok_count;
        if (created_relset) {
            ++created_relset_count;
        }
        LogLine(log_sink,
                "OK   %s add-rel created_main=%d input_lines=%d existing_lines=%d final_lines=%d",
                mdl_tag.c_str(),
                created_relset ? 1 : 0,
                static_cast<int>(append_lines.size()),
                existing_line_count,
                final_line_count);
    }

    LogLine(log_sink,
            "Summary mode=add-relations targets=%d ok=%d fail=%d skip_duplicate=%d created_main_relset=%d input_lines=%d",
            static_cast<int>(models.size()),
            ok_count,
            fail_count,
            skip_duplicate_count,
            created_relset_count,
            static_cast<int>(append_lines.size()));
    LogLine(log_sink, "Add relations skip_duplicate=%d", skip_duplicate_count);
}

} // namespace autobbox::application
