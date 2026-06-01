#pragma once

#include "autobbox/application/drawing_view_brush.h"

#include <functional>
#include <string>

namespace autobbox::ui {

using DrawingViewBrushDialogLogSink = std::function<void(const std::string &line)>;

struct DrawingViewBrushDialogRequest {
    application::DrawingViewBrushMode mode = application::DrawingViewBrushMode::MainView;
    application::DrawingViewBrushSource source = application::DrawingViewBrushSource::ReferenceView;
    application::DrawingViewBrushPreset preset = application::DrawingViewBrushPreset::Front;
};

bool PromptDrawingViewBrushOptions(DrawingViewBrushDialogRequest &request,
                                   bool &cancelled,
                                   const DrawingViewBrushDialogLogSink &log_sink);

} // namespace autobbox::ui
