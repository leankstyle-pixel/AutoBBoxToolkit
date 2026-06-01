#include "autobbox/application/param_tool.h"

#include "autobbox/common/strings.h"
#include "autobbox/creo/parameter_api.h"

#include <ProModelitem.h>
#include <ProParameter.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <unordered_set>

namespace autobbox::application {

namespace {

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

bool IsValidParamToolName(const std::wstring &name)
{
    if (name.empty()) {
        return false;
    }

    const wchar_t first = name.front();
    if (!(std::iswalpha(first) || first == L'_' || first > 127)) {
        return false;
    }

    for (wchar_t ch : name) {
        if (std::iswspace(ch) || ch == L'=') {
            return false;
        }
        if (ch <= 127 && !(std::iswalnum(ch) || ch == L'_')) {
            return false;
        }
    }
    return true;
}

bool ParseBooleanLiteral(const std::wstring &text, short &value_out)
{
    const std::wstring upper = UppercaseAscii(TrimWhitespace(text));
    if (upper == L"TRUE" || upper == L"YES") {
        value_out = 1;
        return true;
    }
    if (upper == L"FALSE" || upper == L"NO") {
        value_out = 0;
        return true;
    }
    return false;
}

bool ParseIntegerLiteral(const std::wstring &text, int &value_out)
{
    const std::wstring trimmed = TrimWhitespace(text);
    if (trimmed.empty()) {
        return false;
    }
    wchar_t *end = nullptr;
    const long long value = std::wcstoll(trimmed.c_str(), &end, 10);
    if (end == nullptr || *end != L'\0') {
        return false;
    }
    if (value < static_cast<long long>(std::numeric_limits<int>::min()) ||
        value > static_cast<long long>(std::numeric_limits<int>::max())) {
        return false;
    }
    value_out = static_cast<int>(value);
    return true;
}

bool ParseDoubleLiteral(const std::wstring &text, double &value_out)
{
    const std::wstring trimmed = TrimWhitespace(text);
    if (trimmed.empty()) {
        return false;
    }
    wchar_t *end = nullptr;
    const double value = std::wcstod(trimmed.c_str(), &end);
    if (end == nullptr || *end != L'\0') {
        return false;
    }
    value_out = value;
    return true;
}

std::vector<std::wstring> SplitMultilineText(const std::wstring &text)
{
    std::vector<std::wstring> lines;
    std::wstring current;
    for (wchar_t ch : text) {
        if (ch == L'\r') {
            continue;
        }
        if (ch == L'\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    lines.push_back(current);
    return lines;
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

void LogLine(const ParamToolLogSink &log_sink, const char *fmt, ...)
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

ProError CreateModelParameterFromSpec(ProMdl mdl, const core::ParamAddSpec &spec)
{
    if (mdl == nullptr || spec.name.empty()) {
        return PRO_TK_BAD_INPUTS;
    }

    ProModelitem owner = {};
    ProMdlToModelitem(mdl, &owner);
    ProParameter param;
    ProName pname = {0};
    for (size_t i = 0; i + 1 < sizeof(pname) / sizeof(pname[0]) && i < spec.name.size(); ++i) {
        pname[i] = spec.name[i];
    }
    if (ProParameterInit(&owner, pname, &param) == PRO_TK_NO_ERROR) {
        return PRO_TK_E_FOUND;
    }

    switch (spec.type) {
    case PRO_PARAM_STRING:
        return creo::SetStringParamOnOwner(&owner, spec.name.c_str(), spec.string_value);
    case PRO_PARAM_DOUBLE:
        return creo::SetDoubleParamOnOwner(&owner, spec.name.c_str(), spec.double_value);
    case PRO_PARAM_INTEGER:
        return creo::SetIntegerParamOnOwner(&owner, spec.name.c_str(), spec.int_value);
    case PRO_PARAM_BOOLEAN:
        return creo::SetBooleanParamOnOwner(&owner, spec.name.c_str(), spec.bool_value);
    default:
        return PRO_TK_BAD_INPUTS;
    }
}

} // namespace

bool ParseParamToolInputSpecs(const std::wstring &raw_text,
                              std::vector<core::ParamAddSpec> &specs_out,
                              std::vector<std::wstring> &errors_out)
{
    specs_out.clear();
    errors_out.clear();
    const std::vector<std::wstring> lines = SplitMultilineText(raw_text);
    std::unordered_set<std::wstring> seen_names;

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::wstring line = TrimWhitespace(lines[i]);
        if (line.empty()) {
            continue;
        }

        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) {
            errors_out.push_back(L"第" + std::to_wstring(i + 1) + L"行缺少 '=' ：" + line);
            continue;
        }

        const std::wstring name = creo::NormalizeParameterName(line.substr(0, eq));
        if (!IsValidParamToolName(name)) {
            errors_out.push_back(L"第" + std::to_wstring(i + 1) + L"行参数名无效：" + line);
            continue;
        }
        if (!seen_names.insert(name).second) {
            errors_out.push_back(L"第" + std::to_wstring(i + 1) + L"行参数名重复：" + name);
            continue;
        }

        core::ParamAddSpec spec;
        spec.name = name;
        spec.raw_value = TrimWhitespace(line.substr(eq + 1));
        if (ParseBooleanLiteral(spec.raw_value, spec.bool_value)) {
            spec.type = PRO_PARAM_BOOLEAN;
        } else if (ParseIntegerLiteral(spec.raw_value, spec.int_value)) {
            spec.type = PRO_PARAM_INTEGER;
        } else if (spec.raw_value.find(L'.') != std::wstring::npos &&
                   ParseDoubleLiteral(spec.raw_value, spec.double_value)) {
            spec.type = PRO_PARAM_DOUBLE;
        } else {
            spec.type = PRO_PARAM_STRING;
            spec.string_value = spec.raw_value;
        }
        specs_out.push_back(spec);
    }

    return errors_out.empty();
}

core::ParamToolExecuteSummary ExecuteParamToolOperations(
    const std::vector<ProMdl> &models,
    const std::unordered_set<std::wstring> &delete_names,
    const std::vector<core::ParamAddSpec> &add_specs,
    const ParamToolModelTagFormatter &format_model_tag,
    const ParamToolLogSink &log_sink)
{
    core::ParamToolExecuteSummary summary;
    summary.delete_selected = static_cast<int>(delete_names.size());
    summary.add_input = static_cast<int>(add_specs.size());

    for (ProMdl mdl : models) {
        if (mdl == nullptr) {
            continue;
        }

        ProModelitem owner = {};
        ProMdlToModelitem(mdl, &owner);

        for (const std::wstring &name : delete_names) {
            ProParameter param;
            ProName pname = {0};
            for (size_t i = 0; i + 1 < sizeof(pname) / sizeof(pname[0]) && i < name.size(); ++i) {
                pname[i] = name[i];
            }

            const ProError init_st = ProParameterInit(&owner, pname, &param);
            if (init_st != PRO_TK_NO_ERROR) {
                ++summary.delete_skip_missing;
                LogLine(log_sink,
                        "SKIP %s delete-param name=%s reason=missing",
                        format_model_tag ? format_model_tag(mdl).c_str() : "",
                        autobbox::common::WToA(name.c_str()).c_str());
                continue;
            }

            const ProError del_st = ProParameterDelete(&param);
            if (del_st == PRO_TK_NO_ERROR) {
                ++summary.delete_ok;
                LogLine(log_sink,
                        "OK   %s delete-param name=%s",
                        format_model_tag ? format_model_tag(mdl).c_str() : "",
                        autobbox::common::WToA(name.c_str()).c_str());
            } else {
                ++summary.delete_fail;
                LogLine(log_sink,
                        "FAIL %s reason=delete-param name=%s status=%d",
                        format_model_tag ? format_model_tag(mdl).c_str() : "",
                        autobbox::common::WToA(name.c_str()).c_str(),
                        static_cast<int>(del_st));
            }
        }

        for (const core::ParamAddSpec &spec : add_specs) {
            const ProError st = CreateModelParameterFromSpec(mdl, spec);
            if (st == PRO_TK_NO_ERROR) {
                ++summary.add_created;
                LogLine(log_sink,
                        "OK   %s add-param name=%s type=%s value=%s",
                        format_model_tag ? format_model_tag(mdl).c_str() : "",
                        autobbox::common::WToA(spec.name.c_str()).c_str(),
                        autobbox::common::WToA(ParamTypeLabel(spec.type)).c_str(),
                        autobbox::common::WToA(spec.raw_value.c_str()).c_str());
            } else if (st == PRO_TK_E_FOUND) {
                ++summary.add_skip_existing;
                LogLine(log_sink,
                        "SKIP %s add-param name=%s reason=exists",
                        format_model_tag ? format_model_tag(mdl).c_str() : "",
                        autobbox::common::WToA(spec.name.c_str()).c_str());
            } else {
                ++summary.add_fail;
                LogLine(log_sink,
                        "FAIL %s reason=create-param name=%s status=%d",
                        format_model_tag ? format_model_tag(mdl).c_str() : "",
                        autobbox::common::WToA(spec.name.c_str()).c_str(),
                        static_cast<int>(st));
            }
        }
    }

    return summary;
}

} // namespace autobbox::application
