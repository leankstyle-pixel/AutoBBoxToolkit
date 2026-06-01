#pragma once

#include <functional>
#include <string>

namespace autobbox::ui {

using DeleteParamsDialogLogSink = std::function<void(const std::string &line)>;

bool PromptDeleteParamsDialog(bool &delete_size,
                              bool &delete_volume,
                              bool &cancelled,
                              const DeleteParamsDialogLogSink &log_sink);

} // namespace autobbox::ui
