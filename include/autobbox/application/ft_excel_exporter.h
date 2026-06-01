#pragma once
#include "autobbox/core/family_table_types.h"
#include <string>
namespace autobbox::application {
bool ExportFtCurrentLevelExcel(const core::FtWorkspace &workspace, std::wstring &file_path_out);
bool ExportFtAllLevelsExcel(const core::FtWorkspace &workspace, std::wstring &file_path_out);
}
