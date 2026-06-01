#include "autobbox/ui/quick_simprep_dialog.h"

#include "autobbox/application/quick_simprep.h"
#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/ui/message_dialog.h"

#include <ProUICheckbutton.h>
#include <ProUIDialog.h>
#include <ProUIInputpanel.h>
#include <ProUILabel.h>
#include <ProUIPushbutton.h>
#include <ProUITable.h>
#include <ProToolkit.h>
#include <ProUI.h>
#include <ProUtil.h>
#include <ProArray.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace autobbox::ui {

namespace {

struct QuickSimprepDialogConfig {
    const char *dialog_inst_name = nullptr;
    const char *resource_base_name = nullptr;
    const char *summary_comp = nullptr;
    const char *rep_table_comp = nullptr;
    const char *category_table_comp = nullptr;
    const char *base_check_comp = nullptr;
    const char *refresh_comp = nullptr;
    const char *create_per_category_comp = nullptr;
    const char *create_merged_comp = nullptr;
    const char *update_current_comp = nullptr;
    const char *rename_input_comp = nullptr;
    const char *rename_rep_comp = nullptr;
    const char *close_comp = nullptr;
    int status_close = 0;
};

QuickSimprepDialogConfig DefaultQuickSimprepDialogConfig()
{
    QuickSimprepDialogConfig config = {};
    config.dialog_inst_name = "autobbox_quick_simprep_inst";
    config.resource_base_name = "autobbox_quick_simprep";
    config.summary_comp = "SummaryLabel";
    config.rep_table_comp = "ExistingRepTable";
    config.category_table_comp = "CategoryTable";
    config.base_check_comp = "BaseCategoryCheck";
    config.refresh_comp = "RefreshBtn";
    config.create_per_category_comp = "CreatePerCategoryBtn";
    config.create_merged_comp = "CreateMergedBtn";
    config.update_current_comp = "UpdateCurrentBtn";
    config.rename_input_comp = "RenameInput";
    config.rename_rep_comp = "RenameBtn";
    config.close_comp = "CloseBtn";
    config.status_close = 0;
    return config;
}

struct TableUiState {
    std::vector<int> column_widths;
    std::string selected_row;
    std::string focused_row;
    std::string focused_column;
};

struct QuickSimprepDialogState {
    core::QuickSimprepCollectResult *collect_result = nullptr;
    core::QuickSimprepExistingRepsResult existing_reps_result;
    int active_rep_index = -1;
    std::unordered_map<std::string, std::string> checkbox_component_by_item_name;
    int checkbox_render_serial = 0;
    TableUiState rep_table_ui_state;
    TableUiState category_table_ui_state;
};

struct QuickSimprepDialogRuntime {
    QuickSimprepDialogState *state = nullptr;
    const QuickSimprepDialogConfig *config = nullptr;
    QuickSimprepDialogLogSink log_sink;
};

void PopulateExistingRepTable(char *dialog, QuickSimprepDialogState *state, const QuickSimprepDialogConfig &config);
void PopulateCategoryTable(char *dialog, QuickSimprepDialogState *state, const QuickSimprepDialogConfig &config);
std::wstring BuildCreateDialogFailureText(const std::string &used_resource, ProError status);

void LogLine(const QuickSimprepDialogLogSink &log_sink, const char *fmt, ...)
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

std::wstring BuildCreateDialogFailureText(const std::string &used_resource, ProError status)
{
    std::wstring text = L"\u6253\u5f00\u5feb\u901f\u7b80\u5316\u8868\u793a\u5bf9\u8bdd\u6846\u5931\u8d25\u3002";
    text += L"\nstatus=" + std::to_wstring(static_cast<int>(status));
    text += L"\nresource=";
    text += autobbox::common::AToW(used_resource.c_str());
    return text;
}

void OnDialogExit(char *dialog, char *, ProAppData app_data)
{
    if (dialog != nullptr) {
        ProUIDialogExit(dialog, static_cast<int>(reinterpret_cast<std::intptr_t>(app_data)));
    }
}

void SetLabelText(char *dialog, const char *component, const wchar_t *text)
{
    if (dialog != nullptr && component != nullptr) {
        ProUILabelTextSet(dialog, const_cast<char *>(component), const_cast<wchar_t *>(text == nullptr ? L"" : text));
    }
}

void SetButtonText(char *dialog, const char *component, const wchar_t *text)
{
    if (dialog != nullptr && component != nullptr) {
        ProUIPushbuttonTextSet(dialog, const_cast<char *>(component), const_cast<wchar_t *>(text == nullptr ? L"" : text));
    }
}

void SetInputValue(char *dialog, const char *component, const std::wstring &value)
{
    if (dialog != nullptr && component != nullptr) {
        ProUIInputpanelValueSet(dialog, const_cast<char *>(component), const_cast<wchar_t *>(value.c_str()));
    }
}

void SaveTableUiState(char *dialog, const char *component, bool save_selected_row, TableUiState &state)
{
    state.column_widths.clear();
    state.selected_row.clear();
    state.focused_row.clear();
    state.focused_column.clear();

    if (dialog == nullptr || component == nullptr) {
        return;
    }

    int width_count = 0;
    int *width_values = nullptr;
    if (ProUITableColumnwidthsGet(dialog, const_cast<char *>(component), &width_count, &width_values) == PRO_TK_NO_ERROR &&
        width_count > 0 &&
        width_values != nullptr) {
        state.column_widths.assign(width_values, width_values + width_count);
        ProArray widths_array = width_values;
        ProArrayFree(&widths_array);
    }

    char *focus_row = nullptr;
    char *focus_column = nullptr;
    if (ProUITableFocusCellGet(dialog, const_cast<char *>(component), &focus_row, &focus_column) == PRO_TK_NO_ERROR) {
        if (focus_row != nullptr) {
            state.focused_row = focus_row;
            ProStringFree(focus_row);
        }
        if (focus_column != nullptr) {
            state.focused_column = focus_column;
            ProStringFree(focus_column);
        }
    } else {
        if (focus_row != nullptr) {
            ProStringFree(focus_row);
        }
        if (focus_column != nullptr) {
            ProStringFree(focus_column);
        }
    }

    if (!save_selected_row) {
        return;
    }

    int selected_count = 0;
    char **selected_rows = nullptr;
    if (ProUITableSelectedrownamesGet(dialog, const_cast<char *>(component), &selected_count, &selected_rows) == PRO_TK_NO_ERROR &&
        selected_count > 0 &&
        selected_rows != nullptr &&
        selected_rows[0] != nullptr) {
        state.selected_row = selected_rows[0];
    }
    if (selected_rows != nullptr) {
        ProStringarrayFree(selected_rows, selected_count);
    }
}

void RestoreTableUiState(char *dialog, const char *component, const TableUiState &state, bool restore_selected_row)
{
    if (dialog == nullptr || component == nullptr) {
        return;
    }

    if (!state.column_widths.empty()) {
        std::vector<int> widths = state.column_widths;
        ProUITableColumnwidthsSet(dialog,
                                  const_cast<char *>(component),
                                  static_cast<int>(widths.size()),
                                  widths.data());
    }

    if (restore_selected_row && !state.selected_row.empty()) {
        char *selected_row = const_cast<char *>(state.selected_row.c_str());
        ProUITableSelectedrownamesSet(dialog, const_cast<char *>(component), 1, &selected_row);
    }

    if (!state.focused_row.empty() && !state.focused_column.empty()) {
        ProUITableFocusCellSet(dialog,
                               const_cast<char *>(component),
                               const_cast<char *>(state.focused_row.c_str()),
                               const_cast<char *>(state.focused_column.c_str()));
    }
}

std::wstring GetInputValue(char *dialog, const char *component)
{
    std::wstring result;
    if (dialog == nullptr || component == nullptr) {
        return result;
    }

    wchar_t *raw = nullptr;
    if (ProUIInputpanelValueGet(dialog, const_cast<char *>(component), &raw) == PRO_TK_NO_ERROR && raw != nullptr) {
        result.assign(raw);
        ProWstringFree(raw);
    }
    return result;
}

std::wstring BuildSummaryText(const core::QuickSimprepCollectResult &collect_result,
                              const core::QuickSimprepExistingRepsResult &reps_result)
{
    return autobbox::application::BuildQuickSimprepCollectSummary(collect_result) +
           L"  \u5df2\u6709\u7b80\u5316\u8868\u793a\uff1a" + std::to_wstring(reps_result.total_count) +
           L"  \u5de6\u4fa7\u9009\u8868\u793a\uff0c\u53f3\u4fa7\u52fe\u9009\u5206\u7c7b\uff1b\u53ef\u65b0\u5efa\u6216\u66f4\u65b0\u5f53\u524d\u8868\u793a";
}

void ApplyChineseDialogText(const QuickSimprepDialogConfig &config,
                            const core::QuickSimprepCollectResult &collect_result,
                            const core::QuickSimprepExistingRepsResult &reps_result)
{
    char *dialog = const_cast<char *>(config.dialog_inst_name);
    ProUIDialogTitleSet(dialog, const_cast<wchar_t *>(L"\u5feb\u901f\u7b80\u5316\u8868\u793a"));
    SetLabelText(dialog, config.summary_comp, BuildSummaryText(collect_result, reps_result).c_str());
    SetButtonText(dialog, config.refresh_comp, L"\u5237\u65b0");
    SetButtonText(dialog, config.create_per_category_comp, L"\u6309\u5206\u7c7b\u65b0\u5efa");
    SetButtonText(dialog, config.create_merged_comp, L"\u5408\u5e76\u65b0\u5efa");
    SetButtonText(dialog, config.update_current_comp, L"\u66f4\u65b0\u5f53\u524d\u8868\u793a");
    SetButtonText(dialog, config.rename_rep_comp, L"\u91cd\u547d\u540d");
    SetButtonText(dialog, config.close_comp, L"\u5173\u95ed");
}

std::vector<core::QuickSimprepCategory> *ActiveCategories(QuickSimprepDialogState *state)
{
    if (state == nullptr) {
        return nullptr;
    }
    if (state->active_rep_index >= 0 &&
        static_cast<size_t>(state->active_rep_index) < state->existing_reps_result.reps.size()) {
        return &state->existing_reps_result.reps[static_cast<size_t>(state->active_rep_index)].categories;
    }
    return state->collect_result == nullptr ? nullptr : &state->collect_result->categories;
}

const std::vector<core::QuickSimprepCategory> *ActiveCategories(const QuickSimprepDialogState *state)
{
    return ActiveCategories(const_cast<QuickSimprepDialogState *>(state));
}

core::QuickSimprepExistingRep *ActiveRep(QuickSimprepDialogState *state)
{
    if (state == nullptr || state->active_rep_index < 0 ||
        static_cast<size_t>(state->active_rep_index) >= state->existing_reps_result.reps.size()) {
        return nullptr;
    }
    return &state->existing_reps_result.reps[static_cast<size_t>(state->active_rep_index)];
}

void ChooseActiveRep(QuickSimprepDialogState *state, const std::wstring &preferred_name)
{
    if (state == nullptr || state->existing_reps_result.reps.empty()) {
        if (state != nullptr) {
            state->active_rep_index = -1;
        }
        return;
    }

    if (!preferred_name.empty()) {
        for (size_t i = 0; i < state->existing_reps_result.reps.size(); ++i) {
            if (state->existing_reps_result.reps[i].rep_name == preferred_name) {
                state->active_rep_index = static_cast<int>(i);
                return;
            }
        }
    }

    for (size_t i = 0; i < state->existing_reps_result.reps.size(); ++i) {
        if (state->existing_reps_result.reps[i].is_active) {
            state->active_rep_index = static_cast<int>(i);
            return;
        }
    }

    state->active_rep_index = 0;
}

void SyncCategorySelectionFromDialog(char *dialog, QuickSimprepDialogState *state)
{
    auto *categories = ActiveCategories(state);
    if (dialog == nullptr || categories == nullptr) {
        return;
    }

    for (core::QuickSimprepCategory &category : *categories) {
        const auto it = state->checkbox_component_by_item_name.find(category.item_name);
        if (it == state->checkbox_component_by_item_name.end()) {
            continue;
        }
        ProBoolean checked = PRO_B_FALSE;
        if (ProUICheckbuttonGetState(dialog, const_cast<char *>(it->second.c_str()), &checked) == PRO_TK_NO_ERROR) {
            category.selected = (checked == PRO_B_TRUE);
        }
    }
}

std::vector<std::wstring> SelectedCategoryNames(char *dialog, QuickSimprepDialogState *state)
{
    SyncCategorySelectionFromDialog(dialog, state);
    std::vector<std::wstring> names;
    auto *categories = ActiveCategories(state);
    if (categories == nullptr) {
        return names;
    }
    for (const core::QuickSimprepCategory &category : *categories) {
        if (category.selected) {
            names.push_back(category.common_name);
        }
    }
    return names;
}

void PopulateExistingRepTable(char *dialog, QuickSimprepDialogState *state, const QuickSimprepDialogConfig &config)
{
    if (dialog == nullptr || state == nullptr) {
        return;
    }

    std::vector<std::string> col_names_storage = {"ACTIVE", "ENAME", "EITEMS"};
    std::vector<std::wstring> col_labels_storage = {L"\u5f53\u524d", L"\u7b80\u5316\u8868\u793a", L"\u5305\u542b"};
    std::vector<int> col_resizings = {0, 4, 0};

    std::vector<char *> col_names;
    std::vector<wchar_t *> col_labels;
    for (std::string &name : col_names_storage) {
        col_names.push_back(const_cast<char *>(name.c_str()));
    }
    for (std::wstring &label : col_labels_storage) {
        col_labels.push_back(const_cast<wchar_t *>(label.c_str()));
    }

    std::vector<std::string> row_names_storage;
    std::vector<char *> row_names;
    std::vector<wchar_t *> row_labels;
    row_names_storage.reserve(state->existing_reps_result.reps.size());
    for (size_t i = 0; i < state->existing_reps_result.reps.size(); ++i) {
        row_names_storage.push_back("rep_" + std::to_string(i));
    }
    for (std::string &name : row_names_storage) {
        row_names.push_back(const_cast<char *>(name.c_str()));
        row_labels.push_back(const_cast<wchar_t *>(L""));
    }

    ProUITableColumnnamesSet(dialog, const_cast<char *>(config.rep_table_comp), static_cast<int>(col_names.size()), col_names.data());
    ProUITableColumnlabelsSet(dialog, const_cast<char *>(config.rep_table_comp), static_cast<int>(col_labels.size()), col_labels.data());
    ProUITableColumnresizingsSet(dialog, const_cast<char *>(config.rep_table_comp), static_cast<int>(col_resizings.size()), col_resizings.data());
    ProUITableRownamesSet(dialog, const_cast<char *>(config.rep_table_comp), static_cast<int>(row_names.size()), row_names.empty() ? nullptr : row_names.data());
    ProUITableRowlabelsSet(dialog, const_cast<char *>(config.rep_table_comp), static_cast<int>(row_labels.size()), row_labels.empty() ? nullptr : row_labels.data());
    ProUITableSelectionpolicySet(dialog, const_cast<char *>(config.rep_table_comp), PROUISELPOLICY_SINGLE);
    ProUITableColumnselectionpolicySet(dialog, const_cast<char *>(config.rep_table_comp), PROUISELPOLICY_NONE);
    ProUITableVisiblerowsSet(dialog, const_cast<char *>(config.rep_table_comp), std::min(14, std::max(4, static_cast<int>(state->existing_reps_result.reps.size()))));
    ProUITableMinrowsSet(dialog, const_cast<char *>(config.rep_table_comp), std::min(14, std::max(4, static_cast<int>(state->existing_reps_result.reps.size()))));
    ProUITableShowgridSet(dialog, const_cast<char *>(config.rep_table_comp), PRO_B_TRUE);
    ProUITableLockedcolumnsSet(dialog, const_cast<char *>(config.rep_table_comp), 0);

    for (size_t i = 0; i < state->existing_reps_result.reps.size(); ++i) {
        const core::QuickSimprepExistingRep &rep = state->existing_reps_result.reps[i];
        const char *rn = row_names_storage[i].c_str();
        const std::wstring active = rep.is_active ? L"\u25cf" : L"";
        const std::wstring count = std::to_wstring(rep.item_count);
        ProUITableCellLabelSet(dialog, const_cast<char *>(config.rep_table_comp), const_cast<char *>(rn), const_cast<char *>("ACTIVE"), const_cast<wchar_t *>(active.c_str()));
        ProUITableCellLabelSet(dialog, const_cast<char *>(config.rep_table_comp), const_cast<char *>(rn), const_cast<char *>("ENAME"), const_cast<wchar_t *>(rep.rep_name.c_str()));
        ProUITableCellLabelSet(dialog, const_cast<char *>(config.rep_table_comp), const_cast<char *>(rn), const_cast<char *>("EITEMS"), const_cast<wchar_t *>(count.c_str()));
    }

    if (state->active_rep_index >= 0 && static_cast<size_t>(state->active_rep_index) < row_names_storage.size()) {
        char *selected = const_cast<char *>(row_names_storage[static_cast<size_t>(state->active_rep_index)].c_str());
        ProUITableSelectedrownamesSet(dialog, const_cast<char *>(config.rep_table_comp), 1, &selected);
    }

    RestoreTableUiState(dialog, config.rep_table_comp, state->rep_table_ui_state, true);
}

void PopulateCategoryTable(char *dialog, QuickSimprepDialogState *state, const QuickSimprepDialogConfig &config)
{
    if (dialog == nullptr || state == nullptr) {
        return;
    }

    const auto *categories = ActiveCategories(state);
    if (categories == nullptr) {
        return;
    }

    ++state->checkbox_render_serial;
    state->checkbox_component_by_item_name.clear();

    std::vector<std::string> column_names_storage = {"USE", "COMMON", "QTY", "STATUS"};
    std::vector<std::wstring> column_labels_storage = {L"\u5305\u542b", L"PTC_COMMON_NAME", L"\u7ec4\u4ef6", L"\u72b6\u6001"};
    std::vector<int> column_resizings = {0, 4, 0, 2};

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
    row_names.reserve(categories->size());
    row_labels.reserve(categories->size());
    for (const core::QuickSimprepCategory &category : *categories) {
        row_names.push_back(const_cast<char *>(category.item_name.c_str()));
        row_labels.push_back(const_cast<wchar_t *>(L""));
    }

    ProUITableColumnnamesSet(dialog, const_cast<char *>(config.category_table_comp), static_cast<int>(column_names.size()), column_names.data());
    ProUITableColumnlabelsSet(dialog, const_cast<char *>(config.category_table_comp), static_cast<int>(column_labels.size()), column_labels.data());
    ProUITableColumnresizingsSet(dialog, const_cast<char *>(config.category_table_comp), static_cast<int>(column_resizings.size()), column_resizings.data());
    ProUITableRownamesSet(dialog, const_cast<char *>(config.category_table_comp), static_cast<int>(row_names.size()), row_names.empty() ? nullptr : row_names.data());
    ProUITableRowlabelsSet(dialog, const_cast<char *>(config.category_table_comp), static_cast<int>(row_labels.size()), row_labels.empty() ? nullptr : row_labels.data());
    ProUITableSelectionpolicySet(dialog, const_cast<char *>(config.category_table_comp), PROUISELPOLICY_NONE);
    ProUITableColumnselectionpolicySet(dialog, const_cast<char *>(config.category_table_comp), PROUISELPOLICY_NONE);
    ProUITableVisiblerowsSet(dialog, const_cast<char *>(config.category_table_comp), std::min(14, std::max(4, static_cast<int>(categories->size()))));
    ProUITableMinrowsSet(dialog, const_cast<char *>(config.category_table_comp), std::min(14, std::max(4, static_cast<int>(categories->size()))));
    ProUITableShowgridSet(dialog, const_cast<char *>(config.category_table_comp), PRO_B_TRUE);
    ProUITableLockedcolumnsSet(dialog, const_cast<char *>(config.category_table_comp), 0);

    for (const core::QuickSimprepCategory &category : *categories) {
        char check_name[96] = {0};
        std::snprintf(check_name, sizeof(check_name), "qsrchk_%d_%s", state->checkbox_render_serial, category.item_name.c_str());
        state->checkbox_component_by_item_name[category.item_name] = check_name;

        ProUITableCellComponentCopy(
            dialog,
            const_cast<char *>(config.category_table_comp),
            const_cast<char *>(category.item_name.c_str()),
            const_cast<char *>("USE"),
            dialog,
            const_cast<char *>(config.base_check_comp),
            const_cast<char *>(state->checkbox_component_by_item_name[category.item_name].c_str()));
        ProUICheckbuttonTextSet(dialog, const_cast<char *>(state->checkbox_component_by_item_name[category.item_name].c_str()), const_cast<wchar_t *>(L""));
        if (category.selected) {
            ProUICheckbuttonSet(dialog, const_cast<char *>(state->checkbox_component_by_item_name[category.item_name].c_str()));
        } else {
            ProUICheckbuttonUnset(dialog, const_cast<char *>(state->checkbox_component_by_item_name[category.item_name].c_str()));
        }

        const std::wstring qty = std::to_wstring(static_cast<int>(category.occurrences.size()));
        const std::wstring status = category.status_text.empty() ? L"" : category.status_text;
        ProUITableCellLabelSet(dialog, const_cast<char *>(config.category_table_comp), const_cast<char *>(category.item_name.c_str()), const_cast<char *>("COMMON"), const_cast<wchar_t *>(category.common_name.c_str()));
        ProUITableCellLabelSet(dialog, const_cast<char *>(config.category_table_comp), const_cast<char *>(category.item_name.c_str()), const_cast<char *>("QTY"), const_cast<wchar_t *>(qty.c_str()));
        ProUITableCellLabelSet(dialog, const_cast<char *>(config.category_table_comp), const_cast<char *>(category.item_name.c_str()), const_cast<char *>("STATUS"), const_cast<wchar_t *>(status.c_str()));
        ProUITableCellHelptextStringSet(dialog, const_cast<char *>(config.category_table_comp), const_cast<char *>(category.item_name.c_str()), const_cast<char *>("COMMON"), const_cast<wchar_t *>(category.common_name.c_str()));
        ProUITableCellHelptextStringSet(dialog, const_cast<char *>(config.category_table_comp), const_cast<char *>(category.item_name.c_str()), const_cast<char *>("STATUS"), const_cast<wchar_t *>(status.c_str()));
    }

    RestoreTableUiState(dialog, config.category_table_comp, state->category_table_ui_state, false);
}

int SelectedRepIndexFromDialog(char *dialog, const QuickSimprepDialogConfig &config)
{
    int selected_index = -1;
    char *focus_row = nullptr;
    char *focus_col = nullptr;
    if (ProUITableFocusCellGet(dialog, const_cast<char *>(config.rep_table_comp), &focus_row, &focus_col) == PRO_TK_NO_ERROR &&
        focus_row != nullptr) {
        const std::string rn(focus_row);
        if (rn.rfind("rep_", 0) == 0) {
            selected_index = std::atoi(rn.c_str() + 4);
        }
    }
    if (focus_row != nullptr) {
        ProStringFree(focus_row);
    }
    if (focus_col != nullptr) {
        ProStringFree(focus_col);
    }
    if (selected_index >= 0) {
        return selected_index;
    }

    int sel_count = 0;
    char **sel_rows = nullptr;
    if (ProUITableSelectedrownamesGet(dialog, const_cast<char *>(config.rep_table_comp), &sel_count, &sel_rows) == PRO_TK_NO_ERROR &&
        sel_rows != nullptr) {
        for (int i = 0; i < sel_count; ++i) {
            if (sel_rows[i] == nullptr) {
                continue;
            }
            const std::string rn(sel_rows[i]);
            if (rn.rfind("rep_", 0) == 0) {
                selected_index = std::atoi(rn.c_str() + 4);
                break;
            }
        }
        ProStringarrayFree(sel_rows, sel_count);
    }
    return selected_index;
}

void RefreshAll(char *dialog,
                QuickSimprepDialogRuntime *runtime,
                const std::wstring &preferred_rep_name)
{
    if (dialog == nullptr || runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr ||
        runtime->state->collect_result == nullptr) {
        return;
    }

    SaveTableUiState(dialog, runtime->config->rep_table_comp, true, runtime->state->rep_table_ui_state);
    SaveTableUiState(dialog, runtime->config->category_table_comp, false, runtime->state->category_table_ui_state);

    *runtime->state->collect_result = autobbox::application::CollectQuickSimprepCategories();
    runtime->state->existing_reps_result = autobbox::application::EnumerateExistingSimpreps();
    ChooseActiveRep(runtime->state, preferred_rep_name);
    ApplyChineseDialogText(*runtime->config, *runtime->state->collect_result, runtime->state->existing_reps_result);
    PopulateExistingRepTable(dialog, runtime->state, *runtime->config);
    PopulateCategoryTable(dialog, runtime->state, *runtime->config);

    core::QuickSimprepExistingRep *active = ActiveRep(runtime->state);
    SetInputValue(dialog, runtime->config->rename_input_comp, active == nullptr ? L"" : active->rep_name);
}

void OnRepTableSelect(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<QuickSimprepDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return;
    }

