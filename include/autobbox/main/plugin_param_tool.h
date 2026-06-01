#pragma once

#include <ProMdl.h>
#include <ProToolkit.h>

#include <string>

namespace autobbox::core {
struct BomToolState;
}

namespace autobbox::main {

struct PluginParamToolRuntime {
    bool *task_running = nullptr;
    ProBoolean *parts = nullptr;
    ProBoolean *assemblies = nullptr;
    ProBoolean *top_level_only = nullptr;
    autobbox::core::BomToolState *persisted_state = nullptr;
    std::string (*format_model_tag)(ProMdl mdl) = nullptr;
};

int RunPluginParamToolTask(const PluginParamToolRuntime &runtime);

} // namespace autobbox::main
