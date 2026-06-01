#pragma once
#include "autobbox/core/family_table_types.h"
#include <string>
#include <vector>
namespace autobbox::application {
bool ValidateFtWorkspaceForApply(const core::FtWorkspace &workspace, std::vector<std::wstring> &issues);
}
