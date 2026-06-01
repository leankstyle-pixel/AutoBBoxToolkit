#pragma once

#include <ProMdl.h>

#include <string>

namespace autobbox::main {

struct PluginModelDiagnosticsRuntime {
    bool *task_running = nullptr;
    std::string (*format_model_tag)(ProMdl mdl) = nullptr;
    void (*open_report_file)() = nullptr;
};

int RunPluginModelDiagnosticsTask(const PluginModelDiagnosticsRuntime &runtime);

} // namespace autobbox::main
