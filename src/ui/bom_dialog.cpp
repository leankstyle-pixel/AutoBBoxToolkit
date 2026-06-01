#include "autobbox/ui/bom_dialog.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/ui/message_dialog.h"

#include <ProToolkit.h>
#include <ProNotify.h>
#include <ProPopupmenu.h>
#include <ProUICheckbutton.h>
#include <ProUICmd.h>
#include <ProUIDialog.h>
#include <ProUIInputpanel.h>
#include <ProUILabel.h>
#include <ProUIOptionmenu.h>
#include <ProUIPushbutton.h>
#include <ProUITable.h>
#include <ProUtil.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <unordered_set>
#include <vector>

namespace autobbox::ui {

namespace {

struct BomDialogConfig {
    const char *dialog_inst_name = nullptr;
    const char *resource_base_name = nullptr;
    const char *add_comp = nullptr;
    const char *delete_comp = nullptr;
    const char *move_left_comp = nullptr;
    const char *move_right_comp = nullptr;
    const char *refresh_comp = nullptr;
    const char *update_comp = nullptr;
    const char *export_comp = nullptr;
    const char *cancel_comp = nullptr;
    const char *summary_comp = nullptr;
    const char *simprep_label_comp = nullptr;
    const char *simprep_menu_comp = nullptr;
    const char *select_all_rows_comp = nullptr;
    const char *max_level_menu_comp = nullptr;
    const char *assemblies_filter_comp = nullptr;
    const char *parts_filter_comp = nullptr;
    const char *model_filter_label_comp = nullptr;
    const char *model_filter_input_comp = nullptr;
    const char *param_filter_label_comp = nullptr;
    const char *param_filter_input_comp = nullptr;
    const char *value_filter_label_comp = nullptr;
    const char *value_filter_input_comp = nullptr;
    const char *available_label_comp = nullptr;
    const char *table_label_comp = nullptr;
    const char *display_name_comp = nullptr;
    const char *column_width_comp = nullptr;
    const char *align_menu_comp = nullptr;
    const char *field_type_menu_comp = nullptr;
    const char *param_list_comp = nullptr;
    const char *create_type_comp = nullptr;
    const char *bom_table_comp = nullptr;
    const char *create_name_comp = nullptr;
    const char *create_bool_label_comp = nullptr;
    const char *create_bool_comp = nullptr;
    const char *default_value_comp = nullptr;
    const char *optional_value_comp = nullptr;
    const char *update_to_model_check_comp = nullptr;
    const char *param_update_comp = nullptr;
    const char *base_avail_check_comp = nullptr;
    const char *base_bom_row_check_comp = nullptr;
    const char *cell_input_base_comp = nullptr;
    const char *cell_bool_base_comp = nullptr;
    int status_cancel = 0;
    int status_add = 0;
    int status_move_left = 0;
    int status_move_right = 0;
    int status_refresh = 0;
    int status_rebuild = 0;
    int status_update = 0;
    int status_export = 0;
    int status_delete_param = 0;
    int status_update_param = 0;
};

BomDialogConfig DefaultBomDialogConfig()
{
    BomDialogConfig config = {};
    config.dialog_inst_name = "autobbox_param_tool_inst";
    config.resource_base_name = "autobbox_param_tool";
    config.simprep_label_comp = "SimprepLabel";
    config.simprep_menu_comp = "SimprepMenu";
    config.select_all_rows_comp = "BomSelectAllCheck";
    config.max_level_menu_comp = "MaxLevelMenu";
    config.assemblies_filter_comp = "AssembliesFilterCheck";
    config.parts_filter_comp = "PartsFilterCheck";
    config.model_filter_label_comp = "BomModelFilterLabel";
    config.model_filter_input_comp = "BomModelFilterInput";
    config.param_filter_label_comp = "BomParamFilterLabel";
    config.param_filter_input_comp = "BomParamFilterInput";
    config.value_filter_label_comp = "BomValueFilterLabel";
    config.value_filter_input_comp = "BomValueFilterInput";
    config.add_comp = "ParamAddBtn";
    config.delete_comp = "ParamDeleteBtn";
    config.move_left_comp = "MoveLeftBtn";
    config.move_right_comp = "MoveRightBtn";
    config.refresh_comp = "RefreshBtn";
    config.update_comp = "UpdateBtn";
    config.export_comp = "ExportBtn";
    config.cancel_comp = "CancelBtn";
    config.summary_comp = "SummaryLabel";
    config.available_label_comp = "AvailableLabel";
    config.table_label_comp = "TableLabel";
    config.display_name_comp = "ParamDisplayNameInput";
    config.column_width_comp = "ParamColumnWidthInput";
    config.align_menu_comp = "ParamAlignMenu";
    config.field_type_menu_comp = "ParamFieldTypeMenu";
    config.param_list_comp = "ParamList";
    config.create_type_comp = "CreateTypeMenu";
    config.bom_table_comp = "BomTable";
    config.create_name_comp = "CreateNameInput";
    config.create_bool_label_comp = "CreateBoolValueLabel";
    config.create_bool_comp = "CreateBoolValueMenu";
    config.default_value_comp = "ParamDefaultValueInput";
    config.optional_value_comp = "ParamOptionValueMenu";
    config.update_to_model_check_comp = nullptr;
    config.param_update_comp = "ParamUpdateBtn";
    config.base_avail_check_comp = "BaseAvailCheck";
    config.base_bom_row_check_comp = "BaseBomRowCheck";
    config.cell_input_base_comp = "CellInputBase";
    config.cell_bool_base_comp = "CellBoolBase";
    config.status_cancel = 0;
    config.status_add = 1;
    config.status_move_left = 2;
    config.status_move_right = 3;
    config.status_refresh = 4;
    config.status_update = 5;
    config.status_export = 6;
    config.status_delete_param = 7;
    config.status_update_param = 8;
    config.status_rebuild = 9;
    return config;
}

constexpr const char *kParamListPopupMenuName = "autobbox_bom_param_popup";
constexpr const char *kParamListPopupDeleteCmd = "AutoBBox.BomParamListDelete";
constexpr const char *kParamListPopupDeleteBtn = "ABBomParamListDeleteBtn";

struct BomDialogRuntimeContext;

BomDialogRuntimeContext *g_active_bom_runtime = nullptr;
uiCmdCmdId g_param_list_popup_delete_cmd = nullptr;
bool g_popup_hooks_registered = false;

void RenderBomDialogContents(BomToolDialogState &dialog_state,
                             const struct BomDialogRenderConfig &config,
                             const struct BomDialogRenderCallbacks &callbacks);
void ClearBomActiveEditor(BomToolDialogState &dialog_state);
bool TryResolveSelectedAvailableParamName(BomToolDialogState &dialog_state,
                                          const char *dialog_inst_name,
                                          const char *param_list_comp,
                                          std::wstring &param_name_out);
void SaveSelectedAvailableParam(BomToolDialogState &dialog_state,
                                const char *dialog_inst_name,
                                const char *param_list_comp);
void RestoreSelectedAvailableParam(const BomToolDialogState &dialog_state,
                                   const char *dialog_inst_name,
                                   const char *param_list_comp);
void SaveAvailableFocusedCell(BomToolDialogState &dialog_state,
                              const char *dialog_inst_name,
                              const char *param_list_comp);
void RestoreAvailableFocusedCell(const BomToolDialogState &dialog_state,
                                 const char *dialog_inst_name,
                                 const char *param_list_comp);
void SaveBomFocusedCell(BomToolDialogState &dialog_state,
                        const char *dialog_inst_name,
                        const char *bom_table_comp);
void RestoreBomFocusedCell(const BomToolDialogState &dialog_state,
                           const char *dialog_inst_name,
                           const char *bom_table_comp);
void SaveAvailableColumnWidths(BomToolDialogState &dialog_state,
                               const char *dialog_inst_name,
                               const char *param_list_comp);
void SaveBomVisibleColumnOrder(BomToolDialogState &dialog_state, const char *dialog_inst_name, const char *bom_table_comp);
void SaveBomColumnWidths(BomToolDialogState &dialog_state,
                         const char *dialog_inst_name,
                         const char *bom_table_comp);
void CaptureBomRowUpdateSelection(BomToolDialogState &dialog_state,
                                  const char *dialog_inst_name);
void SetAllBomRowsChecked(BomToolDialogState &dialog_state,
                          const struct BomDialogInteractionConfig &config,
                          bool checked);
bool TryHandleBomRowSelectHeader(BomToolDialogState &dialog_state,
                                 const struct BomDialogInteractionConfig &config,
                                 const struct BomDialogInteractionCallbacks &callbacks);
void HarvestBomInlineDraftInputs(const BomToolDialogState &dialog_state,
                                 const struct BomDialogInteractionConfig &config,
                                 const struct BomDialogInteractionCallbacks &callbacks);
void HandleBomTableSelect(BomToolDialogState &dialog_state,
                          const struct BomDialogInteractionConfig &config,
                          const struct BomDialogInteractionCallbacks &callbacks);
void HandleParamListSelect(BomToolDialogState &dialog_state,
                           const char *dialog_inst_name,
                           const char *param_list_comp);
void ApplySelectedParamToForm(BomToolDialogState &dialog_state,
                              const struct BomDialogInteractionConfig &config,
                              const struct BomDialogInteractionCallbacks &callbacks);
void HandleBomCreateTypeChanged(BomToolDialogState &dialog_state,
                                const struct BomDialogInteractionConfig &config,
                                const struct BomDialogInteractionCallbacks &callbacks);
void CaptureBomDialogUiState(BomToolDialogState &dialog_state,
                             const BomDialogConfig &config,
                             const BomDialogInteractionConfig &interaction_config,
                             const BomDialogInteractionCallbacks &interaction_callbacks,
                             const BomDialogCallbacks &callbacks);
void EnsureParamListPopupSupport();
ProError OnPopupmenuCreatePost(const char *name);
int OnParamListPopupDelete(uiCmdCmdId, uiCmdValue *, void *);
uiCmdAccessState OnParamListPopupDeleteAccess(uiCmdAccessMode);

struct BomDialogRenderConfig {
    const char *dialog_inst_name = nullptr;
    const char *summary_comp = nullptr;
    const char *simprep_label_comp = nullptr;
    const char *simprep_menu_comp = nullptr;
    const char *select_all_rows_comp = nullptr;
    const char *max_level_menu_comp = nullptr;
    const char *assemblies_filter_comp = nullptr;
    const char *parts_filter_comp = nullptr;
    const char *model_filter_label_comp = nullptr;
    const char *model_filter_input_comp = nullptr;
    const char *param_filter_label_comp = nullptr;
    const char *param_filter_input_comp = nullptr;
    const char *value_filter_label_comp = nullptr;
    const char *value_filter_input_comp = nullptr;
    const char *available_label_comp = nullptr;
    const char *table_label_comp = nullptr;
    const char *display_name_comp = nullptr;
    const char *column_width_comp = nullptr;
    const char *align_menu_comp = nullptr;
    const char *field_type_menu_comp = nullptr;
    const char *param_list_comp = nullptr;
    const char *create_name_comp = nullptr;
    const char *create_type_comp = nullptr;
    const char *create_bool_label_comp = nullptr;
    const char *create_bool_comp = nullptr;
    const char *default_value_comp = nullptr;
    const char *optional_value_comp = nullptr;
    const char *update_to_model_check_comp = nullptr;
    const char *bom_table_comp = nullptr;
    const char *base_avail_check_comp = nullptr;
    const char *base_bom_row_check_comp = nullptr;
    const char *cell_bool_base_comp = nullptr;
};

struct BomDialogRenderCallbacks {
    std::function<std::wstring(const core::BomToolState &state)> build_summary_text;
    std::function<std::wstring(const std::set<ProParamvalueType> &types)> join_type_labels;
    std::function<const wchar_t *(ProParamvalueType type)> param_add_type_menu_label;
    std::function<void(const core::BomToolState &state)> refresh_create_value_controls;
    std::function<const core::BomAvailableParam *(
        const core::BomToolState &state,
        const std::wstring &name)> find_available_param;
    std::function<core::BomCellView(
        const core::BomToolState &state,
        const core::BomRow &row,
        const core::BomAvailableParam &column)> build_cell_view;
    std::function<void(
        const std::string &row_name,
        const std::string &column_name,
        const core::BomCellView &view)> apply_cell_visual_state;
};

struct BomDialogInteractionConfig {
    const char *dialog_inst_name = nullptr;
    int refresh_status = 0;
    int rebuild_status = 0;
    const char *param_list_comp = nullptr;
    const char *bom_table_comp = nullptr;
    const char *select_all_rows_comp = nullptr;
    const char *model_filter_input_comp = nullptr;
    const char *param_filter_input_comp = nullptr;
    const char *value_filter_input_comp = nullptr;
    const char *cell_input_base_comp = nullptr;
    const char *cell_bool_base_comp = nullptr;
    const char *create_name_comp = nullptr;
    const char *create_type_comp = nullptr;
    const char *default_value_comp = nullptr;
    const char *optional_value_comp = nullptr;
};

struct BomDialogInteractionCallbacks {
    std::function<const core::BomRow *(
        const core::BomToolState &state,
        const std::string &row_name)> find_row_by_name;
    std::function<const core::BomRow *(
        const core::BomToolState &state,
        const std::wstring &row_key)> find_row_by_key;
    std::function<const core::BomAvailableParam *(
        const core::BomToolState &state,
        const std::wstring &name)> find_available_param;
    std::function<core::BomCellView(
        const core::BomToolState &state,
        const core::BomRow &row,
        const core::BomAvailableParam &column)> build_cell_view;
    std::function<void(
        const std::string &row_name,
        const std::string &column_name,
        const core::BomCellView &view)> apply_cell_visual_state;
    std::function<std::wstring(
        const std::wstring &row_key,
        const std::wstring &param_name)> make_draft_key;
    std::function<bool(
        const std::wstring &key,
        std::wstring &row_key,
        std::wstring &param_name)> split_draft_key;
    std::function<std::wstring(
        const core::BomAvailableParam &column,
        const std::wstring &input_value)> normalize_inline_input;
    std::function<const wchar_t *(ProParamvalueType type)> param_add_type_menu_label;
    std::function<bool(const std::wstring &label, ProParamvalueType &type_out)> param_type_from_menu_label;
    std::function<void(const core::BomToolState &state)> refresh_create_value_controls;
};

struct BomDialogRuntimeContext {
    BomToolDialogState *dialog_state = nullptr;
    BomDialogInteractionConfig interaction_config = {};
    BomDialogInteractionCallbacks interaction_callbacks = {};
};

ProError TryCreateDialog(const BomDialogConfig &config, std::string &used_resource)
{
    used_resource.clear();
    if (config.dialog_inst_name == nullptr || config.resource_base_name == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    const std::string base_name = config.resource_base_name;
    const std::vector<std::string> rel_candidates = {
        base_name,
        std::string("text\\resource\\") + base_name,
        std::string("resource\\") + base_name,
        std::string("text\\usascii\\resource\\") + base_name,
        std::string("usascii\\resource\\") + base_name
    };

    ProError last = PRO_TK_GENERAL_ERROR;
    for (const std::string &res : rel_candidates) {
        last = ProUIDialogCreate(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(res.c_str()));
        if (last == PRO_TK_NO_ERROR) {
            used_resource = res;
            return last;
        }
    }

    ProPath wtext = {0};
    if (ProToolkitApplTextPathGet(wtext) == PRO_TK_NO_ERROR) {
        const std::string text_root = autobbox::common::WToA(wtext);
        if (!text_root.empty()) {
            const std::vector<std::string> abs_candidates = {
                text_root + "\\text\\resource\\" + base_name,
                text_root + "\\text\\resource\\" + base_name + ".res",
                text_root + "\\resource\\" + base_name,
                text_root + "\\resource\\" + base_name + ".res",
                text_root + "\\text\\usascii\\resource\\" + base_name,
                text_root + "\\text\\usascii\\resource\\" + base_name + ".res"
            };
            for (const std::string &path : abs_candidates) {
                if (!autobbox::common::FileExistsA(path)) {
                    continue;
                }
                last = ProUIDialogCreate(
                    const_cast<char *>(config.dialog_inst_name),
                    const_cast<char *>(path.c_str()));
                if (last == PRO_TK_NO_ERROR) {
                    used_resource = path;
                    return last;
                }
            }
        }
    }

    return last;
}

void OnDialogExit(char *dialog, char *, ProAppData app_data)
{
    if (dialog != nullptr) {
        ProUIDialogExit(dialog, static_cast<int>(reinterpret_cast<std::intptr_t>(app_data)));
    }
}

void OnCreateTypeChanged(char *, char *, ProAppData app_data)
{
    BomDialogRuntimeContext *runtime = reinterpret_cast<BomDialogRuntimeContext *>(app_data);
    if (runtime == nullptr || runtime->dialog_state == nullptr) {
        return;
    }
    HandleBomCreateTypeChanged(
        *runtime->dialog_state,
        runtime->interaction_config,
        runtime->interaction_callbacks);
}

void OnSimprepChanged(char *dialog, char *, ProAppData app_data)
{
    BomDialogRuntimeContext *runtime = reinterpret_cast<BomDialogRuntimeContext *>(app_data);
    if (runtime == nullptr || runtime->dialog_state == nullptr || dialog == nullptr) {
        return;
    }
    ProUIDialogExit(dialog, runtime->interaction_config.rebuild_status);
}

void OnBomFilterChanged(char *dialog, char *, ProAppData app_data)
{
    OnSimprepChanged(dialog, nullptr, app_data);
}

void OnBomSelectAllChanged(char *, char *, ProAppData app_data)
{
    BomDialogRuntimeContext *runtime = reinterpret_cast<BomDialogRuntimeContext *>(app_data);
    if (runtime == nullptr || runtime->dialog_state == nullptr) {
        return;
    }

    ProBoolean checked = PRO_B_FALSE;
    if (runtime->interaction_config.select_all_rows_comp != nullptr) {
        ProUICheckbuttonGetState(
            const_cast<char *>(runtime->interaction_config.dialog_inst_name),
            const_cast<char *>(runtime->interaction_config.select_all_rows_comp),
            &checked);
    }
    SetAllBomRowsChecked(
        *runtime->dialog_state,
        runtime->interaction_config,
        checked == PRO_B_TRUE);
}

void OnTableSelect(char *, char *, ProAppData app_data)
{
    BomDialogRuntimeContext *runtime = reinterpret_cast<BomDialogRuntimeContext *>(app_data);
    if (runtime == nullptr || runtime->dialog_state == nullptr) {
        return;
    }
    HandleBomTableSelect(
        *runtime->dialog_state,
        runtime->interaction_config,
        runtime->interaction_callbacks);
}

void OnParamListSelect(char *, char *, ProAppData app_data)
{
    BomDialogRuntimeContext *runtime = reinterpret_cast<BomDialogRuntimeContext *>(app_data);
    if (runtime == nullptr || runtime->dialog_state == nullptr) {
        return;
    }
    HandleParamListSelect(
        *runtime->dialog_state,
        runtime->interaction_config.dialog_inst_name,
        runtime->interaction_config.param_list_comp);
    ApplySelectedParamToForm(
        *runtime->dialog_state,
        runtime->interaction_config,
        runtime->interaction_callbacks);
}

void EnsureParamListPopupSupport()
{
    if (g_param_list_popup_delete_cmd == nullptr) {
        ProCmdActionAdd(
            const_cast<char *>(kParamListPopupDeleteCmd),
            OnParamListPopupDelete,
            uiProeAsynch,
            OnParamListPopupDeleteAccess,
            PRO_B_FALSE,
            PRO_B_FALSE,
            &g_param_list_popup_delete_cmd);
    }

    if (!g_popup_hooks_registered) {
        ProNotificationSet(
            PRO_POPUPMENU_CREATE_POST,
            reinterpret_cast<ProFunction>(OnPopupmenuCreatePost));
        g_popup_hooks_registered = true;
    }
}

ProError OnPopupmenuCreatePost(const char *name)
{
    if (name == nullptr || std::strcmp(name, kParamListPopupMenuName) != 0 || g_param_list_popup_delete_cmd == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    ProPopupMenuId menu_id = 0;
    if (ProPopupmenuIdGet(name, &menu_id) != PRO_TK_NO_ERROR) {
        return PRO_TK_NO_ERROR;
    }

    ProMenuName button_name = {0};
    ProLine button_label = {0};
    ProLine button_help = {0};
    std::snprintf(button_name, sizeof(button_name), "%s", kParamListPopupDeleteBtn);
    std::swprintf(button_label, sizeof(button_label) / sizeof(button_label[0]), L"%ls", L"删除参数");
    std::swprintf(button_help, sizeof(button_help) / sizeof(button_help[0]), L"%ls", L"删除选中的自定义参数");
    ProPopupmenuButtonAdd(
        menu_id,
        PRO_VALUE_UNUSED,
        button_name,
        button_label,
        button_help,
        g_param_list_popup_delete_cmd,
        nullptr,
        nullptr);
    return PRO_TK_NO_ERROR;
}

int OnParamListPopupDelete(uiCmdCmdId, uiCmdValue *, void *)
{
    if (g_active_bom_runtime == nullptr || g_active_bom_runtime->dialog_state == nullptr) {
        return 0;
    }

    HandleParamListSelect(
        *g_active_bom_runtime->dialog_state,
        g_active_bom_runtime->interaction_config.dialog_inst_name,
        g_active_bom_runtime->interaction_config.param_list_comp);
    ProUIDialogExit(
        const_cast<char *>(g_active_bom_runtime->interaction_config.dialog_inst_name),
        g_active_bom_runtime->dialog_state != nullptr ? DefaultBomDialogConfig().status_delete_param : 0);
    return 0;
}

uiCmdAccessState OnParamListPopupDeleteAccess(uiCmdAccessMode)
{
    if (g_active_bom_runtime == nullptr ||
        g_active_bom_runtime->dialog_state == nullptr ||
        g_active_bom_runtime->dialog_state->tool_state == nullptr) {
        return ACCESS_UNAVAILABLE;
    }
    return ACCESS_AVAILABLE;
}

bool IsBooleanOptionValue(const std::wstring &value)
{
    return value == L"YES" || value == L"NO";
}

std::wstring BomRowSelectHeaderLabel(const core::BomToolState &state)
{
    if (state.rows.empty()) {
        return L"☐";
    }

    int checked_rows = 0;
    for (const core::BomRow &row : state.rows) {
        if (state.checked_update_row_keys.find(row.key) != state.checked_update_row_keys.end()) {
            ++checked_rows;
        }
    }

    if (checked_rows == static_cast<int>(state.rows.size())) {
        return L"☑";
    }
    if (checked_rows == 0) {
        return L"☐";
    }
    return L"◩";
}

void RenderBomDialogContents(BomToolDialogState &dialog_state,
                             const BomDialogRenderConfig &config,
                             const BomDialogRenderCallbacks &callbacks)
{
    if (dialog_state.tool_state == nullptr) {
        return;
    }

    core::BomToolState &state = *dialog_state.tool_state;
    dialog_state.row_index_by_name.clear();
    dialog_state.param_name_by_column.clear();
    dialog_state.param_name_by_available_row.clear();
    dialog_state.available_row_by_param.clear();
    dialog_state.available_checkbox_by_param.clear();
    dialog_state.update_checkbox_by_row_key.clear();
    dialog_state.active_edit_draft_key.clear();
    dialog_state.active_edit_row_name.clear();
    dialog_state.active_edit_column_name.clear();
    dialog_state.active_edit_component_name.clear();
    ++dialog_state.available_render_serial;

    const std::wstring summary = callbacks.build_summary_text ? callbacks.build_summary_text(state) : L"";
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.summary_comp),
        const_cast<wchar_t *>(summary.c_str()));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.available_label_comp),
        const_cast<wchar_t *>(L"可用参数列表"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.table_label_comp),
        const_cast<wchar_t *>(L"BOM 表格"));

    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.simprep_label_comp),
        const_cast<wchar_t *>(L"\u7B80\u5316\u8868\u793A"));
    if (config.select_all_rows_comp != nullptr) {
        ProUICheckbuttonTextSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.select_all_rows_comp),
            const_cast<wchar_t *>(L"全选"));
        const bool all_rows_checked =
            !state.rows.empty() &&
            std::all_of(state.rows.begin(), state.rows.end(), [&state](const core::BomRow &row) {
                return state.checked_update_row_keys.find(row.key) != state.checked_update_row_keys.end();
            });
        if (all_rows_checked) {
            ProUICheckbuttonSet(
                const_cast<char *>(config.dialog_inst_name),
                const_cast<char *>(config.select_all_rows_comp));
        } else {
            ProUICheckbuttonUnset(
                const_cast<char *>(config.dialog_inst_name),
                const_cast<char *>(config.select_all_rows_comp));
        }
    }
    ProUICheckbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.assemblies_filter_comp),
        const_cast<wchar_t *>(L"组件"));
    ProUICheckbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.parts_filter_comp),
        const_cast<wchar_t *>(L"零件"));
    if (state.assemblies_option == PRO_B_TRUE) {
        ProUICheckbuttonSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.assemblies_filter_comp));
    } else {
        ProUICheckbuttonUnset(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.assemblies_filter_comp));
    }
    if (state.parts_option == PRO_B_TRUE) {
        ProUICheckbuttonSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.parts_filter_comp));
    } else {
        ProUICheckbuttonUnset(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.parts_filter_comp));
    }

    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.model_filter_label_comp),
        const_cast<wchar_t *>(L"模型"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_filter_label_comp),
        const_cast<wchar_t *>(L"参数"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.value_filter_label_comp),
        const_cast<wchar_t *>(L"值"));
    ProUIInputpanelColumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.model_filter_input_comp),
        18);
    ProUIInputpanelValueSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.model_filter_input_comp),
        const_cast<wchar_t *>(state.filter_model_name.c_str()));
    ProUIInputpanelColumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_filter_input_comp),
        14);
    ProUIInputpanelValueSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_filter_input_comp),
        const_cast<wchar_t *>(state.filter_param_name.c_str()));
    ProUIInputpanelColumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.value_filter_input_comp),
        18);
    ProUIInputpanelValueSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.value_filter_input_comp),
        const_cast<wchar_t *>(state.filter_param_value.c_str()));

    std::vector<std::string> simprep_names_storage;
    std::vector<std::wstring> simprep_labels_storage;
    std::vector<char *> simprep_names;
    std::vector<wchar_t *> simprep_labels;
    simprep_names_storage.reserve(state.simprep_options.size());
    simprep_labels_storage.reserve(state.simprep_options.size());
    simprep_names.reserve(state.simprep_options.size());
    simprep_labels.reserve(state.simprep_options.size());
    for (size_t i = 0; i < state.simprep_options.size(); ++i) {
        const core::Dwg3SimprepOption &option = state.simprep_options[i];
        char item_name[32] = {0};
        std::snprintf(item_name, sizeof(item_name), "simp_%zu", i);
        simprep_names_storage.push_back(item_name);
        simprep_labels_storage.push_back(option.display_label);
        simprep_names.push_back(const_cast<char *>(simprep_names_storage.back().c_str()));
        simprep_labels.push_back(const_cast<wchar_t *>(simprep_labels_storage.back().c_str()));
    }
    const int simprep_count = static_cast<int>(simprep_names.size());
    ProUIOptionmenuNamesSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.simprep_menu_comp),
        simprep_count,
        simprep_count > 0 ? simprep_names.data() : nullptr);
    ProUIOptionmenuLabelsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.simprep_menu_comp),
        simprep_count,
        simprep_count > 0 ? simprep_labels.data() : nullptr);
    ProUIOptionmenuColumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.simprep_menu_comp),
        24);
    ProUIOptionmenuVisiblerowsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.simprep_menu_comp),
        std::min(8, std::max(1, simprep_count)));
    if (simprep_count > 0 && !state.active_simprep_label.empty()) {
        ProUIOptionmenuValueSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.simprep_menu_comp),
            const_cast<wchar_t *>(state.active_simprep_label.c_str()));
    }

    std::vector<std::string> level_names_storage;
    std::vector<std::wstring> level_labels_storage;
    std::vector<char *> level_names;
    std::vector<wchar_t *> level_labels;
    for (int level = 1; level <= 20; ++level) {
        level_names_storage.push_back("level_" + std::to_string(level));
        level_labels_storage.push_back(std::to_wstring(level) + L"层");
    }
    for (std::string &name : level_names_storage) {
        level_names.push_back(const_cast<char *>(name.c_str()));
    }
    for (std::wstring &label : level_labels_storage) {
        level_labels.push_back(const_cast<wchar_t *>(label.c_str()));
    }
    ProUIOptionmenuNamesSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.max_level_menu_comp),
        static_cast<int>(level_names.size()),
        level_names.data());
    ProUIOptionmenuLabelsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.max_level_menu_comp),
        static_cast<int>(level_labels.size()),
        level_labels.data());
    ProUIOptionmenuColumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.max_level_menu_comp),
        8);
    ProUIOptionmenuVisiblerowsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.max_level_menu_comp),
        10);
    const int selected_level = std::min(20, std::max(1, state.max_bom_level));
    ProUIOptionmenuValueSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.max_level_menu_comp),
        const_cast<wchar_t *>(level_labels_storage[static_cast<size_t>(selected_level - 1)].c_str()));

    std::vector<std::string> avail_col_names_storage = { "USE", "NAME", "TYPE", "HIT" };
    std::vector<std::wstring> avail_col_labels_storage = { L"选", L"参数名", L"类型", L"命中" };
    std::vector<int> avail_col_widths = { 4, 18, 12, 6 };
    std::vector<int> avail_col_resizings = { 0, 3, 1, 0 };
    for (size_t i = 0; i < avail_col_names_storage.size() && i < avail_col_widths.size(); ++i) {
        const auto width_it = dialog_state.available_column_width_by_name.find(avail_col_names_storage[i]);
        if (width_it != dialog_state.available_column_width_by_name.end() && width_it->second > 0) {
            avail_col_widths[i] = width_it->second;
        }
    }
    std::vector<char *> avail_col_names;
    std::vector<wchar_t *> avail_col_labels;
    for (std::string &name : avail_col_names_storage) {
        avail_col_names.push_back(const_cast<char *>(name.c_str()));
    }
    for (std::wstring &label : avail_col_labels_storage) {
        avail_col_labels.push_back(const_cast<wchar_t *>(label.c_str()));
    }

    std::vector<std::string> avail_row_names_storage;
    std::vector<char *> avail_row_names;
    std::vector<wchar_t *> avail_row_labels;
    avail_row_names_storage.reserve(state.available_params.size());
    avail_row_names.reserve(state.available_params.size());
    avail_row_labels.reserve(state.available_params.size());
    for (size_t i = 0; i < state.available_params.size(); ++i) {
        core::BomAvailableParam &param = state.available_params[i];
        char row_name[32] = {0};
        std::snprintf(row_name, sizeof(row_name), "avail_%zu", i);
        avail_row_names_storage.push_back(row_name);
        avail_row_names.push_back(const_cast<char *>(avail_row_names_storage.back().c_str()));
        avail_row_labels.push_back(const_cast<wchar_t *>(L""));
        dialog_state.param_name_by_available_row[avail_row_names_storage.back()] = param.name;
        dialog_state.available_row_by_param[param.name] = avail_row_names_storage.back();
    }

    ProUITableColumnnamesSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_list_comp),
        static_cast<int>(avail_col_names.size()),
        avail_col_names.data());
    ProUITableColumnlabelsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_list_comp),
        static_cast<int>(avail_col_labels.size()),
        avail_col_labels.data());
    ProUITableColumnwidthsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_list_comp),
        static_cast<int>(avail_col_widths.size()),
        avail_col_widths.data());
    ProUITableColumnresizingsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_list_comp),
        static_cast<int>(avail_col_resizings.size()),
        avail_col_resizings.data());
    ProUITableRownamesSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_list_comp),
        static_cast<int>(avail_row_names.size()),
        avail_row_names.empty() ? nullptr : avail_row_names.data());
    ProUITableRowlabelsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_list_comp),
        static_cast<int>(avail_row_labels.size()),
        avail_row_labels.empty() ? nullptr : avail_row_labels.data());
    ProUITableSelectionpolicySet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_list_comp),
        PROUISELPOLICY_SINGLE);
    ProUITableColumnselectionpolicySet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_list_comp),
        PROUISELPOLICY_NONE);
    ProUITableVisiblerowsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_list_comp),
        std::min(16, std::max(6, static_cast<int>(state.available_params.size()) + 1)));
    ProUITableMinrowsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_list_comp),
        std::min(16, std::max(6, static_cast<int>(state.available_params.size()))));
    ProUITableShowgridSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_list_comp),
        PRO_B_TRUE);
    ProUITableAutohighlightEnable(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_list_comp));
    ProUITablePopupmenuSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_list_comp),
        const_cast<char *>(kParamListPopupMenuName));
    RestoreSelectedAvailableParam(dialog_state, config.dialog_inst_name, config.param_list_comp);
    RestoreAvailableFocusedCell(dialog_state, config.dialog_inst_name, config.param_list_comp);
    ProUIInputpanelColumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.create_name_comp),
        18);
    ProUIInputpanelValueSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.create_name_comp),
        const_cast<wchar_t *>(state.pending_create_name.c_str()));
    ProUIInputpanelColumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.default_value_comp),
        18);
    ProUIInputpanelValueSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.default_value_comp),
        const_cast<wchar_t *>(state.pending_default_value.c_str()));

    std::vector<std::string> type_names_storage = { "STRING", "INTEGER", "DOUBLE", "BOOLEAN" };
    std::vector<std::wstring> type_labels_storage = { L"字符串", L"整数", L"实数", L"是/否" };
    std::vector<char *> type_names;
    std::vector<wchar_t *> type_labels;
    for (std::string &name : type_names_storage) {
        type_names.push_back(const_cast<char *>(name.c_str()));
    }
    for (std::wstring &label : type_labels_storage) {
        type_labels.push_back(const_cast<wchar_t *>(label.c_str()));
    }
    ProUIOptionmenuNamesSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.create_type_comp),
        static_cast<int>(type_names.size()),
        type_names.data());
    ProUIOptionmenuLabelsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.create_type_comp),
        static_cast<int>(type_labels.size()),
        type_labels.data());
    ProUIOptionmenuColumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.create_type_comp),
        12);
    ProUIOptionmenuVisiblerowsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.create_type_comp),
        4);
    if (callbacks.param_add_type_menu_label) {
        ProUIOptionmenuValueSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.create_type_comp),
            const_cast<wchar_t *>(callbacks.param_add_type_menu_label(state.pending_create_type)));
    }

    std::vector<std::string> option_names_storage = { "BLANK" };
    std::vector<std::wstring> option_labels_storage = { state.pending_option_value.empty() ? L"" : state.pending_option_value };
    std::vector<char *> option_names;
    std::vector<wchar_t *> option_labels;
    for (std::string &name : option_names_storage) {
        option_names.push_back(const_cast<char *>(name.c_str()));
    }
    for (std::wstring &label : option_labels_storage) {
        option_labels.push_back(const_cast<wchar_t *>(label.c_str()));
    }
    ProUIOptionmenuNamesSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.optional_value_comp),
        static_cast<int>(option_names.size()),
        option_names.data());
    ProUIOptionmenuLabelsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.optional_value_comp),
        static_cast<int>(option_labels.size()),
        option_labels.data());
    ProUIOptionmenuColumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.optional_value_comp),
        18);
    ProUIOptionmenuValueSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.optional_value_comp),
        const_cast<wchar_t *>(option_labels_storage.front().c_str()));
    std::vector<std::string> bool_names_storage = { "YES", "NO" };
    std::vector<std::wstring> bool_labels_storage = { L"YES", L"NO" };
    std::vector<char *> bool_names;
    std::vector<wchar_t *> bool_labels;
    for (std::string &name : bool_names_storage) {
        bool_names.push_back(const_cast<char *>(name.c_str()));
    }
    for (std::wstring &label : bool_labels_storage) {
        bool_labels.push_back(const_cast<wchar_t *>(label.c_str()));
    }
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.create_bool_label_comp),
        const_cast<wchar_t *>(L"是/否值"));
    ProUIOptionmenuNamesSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.create_bool_comp),
        static_cast<int>(bool_names.size()),
        bool_names.data());
    ProUIOptionmenuLabelsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.create_bool_comp),
        static_cast<int>(bool_labels.size()),
        bool_labels.data());
    ProUIOptionmenuColumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.create_bool_comp),
        8);
    ProUIOptionmenuVisiblerowsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.create_bool_comp),
        2);
    ProUIOptionmenuNamesSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.cell_bool_base_comp),
        static_cast<int>(bool_names.size()),
        bool_names.data());
    ProUIOptionmenuLabelsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.cell_bool_base_comp),
        static_cast<int>(bool_labels.size()),
        bool_labels.data());
    ProUIOptionmenuColumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.cell_bool_base_comp),
        8);
    ProUIOptionmenuVisiblerowsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.cell_bool_base_comp),
        2);
    if (callbacks.refresh_create_value_controls) {
        callbacks.refresh_create_value_controls(state);
    }

    for (size_t i = 0; i < state.available_params.size(); ++i) {
        core::BomAvailableParam &param = state.available_params[i];
        const char *row_name = avail_row_names_storage[i].c_str();
        const std::wstring type_text = callbacks.join_type_labels ? callbacks.join_type_labels(param.types) : L"";
        const std::wstring hit_text = std::to_wstring(param.hit_count);
        char check_name[48] = {0};
        std::snprintf(check_name, sizeof(check_name), "availchk_%d_%zu", dialog_state.available_render_serial, i);
        param.checkbox_component_name = check_name;
        dialog_state.available_checkbox_by_param[param.name] = param.checkbox_component_name;
        ProUITableCellComponentCopy(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.param_list_comp),
            const_cast<char *>(row_name),
            const_cast<char *>("USE"),
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.base_avail_check_comp),
            const_cast<char *>(param.checkbox_component_name.c_str()));
        ProUICheckbuttonTextSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(param.checkbox_component_name.c_str()),
            const_cast<wchar_t *>(L""));
        if (state.checked_available_names.find(param.name) != state.checked_available_names.end()) {
            ProUICheckbuttonSet(
                const_cast<char *>(config.dialog_inst_name),
                const_cast<char *>(param.checkbox_component_name.c_str()));
        } else {
            ProUICheckbuttonUnset(
                const_cast<char *>(config.dialog_inst_name),
                const_cast<char *>(param.checkbox_component_name.c_str()));
        }
        ProUITableCellLabelSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.param_list_comp),
            const_cast<char *>(row_name),
            const_cast<char *>("NAME"),
            const_cast<wchar_t *>(param.name.c_str()));
        ProUITableCellLabelSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.param_list_comp),
            const_cast<char *>(row_name),
            const_cast<char *>("TYPE"),
            const_cast<wchar_t *>(type_text.c_str()));
        ProUITableCellLabelSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.param_list_comp),
            const_cast<char *>(row_name),
            const_cast<char *>("HIT"),
            const_cast<wchar_t *>(hit_text.c_str()));
        if (param.mixed_type) {
            for (const char *col_name : { "USE", "NAME", "TYPE", "HIT" }) {
                ProUITableCellBackgroundColorSet(
                    const_cast<char *>(config.dialog_inst_name),
                    const_cast<char *>(config.param_list_comp),
                    const_cast<char *>(row_name),
                    const_cast<char *>(col_name),
                    PRO_UI_COLOR_3D_LIGHT_SHADOW);
            }
        }
    }

    std::vector<std::string> column_names_storage;
    std::vector<std::wstring> column_labels_storage;
    std::vector<int> column_widths;
    std::vector<int> column_resizings;
    column_names_storage.push_back("EDIT");
    column_names_storage.push_back("SEQ");
    column_names_storage.push_back("LEVEL");
    column_names_storage.push_back("MODEL");
    column_names_storage.push_back("QTY");
    column_labels_storage.push_back(BomRowSelectHeaderLabel(state));
    column_labels_storage.push_back(L"序号");
    column_labels_storage.push_back(L"层级");
    column_labels_storage.push_back(L"模型名称");
    column_labels_storage.push_back(L"数量");
    column_widths.push_back(4);
    column_widths.push_back(6);
    column_widths.push_back(6);
    column_widths.push_back(28);
    column_widths.push_back(8);
    column_resizings.push_back(0);
    column_resizings.push_back(0);
    column_resizings.push_back(0);
    column_resizings.push_back(0);
    column_resizings.push_back(0);

    for (size_t i = 0; i < state.visible_param_names.size(); ++i) {
        char name[32] = {0};
        std::snprintf(name, sizeof(name), "col_%zu", i);
        column_names_storage.push_back(name);
        column_labels_storage.push_back(state.visible_param_names[i]);
        column_widths.push_back(18);
        column_resizings.push_back(1);
        dialog_state.param_name_by_column[column_names_storage.back()] = state.visible_param_names[i];
    }

    for (size_t i = 0; i < column_names_storage.size() && i < column_widths.size(); ++i) {
        const auto param_it = dialog_state.param_name_by_column.find(column_names_storage[i]);
        if (param_it != dialog_state.param_name_by_column.end()) {
            const auto width_it = dialog_state.bom_param_column_width_by_name.find(param_it->second);
            if (width_it != dialog_state.bom_param_column_width_by_name.end() && width_it->second > 0) {
                column_widths[i] = width_it->second;
            }
            continue;
        }

        const auto fixed_width_it = dialog_state.bom_fixed_column_width_by_name.find(column_names_storage[i]);
        if (fixed_width_it != dialog_state.bom_fixed_column_width_by_name.end() && fixed_width_it->second > 0) {
            column_widths[i] = fixed_width_it->second;
        }
    }

    std::vector<char *> column_names;
    std::vector<wchar_t *> column_labels;
    for (std::string &name : column_names_storage) {
        column_names.push_back(const_cast<char *>(name.c_str()));
    }
    for (std::wstring &label : column_labels_storage) {
        column_labels.push_back(const_cast<wchar_t *>(label.c_str()));
    }

    std::vector<char *> row_names;
    std::vector<wchar_t *> row_labels;
    row_names.reserve(state.rows.size());
    row_labels.reserve(state.rows.size());
    for (size_t i = 0; i < state.rows.size(); ++i) {
        core::BomRow &row = state.rows[i];
        dialog_state.row_index_by_name[row.row_name] = i;
        row_names.push_back(const_cast<char *>(row.row_name.c_str()));
        row_labels.push_back(const_cast<wchar_t *>(L""));
    }

    ProUITableColumnnamesSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        static_cast<int>(column_names.size()),
        column_names.data());
    ProUITableColumnlabelsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        static_cast<int>(column_labels.size()),
        column_labels.data());
    ProUITableColumnwidthsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        static_cast<int>(column_widths.size()),
        column_widths.data());
    ProUITableColumnresizingsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        static_cast<int>(column_resizings.size()),
        column_resizings.data());
    ProUITableRownamesSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        static_cast<int>(row_names.size()),
        row_names.empty() ? nullptr : row_names.data());
    ProUITableRowlabelsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        static_cast<int>(row_labels.size()),
        row_labels.empty() ? nullptr : row_labels.data());
    ProUITableLockedcolumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        5);
    ProUITableSelectionpolicySet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        PROUISELPOLICY_SINGLE);
    ProUITableColumnselectionpolicySet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        PROUISELPOLICY_MULTIPLE);
    ProUITableVisiblerowsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        std::min(18, std::max(6, static_cast<int>(state.rows.size()) + 1)));
    ProUITableMinrowsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        std::min(16, std::max(6, static_cast<int>(state.rows.size()))));
    ProUITableShowgridSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        PRO_B_TRUE);
    ProUITableAutohighlightEnable(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp));
    ProUITableActivateonreturnEnable(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp));
    RestoreBomFocusedCell(dialog_state, config.dialog_inst_name, config.bom_table_comp);

    for (size_t i = 0; i < state.rows.size(); ++i) {
        core::BomRow &row = state.rows[i];
        char row_check_name[64] = {0};
        std::snprintf(row_check_name, sizeof(row_check_name), "bomrowchk_%d_%zu", dialog_state.available_render_serial, i);
        dialog_state.update_checkbox_by_row_key[row.key] = row_check_name;
        ProUITableCellComponentCopy(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.bom_table_comp),
            const_cast<char *>(row.row_name.c_str()),
            const_cast<char *>("EDIT"),
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.base_bom_row_check_comp),
            row_check_name);
        ProUICheckbuttonTextSet(
            const_cast<char *>(config.dialog_inst_name),
            row_check_name,
            const_cast<wchar_t *>(L""));
        if (state.checked_update_row_keys.find(row.key) != state.checked_update_row_keys.end()) {
            ProUICheckbuttonSet(
                const_cast<char *>(config.dialog_inst_name),
                row_check_name);
        } else {
            ProUICheckbuttonUnset(
                const_cast<char *>(config.dialog_inst_name),
                row_check_name);
        }
        ProUITableCellLabelSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.bom_table_comp),
            const_cast<char *>(row.row_name.c_str()),
            const_cast<char *>("SEQ"),
            const_cast<wchar_t *>(std::to_wstring(i + 1).c_str()));
        ProUITableCellLabelSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.bom_table_comp),
            const_cast<char *>(row.row_name.c_str()),
            const_cast<char *>("LEVEL"),
            const_cast<wchar_t *>(std::to_wstring(std::max(1, row.level)).c_str()));
        ProUITableCellLabelSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.bom_table_comp),
            const_cast<char *>(row.row_name.c_str()),
            const_cast<char *>("MODEL"),
            const_cast<wchar_t *>(row.display_name.c_str()));
        ProUITableCellLabelSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.bom_table_comp),
            const_cast<char *>(row.row_name.c_str()),
            const_cast<char *>("QTY"),
            const_cast<wchar_t *>(std::to_wstring(row.quantity).c_str()));

        ProUITableCellBackgroundColorSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.bom_table_comp),
            const_cast<char *>(row.row_name.c_str()),
            const_cast<char *>("EDIT"),
            PRO_UI_COLOR_LT_GREY);
        for (const char *fixed_col : { "SEQ", "LEVEL", "MODEL", "QTY" }) {
            ProUITableCellBackgroundColorSet(
                const_cast<char *>(config.dialog_inst_name),
                const_cast<char *>(config.bom_table_comp),
                const_cast<char *>(row.row_name.c_str()),
                const_cast<char *>(fixed_col),
                PRO_UI_COLOR_LT_GREY);
            ProUITableCellForegroundColorSet(
                const_cast<char *>(config.dialog_inst_name),
                const_cast<char *>(config.bom_table_comp),
                const_cast<char *>(row.row_name.c_str()),
                const_cast<char *>(fixed_col),
                PRO_UI_COLOR_BLACK);
        }

    for (size_t col_index = 0; col_index < state.visible_param_names.size(); ++col_index) {
            const std::string &column_name = column_names_storage[col_index + 5];
            const core::BomAvailableParam *column =
                callbacks.find_available_param ? callbacks.find_available_param(state, state.visible_param_names[col_index]) : nullptr;
            if (column == nullptr || !callbacks.build_cell_view) {
                continue;
            }
            const core::BomCellView view = callbacks.build_cell_view(state, row, *column);
            ProUITableCellComponentDelete(
                const_cast<char *>(config.dialog_inst_name),
                const_cast<char *>(config.bom_table_comp),
                const_cast<char *>(row.row_name.c_str()),
                const_cast<char *>(column_name.c_str()));
            ProUITableCellLabelSet(
                const_cast<char *>(config.dialog_inst_name),
                const_cast<char *>(config.bom_table_comp),
                const_cast<char *>(row.row_name.c_str()),
                const_cast<char *>(column_name.c_str()),
                const_cast<wchar_t *>(view.display_value.c_str()));
            if (!view.helptext.empty()) {
                ProUITableCellHelptextStringSet(
                    const_cast<char *>(config.dialog_inst_name),
                    const_cast<char *>(config.bom_table_comp),
                    const_cast<char *>(row.row_name.c_str()),
                    const_cast<char *>(column_name.c_str()),
                    const_cast<wchar_t *>(view.helptext.c_str()));
            }
            if (callbacks.apply_cell_visual_state) {
                callbacks.apply_cell_visual_state(row.row_name, column_name, view);
            }
        }
    }
}

