#pragma once

#include <ProMdl.h>
#include <ProToolkit.h>

#include <string>

namespace autobbox::main {

struct PluginRandomColorRuntime {
    bool *task_running = nullptr;
    ProBoolean *parts = nullptr;
    ProBoolean *assemblies = nullptr;
    ProBoolean *top_level_only = nullptr;
    std::string (*format_model_tag)(ProMdl mdl) = nullptr;
};

int RunPluginRandomColorTask(const PluginRandomColorRuntime &runtime);

} // namespace autobbox::main
