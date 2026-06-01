#include "autobbox/ui/message_dialog.h"

#include <ProArray.h>

namespace autobbox::ui {

void ShowSimpleMessageDialog(ProUIMessageType type,
                             const wchar_t *title,
                             const wchar_t *message)
{
    ProUIMessageButton choice = PRO_UI_MESSAGE_OK;
    ProUIMessageButton *buttons = nullptr;
    if (ProArrayAlloc(0, sizeof(ProUIMessageButton), 1, (ProArray *)&buttons) != PRO_TK_NO_ERROR ||
        buttons == nullptr) {
        return;
    }

    ProUIMessageButton ok_button = PRO_UI_MESSAGE_OK;
    ProArrayObjectAdd((ProArray *)&buttons, PRO_VALUE_UNUSED, 1, &ok_button);
    ProUIMessageDialogDisplay(
        type,
        const_cast<wchar_t *>(title == nullptr ? L"AutoBBox" : title),
        const_cast<wchar_t *>(message == nullptr ? L"" : message),
        buttons,
        PRO_UI_MESSAGE_OK,
        &choice);
    ProArrayFree((ProArray *)&buttons);
}

bool ShowYesNoMessageDialog(ProUIMessageType type,
                            const wchar_t *title,
                            const wchar_t *message,
                            bool default_yes)
{
    ProUIMessageButton choice = default_yes ? PRO_UI_MESSAGE_YES : PRO_UI_MESSAGE_NO;
    ProUIMessageButton *buttons = nullptr;
    if (ProArrayAlloc(0, sizeof(ProUIMessageButton), 1, (ProArray *)&buttons) != PRO_TK_NO_ERROR ||
        buttons == nullptr) {
        return false;
    }

    ProUIMessageButton yes_button = PRO_UI_MESSAGE_YES;
    ProUIMessageButton no_button = PRO_UI_MESSAGE_NO;
    ProArrayObjectAdd((ProArray *)&buttons, PRO_VALUE_UNUSED, 1, &yes_button);
    ProArrayObjectAdd((ProArray *)&buttons, PRO_VALUE_UNUSED, 1, &no_button);
    ProUIMessageDialogDisplay(
        type,
        const_cast<wchar_t *>(title == nullptr ? L"AutoBBox" : title),
        const_cast<wchar_t *>(message == nullptr ? L"" : message),
        buttons,
        default_yes ? PRO_UI_MESSAGE_YES : PRO_UI_MESSAGE_NO,
        &choice);
    ProArrayFree((ProArray *)&buttons);
    return choice == PRO_UI_MESSAGE_YES;
}

} // namespace autobbox::ui
