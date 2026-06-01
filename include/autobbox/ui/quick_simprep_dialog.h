#pragma once

#include "autobbox/core/quick_simprep_types.h"

#include <functional>
#include <string>

namespace autobbox::ui {

using QuickSimprepDialogLogSink = std::function<void(const std::string &line)>;

bool PromptQuickSimprepDialog(core::QuickSimprepCollectResult &collect_result,
                              bool &cancelled,
                              const QuickSimprepDialogLogSink &log_sink);

} // namespace autobbox::ui
