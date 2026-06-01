#pragma once
#include "autobbox/core/family_table_types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>
namespace autobbox::ui {
struct FtGridRenderState {
    std::unordered_map<std::string, size_t> row_index_by_name;
    std::unordered_map<std::string, std::wstring> column_key_by_name;
    std::unordered_map<std::string, std::pair<std::string, std::string>> editor_cell_by_component;
    std::string active_row_name;
    std::string active_column_name;
    std::string active_component_name;
    std::vector<std::pair<std::wstring, std::wstring>> renamed_instances;
    int editor_serial = 0;
};
bool RenderFamilyTableGrid(char *dialog, const char *table_comp, const char *cell_input_base_comp, core::FtLevelNode &level, FtGridRenderState &state, const std::wstring &filter_text = L"");
bool HarvestFamilyTableGridEditor(char *dialog, const char *table_comp, core::FtLevelNode &level, FtGridRenderState &state);
bool ActivateFamilyTableGridEditor(char *dialog, const char *table_comp, const char *cell_input_base_comp, core::FtLevelNode &level, FtGridRenderState &state, const std::string &row_name, const std::string &column_name);
}
