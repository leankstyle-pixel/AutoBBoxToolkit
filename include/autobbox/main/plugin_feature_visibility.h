#pragma once

namespace autobbox::main {

struct PluginFeatureVisibilityRuntime {
    bool *task_running = nullptr;
};

int RunPluginFeatureVisibilityTask(const PluginFeatureVisibilityRuntime &runtime);

} // namespace autobbox::main
