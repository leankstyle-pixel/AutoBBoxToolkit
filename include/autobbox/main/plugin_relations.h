#pragma once

#include <ProMdl.h>
#include <ProToolkit.h>

#include <string>

namespace autobbox::main {

enum class PluginRelationsMode {
    Clean,
    Add
};

struct PluginRelationsRuntime {
    bool *task_running = nullptr;
    ProBoolean *parts = nullptr;
    ProBoolean *assemblies = nullptr;
    ProBoolean *surface = nullptr;
    ProBoolean *curve = nullptr;
    ProBoolean *recompute = nullptr;
    ProBoolean *top_level_only = nullptr;
    std::string (*format_model_tag)(ProMdl mdl) = nullptr;
};

int RunPluginRelationsTask(PluginRelationsMode mode,
                           const PluginRelationsRuntime &runtime);

} // namespace autobbox::main
