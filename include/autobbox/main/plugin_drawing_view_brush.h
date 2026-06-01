#pragma once

namespace autobbox::main {

struct PluginDrawingViewBrushRuntime {
    bool *task_running = nullptr;
};

int RunPluginDrawingViewBrushTask(const PluginDrawingViewBrushRuntime &runtime);

} // namespace autobbox::main
