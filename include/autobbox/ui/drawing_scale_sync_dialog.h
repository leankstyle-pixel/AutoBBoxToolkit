#pragma once

#include "autobbox/application/drawing_page_scale_sync.h"

#include <functional>
#include <string>

namespace autobbox::ui {

using DrawingScaleSyncDialogLogSink = std::function<void(const std::string &line)>;

bool PromptDrawingScaleSyncOptionsDialog(
    autobbox::application::DrawingPageScaleSyncOptions &options_io,
    bool &cancelled,
    const DrawingScaleSyncDialogLogSink &log_sink = DrawingScaleSyncDialogLogSink());

} // namespace autobbox::ui