    const int selected = SelectedRepIndexFromDialog(dialog, *runtime->config);
    if (selected >= 0 && static_cast<size_t>(selected) < runtime->state->existing_reps_result.reps.size()) {
        runtime->state->active_rep_index = selected;
        PopulateCategoryTable(dialog, runtime->state, *runtime->config);
        SetInputValue(dialog,
                      runtime->config->rename_input_comp,
                      runtime->state->existing_reps_result.reps[static_cast<size_t>(selected)].rep_name);
        const int checked_count = static_cast<int>(std::count_if(
            runtime->state->existing_reps_result.reps[static_cast<size_t>(selected)].categories.begin(),
            runtime->state->existing_reps_result.reps[static_cast<size_t>(selected)].categories.end(),
            [](const core::QuickSimprepCategory &category) { return category.selected; }));
        LogLine(runtime->log_sink,
                "quick-simprep-dialog select-rep index=%d name=%s checked_categories=%d item_count=%d",
                selected,
                autobbox::common::WToA(runtime->state->existing_reps_result.reps[static_cast<size_t>(selected)].rep_name.c_str()).c_str(),
                checked_count,
                runtime->state->existing_reps_result.reps[static_cast<size_t>(selected)].item_count);
    }
}

void OnRefresh(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<QuickSimprepDialogRuntime *>(app_data);
    std::wstring preferred;
    if (runtime != nullptr && runtime->state != nullptr) {
        core::QuickSimprepExistingRep *active = ActiveRep(runtime->state);
        if (active != nullptr) {
            preferred = active->rep_name;
        }
    }
    RefreshAll(dialog, runtime, preferred);
}