void ClearBomActiveEditor(BomToolDialogState &dialog_state)
{
    dialog_state.active_edit_draft_key.clear();
    dialog_state.active_edit_row_name.clear();
    dialog_state.active_edit_column_name.clear();
    dialog_state.active_edit_component_name.clear();
    dialog_state.active_edit_uses_optionmenu = false;
}

bool TryResolveSelectedAvailableParamName(BomToolDialogState &dialog_state,
                                          const char *dialog_inst_name,
                                          const char *param_list_comp,
                                          std::wstring &param_name_out)
{
    param_name_out.clear();
    if (dialog_state.tool_state == nullptr || dialog_inst_name == nullptr || param_list_comp == nullptr) {
        return false;
    }

    int selected_count = 0;
    char **selected_rows = nullptr;
    if (ProUITableSelectedrownamesGet(
            const_cast<char *>(dialog_inst_name),
            const_cast<char *>(param_list_comp),
            &selected_count,
            &selected_rows) == PRO_TK_NO_ERROR &&
        selected_rows != nullptr) {
        for (int i = 0; i < selected_count; ++i) {
            if (selected_rows[i] == nullptr) {
                continue;
            }
            const auto found = dialog_state.param_name_by_available_row.find(selected_rows[i]);
            if (found != dialog_state.param_name_by_available_row.end()) {
                param_name_out = found->second;
                ProStringarrayFree(selected_rows, selected_count);
                return true;
            }
        }
        ProStringarrayFree(selected_rows, selected_count);
    }

    char *row_name = nullptr;
    char *column_name = nullptr;
    const ProError st = ProUITableFocusCellGet(
        const_cast<char *>(dialog_inst_name),
        const_cast<char *>(param_list_comp),
        &row_name,
        &column_name);
    if (st != PRO_TK_NO_ERROR || row_name == nullptr) {
        if (row_name != nullptr) {
            ProStringFree(row_name);
        }
        if (column_name != nullptr) {
            ProStringFree(column_name);
        }
        return false;
    }

    const auto found = dialog_state.param_name_by_available_row.find(row_name);
    if (found != dialog_state.param_name_by_available_row.end()) {
        param_name_out = found->second;
    }
    ProStringFree(row_name);
    if (column_name != nullptr) {
        ProStringFree(column_name);
    }
    return !param_name_out.empty();
}

