#pragma once

#include <ProMdl.h>

#include <string>

namespace autobbox::main {

struct PluginQuickSimprepRuntime {
    bool *task_running = nullptr;
    std::string (*format_model_tag)(ProMdl mdl) = nullptr;
};

int RunPluginQuickSimprepTask(const PluginQuickSimprepRuntime &runtime);

} // namespace autobbox::main