void OnSaveCurrent(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<QuickSimprepDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return;
    }

    core::QuickSimprepExistingRep *target = ActiveRep(runtime->state);
    if (target == nullptr) {
        ShowSimpleMessageDialog(PROUIMESSAGE_WARNING,
                                L"\u5feb\u901f\u7b80\u5316\u8868\u793a",
                                L"\u5de6\u4fa7\u6ca1\u6709\u53ef\u66f4\u65b0\u7684\u5df2\u6709\u7b80\u5316\u8868\u793a\u3002");
        return;
    }

    const std::wstring rep_name = target->rep_name;
    const std::vector<std::wstring> selected_names = SelectedCategoryNames(dialog, runtime->state);

    core::QuickSimprepManageSummary summary;
    const bool ok = autobbox::application::UpdateCategoriesInRep(
        *target,
        selected_names,
        summary,
        runtime->log_sink);

    RefreshAll(dialog, runtime, rep_name);
    ShowSimpleMessageDialog(ok ? PROUIMESSAGE_INFO : PROUIMESSAGE_ERROR,
                            L"\u5feb\u901f\u7b80\u5316\u8868\u793a",
                            summary.summary_text.empty()
                                ? (ok ? L"\u66f4\u65b0\u5b8c\u6210\u3002" : L"\u66f4\u65b0\u5931\u8d25\u3002")
                                : summary.summary_text.c_str());
}

