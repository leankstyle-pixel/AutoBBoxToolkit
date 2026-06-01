#pragma once

#include <ProParamval.h>
#include <ProToolkit.h>

#include "autobbox/core/bom_types.h"

#include <functional>
#include <set>
#include <string>
#include <unordered_map>

namespace autobbox::ui {

struct BomToolDialogState {
    core::BomToolState *tool_state = nullptr;
    std::unordered_map<std::string, size_t> row_index_by_name;
    std::unordered_map<std::string, std::wstring> param_name_by_column;
    std::unordered_map<std::string, std::wstring> param_name_by_available_row;
    std::unordered_map<std::wstring, std::string> available_row_by_param;
    std::unordered_map<std::wstring, std::string> available_checkbox_by_param;
    std::unordered_map<std::wstring, std::string> update_checkbox_by_row_key;
    std::wstring selected_available_param_name;
    std::string focused_available_column_name;
    std::wstring active_edit_draft_key;
    std::string active_edit_row_name;
    std::string active_edit_column_name;
    std::string active_edit_component_name;
    bool active_edit_uses_optionmenu = false;
    std::string focused_bom_row_name;
    std::string focused_bom_column_name;
    std::unordered_map<std::string, int> available_column_width_by_name;
    std::unordered_map<std::string, int> bom_fixed_column_width_by_name;
    std::unordered_map<std::wstring, int> bom_param_column_width_by_name;
    int available_render_serial = 0;
};

enum class BomToolDialogAction {
    Close,
    UpdateModel,
    ExportCsv
};

struct BomDialogCallbacks {
    std::function<std::wstring(const core::BomToolState &state)> build_summary_text;
    std::function<bool(const std::wstring &label, ProParamvalueType &type_out)> param_type_from_menu_label;
    std::function<bool(const std::wstring &label, short &value_out)> bool_menu_value_to_short;
    std::function<const wchar_t *(ProParamvalueType type)> param_add_type_menu_label;
    std::function<const core::BomAvailableParam *(
        const core::BomToolState &state,
        const std::wstring &name)> find_available_param;
    std::function<core::BomCellView(
        const core::BomToolState &state,
        const core::BomRow &row,
        const core::BomAvailableParam &column)> build_cell_view;
    std::function<bool(core::BomToolState &state,
                       std::wstring &error_out,
                       std::wstring &warning_out)> handle_add_action;
    std::function<bool(core::BomToolState &state, std::wstring &error_out)> handle_move_left_action;
    std::function<bool(core::BomToolState &state, std::wstring &error_out)> handle_move_right_action;
    std::function<void(core::BomToolState &state)> handle_refresh_action;
    std::function<void(core::BomToolState &state)> handle_rebuild_action;
    std::function<bool(core::BomToolState &state,
                       const std::wstring &param_name,
                       std::wstring &error_out)> handle_delete_param_action;
    std::function<bool(core::BomToolState &state,
                       const std::wstring &param_name,
                       std::wstring &error_out)> handle_update_param_action;
};

bool PromptBomToolDialog(core::BomToolState &state,
                         BomToolDialogAction &action,
                         const BomDialogCallbacks &callbacks);

} // namespace autobbox::ui
