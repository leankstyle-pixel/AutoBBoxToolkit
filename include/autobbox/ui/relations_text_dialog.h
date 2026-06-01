#pragma once

#include <functional>
#include <string>

namespace autobbox::ui {

using RelationsTextDialogLogSink = std::function<void(const std::string &line)>;

bool PromptRelationsTextDialog(std::wstring &text,
                               bool &cancelled,
                               const RelationsTextDialogLogSink &log_sink);

} // namespace autobbox::ui