void OnCreateQuickSimprep(char *dialog,
                          QuickSimprepDialogRuntime *runtime,
                          core::QuickSimprepCreateMode mode)
{
    if (dialog == nullptr || runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return;
    }

    auto *categories = ActiveCategories(runtime->state);
    if (categories == nullptr) {
        ShowSimpleMessageDialog(PROUIMESSAGE_WARNING,
                                L"\u5feb\u901f\u7b80\u5316\u8868\u793a",
                                L"\u6ca1\u6709\u53ef\u7528\u7684 PTC_COMMON_NAME \u5206\u7c7b\u3002");
        return;
    }

    SyncCategorySelectionFromDialog(dialog, runtime->state);
    std::vector<core::QuickSimprepCategory> create_categories = *categories;

    core::QuickSimprepCreateSummary summary;
    const bool ok = autobbox::application::CreateQuickSimpreps(
        create_categories,
        mode,
        summary,
        runtime->log_sink);

    RefreshAll(dialog, runtime, L"");
    ShowSimpleMessageDialog(ok ? PROUIMESSAGE_INFO : PROUIMESSAGE_ERROR,
                            L"\u5feb\u901f\u7b80\u5316\u8868\u793a",
                            summary.summary_text.empty()
                                ? (ok ? L"\u65b0\u5efa\u5b8c\u6210\u3002" : L"\u65b0\u5efa\u5931\u8d25\u3002")
                                : summary.summary_text.c_str());
}

