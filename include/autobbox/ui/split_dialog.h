#pragma once

#include "autobbox/core/split_types.h"

#include <functional>
#include <string>
#include <vector>

namespace autobbox::ui {

using SplitDialogLogSink = std::function<void(const std::string &line)>;

bool PromptSplitDialog(const std::vector<core::SplitCandidate> &candidates,
                       std::vector<core::SplitCandidate> &selected,
                       core::SplitRunOptions &options,
                       bool &cancelled,
                       const SplitDialogLogSink &log_sink);

} // namespace autobbox::ui
