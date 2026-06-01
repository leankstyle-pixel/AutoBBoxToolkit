#include "autobbox/application/bom_update.h"

#include "autobbox/common/strings.h"
#include "autobbox/creo/parameter_api.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <unordered_map>
#include <vector>

namespace autobbox::application {

namespace {

void LogLine(const BomLogSink &log_sink, const char *fmt, ...)
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

bool ParseBomDraftValue(const core::BomAvailableParam &column,
                        const std::wstring &raw_value,
                        core::ParamAddSpec &spec_out,
                        std::wstring &error_out)
{
    error_out.clear();
    spec_out = {};
    spec_out.name = column.name;
    spec_out.type = column.write_type;
    spec_out.raw_value = raw_value;

    switch (column.write_type) {
    case PRO_PARAM_STRING:
        spec_out.string_value = raw_value;
        return true;
    case PRO_PARAM_INTEGER:
        if (ParseIntegerLiteral(raw_value, spec_out.int_value)) {
            return true;
        }
        error_out = L"请输入合法整数";
        return false;
    case PRO_PARAM_DOUBLE:
        if (ParseDoubleLiteral(raw_value, spec_out.double_value)) {
            return true;
        }
        error_out = L"请输入合法实数";
        return false;
    case PRO_PARAM_BOOLEAN:
        if (ParseBooleanLiteral(raw_value, spec_out.bool_value)) {
            return true;
        }
        error_out = L"布尔值仅支持 TRUE/FALSE/YES/NO";
        return false;
    default:
        error_out = L"当前列类型不支持回写";
        return false;
    }
}

bool SplitBomDraftKey(const std::wstring &key, std::wstring &row_key, std::wstring &param_name)
{
    const size_t sep = key.find(L'\x1f');
    if (sep == std::wstring::npos) {
        return false;
    }
    row_key = key.substr(0, sep);
    param_name = key.substr(sep + 1);
    return !row_key.empty() && !param_name.empty();
}

const core::BomAvailableParam *FindBomAvailableParam(const core::BomToolState &state, const std::wstring &name)
{
    const auto it = state.available_index_by_name.find(name);
    if (it == state.available_index_by_name.end()) {
        return nullptr;
    }
    return &state.available_params[it->second];
}

const core::BomModelSnapshot *FindBomSnapshot(const core::BomToolState &state, ProMdl mdl)
{
    const auto it = state.snapshots_by_mdl.find(reinterpret_cast<std::uintptr_t>(mdl));
    if (it == state.snapshots_by_mdl.end()) {
        return nullptr;
    }
    return &it->second;
}

} // namespace

core::BomUpdateSummary ApplyBomDraftsToModels(
    core::BomToolState &state,
    const BomModelTagFormatter &format_model_tag,
    const BomLogSink &log_sink)
{
    core::BomUpdateSummary summary;
    if (state.draft_values.empty() || !format_model_tag) {
        return summary;
    }

    std::unordered_map<std::wstring, size_t> row_index_by_key;
    for (size_t i = 0; i < state.rows.size(); ++i) {
        row_index_by_key[state.rows[i].key] = i;
    }

    std::vector<std::wstring> draft_keys;
    draft_keys.reserve(state.draft_values.size());
    for (const auto &entry : state.draft_values) {
        draft_keys.push_back(entry.first);
    }

    for (const std::wstring &draft_key : draft_keys) {
        const auto draft_it = state.draft_values.find(draft_key);
        if (draft_it == state.draft_values.end()) {
            continue;
        }

        std::wstring row_key;
        std::wstring param_name;
        if (!SplitBomDraftKey(draft_key, row_key, param_name)) {
            continue;
        }

        const auto row_found = row_index_by_key.find(row_key);
        const core::BomAvailableParam *column = FindBomAvailableParam(state, param_name);
        if (row_found == row_index_by_key.end() || column == nullptr) {
            continue;
        }
        if (state.checked_update_row_keys.find(row_key) == state.checked_update_row_keys.end()) {
            continue;
        }

        ++summary.modified_cells;
        const core::BomRow &row = state.rows[row_found->second];
        core::ParamAddSpec spec;
        std::wstring parse_error;
        if (!ParseBomDraftValue(*column, draft_it->second, spec, parse_error)) {
            ++summary.cell_fail;
            ++summary.parse_fail;
            LogLine(log_sink,
                    "FAIL bom-update row=%s param=%s reason=parse value=%s detail=%s",
                    autobbox::common::WToA(row.display_name.c_str()).c_str(),
                    autobbox::common::WToA(param_name.c_str()).c_str(),
                    autobbox::common::WToA(draft_it->second.c_str()).c_str(),
                    autobbox::common::WToA(parse_error.c_str()).c_str());
            continue;
        }

        bool any_success = false;
        bool any_skip = false;
        bool any_fail = false;
        for (ProMdl mdl : row.models) {
            const core::BomModelSnapshot *snapshot = FindBomSnapshot(state, mdl);
            bool writable = true;
            std::wstring reason;
            if (snapshot != nullptr) {
                const auto param_it = snapshot->params.find(param_name);
                if (param_it != snapshot->params.end() && param_it->second.exists) {
                    writable = param_it->second.writable;
                    reason = param_it->second.readonly_reason;
                }
            }

            if (!writable) {
                any_skip = true;
                ++summary.write_skip;
                LogLine(log_sink,
                        "SKIP bom-update model=%s row=%s param=%s reason=%s",
                        format_model_tag(mdl).c_str(),
                        autobbox::common::WToA(row.display_name.c_str()).c_str(),
                        autobbox::common::WToA(param_name.c_str()).c_str(),
                        autobbox::common::WToA(reason.c_str()).c_str());
                continue;
            }

            if (creo::SetModelParameterFromSpec(mdl, spec)) {
                any_success = true;
                ++summary.write_success;
                LogLine(log_sink,
                        "OK   bom-update model=%s row=%s param=%s value=%s",
                        format_model_tag(mdl).c_str(),
                        autobbox::common::WToA(row.display_name.c_str()).c_str(),
                        autobbox::common::WToA(param_name.c_str()).c_str(),
                        autobbox::common::WToA(draft_it->second.c_str()).c_str());
            } else {
                any_fail = true;
                ++summary.write_fail;
                LogLine(log_sink,
                        "FAIL bom-update model=%s row=%s param=%s value=%s",
                        format_model_tag(mdl).c_str(),
                        autobbox::common::WToA(row.display_name.c_str()).c_str(),
                        autobbox::common::WToA(param_name.c_str()).c_str(),
                        autobbox::common::WToA(draft_it->second.c_str()).c_str());
            }
        }

        if (!any_fail && !any_skip && any_success) {
            ++summary.cell_success;
            state.draft_values.erase(draft_it);
        } else if (any_fail) {
            ++summary.cell_fail;
        } else {
            ++summary.cell_skip;
        }
    }

    return summary;
}

} // namespace autobbox::application
