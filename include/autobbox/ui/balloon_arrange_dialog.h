#pragma once

#include "autobbox/application/balloon_arrange.h"

#include <string>

namespace autobbox::ui {

bool PromptBalloonArrangeOptionsDialog(autobbox::application::BalloonArrangeOptions &options_io,
                                       bool &cancelled,
                                       std::wstring &error_out);

} // namespace autobbox::ui
