#pragma once
#include "autobbox/core/family_table_types.h"
#include <vector>
namespace autobbox::application {
std::vector<core::FtSupportMatrixEntry> BuildDefaultFtSupportMatrix();
core::FtSupportStatus SupportForColumnCategory(core::FtColumnCategory category);
bool IsFtStatusEditable(core::FtSupportStatus status);
bool IsFtColumnEditable(core::FtColumnCategory category, core::FtSupportStatus status);
}
