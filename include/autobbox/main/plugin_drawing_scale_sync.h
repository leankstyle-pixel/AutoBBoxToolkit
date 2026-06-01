#pragma once

namespace autobbox::main {

struct PluginDrawingScaleSyncRuntime {
    bool *task_running = nullptr;
};

int RunPluginDrawingScaleSyncTask(const PluginDrawingScaleSyncRuntime &runtime);

} // namespace autobbox::main
