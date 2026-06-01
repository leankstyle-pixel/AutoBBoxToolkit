#pragma once

#include "autobbox/application/drawing_arrange.h"

#include <string>

namespace autobbox::ui {

bool PromptDrawingArrangeOptionsDialog(autobbox::application::DrawingArrangeOptions &options_io,
                                       bool &cancelled,
                                       std::wstring &error_out);

} // namespace autobbox::ui
