#pragma once

#include "autobbox/core/param_types.h"

#include <ProToolkit.h>

#include <functional>
#include <string>

namespace autobbox::ui {

struct ParamAddDialogCallbacks {
    std::function<const wchar_t *(ProParamvalueType type)> param_add_type_menu_label;
    std::function<bool(
        const std::wstring &name_text,
        const std::wstring &type_label,
        const std::wstring &value_text,
        core::ParamAddSpec &spec_out,
        std::wstring &error_out)> parse_param_add_dialog_spec;
};

bool PromptParamAddDialog(core::ParamAddSpec &spec_io,
                          bool &cancelled,
                          std::wstring &error_out,
                          const ParamAddDialogCallbacks &callbacks);

} // namespace autobbox::ui
