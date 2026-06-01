#pragma once

#include <ProMdl.h>

#include <string>

namespace autobbox::main {

struct PluginSaveCopyToWorkdirRuntime {
    bool *task_running = nullptr;
    std::string (*format_model_tag)(ProMdl mdl) = nullptr;
};

int RunPluginSaveCopyToWorkdirTask(const PluginSaveCopyToWorkdirRuntime &runtime);

} // namespace autobbox::main
