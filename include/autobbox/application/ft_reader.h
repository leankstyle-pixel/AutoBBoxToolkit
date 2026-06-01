#pragma once
#include "autobbox/core/family_table_types.h"
namespace autobbox::application {
ProError ReadFamilyTableWorkspace(core::FtWorkspace &workspace, bool reset_snapshot = true);
}