void SaveSelectedAvailableParam(BomToolDialogState &dialog_state,
                                const char *dialog_inst_name,
                                const char *param_list_comp)
{
    std::wstring selected_param_name;
    if (TryResolveSelectedAvailableParamName(
            dialog_state,
            dialog_inst_name,
            param_list_comp,
            selected_param_name)) {
        dialog_state.selected_available_param_name = selected_param_name;
    }
}

void RestoreSelectedAvailableParam(const BomToolDialogState &dialog_state,
                                   const char *dialog_inst_name,
                                   const char *param_list_comp)
{
    if (dialog_state.selected_available_param_name.empty() ||
        dialog_inst_name == nullptr ||
        param_list_comp == nullptr) {
        return;
    }

    for (const auto &entry : dialog_state.param_name_by_available_row) {
        if (entry.second != dialog_state.selected_available_param_name) {
            continue;
        }
        char *row_names[] = { const_cast<char *>(entry.first.c_str()) };
        ProUITableSelectedrownamesSet(
            const_cast<char *>(dialog_inst_name),
            const_cast<char *>(param_list_comp),
            1,
            row_names);
        return;
    }
}

void SaveAvailableFocusedCell(BomToolDialogState &dialog_state,
                              const char *dialog_inst_name,
                              const char *param_list_comp)
{
    dialog_state.focused_available_column_name.clear();
    if (dialog_inst_name == nullptr || param_list_comp == nullptr) {
        return;
    }

    std::wstring selected_param_name;
    TryResolveSelectedAvailableParamName(dialog_state, dialog_inst_name, param_list_comp, selected_param_name);
    if (!selected_param_name.empty()) {
        dialog_state.selected_available_param_name = selected_param_name;
    }

    char *row_name = nullptr;
    char *column_name = nullptr;
    if (ProUITableFocusCellGet(
            const_cast<char *>(dialog_inst_name),
            const_cast<char *>(param_list_comp),
            &row_name,
            &column_name) != PRO_TK_NO_ERROR) {
        if (row_name != nullptr) {
            ProStringFree(row_name);
        }
        if (column_name != nullptr) {
            ProStringFree(column_name);
        }
        return;
    }

    if (column_name != nullptr) {
        dialog_state.focused_available_column_name = column_name;
        ProStringFree(column_name);
    }
    if (row_name != nullptr) {
        ProStringFree(row_name);
    }
}

