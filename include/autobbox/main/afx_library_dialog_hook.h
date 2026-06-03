#pragma once

#include <string>

namespace autobbox::main {

void StartAfxLibraryDialogHook(const std::string &startup_log);
void StopAfxLibraryDialogHook();

} // namespace autobbox::main
