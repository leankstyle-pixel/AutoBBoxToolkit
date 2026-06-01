#pragma once

#include "autobbox/core/quick_rename_types.h"

#include <functional>
#include <string>

namespace autobbox::ui {

enum class QuickRenameDialogAction {
    rename,
    clone,
};

using QuickRenameDialogLogSink = std::function<void(const std::string &line)>;

bool PromptQuickRenameDialog(const core::QuickRenameTarget &target,
                             std::wstring &new_name,
                             QuickRenameDialogAction &action,
                             bool &cancelled,
                             std::wstring &error_out,
                             const QuickRenameDialogLogSink &log_sink = QuickRenameDialogLogSink());

} // namespace autobbox::ui