void RestoreAvailableFocusedCell(const BomToolDialogState &dialog_state,
                                 const char *dialog_inst_name,
                                 const char *param_list_comp)
{
    if (dialog_inst_name == nullptr ||
        param_list_comp == nullptr ||
        dialog_state.selected_available_param_name.empty()) {
        return;
    }

    const auto row_it = dialog_state.available_row_by_param.find(dialog_state.selected_available_param_name);
    if (row_it == dialog_state.available_row_by_param.end()) {
        return;
    }

    const char *column_name = dialog_state.focused_available_column_name.empty()
                                  ? "NAME"
                                  : dialog_state.focused_available_column_name.c_str();
    ProUITableFocusCellSet(
        const_cast<char *>(dialog_inst_name),
        const_cast<char *>(param_list_comp),
        const_cast<char *>(row_it->second.c_str()),
        const_cast<char *>(column_name));
}

void SaveBomFocusedCell(BomToolDialogState &dialog_state,
                        const char *dialog_inst_name,
                        const char *bom_table_comp)
{
    dialog_state.focused_bom_row_name.clear();
    dialog_state.focused_bom_column_name.clear();
    if (dialog_state.tool_state == nullptr || dialog_inst_name == nullptr || bom_table_comp == nullptr) {
        return;
    }

    char *row_name = nullptr;
    char *column_name = nullptr;
    if (ProUITableFocusCellGet(
            const_cast<char *>(dialog_inst_name),
            const_cast<char *>(bom_table_comp),
            &row_name,
            &column_name) != PRO_TK_NO_ERROR) {
        if (row_name != nullptr) {
            ProStringFree(row_name);
        }
        if (column_name != nullptr) {
            ProStringFree(column_name);
        }
        return;
    }

    if (row_name != nullptr) {
        dialog_state.focused_bom_row_name = row_name;
        ProStringFree(row_name);
    }
    if (column_name != nullptr) {
        dialog_state.focused_bom_column_name = column_name;
        ProStringFree(column_name);
    }
}

