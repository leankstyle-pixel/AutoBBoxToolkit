#pragma once

#include <ProMdl.h>
#include <ProToolkit.h>

#include <string>

namespace autobbox::main {

struct PluginBatchRenameRuntime {
    bool *task_running = nullptr;
    const ProBoolean *parts = nullptr;
    const ProBoolean *assemblies = nullptr;
    const ProBoolean *top_level_only = nullptr;
    std::string (*format_model_tag)(ProMdl mdl) = nullptr;
};

int RunPluginBatchRenameTask(const PluginBatchRenameRuntime &runtime);

} // namespace autobbox::main
