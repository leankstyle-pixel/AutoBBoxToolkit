#pragma once

#include "autobbox/core/quick_rename_types.h"

#include <ProAsmcomp.h>
#include <ProAssembly.h>
#include <ProMdl.h>
#include <ProToolkit.h>

#include <functional>
#include <string>

namespace autobbox::application {

struct SaveCopyToWorkdirSource {
    ProMdl mdl = nullptr;
    ProMdlType type = PRO_MDL_UNUSED;
    std::wstring name;
    std::wstring source_path;
    bool from_file_picker = false;
    bool can_replace_component = false;
    core::QuickRenameTarget replacement_target = {};
};

struct SaveCopyToWorkdirValidationResult {
    bool ok = false;
    bool unchanged = false;
    bool target_file_exists = false;
    bool session_name_conflict = false;
    ProMdl existing_mdl = nullptr;
    std::wstring normalized_name;
    std::wstring target_path;
    std::wstring error_text;
};

bool ResolveSaveCopyToWorkdirSource(
    SaveCopyToWorkdirSource &source_out,
    bool &cancelled,
    std::wstring &error_text,
    const std::function<void(const std::string &line)> &log_sink);

SaveCopyToWorkdirValidationResult ValidateSaveCopyToWorkdirName(
    const SaveCopyToWorkdirSource &source,
    const std::wstring &input_name,
    const std::wstring &target_directory);

ProError SaveModelCopyToWorkdir(const SaveCopyToWorkdirSource &source,
                                const std::wstring &new_name,
                                ProMdl *copied_mdl);

ProError AssembleSavedCopyToAssembly(ProAssembly target_assembly,
                                     ProMdl copied_mdl,
                                     ProAsmcomp *assembled_component,
                                     ProError *constraint_ui_status);

std::wstring SaveCopyToWorkdirStatusMessage(ProError status);

std::wstring AssembleSavedCopyToAssemblyStatusMessage(ProError status);

} // namespace autobbox::application