void RestoreBomFocusedCell(const BomToolDialogState &dialog_state,
                           const char *dialog_inst_name,
                           const char *bom_table_comp)
{
    if (dialog_state.focused_bom_row_name.empty() ||
        dialog_state.focused_bom_column_name.empty() ||
        dialog_inst_name == nullptr ||
        bom_table_comp == nullptr) {
        return;
    }

    ProUITableFocusCellSet(
        const_cast<char *>(dialog_inst_name),
        const_cast<char *>(bom_table_comp),
        const_cast<char *>(dialog_state.focused_bom_row_name.c_str()),
        const_cast<char *>(dialog_state.focused_bom_column_name.c_str()));
}

void HandleParamListSelect(BomToolDialogState &dialog_state,
                           const char *dialog_inst_name,
                           const char *param_list_comp)
{
    SaveSelectedAvailableParam(dialog_state, dialog_inst_name, param_list_comp);
    if (dialog_state.tool_state == nullptr || dialog_state.selected_available_param_name.empty()) {
        return;
    }

    core::BomToolState &state = *dialog_state.tool_state;
    const std::wstring selected_name = dialog_state.selected_available_param_name;
    state.pending_create_name = selected_name;
    state.pending_display_name = selected_name;
    state.pending_default_value.clear();
    state.pending_option_value.clear();

    const auto custom_it = state.custom_param_specs.find(selected_name);
    if (custom_it != state.custom_param_specs.end()) {
        state.pending_create_type = custom_it->second.type;
        state.pending_default_value = custom_it->second.raw_value;
        return;
    }

    const auto index_it = state.available_index_by_name.find(selected_name);
    if (index_it == state.available_index_by_name.end()) {
        return;
    }

    const core::BomAvailableParam &param = state.available_params[index_it->second];
    if (!param.mixed_type && !param.types.empty()) {
        state.pending_create_type = *param.types.begin();
    }

    if (state.pending_default_value.empty()) {
        for (const auto &snapshot_entry : state.snapshots_by_mdl) {
            const auto value_it = snapshot_entry.second.params.find(selected_name);
            if (value_it != snapshot_entry.second.params.end() && value_it->second.exists) {
                state.pending_default_value = value_it->second.display_value;
                break;
            }
        }
    }
}

void ApplySelectedParamToForm(BomToolDialogState &dialog_state,
                              const BomDialogInteractionConfig &config,
                              const BomDialogInteractionCallbacks &callbacks)
{
    if (dialog_state.tool_state == nullptr || config.dialog_inst_name == nullptr) {
        return;
    }

    const core::BomToolState &state = *dialog_state.tool_state;
    ProUIInputpanelValueSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.create_name_comp),
        const_cast<wchar_t *>(state.pending_create_name.c_str()));
    ProUIInputpanelValueSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.default_value_comp),
        const_cast<wchar_t *>(state.pending_default_value.c_str()));
    if (callbacks.param_add_type_menu_label) {
        ProUIOptionmenuValueSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.create_type_comp),
            const_cast<wchar_t *>(callbacks.param_add_type_menu_label(state.pending_create_type)));
    }
    ProUIOptionmenuValueSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.optional_value_comp),
        const_cast<wchar_t *>(state.pending_option_value.empty() ? L"" : state.pending_option_value.c_str()));
}

void SaveAvailableColumnWidths(BomToolDialogState &dialog_state,
                               const char *dialog_inst_name,
                               const char *param_list_comp)
{
    if (dialog_inst_name == nullptr || param_list_comp == nullptr) {
        return;
    }

    int column_count = 0;
    char **column_names = nullptr;
    if (ProUITableColumnnamesGet(
            const_cast<char *>(dialog_inst_name),
            const_cast<char *>(param_list_comp),
            &column_count,
            &column_names) != PRO_TK_NO_ERROR ||
        column_names == nullptr) {
        return;
    }

    for (int i = 0; i < column_count; ++i) {
        if (column_names[i] == nullptr) {
            continue;
        }
        int width = 0;
        if (ProUITableColumnWidthGet(
                const_cast<char *>(dialog_inst_name),
                const_cast<char *>(param_list_comp),
                column_names[i],
                &width) == PRO_TK_NO_ERROR &&
            width > 0) {
            dialog_state.available_column_width_by_name[column_names[i]] = width;
        }
    }
    ProStringarrayFree(column_names, column_count);
}

void SaveBomVisibleColumnOrder(BomToolDialogState &dialog_state, const char *dialog_inst_name, const char *bom_table_comp)
{
    if (dialog_state.tool_state == nullptr || dialog_inst_name == nullptr || bom_table_comp == nullptr) {
        return;
    }

    int column_count = 0;
    char **column_names = nullptr;
    if (ProUITableColumnnamesGet(
            const_cast<char *>(dialog_inst_name),
            const_cast<char *>(bom_table_comp),
            &column_count,
            &column_names) != PRO_TK_NO_ERROR ||
        column_names == nullptr) {
        return;
    }

    std::vector<std::wstring> ordered_names;
    std::unordered_set<std::wstring> seen;
    ordered_names.reserve(dialog_state.tool_state->visible_param_names.size());
    for (int i = 0; i < column_count; ++i) {
        if (column_names[i] == nullptr) {
            continue;
        }
        const auto found = dialog_state.param_name_by_column.find(column_names[i]);
        if (found == dialog_state.param_name_by_column.end()) {
            continue;
        }
        if (seen.insert(found->second).second) {
            ordered_names.push_back(found->second);
        }
    }
    ProStringarrayFree(column_names, column_count);

    if (ordered_names.empty()) {
        return;
    }

    for (const std::wstring &name : dialog_state.tool_state->visible_param_names) {
        if (seen.insert(name).second) {
            ordered_names.push_back(name);
        }
    }
    dialog_state.tool_state->visible_param_names.swap(ordered_names);
}

