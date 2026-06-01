#pragma once

#include "autobbox/application/save_copy_to_workdir.h"

#include <functional>
#include <string>

namespace autobbox::ui {

struct SaveCopyToWorkdirDialogResult {
    std::wstring new_name;
    bool replace_component = false;
};

using SaveCopyToWorkdirDialogLogSink = std::function<void(const std::string &line)>;

bool PromptSaveCopyToWorkdirDialog(
    const application::SaveCopyToWorkdirSource &source,
    SaveCopyToWorkdirDialogResult &result,
    bool &cancelled,
    std::wstring &error_out,
    const SaveCopyToWorkdirDialogLogSink &log_sink = SaveCopyToWorkdirDialogLogSink());

} // namespace autobbox::ui
