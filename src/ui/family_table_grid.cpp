#include "autobbox/ui/family_table_grid.h"

#include <ProUI.h>
#include <ProUIDialog.h>
#include <ProUIInputpanel.h>
#include <ProUITable.h>
#include <ProUtil.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace autobbox::ui {
namespace {

std::string ColName(size_t i)
{
    return "C" + std::to_string(i);
}

std::wstring CellText(const core::FtRow &row, const std::wstring &key)
{
    for (const auto &cell : row.cells) if (cell.column_key == key) return cell.value;
    return L"";
}

core::FtCell *FindCell(core::FtRow &row, const std::wstring &key)
{
    for (auto &cell : row.cells) if (cell.column_key == key) return &cell;
    return nullptr;
}

bool IsEditableKey(const core::FtLevelNode &level, const std::wstring &key, core::FtRowKind row_kind)
{
    if (row_kind == core::FtRowKind::Generic) return false;
    for (const auto &col : level.columns) {
        if (col.column_key == key) return col.editable && col.support_status == core::FtSupportStatus::Full && key != L"ROW_KIND" && key != L"VERIFY_STATUS" && key != L"IS_EXT_LOCKED" && key != L"SUPPORT_STATUS";
    }
    return false;
}

ProUIColor DiffColor(const core::FtCell *cell, const core::FtRow &row)
{
    if (row.action == core::FtRowAction::Delete || row.change_kind == core::FtChangeKind::Delete) return PRO_UI_COLOR_RGB(255, 210, 210);
    if (row.action == core::FtRowAction::New || row.change_kind == core::FtChangeKind::New) return PRO_UI_COLOR_RGB(210, 255, 210);
    if (cell != nullptr && (cell->changed || cell->change_kind == core::FtChangeKind::Modify)) return PRO_UI_COLOR_YELLOW;
    if (row.row_kind == core::FtRowKind::Generic) return PRO_UI_COLOR_LT_GREY;
    return PRO_UI_COLOR_WHITE;
}

} // namespace

bool HarvestFamilyTableGridEditor(char *dialog, const char *, core::FtLevelNode &level, FtGridRenderState &state)
{
    bool ok = true;
    state.renamed_instances.clear();
    auto harvest_one = [&](const std::string &component_name, const std::string &row_name, const std::string &column_name) {
        auto row_it = state.row_index_by_name.find(row_name);
        auto col_it = state.column_key_by_name.find(column_name);
        if (row_it == state.row_index_by_name.end() || col_it == state.column_key_by_name.end() || row_it->second >= level.rows.size()) {
            ok = false;
            return;
        }
        wchar_t *value = nullptr;
        if (ProUIInputpanelValueGet(dialog, const_cast<char *>(component_name.c_str()), &value) != PRO_TK_NO_ERROR || value == nullptr) {
            return;
        }
        core::FtRow &row = level.rows[row_it->second];
        core::FtCell *cell = FindCell(row, col_it->second);
        if (cell != nullptr) {
            std::wstring new_value(value);
            if (cell->value != new_value) {
                const std::wstring old_instance_name = row.instance_name;
                cell->value = new_value;
                cell->changed = true;
                cell->change_kind = core::FtChangeKind::Modify;
                if (row.action == core::FtRowAction::Keep) row.action = core::FtRowAction::Modify;
                if (col_it->second == L"INSTANCE_NAME") {
                    row.instance_name = new_value;
                    state.renamed_instances.emplace_back(old_instance_name, new_value);
                }
                if (col_it->second == L"COMMON_NAME") row.common_name = new_value;
                if (col_it->second == L"IS_LOCKED") row.is_locked = (new_value == L"TRUE" || new_value == L"1");
            }
        }
        ProWstringFree(value);
    };

    if (!state.active_component_name.empty() && !state.active_row_name.empty() && !state.active_column_name.empty()) {
        harvest_one(state.active_component_name, state.active_row_name, state.active_column_name);
    }

    state.editor_cell_by_component.clear();
    state.active_component_name.clear();
    state.active_row_name.clear();
    state.active_column_name.clear();
    return ok;
}

bool ActivateFamilyTableGridEditor(char *dialog,
                                   const char *table_comp,
                                   const char *cell_input_base_comp,
                                   core::FtLevelNode &level,
                                   FtGridRenderState &state,
                                   const std::string &row_name,
                                   const std::string &column_name)
{
    auto row_it = state.row_index_by_name.find(row_name);
    auto col_it = state.column_key_by_name.find(column_name);
    if (row_it == state.row_index_by_name.end() || col_it == state.column_key_by_name.end() || row_it->second >= level.rows.size()) return false;
    core::FtRow &row = level.rows[row_it->second];
    if (!IsEditableKey(level, col_it->second, row.row_kind)) return false;

    ++state.editor_serial;
    char input_name[128] = {0};
    std::snprintf(input_name, sizeof(input_name), "ftinp_%d_%s_%s", state.editor_serial, row_name.c_str(), column_name.c_str());
    ProUITableCellComponentDelete(dialog, const_cast<char *>(table_comp), const_cast<char *>(row_name.c_str()), const_cast<char *>(column_name.c_str()));
    ProUITableCellComponentCopy(dialog, const_cast<char *>(table_comp), const_cast<char *>(row_name.c_str()), const_cast<char *>(column_name.c_str()), dialog, const_cast<char *>(cell_input_base_comp), input_name);
    ProUIInputpanelColumnsSet(dialog, input_name, 24);
    std::wstring value = CellText(row, col_it->second);
    ProUIInputpanelValueSet(dialog, input_name, const_cast<wchar_t *>(value.c_str()));
    state.active_row_name = row_name;
    state.active_column_name = column_name;
    state.active_component_name = input_name;
    ProUIDialogFocusSet(dialog, input_name);
    return true;
}