void SaveBomColumnWidths(BomToolDialogState &dialog_state,
                         const char *dialog_inst_name,
                         const char *bom_table_comp)
{
    if (dialog_state.tool_state == nullptr || dialog_inst_name == nullptr || bom_table_comp == nullptr) {
        return;
    }

    int column_count = 0;
    char **column_names = nullptr;
    if (ProUITableColumnnamesGet(
            const_cast<char *>(dialog_inst_name),
            const_cast<char *>(bom_table_comp),
            &column_count,
            &column_names) != PRO_TK_NO_ERROR ||
        column_names == nullptr) {
        return;
    }

    for (int i = 0; i < column_count; ++i) {
        if (column_names[i] == nullptr) {
            continue;
        }
        int width = 0;
        if (ProUITableColumnWidthGet(
                const_cast<char *>(dialog_inst_name),
                const_cast<char *>(bom_table_comp),
                column_names[i],
                &width) != PRO_TK_NO_ERROR ||
            width <= 0) {
            continue;
        }

        const auto param_it = dialog_state.param_name_by_column.find(column_names[i]);
        if (param_it != dialog_state.param_name_by_column.end()) {
            dialog_state.bom_param_column_width_by_name[param_it->second] = width;
        } else {
            dialog_state.bom_fixed_column_width_by_name[column_names[i]] = width;
        }
    }
    ProStringarrayFree(column_names, column_count);
}

void CaptureBomRowUpdateSelection(BomToolDialogState &dialog_state,
                                  const char *dialog_inst_name)
{
    if (dialog_state.tool_state == nullptr || dialog_inst_name == nullptr) {
        return;
    }

    core::BomToolState &state = *dialog_state.tool_state;
    state.checked_update_row_keys.clear();
    for (const auto &entry : dialog_state.update_checkbox_by_row_key) {
        ProBoolean checked = PRO_B_FALSE;
        if (ProUICheckbuttonGetState(
                const_cast<char *>(dialog_inst_name),
                const_cast<char *>(entry.second.c_str()),
                &checked) == PRO_TK_NO_ERROR &&
            checked == PRO_B_TRUE) {
            state.checked_update_row_keys.insert(entry.first);
        }
    }
}

void SetAllBomRowsChecked(BomToolDialogState &dialog_state,
                          const BomDialogInteractionConfig &config,
                          bool checked)
{
    if (dialog_state.tool_state == nullptr || config.dialog_inst_name == nullptr) {
        return;
    }

    core::BomToolState &state = *dialog_state.tool_state;
    state.checked_update_row_keys.clear();
    for (const core::BomRow &row : state.rows) {
        const auto comp_it = dialog_state.update_checkbox_by_row_key.find(row.key);
        if (checked) {
            state.checked_update_row_keys.insert(row.key);
            if (comp_it != dialog_state.update_checkbox_by_row_key.end()) {
                ProUICheckbuttonSet(
                    const_cast<char *>(config.dialog_inst_name),
                    const_cast<char *>(comp_it->second.c_str()));
            }
        } else if (comp_it != dialog_state.update_checkbox_by_row_key.end()) {
            ProUICheckbuttonUnset(
                const_cast<char *>(config.dialog_inst_name),
                const_cast<char *>(comp_it->second.c_str()));
        }
    }

    const std::wstring header_label = BomRowSelectHeaderLabel(state);
    ProUITableColumnLabelSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        const_cast<char *>("EDIT"),
        const_cast<wchar_t *>(header_label.c_str()));
    if (config.select_all_rows_comp != nullptr) {
        if (checked && !state.rows.empty()) {
            ProUICheckbuttonSet(
                const_cast<char *>(config.dialog_inst_name),
                const_cast<char *>(config.select_all_rows_comp));
        } else {
            ProUICheckbuttonUnset(
                const_cast<char *>(config.dialog_inst_name),
                const_cast<char *>(config.select_all_rows_comp));
        }
    }
}

bool TryHandleBomRowSelectHeader(BomToolDialogState &dialog_state,
                                 const BomDialogInteractionConfig &config,
                                 const BomDialogInteractionCallbacks &callbacks)
{
    if (dialog_state.tool_state == nullptr || config.dialog_inst_name == nullptr || config.bom_table_comp == nullptr) {
        return false;
    }

    int selected_count = 0;
    char **selected_columns = nullptr;
    bool edit_column_selected = false;
    if (ProUITableSelectedcolumnnamesGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.bom_table_comp),
            &selected_count,
            &selected_columns) == PRO_TK_NO_ERROR &&
        selected_columns != nullptr) {
        for (int i = 0; i < selected_count; ++i) {
            if (selected_columns[i] != nullptr && std::strcmp(selected_columns[i], "EDIT") == 0) {
                edit_column_selected = true;
                break;
            }
        }
        ProStringarrayFree(selected_columns, selected_count);
    }

    if (!edit_column_selected) {
        return false;
    }

    if (!dialog_state.active_edit_draft_key.empty()) {
        HarvestBomInlineDraftInputs(dialog_state, config, callbacks);
        ClearBomActiveEditor(dialog_state);
    }

    CaptureBomRowUpdateSelection(dialog_state, config.dialog_inst_name);

    core::BomToolState &state = *dialog_state.tool_state;
    bool all_checked = !state.rows.empty();
    for (const core::BomRow &row : state.rows) {
        if (state.checked_update_row_keys.find(row.key) == state.checked_update_row_keys.end()) {
            all_checked = false;
            break;
        }
    }

    SetAllBomRowsChecked(dialog_state, config, !all_checked);
    ProUITableSelectedcolumnnamesSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        0,
        nullptr);
    return true;
}

void ActivateBomInlineEditor(BomToolDialogState &dialog_state,
                             const core::BomRow &row,
                             const std::string &column_name,
                             const core::BomAvailableParam &column,
                             const BomDialogInteractionConfig &config,
                             const BomDialogInteractionCallbacks &callbacks)
{
    if (dialog_state.tool_state == nullptr || !callbacks.build_cell_view || !callbacks.make_draft_key) {
        return;
    }

    core::BomToolState &state = *dialog_state.tool_state;
    const core::BomCellView view = callbacks.build_cell_view(state, row, column);
    if (!view.editable) {
        return;
    }

    const std::wstring draft_key = callbacks.make_draft_key(row.key, column.name);
    char input_name[64] = {0};
    std::snprintf(input_name,
                  sizeof(input_name),
                  "bomcell_%d_%s_%s",
                  dialog_state.available_render_serial,
                  row.row_name.c_str(),
                  column_name.c_str());
    const bool use_bool_optionmenu = (column.write_type == PRO_PARAM_BOOLEAN);
    ProUITableCellComponentDelete(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        const_cast<char *>(row.row_name.c_str()),
        const_cast<char *>(column_name.c_str()));
    ProUITableCellComponentCopy(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        const_cast<char *>(row.row_name.c_str()),
        const_cast<char *>(column_name.c_str()),
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(use_bool_optionmenu ? config.cell_bool_base_comp : config.cell_input_base_comp),
        input_name);
    if (use_bool_optionmenu) {
        if (IsBooleanOptionValue(view.display_value)) {
            ProUIOptionmenuValueSet(
                const_cast<char *>(config.dialog_inst_name),
                input_name,
                const_cast<wchar_t *>(view.display_value.c_str()));
        }
        if (!view.helptext.empty()) {
            ProUIOptionmenuHelptextSet(
                const_cast<char *>(config.dialog_inst_name),
                input_name,
                const_cast<wchar_t *>(view.helptext.c_str()));
        }
    } else {
        ProUIInputpanelValueSet(
            const_cast<char *>(config.dialog_inst_name),
            input_name,
            const_cast<wchar_t *>(view.display_value.c_str()));
        ProUIInputpanelColumnsSet(
            const_cast<char *>(config.dialog_inst_name),
            input_name,
            18);
        ProUIInputpanelAutohighlightEnable(
            const_cast<char *>(config.dialog_inst_name),
            input_name);
        if (!view.helptext.empty()) {
            ProUIInputpanelHelptextSet(
                const_cast<char *>(config.dialog_inst_name),
                input_name,
                const_cast<wchar_t *>(view.helptext.c_str()));
        }
    }
    dialog_state.active_edit_draft_key = draft_key;
    dialog_state.active_edit_row_name = row.row_name;
    dialog_state.active_edit_column_name = column_name;
    dialog_state.active_edit_component_name = input_name;
    dialog_state.active_edit_uses_optionmenu = use_bool_optionmenu;
    ProUIDialogFocusSet(
        const_cast<char *>(config.dialog_inst_name),
        input_name);
}

void HarvestBomInlineDraftInputs(const BomToolDialogState &dialog_state,
                                 const BomDialogInteractionConfig &config,
                                 const BomDialogInteractionCallbacks &callbacks)
{
    if (dialog_state.tool_state == nullptr ||
        dialog_state.active_edit_draft_key.empty() ||
        dialog_state.active_edit_component_name.empty() ||
        !callbacks.split_draft_key ||
        !callbacks.find_row_by_key ||
        !callbacks.find_available_param ||
        !callbacks.build_cell_view ||
        !callbacks.normalize_inline_input ||
        !callbacks.apply_cell_visual_state) {
        return;
    }

    core::BomToolState &state = *dialog_state.tool_state;
    std::wstring row_key;
    std::wstring param_name;
    if (!callbacks.split_draft_key(dialog_state.active_edit_draft_key, row_key, param_name)) {
        return;
    }
    const core::BomRow *row = callbacks.find_row_by_key(state, row_key);
    const core::BomAvailableParam *column = callbacks.find_available_param(state, param_name);
    if (row == nullptr || column == nullptr) {
        return;
    }

    std::wstring normalized_input;
    wchar_t *value = nullptr;
    if (dialog_state.active_edit_uses_optionmenu) {
        if (ProUIOptionmenuValueGet(
                const_cast<char *>(config.dialog_inst_name),
                const_cast<char *>(dialog_state.active_edit_component_name.c_str()),
                &value) == PRO_TK_NO_ERROR &&
            value != nullptr) {
            normalized_input.assign(value);
            ProWstringFree(value);
        }
    } else {
        if (ProUIInputpanelValueGet(
                const_cast<char *>(config.dialog_inst_name),
                const_cast<char *>(dialog_state.active_edit_component_name.c_str()),
                &value) != PRO_TK_NO_ERROR ||
            value == nullptr) {
            return;
        }
        normalized_input.assign(value);
        ProWstringFree(value);
    }

    const core::BomCellView view = callbacks.build_cell_view(state, *row, *column);
    const std::wstring input_raw = callbacks.normalize_inline_input(*column, normalized_input);
    const std::wstring actual_raw = callbacks.normalize_inline_input(*column, view.actual_value);
    if (input_raw == actual_raw) {
        state.draft_values.erase(dialog_state.active_edit_draft_key);
    } else {
        state.draft_values[dialog_state.active_edit_draft_key] = input_raw;
    }

    const core::BomCellView refreshed_view = callbacks.build_cell_view(state, *row, *column);
    ProUITableCellComponentDelete(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        const_cast<char *>(dialog_state.active_edit_row_name.c_str()),
        const_cast<char *>(dialog_state.active_edit_column_name.c_str()));
    ProUITableCellLabelSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        const_cast<char *>(dialog_state.active_edit_row_name.c_str()),
        const_cast<char *>(dialog_state.active_edit_column_name.c_str()),
        const_cast<wchar_t *>(refreshed_view.display_value.c_str()));
    if (!refreshed_view.helptext.empty()) {
        ProUITableCellHelptextStringSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.bom_table_comp),
            const_cast<char *>(dialog_state.active_edit_row_name.c_str()),
            const_cast<char *>(dialog_state.active_edit_column_name.c_str()),
            const_cast<wchar_t *>(refreshed_view.helptext.c_str()));
    }
    callbacks.apply_cell_visual_state(
        dialog_state.active_edit_row_name,
        dialog_state.active_edit_column_name,
        refreshed_view);
}

