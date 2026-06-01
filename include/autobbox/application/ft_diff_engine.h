#pragma once
#include "autobbox/core/family_table_types.h"
namespace autobbox::application {
core::FtDiff BuildFtDiff(const std::vector<core::FtLevelNode> &original, std::vector<core::FtLevelNode> &edited);
void RefreshFtWorkspaceDiff(core::FtWorkspace &workspace);
}
