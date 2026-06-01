#pragma once
#include "autobbox/core/family_table_types.h"
#include <string>

namespace autobbox::application {

struct FtAddColumnSpec {
    core::FtColumnCategory category = core::FtColumnCategory::Parameter;
    std::wstring object_name;
    int insert_index = -1;
};

bool AddFtColumn(core::FtLevelNode &level, const FtAddColumnSpec &spec, std::wstring &error_out);
bool AddFtParameterColumn(core::FtLevelNode &level, const std::wstring &param_name, int insert_index, std::wstring &error_out);
bool DeleteFtColumn(core::FtLevelNode &level, const std::wstring &column_key, std::wstring &error_out);
bool MoveFtColumn(core::FtLevelNode &level, const std::wstring &column_key, int new_index, std::wstring &error_out);

} // namespace autobbox::application