bool RenderFamilyTableGrid(char *dialog,
                           const char *table_comp,
                           const char *cell_input_base_comp,
                           core::FtLevelNode &level,
                           FtGridRenderState &state,
                           const std::wstring &filter_text)
{
    state.row_index_by_name.clear();
    state.column_key_by_name.clear();
    state.editor_cell_by_component.clear();
    state.active_component_name.clear();
    state.active_row_name.clear();
    state.active_column_name.clear();

    std::vector<std::string> col_names_storage;
    std::vector<std::wstring> col_labels_storage;
    std::vector<int> widths;
    for (size_t i = 0; i < level.columns.size(); ++i) {
        if (!level.columns[i].visible) continue;
        std::string name = ColName(col_names_storage.size());
        state.column_key_by_name[name] = level.columns[i].column_key;
        col_names_storage.push_back(name);
        std::wstring label = level.columns[i].column_display_name.empty() ? level.columns[i].column_key : level.columns[i].column_display_name;
        if (level.columns[i].support_status != core::FtSupportStatus::Full) label += L" [" + std::wstring(core::FtSupportStatusName(level.columns[i].support_status)) + L"]";
        col_labels_storage.push_back(label);
        widths.push_back(level.columns[i].column_category == core::FtColumnCategory::Fixed ? 14 : 20);
    }

    std::vector<char *> col_names;
    std::vector<wchar_t *> col_labels;
    for (auto &s : col_names_storage) col_names.push_back(const_cast<char *>(s.c_str()));
    for (auto &s : col_labels_storage) col_labels.push_back(const_cast<wchar_t *>(s.c_str()));

    std::vector<std::string> row_names_storage;
    std::vector<wchar_t *> row_labels;
    for (size_t i = 0; i < level.rows.size(); ++i) {
        const auto &row = level.rows[i];
        if (!filter_text.empty() && row.instance_name.find(filter_text) == std::wstring::npos) continue;
        std::string rn = "R" + std::to_string(row_names_storage.size());
        state.row_index_by_name[rn] = i;
        row_names_storage.push_back(rn);
        row_labels.push_back(const_cast<wchar_t *>(L""));
    }
    std::vector<char *> row_names;
    for (auto &s : row_names_storage) row_names.push_back(const_cast<char *>(s.c_str()));

    ProUITableColumnnamesSet(dialog, const_cast<char *>(table_comp), static_cast<int>(col_names.size()), col_names.empty() ? nullptr : col_names.data());
    ProUITableColumnlabelsSet(dialog, const_cast<char *>(table_comp), static_cast<int>(col_labels.size()), col_labels.empty() ? nullptr : col_labels.data());
    ProUITableColumnwidthsSet(dialog, const_cast<char *>(table_comp), static_cast<int>(widths.size()), widths.empty() ? nullptr : widths.data());
    ProUITableRownamesSet(dialog, const_cast<char *>(table_comp), static_cast<int>(row_names.size()), row_names.empty() ? nullptr : row_names.data());
    ProUITableRowlabelsSet(dialog, const_cast<char *>(table_comp), static_cast<int>(row_labels.size()), row_labels.empty() ? nullptr : row_labels.data());
    // Native-like editing keeps only INSTANCE_NAME frozen. The other status
    // fields remain in the internal model/logs but are hidden from the sheet.
    ProUITableLockedcolumnsSet(dialog, const_cast<char *>(table_comp), std::min(1, static_cast<int>(col_names.size())));
    ProUITableSelectionpolicySet(dialog, const_cast<char *>(table_comp), PROUISELPOLICY_SINGLE);
    ProUITableColumnselectionpolicySet(dialog, const_cast<char *>(table_comp), PROUISELPOLICY_SINGLE);
    ProUITableVisiblerowsSet(dialog, const_cast<char *>(table_comp), std::min(22, std::max(6, static_cast<int>(row_names.size()) + 1)));
    ProUITableMinrowsSet(dialog, const_cast<char *>(table_comp), std::min(18, std::max(6, static_cast<int>(row_names.size()))));
    ProUITableShowgridSet(dialog, const_cast<char *>(table_comp), PRO_B_TRUE);
    ProUITableAutohighlightEnable(dialog, const_cast<char *>(table_comp));
    ProUITableActivateonreturnEnable(dialog, const_cast<char *>(table_comp));

    for (const auto &rn : row_names_storage) {
        size_t row_index = state.row_index_by_name[rn];
        core::FtRow &row = level.rows[row_index];
        for (const auto &cn : col_names_storage) {
            const std::wstring key = state.column_key_by_name[cn];
            core::FtCell *cell = FindCell(row, key);
            ProUITableCellComponentDelete(dialog, const_cast<char *>(table_comp), const_cast<char *>(rn.c_str()), const_cast<char *>(cn.c_str()));
            const std::wstring value = CellText(row, key);
            ProUITableCellLabelSet(dialog, const_cast<char *>(table_comp), const_cast<char *>(rn.c_str()), const_cast<char *>(cn.c_str()), const_cast<wchar_t *>(value.c_str()));
            ProUITableCellBackgroundColorSet(dialog, const_cast<char *>(table_comp), const_cast<char *>(rn.c_str()), const_cast<char *>(cn.c_str()), DiffColor(cell, row));
            if (cell != nullptr && !cell->editable) ProUITableCellForegroundColorSet(dialog, const_cast<char *>(table_comp), const_cast<char *>(rn.c_str()), const_cast<char *>(cn.c_str()), PRO_UI_COLOR_DK_GREY);
        }
    }
    return true;
}

} // namespace autobbox::ui
