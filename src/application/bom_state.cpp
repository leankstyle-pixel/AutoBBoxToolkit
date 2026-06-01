#include "autobbox/application/bom_state.h"

#include "autobbox/application/target_collectors.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/creo/parameter_api.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

namespace autobbox::application {

namespace {

const wchar_t *MdlTypeShortLabel(ProMdlType type)
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

std::vector<core::BomRow> BuildBomRows(const std::vector<core::BomTarget> &targets)
{
    std::vector<core::BomRow> rows;
    std::unordered_map<std::wstring, size_t> index_by_name;

    for (const core::BomTarget &target : targets) {
        ProMdl mdl = target.mdl;
        if (mdl == nullptr) {
            continue;
        }
        const ProMdlType model_type = autobbox::creo::ModelType(mdl);
        const std::wstring model_name = autobbox::creo::ModelName(mdl);
        const int level = std::max(1, target.level);
        const std::wstring key = std::to_wstring(level) + L"|" + std::wstring(MdlTypeShortLabel(model_type)) + L"|" + model_name;
        std::wstring display_name = model_name;
        if (model_type == PRO_MDL_ASSEMBLY) {
            display_name += L" [ASM]";
        }

        auto found = index_by_name.find(key);
        if (found == index_by_name.end()) {
            core::BomRow row;
            row.key = key;
            row.display_name = display_name;
            row.model_type = model_type;
            row.level = level;
            row.quantity = 1;
            row.models.push_back(mdl);
            char row_name[32] = {0};
            std::snprintf(row_name, sizeof(row_name), "row_%zu", rows.size());
            row.row_name = row_name;
            index_by_name[key] = rows.size();
            rows.push_back(row);
        } else {
            core::BomRow &row = rows[found->second];
            ++row.quantity;
            row.models.push_back(mdl);
        }
    }

    return rows;
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

bool ContainsIgnoreCase(const std::wstring &text, const std::wstring &needle)
{
    const std::wstring trimmed_needle = TrimWhitespace(needle);
    if (trimmed_needle.empty()) {
        return true;
    }
    return UppercaseAscii(text).find(UppercaseAscii(trimmed_needle)) != std::wstring::npos;
}

bool EqualsIgnoreCase(const std::wstring &lhs, const std::wstring &rhs)
{
    return UppercaseAscii(TrimWhitespace(lhs)) == UppercaseAscii(TrimWhitespace(rhs));
}

const core::BomModelParamInfo *FindSnapshotParamIgnoreCase(const core::BomModelSnapshot &snapshot,
                                                          const std::wstring &param_name)
{
    if (param_name.empty()) {
        return nullptr;
    }

    const auto exact_it = snapshot.params.find(param_name);
    if (exact_it != snapshot.params.end()) {
        return &exact_it->second;
    }

    for (const auto &entry : snapshot.params) {
        if (EqualsIgnoreCase(entry.first, param_name)) {
            return &entry.second;
        }
    }
    return nullptr;
}

bool BomRowMatchesParamFilter(const core::BomRow &row,
                              const std::unordered_map<std::uintptr_t, core::BomModelSnapshot> &snapshots_by_mdl,
                              const std::wstring &param_name_filter,
                              const std::wstring &param_value_filter)
{
    const std::wstring param_name = TrimWhitespace(param_name_filter);
    const std::wstring param_value = TrimWhitespace(param_value_filter);
    if (param_name.empty() && param_value.empty()) {
        return true;
    }

    for (ProMdl mdl : row.models) {
        const auto snapshot_it = snapshots_by_mdl.find(reinterpret_cast<std::uintptr_t>(mdl));
        if (snapshot_it == snapshots_by_mdl.end()) {
            continue;
        }

        const core::BomModelSnapshot &snapshot = snapshot_it->second;
        if (!param_name.empty()) {
            const core::BomModelParamInfo *info = FindSnapshotParamIgnoreCase(snapshot, param_name);
            if (info == nullptr || !info->exists) {
                continue;
            }
            if (param_value.empty() || ContainsIgnoreCase(info->display_value, param_value)) {
                return true;
            }
            continue;
        }

        for (const auto &param_entry : snapshot.params) {
            const core::BomModelParamInfo &info = param_entry.second;
            if (info.exists && ContainsIgnoreCase(info.display_value, param_value)) {
                return true;
            }
        }
    }
    return false;
}

void ApplyBomRowFilters(core::BomToolState &state)
{
    const std::wstring model_filter = TrimWhitespace(state.filter_model_name);
    const std::wstring param_name_filter = TrimWhitespace(state.filter_param_name);
    const std::wstring param_value_filter = TrimWhitespace(state.filter_param_value);
    if (model_filter.empty() && param_name_filter.empty() && param_value_filter.empty()) {
        return;
    }

    std::vector<core::BomRow> filtered_rows;
    filtered_rows.reserve(state.rows.size());
    for (const core::BomRow &row : state.rows) {
        if (!ContainsIgnoreCase(row.display_name, model_filter)) {
            continue;
        }
        if (!BomRowMatchesParamFilter(row, state.snapshots_by_mdl, param_name_filter, param_value_filter)) {
            continue;
        }
        filtered_rows.push_back(row);
    }

    state.rows.swap(filtered_rows);
    for (size_t i = 0; i < state.rows.size(); ++i) {
        char row_name[32] = {0};
        std::snprintf(row_name, sizeof(row_name), "row_%zu", i);
        state.rows[i].row_name = row_name;
    }
}

std::wstring MakeBomDraftKey(const std::wstring &row_key, const std::wstring &param_name)
{
    std::wstring key(row_key);
    key.push_back(L'\x1f');
    key += param_name;
    return key;
}

void RemoveBomDraftsForParams(core::BomToolState &state, const std::unordered_set<std::wstring> &param_names)
{
    if (param_names.empty()) {
        return;
    }

    for (auto it = state.draft_values.begin(); it != state.draft_values.end();) {
        const size_t sep = it->first.find(L'\x1f');
        if (sep == std::wstring::npos) {
            ++it;
            continue;
        }
        const std::wstring param_name = it->first.substr(sep + 1);
        if (param_names.find(param_name) == param_names.end()) {
            ++it;
            continue;
        }
        it = state.draft_values.erase(it);
    }
}

const wchar_t *BoolMenuLabelText(short bool_value)
{
    return bool_value ? L"YES" : L"NO";
}

bool ParseBooleanLiteral(const std::wstring &text, short &value_out)
{
    const std::wstring upper = UppercaseAscii(TrimWhitespace(text));
    if (upper == L"TRUE" || upper == L"YES" || upper == L"1") {
        value_out = 1;
        return true;
    }
    if (upper == L"FALSE" || upper == L"NO" || upper == L"0") {
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

} // namespace

void RefreshBomState(core::BomToolState &state,
                     const BomTargetCollector &collect_targets,
                     const BomAvailableLabelBuilder &build_available_label)
{
    if (!collect_targets || !build_available_label) {
        return;
    }

    const std::vector<std::wstring> old_visible = state.visible_param_names;
    const std::unordered_set<std::wstring> old_checked = state.checked_available_names;
    const std::unordered_set<std::wstring> old_checked_rows = state.checked_update_row_keys;
    const bool had_row_selection = state.update_row_selection_initialized;
    const std::unordered_map<std::wstring, std::wstring> old_drafts = state.draft_values;

    state.simprep_options = CollectBomSimprepOptions();
    if (state.simprep_options.empty()) {
        state.active_simprep_label.clear();
        state.active_simprep_index = -1;
    } else {
        const auto active_it_by_label = std::find_if(
            state.simprep_options.begin(),
            state.simprep_options.end(),
            [&state](const core::Dwg3SimprepOption &option) {
                return !state.active_simprep_label.empty() && option.display_label == state.active_simprep_label;
            });
        const auto active_it = (active_it_by_label != state.simprep_options.end())
            ? active_it_by_label
            : std::find_if(
                  state.simprep_options.begin(),
                  state.simprep_options.end(),
                  [](const core::Dwg3SimprepOption &option) { return option.is_active; });
        const auto selected_it = (active_it != state.simprep_options.end()) ? active_it : state.simprep_options.begin();
        state.active_simprep_index = static_cast<int>(std::distance(state.simprep_options.begin(), selected_it));
        state.active_simprep_label = selected_it->display_label;
    }

    state.rows = BuildBomRows(collect_targets(state));
    state.update_row_selection_initialized = true;
    state.snapshots_by_mdl.clear();
    state.available_params.clear();
    state.available_index_by_name.clear();
    state.selected_column_names.clear();

    std::unordered_map<std::uintptr_t, int> mdl_occurrence_count;

    for (const core::BomRow &row : state.rows) {
        for (ProMdl mdl : row.models) {
            ++mdl_occurrence_count[reinterpret_cast<std::uintptr_t>(mdl)];
        }
    }

    for (const auto &occ : mdl_occurrence_count) {
        const std::uintptr_t key = occ.first;
        ProMdl mdl = reinterpret_cast<ProMdl>(key);
        core::BomModelSnapshot snapshot = creo::CollectBomModelSnapshot(mdl);
        state.snapshots_by_mdl[key] = std::move(snapshot);
    }

    ApplyBomRowFilters(state);

    std::unordered_map<std::wstring, core::BomAvailableParam> available_map;
    mdl_occurrence_count.clear();
    std::unordered_set<std::wstring> valid_row_keys;
    for (const core::BomRow &row : state.rows) {
        valid_row_keys.insert(row.key);
        for (ProMdl mdl : row.models) {
            ++mdl_occurrence_count[reinterpret_cast<std::uintptr_t>(mdl)];
        }
    }

    for (const auto &occ : mdl_occurrence_count) {
        const auto snapshot_it = state.snapshots_by_mdl.find(occ.first);
        if (snapshot_it == state.snapshots_by_mdl.end()) {
            continue;
        }
        for (const auto &param_entry : snapshot_it->second.params) {
            core::BomAvailableParam &available = available_map[param_entry.first];
            available.name = param_entry.first;
            available.types.insert(param_entry.second.type);
            available.hit_count += occ.second;
        }
    }

    for (const auto &entry : state.custom_param_specs) {
        core::BomAvailableParam &available = available_map[entry.first];
        available.name = entry.first;
        available.types.insert(entry.second.type);
    }

    for (auto &entry : available_map) {
        core::BomAvailableParam available = entry.second;
        available.mixed_type = (available.types.size() > 1);
        if (!available.mixed_type && !available.types.empty()) {
            available.write_type = *available.types.begin();
            available.write_supported = creo::IsWritableParameterType(available.write_type);
        }
        state.available_params.push_back(std::move(available));
    }

    std::sort(state.available_params.begin(), state.available_params.end(),
              [](const core::BomAvailableParam &lhs, const core::BomAvailableParam &rhs) {
                  return lhs.name < rhs.name;
              });

    for (size_t i = 0; i < state.available_params.size(); ++i) {
        char item_name[32] = {0};
        std::snprintf(item_name, sizeof(item_name), "bom_param_%zu", i);
        state.available_params[i].item_name = item_name;
        state.available_params[i].label = build_available_label(state.available_params[i]);
        state.available_index_by_name[state.available_params[i].name] = i;
    }

    state.visible_param_names.clear();
    std::unordered_set<std::wstring> seen_visible;
    for (const std::wstring &name : old_visible) {
        if (state.available_index_by_name.find(name) != state.available_index_by_name.end() &&
            seen_visible.insert(name).second) {
            state.visible_param_names.push_back(name);
        }
    }

    state.checked_available_names.clear();
    for (const std::wstring &name : old_checked) {
        if (state.available_index_by_name.find(name) != state.available_index_by_name.end()) {
            state.checked_available_names.insert(name);
        }
    }

    state.checked_update_row_keys.clear();
    for (const core::BomRow &row : state.rows) {
        if (!had_row_selection || old_checked_rows.find(row.key) != old_checked_rows.end()) {
            state.checked_update_row_keys.insert(row.key);
        }
    }

    state.draft_values.clear();
    for (const auto &draft_entry : old_drafts) {
        std::wstring row_key;
        std::wstring param_name;
        const size_t sep = draft_entry.first.find(L'\x1f');
        if (sep == std::wstring::npos) {
            continue;
        }
        row_key = draft_entry.first.substr(0, sep);
        param_name = draft_entry.first.substr(sep + 1);
        if (valid_row_keys.find(row_key) == valid_row_keys.end()) {
            continue;
        }
        if (std::find(state.visible_param_names.begin(), state.visible_param_names.end(), param_name) ==
            state.visible_param_names.end()) {
            continue;
        }
        state.draft_values[draft_entry.first] = draft_entry.second;
    }
}

const wchar_t *ParamAddTypeMenuLabel(ProParamvalueType type)
{
    switch (type) {
    case PRO_PARAM_STRING:
        return L"字符串";
    case PRO_PARAM_INTEGER:
        return L"整数";
    case PRO_PARAM_DOUBLE:
        return L"实数";
    case PRO_PARAM_BOOLEAN:
        return L"是/否";
    default:
        return L"字符串";
    }
}

bool ParamAddTypeFromMenuLabel(const std::wstring &label, ProParamvalueType &type_out)
{
    if (label == L"字符串") {
        type_out = PRO_PARAM_STRING;
        return true;
    }
    if (label == L"整数") {
        type_out = PRO_PARAM_INTEGER;
        return true;
    }
    if (label == L"实数") {
        type_out = PRO_PARAM_DOUBLE;
        return true;
    }
    if (label == L"是/否" || label == L"布尔") {
        type_out = PRO_PARAM_BOOLEAN;
        return true;
    }
    return false;
}

const wchar_t *BoolMenuLabel(short bool_value)
{
    return BoolMenuLabelText(bool_value);
}

bool BoolMenuValueToShort(const std::wstring &label, short &value_out)
{
    return ParseBooleanLiteral(label, value_out);
}

bool ParseParamAddDialogSpec(const std::wstring &name_text,
                             const std::wstring &type_label,
                             const std::wstring &value_text,
                             core::ParamAddSpec &spec_out,
                             std::wstring &error_out)
{
    error_out.clear();
    spec_out = {};
    spec_out.name = creo::NormalizeParameterName(name_text);
    if (!IsValidParamToolName(spec_out.name)) {
        error_out = L"参数名无效。请使用英文字母、数字或下划线，且以字母开头。";
        return false;
    }

    if (!ParamAddTypeFromMenuLabel(TrimWhitespace(type_label), spec_out.type)) {
        error_out = L"参数类型无效。";
        return false;
    }

    spec_out.raw_value = TrimWhitespace(value_text);
    switch (spec_out.type) {
    case PRO_PARAM_STRING:
        spec_out.string_value = spec_out.raw_value;
        return true;
    case PRO_PARAM_INTEGER:
        if (!ParseIntegerLiteral(spec_out.raw_value, spec_out.int_value)) {
            error_out = L"整数类型请输入有效整数。";
            return false;
        }
        return true;
    case PRO_PARAM_DOUBLE:
        if (!ParseDoubleLiteral(spec_out.raw_value, spec_out.double_value)) {
            error_out = L"实数类型请输入有效数字。";
            return false;
        }
        return true;
    case PRO_PARAM_BOOLEAN:
        if (!ParseBooleanLiteral(spec_out.raw_value, spec_out.bool_value)) {
            error_out = L"布尔类型请输入 true/false、yes/no 或 1/0。";
            return false;
        }
        return true;
    default:
        error_out = L"暂不支持该参数类型。";
        return false;
    }
}

bool BuildBomInlineCreateSpec(const core::BomToolState &state,
                              core::ParamAddSpec &spec_out,
                              std::wstring &error_out)
{
    error_out.clear();
    spec_out = {};
    spec_out.name = creo::NormalizeParameterName(state.pending_create_name);
    if (spec_out.name.empty()) {
        error_out = L"";
        return false;
    }
    if (!IsValidParamToolName(spec_out.name)) {
        error_out = L"参数名无效。请使用英文字母、数字或下划线，且以字母开头。";
        return false;
    }

    spec_out.type = state.pending_create_type;
    spec_out.raw_value = TrimWhitespace(state.pending_default_value);
    switch (spec_out.type) {
    case PRO_PARAM_STRING:
        spec_out.string_value = spec_out.raw_value;
        break;
    case PRO_PARAM_INTEGER:
        if (!spec_out.raw_value.empty() && !ParseIntegerLiteral(spec_out.raw_value, spec_out.int_value)) {
            error_out = L"默认值不是合法整数。";
            return false;
        }
        break;
    case PRO_PARAM_DOUBLE:
        if (!spec_out.raw_value.empty() && !ParseDoubleLiteral(spec_out.raw_value, spec_out.double_value)) {
            error_out = L"默认值不是合法实数。";
            return false;
        }
        break;
    case PRO_PARAM_BOOLEAN:
        if (!spec_out.raw_value.empty() && !ParseBooleanLiteral(spec_out.raw_value, spec_out.bool_value)) {
            error_out = L"默认值不是合法是/否值。";
            return false;
        }
        break;
    default:
        break;
    }
    return ValidateBomCustomParamSpec(state, spec_out, error_out);
}

bool ValidateBomCustomParamSpec(const core::BomToolState &state,
                                const core::ParamAddSpec &spec,
                                std::wstring &error_out)
{
    error_out.clear();
    const auto it = state.available_index_by_name.find(spec.name);
    if (it == state.available_index_by_name.end()) {
        return true;
    }

    const core::BomAvailableParam &existing = state.available_params[it->second];
    if (existing.mixed_type) {
        error_out = L"该参数在当前 BOM 中已是混合类型，不能按单一类型新建。";
        return false;
    }
    if (!existing.types.empty() && *existing.types.begin() != spec.type) {
        error_out = L"该参数已存在，且现有类型与所选类型不一致。";
        return false;
    }
    return true;
}

std::vector<std::wstring> AddCheckedBomColumns(core::BomToolState &state)
{
    std::vector<std::wstring> added;
    std::unordered_set<std::wstring> visible_set(state.visible_param_names.begin(), state.visible_param_names.end());
    for (const core::BomAvailableParam &param : state.available_params) {
        if (state.checked_available_names.find(param.name) == state.checked_available_names.end()) {
            continue;
        }
        if (!visible_set.insert(param.name).second) {
            continue;
        }
        state.visible_param_names.push_back(param.name);
        added.push_back(param.name);
    }
    state.last_added_columns = static_cast<int>(added.size());
    return added;
}

std::vector<std::wstring> SyncVisibleBomColumnsFromChecked(core::BomToolState &state)
{
    const std::vector<std::wstring> old_visible = state.visible_param_names;
    std::vector<std::wstring> next_visible;
    std::unordered_set<std::wstring> seen;

    for (const std::wstring &name : old_visible) {
        if (state.checked_available_names.find(name) == state.checked_available_names.end()) {
            continue;
        }
        if (state.available_index_by_name.find(name) == state.available_index_by_name.end()) {
            continue;
        }
        if (seen.insert(name).second) {
            next_visible.push_back(name);
        }
    }

    for (const core::BomAvailableParam &param : state.available_params) {
        if (state.checked_available_names.find(param.name) == state.checked_available_names.end()) {
            continue;
        }
        if (seen.insert(param.name).second) {
            next_visible.push_back(param.name);
        }
    }

    std::unordered_set<std::wstring> old_set(old_visible.begin(), old_visible.end());
    std::unordered_set<std::wstring> next_set(next_visible.begin(), next_visible.end());
    std::vector<std::wstring> removed;
    for (const std::wstring &name : old_visible) {
        if (next_set.find(name) == next_set.end()) {
            removed.push_back(name);
        }
    }

    int added_count = 0;
    for (const std::wstring &name : next_visible) {
        if (old_set.find(name) == old_set.end()) {
            ++added_count;
        }
    }

    if (!removed.empty()) {
        RemoveBomDraftsForParams(state, std::unordered_set<std::wstring>(removed.begin(), removed.end()));
    }

    state.visible_param_names.swap(next_visible);
    state.selected_column_names.clear();
    state.last_added_columns = added_count;
    state.last_removed_columns = static_cast<int>(removed.size());
    return removed;
}

std::vector<std::wstring> AddCustomBomAvailableParams(core::BomToolState &state,
                                                      const std::vector<core::ParamAddSpec> &specs)
{
    std::vector<std::wstring> added;
    for (const core::ParamAddSpec &spec : specs) {
        const bool is_new = state.custom_param_specs.find(spec.name) == state.custom_param_specs.end();
        state.custom_param_specs[spec.name] = spec;
        state.checked_available_names.insert(spec.name);
        if (is_new) {
            added.push_back(spec.name);
        }
        if (!spec.raw_value.empty()) {
            for (const core::BomRow &row : state.rows) {
                state.draft_values[MakeBomDraftKey(row.key, spec.name)] = spec.raw_value;
            }
        }
    }
    return added;
}

std::vector<std::wstring> AddCustomBomColumns(core::BomToolState &state,
                                              const std::vector<core::ParamAddSpec> &specs)
{
    std::vector<std::wstring> added = AddCustomBomAvailableParams(state, specs);
    if (specs.empty()) {
        state.last_added_columns = 0;
        return added;
    }

    std::unordered_set<std::wstring> visible_set(state.visible_param_names.begin(), state.visible_param_names.end());
    int visible_added = 0;
    for (const core::ParamAddSpec &spec : specs) {
        if (visible_set.insert(spec.name).second) {
            state.visible_param_names.push_back(spec.name);
            ++visible_added;
        }
    }

    state.last_added_columns = visible_added;
    return added;
}

std::vector<std::wstring> ClearCheckedBomAvailableParams(core::BomToolState &state)
{
    std::vector<std::wstring> cleared(
        state.checked_available_names.begin(),
        state.checked_available_names.end());
    state.checked_available_names.clear();
    return cleared;
}

bool RemoveCustomBomAvailableParam(core::BomToolState &state,
                                   const std::wstring &param_name,
                                   std::wstring &error_out)
{
    error_out.clear();
    if (param_name.empty()) {
        error_out = L"请先在参数列表中选中要删除的自定义参数。";
        return false;
    }

    const auto custom_it = state.custom_param_specs.find(param_name);
    if (custom_it == state.custom_param_specs.end()) {
        error_out = L"仅支持删除通过 + 新增的自定义参数。";
        return false;
    }

    state.custom_param_specs.erase(custom_it);
    state.checked_available_names.erase(param_name);
    state.selected_column_names.erase(param_name);

    bool removed_visible = false;
    std::vector<std::wstring> kept_visible;
    kept_visible.reserve(state.visible_param_names.size());
    for (const std::wstring &name : state.visible_param_names) {
        if (name == param_name) {
            removed_visible = true;
            continue;
        }
        kept_visible.push_back(name);
    }
    state.visible_param_names.swap(kept_visible);

    RemoveBomDraftsForParams(state, std::unordered_set<std::wstring>{ param_name });
    state.last_added_columns = 0;
    state.last_removed_columns = removed_visible ? 1 : 0;
    return true;
}

bool UpdateCustomBomAvailableParam(core::BomToolState &state,
                                   const std::wstring &old_param_name,
                                   const core::ParamAddSpec &spec,
                                   std::wstring &error_out)
{
    error_out.clear();
    if (old_param_name.empty()) {
        error_out = L"请先在参数列表中选中要更新的自定义参数。";
        return false;
    }

    const auto custom_it = state.custom_param_specs.find(old_param_name);
    if (custom_it == state.custom_param_specs.end()) {
        error_out = L"仅支持更新通过“添加”创建的自定义参数。";
        return false;
    }

    if (spec.name != old_param_name) {
        const auto existing_it = state.available_index_by_name.find(spec.name);
        if (existing_it != state.available_index_by_name.end()) {
            error_out = L"目标参数名已存在，不能重命名为该名称。";
            return false;
        }
    }

    state.custom_param_specs.erase(custom_it);
    state.custom_param_specs[spec.name] = spec;

    const bool was_checked = state.checked_available_names.erase(old_param_name) > 0;
    if (was_checked) {
        state.checked_available_names.insert(spec.name);
    }

    const bool was_selected = state.selected_column_names.erase(old_param_name) > 0;
    if (was_selected) {
        state.selected_column_names.insert(spec.name);
    }

    bool visible_changed = false;
    for (std::wstring &name : state.visible_param_names) {
        if (name == old_param_name) {
            name = spec.name;
            visible_changed = true;
        }
    }

    std::unordered_map<std::wstring, std::wstring> updated_drafts;
    for (const auto &entry : state.draft_values) {
        const size_t sep = entry.first.find(L'\x1f');
        if (sep == std::wstring::npos || entry.first.substr(sep + 1) != old_param_name) {
            updated_drafts[entry.first] = entry.second;
            continue;
        }
        std::wstring new_key = entry.first.substr(0, sep + 1);
        new_key += spec.name;
        updated_drafts[new_key] = entry.second;
    }
    state.draft_values.swap(updated_drafts);

    if (!spec.raw_value.empty()) {
        for (const core::BomRow &row : state.rows) {
            state.draft_values[MakeBomDraftKey(row.key, spec.name)] = spec.raw_value;
        }
    }

    state.last_added_columns = 0;
    state.last_removed_columns = 0;
    if (visible_changed) {
        state.selected_column_names.insert(spec.name);
    }
    return true;
}

bool MoveSelectedBomColumnsLeft(core::BomToolState &state, std::wstring &error_out)
{
    error_out.clear();
    if (state.selected_column_names.empty()) {
        error_out = L"请先在 BOM 表中选中要左移的参数列。";
        return false;
    }

    bool moved = false;
    for (size_t i = 1; i < state.visible_param_names.size(); ++i) {
        if (state.selected_column_names.find(state.visible_param_names[i]) == state.selected_column_names.end()) {
            continue;
        }
        if (state.selected_column_names.find(state.visible_param_names[i - 1]) != state.selected_column_names.end()) {
            continue;
        }
        std::swap(state.visible_param_names[i - 1], state.visible_param_names[i]);
        moved = true;
    }

    if (!moved) {
        error_out = L"选中的参数列已经在最左侧。";
        return false;
    }
    return true;
}

bool MoveSelectedBomColumnsRight(core::BomToolState &state, std::wstring &error_out)
{
    error_out.clear();
    if (state.selected_column_names.empty()) {
        error_out = L"请先在 BOM 表中选中要右移的参数列。";
        return false;
    }

    bool moved = false;
    if (state.visible_param_names.size() < 2) {
        error_out = L"当前没有可右移的参数列。";
        return false;
    }

    for (size_t i = state.visible_param_names.size() - 1; i > 0; --i) {
        const size_t left_index = i - 1;
        if (state.selected_column_names.find(state.visible_param_names[left_index]) == state.selected_column_names.end()) {
            continue;
        }
        if (state.selected_column_names.find(state.visible_param_names[i]) != state.selected_column_names.end()) {
            continue;
        }
        std::swap(state.visible_param_names[left_index], state.visible_param_names[i]);
        moved = true;
    }

    if (!moved) {
        error_out = L"选中的参数列已经在最右侧。";
        return false;
    }
    return true;
}

std::vector<std::wstring> RemoveSelectedBomColumns(core::BomToolState &state)
{
    std::vector<std::wstring> removed;
    if (state.selected_column_names.empty()) {
        state.last_removed_columns = 0;
        return removed;
    }

    std::vector<std::wstring> kept;
    for (const std::wstring &name : state.visible_param_names) {
        if (state.selected_column_names.find(name) != state.selected_column_names.end()) {
            removed.push_back(name);
        } else {
            kept.push_back(name);
        }
    }
    state.visible_param_names.swap(kept);

    std::unordered_set<std::wstring> removed_set(removed.begin(), removed.end());
    RemoveBomDraftsForParams(state, removed_set);
    state.last_removed_columns = static_cast<int>(removed.size());
    state.selected_column_names.clear();
    return removed;
}

} // namespace autobbox::application