void HandleBomTableSelect(BomToolDialogState &dialog_state,
                          const BomDialogInteractionConfig &config,
                          const BomDialogInteractionCallbacks &callbacks)
{
    if (dialog_state.tool_state == nullptr || !callbacks.find_row_by_name || !callbacks.find_available_param) {
        return;
    }

    if (TryHandleBomRowSelectHeader(dialog_state, config, callbacks)) {
        return;
    }

    char *row_name = nullptr;
    char *column_name = nullptr;
    const ProError st = ProUITableFocusCellGet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        &row_name,
        &column_name);
    if (st != PRO_TK_NO_ERROR || row_name == nullptr || column_name == nullptr) {
        if (row_name != nullptr) {
            ProStringFree(row_name);
        }
        if (column_name != nullptr) {
            ProStringFree(column_name);
        }
        return;
    }

    const std::string row_name_str = row_name;
    const std::string column_name_str = column_name;
    ProStringFree(row_name);
    ProStringFree(column_name);

    if (!dialog_state.active_edit_draft_key.empty() &&
        dialog_state.active_edit_row_name == row_name_str &&
        dialog_state.active_edit_column_name == column_name_str) {
        return;
    }

    if (!dialog_state.active_edit_draft_key.empty()) {
        HarvestBomInlineDraftInputs(dialog_state, config, callbacks);
        ClearBomActiveEditor(dialog_state);
    }

    const core::BomRow *row = callbacks.find_row_by_name(*dialog_state.tool_state, row_name_str);
    if (row == nullptr) {
        return;
    }
    const auto col_it = dialog_state.param_name_by_column.find(column_name_str);
    if (col_it == dialog_state.param_name_by_column.end()) {
        return;
    }
    const core::BomAvailableParam *column = callbacks.find_available_param(*dialog_state.tool_state, col_it->second);
    if (column == nullptr) {
        return;
    }

    ActivateBomInlineEditor(dialog_state, *row, column_name_str, *column, config, callbacks);
}

void HandleBomCreateTypeChanged(BomToolDialogState &dialog_state,
                                const BomDialogInteractionConfig &config,
                                const BomDialogInteractionCallbacks &callbacks)
{
    if (dialog_state.tool_state == nullptr ||
        !callbacks.param_type_from_menu_label ||
        !callbacks.refresh_create_value_controls) {
        return;
    }

    wchar_t *type_value = nullptr;
    if (ProUIOptionmenuValueGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.create_type_comp),
            &type_value) == PRO_TK_NO_ERROR &&
        type_value != nullptr) {
        ProParamvalueType type = PRO_PARAM_STRING;
        if (callbacks.param_type_from_menu_label(type_value, type)) {
            dialog_state.tool_state->pending_create_type = type;
        }
        ProWstringFree(type_value);
    }
    callbacks.refresh_create_value_controls(*dialog_state.tool_state);
}

void CaptureBomDialogUiState(BomToolDialogState &dialog_state,
                             const BomDialogConfig &config,
                             const BomDialogInteractionConfig &interaction_config,
                             const BomDialogInteractionCallbacks &interaction_callbacks,
                             const BomDialogCallbacks &callbacks)
{
    if (dialog_state.tool_state == nullptr) {
        return;
    }

    core::BomToolState &state = *dialog_state.tool_state;
    HarvestBomInlineDraftInputs(dialog_state, interaction_config, interaction_callbacks);
    ClearBomActiveEditor(dialog_state);
    SaveAvailableColumnWidths(dialog_state, config.dialog_inst_name, config.param_list_comp);
    SaveBomColumnWidths(dialog_state, config.dialog_inst_name, config.bom_table_comp);
    SaveBomVisibleColumnOrder(dialog_state, config.dialog_inst_name, config.bom_table_comp);
    SaveSelectedAvailableParam(dialog_state, config.dialog_inst_name, config.param_list_comp);
    wchar_t *model_filter_value = nullptr;
    if (ProUIInputpanelValueGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.model_filter_input_comp),
            &model_filter_value) == PRO_TK_NO_ERROR &&
        model_filter_value != nullptr) {
        state.filter_model_name.assign(model_filter_value);
        ProWstringFree(model_filter_value);
    } else {
        state.filter_model_name.clear();
    }
    wchar_t *param_filter_value = nullptr;
    if (ProUIInputpanelValueGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.param_filter_input_comp),
            &param_filter_value) == PRO_TK_NO_ERROR &&
        param_filter_value != nullptr) {
        state.filter_param_name.assign(param_filter_value);
        ProWstringFree(param_filter_value);
    } else {
        state.filter_param_name.clear();
    }
    wchar_t *value_filter_value = nullptr;
    if (ProUIInputpanelValueGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.value_filter_input_comp),
            &value_filter_value) == PRO_TK_NO_ERROR &&
        value_filter_value != nullptr) {
        state.filter_param_value.assign(value_filter_value);
        ProWstringFree(value_filter_value);
    } else {
        state.filter_param_value.clear();
    }
    wchar_t *simprep_value = nullptr;
    if (ProUIOptionmenuValueGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.simprep_menu_comp),
            &simprep_value) == PRO_TK_NO_ERROR &&
        simprep_value != nullptr) {
        state.active_simprep_label = simprep_value;
        state.active_simprep_index = -1;
        for (size_t i = 0; i < state.simprep_options.size(); ++i) {
            if (state.simprep_options[i].display_label == state.active_simprep_label) {
                state.active_simprep_index = static_cast<int>(i);
                break;
            }
        }
        ProWstringFree(simprep_value);
    }
    wchar_t *level_value = nullptr;
    if (ProUIOptionmenuValueGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.max_level_menu_comp),
            &level_value) == PRO_TK_NO_ERROR &&
        level_value != nullptr) {
        int parsed_level = 0;
        if (swscanf_s(level_value, L"%d", &parsed_level) == 1 && parsed_level > 0) {
            state.max_bom_level = std::min(20, parsed_level);
        }
        ProWstringFree(level_value);
    }
    ProBoolean assemblies_checked = PRO_B_FALSE;
    if (ProUICheckbuttonGetState(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.assemblies_filter_comp),
            &assemblies_checked) == PRO_TK_NO_ERROR) {
        state.assemblies_option = assemblies_checked;
    }
    ProBoolean parts_checked = PRO_B_FALSE;
    if (ProUICheckbuttonGetState(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.parts_filter_comp),
            &parts_checked) == PRO_TK_NO_ERROR) {
        state.parts_option = parts_checked;
    }
    SaveAvailableFocusedCell(dialog_state, config.dialog_inst_name, config.param_list_comp);
    SaveBomFocusedCell(dialog_state, config.dialog_inst_name, config.bom_table_comp);

    state.checked_available_names.clear();
    for (const auto &entry : dialog_state.available_checkbox_by_param) {
        ProBoolean checked = PRO_B_FALSE;
        if (ProUICheckbuttonGetState(
                const_cast<char *>(config.dialog_inst_name),
                const_cast<char *>(entry.second.c_str()),
                &checked) == PRO_TK_NO_ERROR &&
            checked == PRO_B_TRUE) {
            state.checked_available_names.insert(entry.first);
        }
    }

    CaptureBomRowUpdateSelection(dialog_state, config.dialog_inst_name);

    state.selected_column_names.clear();
    int selected_count = 0;
    char **selected_columns = nullptr;
    if (ProUITableSelectedcolumnnamesGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.bom_table_comp),
            &selected_count,
            &selected_columns) == PRO_TK_NO_ERROR &&
        selected_columns != nullptr) {
        for (int i = 0; i < selected_count; ++i) {
            if (selected_columns[i] == nullptr) {
                continue;
            }
            const auto found = dialog_state.param_name_by_column.find(selected_columns[i]);
            if (found != dialog_state.param_name_by_column.end()) {
                state.selected_column_names.insert(found->second);
            }
        }
        ProStringarrayFree(selected_columns, selected_count);
    }

    wchar_t *create_name_value = nullptr;
    if (ProUIInputpanelValueGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.create_name_comp),
            &create_name_value) == PRO_TK_NO_ERROR &&
        create_name_value != nullptr) {
        state.pending_create_name.assign(create_name_value);
        ProWstringFree(create_name_value);
    } else {
        state.pending_create_name.clear();
    }

    wchar_t *default_value = nullptr;
    if (ProUIInputpanelValueGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.default_value_comp),
            &default_value) == PRO_TK_NO_ERROR &&
        default_value != nullptr) {
        state.pending_default_value.assign(default_value);
        ProWstringFree(default_value);
    }

    wchar_t *option_value = nullptr;
    if (ProUIOptionmenuValueGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.optional_value_comp),
            &option_value) == PRO_TK_NO_ERROR &&
        option_value != nullptr) {
        state.pending_option_value.assign(option_value);
        ProWstringFree(option_value);
    }

    wchar_t *create_type_value = nullptr;
    if (ProUIOptionmenuValueGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.create_type_comp),
            &create_type_value) == PRO_TK_NO_ERROR &&
        create_type_value != nullptr) {
        ProParamvalueType type = PRO_PARAM_STRING;
        if (callbacks.param_type_from_menu_label &&
            callbacks.param_type_from_menu_label(create_type_value, type)) {
            state.pending_create_type = type;
        }
        ProWstringFree(create_type_value);
    }

    wchar_t *create_bool_value = nullptr;
    if (ProUIOptionmenuValueGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.create_bool_comp),
            &create_bool_value) == PRO_TK_NO_ERROR &&
        create_bool_value != nullptr) {
        short bool_value = 1;
        if (callbacks.bool_menu_value_to_short &&
            callbacks.bool_menu_value_to_short(create_bool_value, bool_value)) {
            state.pending_create_bool = bool_value;
        }
        ProWstringFree(create_bool_value);
    }
}

} // namespace

