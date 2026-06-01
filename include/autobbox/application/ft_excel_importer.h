#pragma once
#include "autobbox/core/family_table_types.h"
#include <string>
namespace autobbox::application {
bool ImportFtExcelToWorkspace(const std::wstring &file_path, core::FtWorkspace &workspace, std::wstring &error_out);
}
