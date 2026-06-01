#pragma once
#include "autobbox/core/family_table_types.h"
#include <ProToolkit.h>
#include <string>
namespace autobbox::application {
ProError ShowNativeFamilyTable(core::FtLevelNode &level);
ProError EditNativeFamilyTable(core::FtLevelNode &level);
ProError EditChildInstanceFamilyTable(core::FtLevelNode &parent_level, const std::wstring &instance_name);
ProError OpenFamilyTableInstance(core::FtLevelNode &level, const std::wstring &instance_name, ProMdl *opened_model = nullptr);
ProError PreviewFamilyTableInstance(core::FtLevelNode &level, const std::wstring &instance_name, ProMdl *opened_model = nullptr);
ProError BridgeNativeFamilyTableAction(core::FtLevelNode &level, const std::wstring &action_key, const std::wstring &instance_name = L"");
ProError EraseLevelFamilyTable(core::FtLevelNode &level);
}
