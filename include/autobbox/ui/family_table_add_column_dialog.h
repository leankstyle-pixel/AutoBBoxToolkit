#pragma once

#include "autobbox/application/ft_column_ops.h"

#include <string>

namespace autobbox::ui {

bool PromptFamilyTableAddColumnDialog(application::FtAddColumnSpec &spec_io,
                                      std::wstring &error_out);

} // namespace autobbox::ui
