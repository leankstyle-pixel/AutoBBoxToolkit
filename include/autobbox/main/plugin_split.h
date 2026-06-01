#pragma once

#include <ProMdl.h>

#include <string>

namespace autobbox::main {

struct PluginSplitRuntime {
    bool *task_running = nullptr;
    std::string (*format_model_tag)(ProMdl mdl) = nullptr;
};

int RunPluginSplitTask(const PluginSplitRuntime &runtime);

} // namespace autobbox::main
