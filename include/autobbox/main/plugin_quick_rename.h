#pragma once

#include <ProMdl.h>

#include <string>

namespace autobbox::main {

struct PluginQuickRenameRuntime {
    bool *task_running = nullptr;
    std::string (*format_model_tag)(ProMdl mdl) = nullptr;
};

int RunPluginQuickRenameTask(const PluginQuickRenameRuntime &runtime);

} // namespace autobbox::main
