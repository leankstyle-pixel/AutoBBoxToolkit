#pragma once

#include "autobbox/core/random_color_types.h"

#include <functional>
#include <string>
#include <vector>

namespace autobbox::ui {

using RandomColorDialogLogSink = std::function<void(const std::string &line)>;

bool PromptRandomColorDialog(const std::vector<core::RandomColorCandidate> &candidates,
                             const std::wstring &default_library_path,
                             std::vector<core::RandomColorAssignment> &selected,
                             std::vector<core::RandomColorParameterPreview> &parameter_selected,
                             std::vector<core::RandomColorCandidate> &clear_targets,
                             bool &use_parameter_colors,
                             std::wstring &parameter_name,
                             bool &clear_all_colors,
                             bool &cancelled,
                             const RandomColorDialogLogSink &log_sink);

} // namespace autobbox::ui
