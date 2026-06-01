#pragma once

#include <ProMdl.h>

#include <string>

namespace autobbox::main {

struct PluginSheetmetalFlatBatchRuntime {
    bool *task_running = nullptr;
    std::string (*format_model_tag)(ProMdl mdl) = nullptr;
};

int RunPluginSheetmetalFlatBatchTask(const PluginSheetmetalFlatBatchRuntime &runtime);

} // namespace autobbox::main
