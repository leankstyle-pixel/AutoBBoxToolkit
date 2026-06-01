#pragma once
#include <ProToolkit.h>
namespace autobbox::main {
struct PluginFamilyTableManagerRuntime {
    bool *task_running = nullptr;
};
int RunPluginFamilyTableManagerTask(const PluginFamilyTableManagerRuntime &runtime);
}
