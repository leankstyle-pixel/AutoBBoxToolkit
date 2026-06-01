#pragma once

#include <ProMdl.h>
#include <ProParameter.h>
#include <ProParamDriver.h>
#include <ProParamval.h>

#include "autobbox/core/dwg3_types.h"
#include "autobbox/core/param_types.h"

#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace autobbox::core {

struct BomAvailableParam {
    std::wstring name;
    std::set<ProParamvalueType> types;
    int hit_count = 0;
    bool mixed_type = false;
    bool write_supported = false;
    ProParamvalueType write_type = PRO_PARAM_NOT_SET;
    std::wstring label;
    std::string item_name;
    std::string checkbox_component_name;
};

struct BomModelParamInfo {
    bool exists = false;
    bool writable = false;
    ProParamvalueType type = PRO_PARAM_NOT_SET;
    ProParameterDriver driver = PRO_PARAMDRIVER_NONE;
    ProLockstatus lock_status = PRO_PARAMLOCKSTATUS_UNLOCKED;
    std::wstring display_value;
    std::wstring readonly_reason;
};

struct BomModelSnapshot {
    ProMdl mdl = nullptr;
    std::unordered_map<std::wstring, BomModelParamInfo> params;
};

struct BomTarget {
    ProMdl mdl = nullptr;
    int level = 1;
};

struct BomRow {
    std::wstring key;
    std::wstring display_name;
    ProMdlType model_type = PRO_MDL_UNUSED;
    int level = 1;
    int quantity = 0;
    std::vector<ProMdl> models;
    std::string row_name;
};

struct BomCellView {
    std::wstring actual_value;
    std::wstring display_value;
    std::wstring helptext;
    std::wstring readonly_reason;
    bool editable = false;
    bool modified = false;
    bool value_mixed = false;
    int missing_targets = 0;
    int existing_targets = 0;
    int writable_targets = 0;
    int readonly_targets = 0;
};

struct BomRenderStats {
    int row_count = 0;
    int column_count = 0;
    int writable_cells = 0;
    int readonly_cells = 0;
};

struct BomUpdateSummary {
    int modified_cells = 0;
    int cell_success = 0;
    int cell_skip = 0;
    int cell_fail = 0;
    int write_success = 0;
    int write_skip = 0;
    int write_fail = 0;
    int parse_fail = 0;
};

struct BomToolState {
    ProBoolean top_level_only = PRO_B_FALSE;
    ProBoolean parts_option = PRO_B_TRUE;
    ProBoolean assemblies_option = PRO_B_TRUE;
    int max_bom_level = 3;
    std::vector<Dwg3SimprepOption> simprep_options;
    std::wstring active_simprep_label;
    int active_simprep_index = -1;
    std::vector<BomRow> rows;
    std::unordered_map<std::uintptr_t, BomModelSnapshot> snapshots_by_mdl;
    std::vector<BomAvailableParam> available_params;
    std::unordered_map<std::wstring, size_t> available_index_by_name;
    std::vector<std::wstring> visible_param_names;
    std::unordered_set<std::wstring> checked_available_names;
    std::unordered_set<std::wstring> checked_update_row_keys;
    std::unordered_set<std::wstring> selected_column_names;
    std::unordered_map<std::wstring, std::wstring> draft_values;
    std::unordered_map<std::wstring, ParamAddSpec> custom_param_specs;
    std::wstring filter_model_name;
    std::wstring filter_param_name;
    std::wstring filter_param_value;
    std::wstring pending_display_name;
    std::wstring pending_column_width = L"18";
    std::wstring pending_alignment = L"左对齐";
    std::wstring pending_field_type = L"模型参数";
    std::wstring pending_create_name;
    ProParamvalueType pending_create_type = PRO_PARAM_STRING;
    std::wstring pending_default_value;
    std::wstring pending_option_value;
    short pending_create_bool = 1;
    ProBoolean pending_update_to_model = PRO_B_FALSE;
    int last_added_columns = 0;
    int last_removed_columns = 0;
    bool update_row_selection_initialized = false;
};

} // namespace autobbox::core
