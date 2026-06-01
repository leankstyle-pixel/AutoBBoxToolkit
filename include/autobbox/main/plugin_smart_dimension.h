#pragma once

namespace autobbox::main {

struct PluginSmartDimensionRuntime {
    bool *task_running = nullptr;
};

int RunPluginSmartDimensionTask(const PluginSmartDimensionRuntime &runtime);

} // namespace autobbox::main
