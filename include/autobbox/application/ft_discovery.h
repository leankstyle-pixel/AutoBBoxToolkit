#pragma once
#include "autobbox/core/family_table_types.h"
#include <ProMdl.h>
namespace autobbox::application {
ProError DiscoverFamilyTableWorkspace(ProMdl current, core::FtWorkspace &workspace);
ProError DiscoverFamilyTableWorkspaceDeep(ProMdl current, core::FtWorkspace &workspace);
}