void OnCreatePerCategory(char *dialog, char *, ProAppData app_data)
{
    OnCreateQuickSimprep(
        dialog,
        reinterpret_cast<QuickSimprepDialogRuntime *>(app_data),
        core::QuickSimprepCreateMode::PerCategory);
}

void OnCreateMerged(char *dialog, char *, ProAppData app_data)
{
    OnCreateQuickSimprep(
        dialog,
        reinterpret_cast<QuickSimprepDialogRuntime *>(app_data),
        core::QuickSimprepCreateMode::Merged);
}

void OnRenameRep(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<QuickSimprepDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return;
    }

    core::QuickSimprepExistingRep *target = ActiveRep(runtime->state);
    if (target == nullptr) {
        ShowSimpleMessageDialog(PROUIMESSAGE_WARNING,
                                L"\u5feb\u901f\u7b80\u5316\u8868\u793a",
                                L"\u8bf7\u5148\u5728\u5de6\u4fa7\u9009\u62e9\u4e00\u4e2a\u5df2\u6709\u7b80\u5316\u8868\u793a\u3002");
        return;
    }

    const std::wstring new_name = GetInputValue(dialog, runtime->config->rename_input_comp);
    core::QuickSimprepManageSummary summary;
    const bool ok = autobbox::application::RenameSimprep(
        *target,
        new_name,
        summary,
        runtime->log_sink);

    RefreshAll(dialog, runtime, ok ? new_name : target->rep_name);
    ShowSimpleMessageDialog(ok ? PROUIMESSAGE_INFO : PROUIMESSAGE_ERROR,
                            L"\u5feb\u901f\u7b80\u5316\u8868\u793a",
                            summary.summary_text.empty()
                                ? (ok ? L"\u91cd\u547d\u540d\u5b8c\u6210\u3002" : L"\u91cd\u547d\u540d\u5931\u8d25\u3002")
                                : summary.summary_text.c_str());
}

