#pragma once

namespace autobbox::main {

struct PluginBalloonArrangeRuntime {
    bool *task_running = nullptr;
};

int RunPluginBalloonArrangeTask(const PluginBalloonArrangeRuntime &runtime);
int RunPluginRebuildBalloonsTask(const PluginBalloonArrangeRuntime &runtime);

} // namespace autobbox::main
