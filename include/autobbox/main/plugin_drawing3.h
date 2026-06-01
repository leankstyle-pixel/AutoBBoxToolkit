#pragma once

#include <ProMdl.h>

#include <string>

namespace autobbox::main {

struct PluginDrawing3Runtime {
    bool *task_running = nullptr;
    std::string (*format_model_tag)(ProMdl mdl) = nullptr;
};

int RunPluginDrawing3Task(const PluginDrawing3Runtime &runtime);

} // namespace autobbox::main