ProError TryCreateDialog(const QuickSimprepDialogConfig &config,
                         const QuickSimprepDialogLogSink &log_sink,
                         std::string &used_resource)
{
    used_resource.clear();
    if (config.dialog_inst_name == nullptr || config.resource_base_name == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    const std::string base_name = config.resource_base_name;
    const std::vector<std::string> rel_candidates = {
        base_name,
        base_name + ".res",
        std::string("resource\\") + base_name,
        std::string("resource\\") + base_name + ".res",
        std::string("text\\resource\\") + base_name,
        std::string("text\\resource\\") + base_name + ".res",
    };

    ProError last = PRO_TK_GENERAL_ERROR;
    for (const std::string &resource : rel_candidates) {
        last = ProUIDialogCreate(const_cast<char *>(config.dialog_inst_name), const_cast<char *>(resource.c_str()));
        LogLine(log_sink, "quick-simprep-dialog create try resource=%s status=%d", resource.c_str(), static_cast<int>(last));
        if (last == PRO_TK_NO_ERROR) {
            used_resource = resource;
            return last;
        }
    }

    ProPath text_root_w = {0};
    if (ProToolkitApplTextPathGet(text_root_w) == PRO_TK_NO_ERROR) {
        const std::string text_root = autobbox::common::WToA(text_root_w);
        const std::vector<std::string> abs_candidates = {
            text_root + "\\resource\\" + base_name,
            text_root + "\\resource\\" + base_name + ".res",
            text_root + "\\text\\resource\\" + base_name,
            text_root + "\\text\\resource\\" + base_name + ".res",
        };
        for (const std::string &path : abs_candidates) {
            if (!autobbox::common::FileExistsA(path)) {
                continue;
            }
            last = ProUIDialogCreate(const_cast<char *>(config.dialog_inst_name), const_cast<char *>(path.c_str()));
            LogLine(log_sink, "quick-simprep-dialog create try resource=%s status=%d", path.c_str(), static_cast<int>(last));
            if (last == PRO_TK_NO_ERROR) {
                used_resource = path;
                return last;
            }
        }
    }
    return last;
}

} // namespace

