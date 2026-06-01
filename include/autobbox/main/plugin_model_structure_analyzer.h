#pragma once

#include <ProMdl.h>

#include <string>

namespace autobbox::main {

struct PluginModelStructureAnalyzerRuntime {
    bool *task_running = nullptr;
    std::string (*format_model_tag)(ProMdl mdl) = nullptr;
};

int RunPluginModelStructureAnalyzerTask(const PluginModelStructureAnalyzerRuntime &runtime);

} // namespace autobbox::main
