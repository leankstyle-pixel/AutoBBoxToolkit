#pragma once

#include <ProUIMessage.h>

namespace autobbox::ui {

void ShowSimpleMessageDialog(ProUIMessageType type,
                             const wchar_t *title,
                             const wchar_t *message);

bool ShowYesNoMessageDialog(ProUIMessageType type,
                            const wchar_t *title,
                            const wchar_t *message,
                            bool default_yes = false);

} // namespace autobbox::ui