bool PromptQuickSimprepDialog(core::QuickSimprepCollectResult &collect_result,
                              bool &cancelled,
                              const QuickSimprepDialogLogSink &log_sink)
{
    cancelled = false;

    const QuickSimprepDialogConfig config = DefaultQuickSimprepDialogConfig();
    std::string used_resource;
    const ProError create_status = TryCreateDialog(config, log_sink, used_resource);
    if (create_status != PRO_TK_NO_ERROR) {
        ShowSimpleMessageDialog(
            PROUIMESSAGE_ERROR,
            L"\u5feb\u901f\u7b80\u5316\u8868\u793a",
            BuildCreateDialogFailureText(used_resource, create_status).c_str());
        cancelled = true;
        return false;
    }

    QuickSimprepDialogState state;
    state.collect_result = &collect_result;
    state.existing_reps_result = autobbox::application::EnumerateExistingSimpreps();
    ChooseActiveRep(&state, L"");

    QuickSimprepDialogRuntime runtime;
    runtime.state = &state;
    runtime.config = &config;
    runtime.log_sink = log_sink;

    char *dialog = const_cast<char *>(config.dialog_inst_name);
    ApplyChineseDialogText(config, collect_result, state.existing_reps_result);
    PopulateExistingRepTable(dialog, &state, config);
    PopulateCategoryTable(dialog, &state, config);
    core::QuickSimprepExistingRep *active = ActiveRep(&state);
    SetInputValue(dialog, config.rename_input_comp, active == nullptr ? L"" : active->rep_name);

    ProUITableSelectActionSet(dialog, const_cast<char *>(config.rep_table_comp), OnRepTableSelect, &runtime);
    ProUITableActivateActionSet(dialog, const_cast<char *>(config.rep_table_comp), OnRepTableSelect, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.refresh_comp), OnRefresh, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.create_per_category_comp), OnCreatePerCategory, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.create_merged_comp), OnCreateMerged, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.update_current_comp), OnSaveCurrent, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.rename_rep_comp), OnRenameRep, &runtime);
    ProUIPushbuttonActivateActionSet(
        dialog,
        const_cast<char *>(config.close_comp),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_close)));
    ProUIDialogCloseActionSet(
        dialog,
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_close)));

    int dialog_status = config.status_close;
    const ProError activate_status = ProUIDialogActivate(dialog, &dialog_status);
    if (activate_status != PRO_TK_NO_ERROR) {
        cancelled = true;
        ProUIDialogDestroy(dialog);
        return false;
    }

    cancelled = (dialog_status == config.status_close);
    ProUIDialogDestroy(dialog);
    return true;
}

} // namespace autobbox::ui
