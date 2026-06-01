#pragma once
#include "autobbox/core/family_table_types.h"
#include <string>
#include <vector>
namespace autobbox::application {
bool AddFtInstanceRow(core::FtLevelNode &level, const std::wstring &instance_name, std::wstring &error_out);
bool DeleteFtInstanceRow(core::FtWorkspace &workspace, const std::wstring &level_path, const std::wstring &instance_name, bool force, std::wstring &error_out);
bool RenameFtInstanceRow(core::FtLevelNode &level, const std::wstring &old_name, const std::wstring &new_name, std::wstring &error_out);
void RenameFtInstanceSubtree(core::FtWorkspace &workspace,
                             const std::wstring &parent_level_path,
                             const std::wstring &old_instance_name,
                             const std::wstring &new_instance_name);
bool CloneFtInstanceRowsSimple(core::FtWorkspace &workspace,
                               const std::wstring &level_path,
                               const std::wstring &source_instance_name,
                               int copy_count,
                               std::vector<std::wstring> &created_names,
                               std::wstring &error_out);
bool CloneFtInstanceRowWithChildren(core::FtWorkspace &workspace,
                                    const std::wstring &level_path,
                                    const std::wstring &source_instance_name,
                                    const std::wstring &target_instance_name,
                                    std::wstring &error_out);
}
