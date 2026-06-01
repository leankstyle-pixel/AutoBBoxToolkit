#pragma once

#include "autobbox/core/quick_rename_types.h"

#include <ProToolkit.h>

#include <functional>
#include <string>

namespace autobbox::application {

bool ResolveQuickRenameTarget(core::QuickRenameTarget &target_out,
                              bool &cancelled,
                              std::wstring &error_text,
                              const std::function<void(const std::string &line)> &log_sink);

core::QuickRenameValidationResult ValidateQuickRenameName(
    const core::QuickRenameTarget &target,
    const std::wstring &input_name,
    bool allow_existing_same_type = false);

ProError RenameModelInSession(const core::QuickRenameTarget &target,
                              const std::wstring &new_name);

ProError CloneModelInSession(const core::QuickRenameTarget &target,
                             const std::wstring &new_name,
                             ProMdl *new_mdl);

ProError ReplaceModelInAssembly(const core::QuickRenameTarget &target,
                                const std::wstring &replacement_name,
                                ProMdl *replacement_mdl);

ProError ReplaceLoadedModelInAssembly(const core::QuickRenameTarget &target,
                                      ProMdl replacement_mdl);

std::wstring QuickRenameStatusMessage(ProError status);

std::wstring QuickCloneStatusMessage(ProError status);

std::wstring QuickReplaceStatusMessage(ProError status);

} // namespace autobbox::application