bool PromptBomToolDialog(core::BomToolState &state,
                         BomToolDialogAction &action,
                         const BomDialogCallbacks &callbacks)
{
    const BomDialogConfig config = DefaultBomDialogConfig();
    action = BomToolDialogAction::Close;

    std::string used_resource;
    if (TryCreateDialog(config, used_resource) != PRO_TK_NO_ERROR) {
        return false;
    }

    ProUIDialogTitleSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<wchar_t *>(L"BOM清单"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>("ParamNameLabel"),
        const_cast<wchar_t *>(L"\u53c2\u6570\u540d\u79f0"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>("ParamTypeLabel"),
        const_cast<wchar_t *>(L"\u53c2\u6570\u7c7b\u578b"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>("ParamDefaultValueLabel"),
        const_cast<wchar_t *>(L"\u9ed8\u8ba4\u503c"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>("ParamOptionValueLabel"),
        const_cast<wchar_t *>(L"\u53ef\u9009\u53d6\u503c"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.add_comp),
        const_cast<wchar_t *>(L"添加"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.delete_comp),
        const_cast<wchar_t *>(L"删除"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_update_comp),
        const_cast<wchar_t *>(L"\u4fee\u6539"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.move_left_comp),
        const_cast<wchar_t *>(L"←"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.move_right_comp),
        const_cast<wchar_t *>(L"→"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.refresh_comp),
        const_cast<wchar_t *>(L"刷新"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.update_comp),
        const_cast<wchar_t *>(L"更新到模型"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.export_comp),
        const_cast<wchar_t *>(L"导出Excel"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.cancel_comp),
        const_cast<wchar_t *>(L"关闭"));

    BomDialogRenderConfig render_config = {};
    render_config.dialog_inst_name = config.dialog_inst_name;
    render_config.summary_comp = config.summary_comp;
    render_config.simprep_label_comp = config.simprep_label_comp;
    render_config.simprep_menu_comp = config.simprep_menu_comp;
    render_config.select_all_rows_comp = config.select_all_rows_comp;
    render_config.max_level_menu_comp = config.max_level_menu_comp;
    render_config.assemblies_filter_comp = config.assemblies_filter_comp;
    render_config.parts_filter_comp = config.parts_filter_comp;
    render_config.model_filter_label_comp = config.model_filter_label_comp;
    render_config.model_filter_input_comp = config.model_filter_input_comp;
    render_config.param_filter_label_comp = config.param_filter_label_comp;
    render_config.param_filter_input_comp = config.param_filter_input_comp;
    render_config.value_filter_label_comp = config.value_filter_label_comp;
    render_config.value_filter_input_comp = config.value_filter_input_comp;
    render_config.available_label_comp = config.available_label_comp;
    render_config.table_label_comp = config.table_label_comp;
    render_config.display_name_comp = config.display_name_comp;
    render_config.column_width_comp = config.column_width_comp;
    render_config.align_menu_comp = config.align_menu_comp;
    render_config.field_type_menu_comp = config.field_type_menu_comp;
    render_config.param_list_comp = config.param_list_comp;
    render_config.create_name_comp = config.create_name_comp;
    render_config.create_type_comp = config.create_type_comp;
    render_config.create_bool_label_comp = config.create_bool_label_comp;
    render_config.create_bool_comp = config.create_bool_comp;
    render_config.default_value_comp = config.default_value_comp;
    render_config.optional_value_comp = config.optional_value_comp;
    render_config.update_to_model_check_comp = config.update_to_model_check_comp;
    render_config.bom_table_comp = config.bom_table_comp;
    render_config.base_avail_check_comp = config.base_avail_check_comp;
    render_config.base_bom_row_check_comp = config.base_bom_row_check_comp;
    render_config.cell_bool_base_comp = config.cell_bool_base_comp;

    BomDialogRenderCallbacks render_callbacks = {};
    render_callbacks.build_summary_text = callbacks.build_summary_text;
    render_callbacks.join_type_labels = [&callbacks](const std::set<ProParamvalueType> &types) {
        if (types.empty()) {
            return std::wstring(L"未知");
        }
        if (types.size() == 1 && callbacks.param_add_type_menu_label) {
            return std::wstring(callbacks.param_add_type_menu_label(*types.begin()));
        }
        if (types.size() > 1) {
            return std::wstring(L"混合类型");
        }
        return std::wstring(L"未知");
    };
    render_callbacks.param_add_type_menu_label = callbacks.param_add_type_menu_label;
    render_callbacks.refresh_create_value_controls = [&config](const core::BomToolState &) {
        ProUILabelHide(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.create_bool_label_comp));
        ProUIOptionmenuHide(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.create_bool_comp));
    };
    render_callbacks.find_available_param = callbacks.find_available_param;
    render_callbacks.build_cell_view = callbacks.build_cell_view;
    render_callbacks.apply_cell_visual_state = [&config](const std::string &row_name,
                                                         const std::string &column_name,
                                                         const core::BomCellView &view) {
        ProUIColor bg = PRO_UI_COLOR_WHITE;
        ProUIColor fg = PRO_UI_COLOR_BLACK;
        if (view.modified) {
            bg = PRO_UI_COLOR_YELLOW;
        } else if (!view.editable) {
            bg = PRO_UI_COLOR_3D_LIGHT_SHADOW;
            fg = PRO_UI_COLOR_DK_GREY;
        }
        ProUITableCellBackgroundColorSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.bom_table_comp),
            const_cast<char *>(row_name.c_str()),
            const_cast<char *>(column_name.c_str()),
            bg);
        ProUITableCellForegroundColorSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.bom_table_comp),
            const_cast<char *>(row_name.c_str()),
            const_cast<char *>(column_name.c_str()),
            fg);
    };

    BomDialogInteractionConfig interaction_config = {};
    interaction_config.dialog_inst_name = config.dialog_inst_name;
    interaction_config.refresh_status = config.status_refresh;
    interaction_config.rebuild_status = config.status_rebuild;
    interaction_config.param_list_comp = config.param_list_comp;
    interaction_config.bom_table_comp = config.bom_table_comp;
    interaction_config.select_all_rows_comp = config.select_all_rows_comp;
    interaction_config.model_filter_input_comp = config.model_filter_input_comp;
    interaction_config.param_filter_input_comp = config.param_filter_input_comp;
    interaction_config.value_filter_input_comp = config.value_filter_input_comp;
    interaction_config.cell_input_base_comp = config.cell_input_base_comp;
    interaction_config.cell_bool_base_comp = config.cell_bool_base_comp;
    interaction_config.create_name_comp = config.create_name_comp;
    interaction_config.create_type_comp = config.create_type_comp;
    interaction_config.default_value_comp = config.default_value_comp;
    interaction_config.optional_value_comp = config.optional_value_comp;

    BomDialogInteractionCallbacks interaction_callbacks = {};
    interaction_callbacks.find_row_by_name = [](const core::BomToolState &tool_state, const std::string &row_name) {
        for (const core::BomRow &row : tool_state.rows) {
            if (row.row_name == row_name) {
                return &row;
            }
        }
        return static_cast<const core::BomRow *>(nullptr);
    };
    interaction_callbacks.find_row_by_key = [](const core::BomToolState &tool_state, const std::wstring &row_key) {
        for (const core::BomRow &row : tool_state.rows) {
            if (row.key == row_key) {
                return &row;
            }
        }
        return static_cast<const core::BomRow *>(nullptr);
    };
    interaction_callbacks.find_available_param = callbacks.find_available_param;
    interaction_callbacks.build_cell_view = callbacks.build_cell_view;
    interaction_callbacks.apply_cell_visual_state = render_callbacks.apply_cell_visual_state;
    interaction_callbacks.make_draft_key = [](const std::wstring &row_key, const std::wstring &param_name) {
        std::wstring key(row_key);
        key.push_back(L'\x1f');
        key += param_name;
        return key;
    };
    interaction_callbacks.split_draft_key = [](const std::wstring &key,
                                               std::wstring &row_key,
                                               std::wstring &param_name) {
        const size_t sep = key.find(L'\x1f');
        if (sep == std::wstring::npos) {
            return false;
        }
        row_key = key.substr(0, sep);
        param_name = key.substr(sep + 1);
        return !row_key.empty() && !param_name.empty();
    };
    interaction_callbacks.normalize_inline_input = [](const core::BomAvailableParam &column,
                                                      const std::wstring &input_value) {
        if (column.write_type == PRO_PARAM_STRING && input_value == L"\"\"") {
            return std::wstring();
        }
        if (column.write_type == PRO_PARAM_BOOLEAN) {
            if (input_value == L"YES" || input_value == L"NO") {
                return input_value;
            }
            return std::wstring();
        }
        return input_value;
    };
    interaction_callbacks.param_add_type_menu_label = callbacks.param_add_type_menu_label;
    interaction_callbacks.param_type_from_menu_label = callbacks.param_type_from_menu_label;
    interaction_callbacks.refresh_create_value_controls = render_callbacks.refresh_create_value_controls;

    BomToolDialogState dialog_state;
    dialog_state.tool_state = &state;
    BomDialogRuntimeContext runtime = {};
    runtime.dialog_state = &dialog_state;
    runtime.interaction_config = interaction_config;
    runtime.interaction_callbacks = interaction_callbacks;
    g_active_bom_runtime = &runtime;
    EnsureParamListPopupSupport();

    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.add_comp),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_add)));
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.delete_comp),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_delete_param)));
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_update_comp),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_update_param)));
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.move_left_comp),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_move_left)));
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.move_right_comp),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_move_right)));
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.refresh_comp),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_refresh)));
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.update_comp),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_update)));
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.export_comp),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_export)));
    ProUIOptionmenuSelectActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.simprep_menu_comp),
        OnSimprepChanged,
        &runtime);
    ProUIOptionmenuSelectActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.max_level_menu_comp),
        OnSimprepChanged,
        &runtime);
    ProUICheckbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.assemblies_filter_comp),
        OnBomFilterChanged,
        &runtime);
    ProUICheckbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.parts_filter_comp),
        OnBomFilterChanged,
        &runtime);
    ProUICheckbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.select_all_rows_comp),
        OnBomSelectAllChanged,
        &runtime);
    ProUIInputpanelActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.model_filter_input_comp),
        OnBomFilterChanged,
        &runtime);
    ProUIInputpanelActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_filter_input_comp),
        OnBomFilterChanged,
        &runtime);
    ProUIInputpanelActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.value_filter_input_comp),
        OnBomFilterChanged,
        &runtime);
    ProUIOptionmenuSelectActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.create_type_comp),
        OnCreateTypeChanged,
        &runtime);
    ProUITableSelectActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_list_comp),
        OnParamListSelect,
        &runtime);
    ProUITableSelectActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        OnTableSelect,
        &runtime);
    ProUITableActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.bom_table_comp),
        OnTableSelect,
        &runtime);
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.cancel_comp),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_cancel)));
    ProUIDialogCloseActionSet(
        const_cast<char *>(config.dialog_inst_name),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_cancel)));
    ProUIDialogDefaultbuttonSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.refresh_comp));

    bool return_value = false;
    bool done = false;
    while (!done) {
        RenderBomDialogContents(dialog_state, render_config, render_callbacks);
        SaveSelectedAvailableParam(dialog_state, config.dialog_inst_name, config.param_list_comp);

        int dlg_status = config.status_cancel;
        const ProError st_act = ProUIDialogActivate(
            const_cast<char *>(config.dialog_inst_name),
            &dlg_status);
        CaptureBomDialogUiState(dialog_state, config, interaction_config, interaction_callbacks, callbacks);

        if (st_act != PRO_TK_NO_ERROR || dlg_status == config.status_cancel) {
            action = BomToolDialogAction::Close;
            return_value = false;
            done = true;
            continue;
        }

        if (dlg_status == config.status_add) {
            std::wstring error_text;
            std::wstring warning_text;
            if (!callbacks.handle_add_action || !callbacks.handle_add_action(state, error_text, warning_text)) {
                if (!error_text.empty()) {
                    ShowSimpleMessageDialog(PROUIMESSAGE_ERROR, L"BOM清单", error_text.c_str());
                }
            } else if (!warning_text.empty()) {
                ShowSimpleMessageDialog(PROUIMESSAGE_WARNING, L"BOM清单", warning_text.c_str());
            }
            continue;
        }

        if (dlg_status == config.status_move_left) {
            std::wstring error_text;
            if (!callbacks.handle_move_left_action ||
                !callbacks.handle_move_left_action(state, error_text)) {
                if (!error_text.empty()) {
                    ShowSimpleMessageDialog(PROUIMESSAGE_INFO, L"BOM娓呭崟", error_text.c_str());
                }
            }
            continue;
        }

        if (dlg_status == config.status_move_right) {
            std::wstring error_text;
            if (!callbacks.handle_move_right_action ||
                !callbacks.handle_move_right_action(state, error_text)) {
                if (!error_text.empty()) {
                    ShowSimpleMessageDialog(PROUIMESSAGE_INFO, L"BOM娓呭崟", error_text.c_str());
                }
            }
            continue;
        }

        if (dlg_status == config.status_refresh) {
            if (callbacks.handle_refresh_action) {
                callbacks.handle_refresh_action(state);
            }
            continue;
        }

        if (dlg_status == config.status_rebuild) {
            if (callbacks.handle_rebuild_action) {
                callbacks.handle_rebuild_action(state);
            }
            continue;
        }

        if (dlg_status == config.status_delete_param) {
            std::wstring error_text;
            if (!callbacks.handle_delete_param_action ||
                !callbacks.handle_delete_param_action(state, dialog_state.selected_available_param_name, error_text)) {
                if (!error_text.empty()) {
                    ShowSimpleMessageDialog(PROUIMESSAGE_INFO, L"BOM清单", error_text.c_str());
                }
            }
            continue;
        }

        if (dlg_status == config.status_update_param) {
            std::wstring error_text;
            if (!callbacks.handle_update_param_action ||
                !callbacks.handle_update_param_action(state, dialog_state.selected_available_param_name, error_text)) {
                if (!error_text.empty()) {
                    ShowSimpleMessageDialog(PROUIMESSAGE_INFO, L"BOM清单", error_text.c_str());
                }
            }
            continue;
        }

        if (dlg_status == config.status_update) {
            action = BomToolDialogAction::UpdateModel;
            return_value = true;
            done = true;
            continue;
        }

        if (dlg_status == config.status_export) {
            action = BomToolDialogAction::ExportCsv;
            return_value = true;
            done = true;
            continue;
        }

        action = BomToolDialogAction::Close;
        return_value = false;
        done = true;
    }

    g_active_bom_runtime = nullptr;
    ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
    return return_value;
}

} // namespace autobbox::ui
