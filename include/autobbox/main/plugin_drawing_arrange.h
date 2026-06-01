#pragma once

namespace autobbox::main {

struct PluginDrawingArrangeRuntime {
    bool *task_running = nullptr;
};

int RunPluginDrawingArrangeTask(const PluginDrawingArrangeRuntime &runtime);

} // namespace autobbox::main
