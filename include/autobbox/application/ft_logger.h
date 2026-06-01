#pragma once
#include "autobbox/core/family_table_types.h"
#include <string>
namespace autobbox::application {
void FtLog(core::FtWorkspace &workspace, const std::wstring &level_path, const std::wstring &severity, const std::wstring &operation, const std::wstring &message, ProError status = PRO_TK_NO_ERROR);
void FtLog(core::FtWorkspace *workspace, const std::wstring &level_path, const std::wstring &severity, const std::wstring &operation, const std::wstring &message, ProError status = PRO_TK_NO_ERROR);
std::wstring BuildFtLogText(const core::FtWorkspace &workspace);
}
