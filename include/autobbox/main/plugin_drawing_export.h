#pragma once

namespace autobbox::main {

struct PluginDrawingExportRuntime {
    bool *task_running = nullptr;
};

int RunPluginDrawingExportTask(const PluginDrawingExportRuntime &runtime);

} // namespace autobbox::main
