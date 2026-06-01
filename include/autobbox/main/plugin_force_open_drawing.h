#pragma once

namespace autobbox::main {

struct PluginForceOpenDrawingRuntime {
    bool *task_running = nullptr;
};

int RunPluginForceOpenDrawingTask(const PluginForceOpenDrawingRuntime &runtime);

} // namespace autobbox::main
