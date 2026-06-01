#include "autobbox/main/plugin_split.h"

#include "autobbox/application/split_instances.h"
#include "autobbox/application/target_collectors.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/split_dialog.h"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace autobbox::main {

namespace {

void LogLine(const char *fmt, ...)
{
    if (fmt == nullptr) {
        return;
    }

    char buffer[2048] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    LogPluginReportLine(buffer);
}

std::string ModelTag(ProMdl mdl, const PluginSplitRuntime &runtime)
{
    if (runtime.format_model_tag != nullptr) {
        return runtime.format_model_tag(mdl);
    }
    return autobbox::creo::DefaultModelTag(mdl);
}

struct TaskGuard {
    explicit TaskGuard(bool *task_running)
        : task_running(task_running)
    {
    }

    ~TaskGuard()
    {
        EndPluginReportSession();
        if (task_running != nullptr) {
            *task_running = false;
        }
    }

    bool *task_running = nullptr;
};

} // namespace

int RunPluginSplitTask(const PluginSplitRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=split reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);
    BeginPluginReportSession();

    LogLine("===== Run begin mode=split =====");
    std::vector<autobbox::core::SplitCandidate> candidates =
        autobbox::application::CollectSplitCandidatesFromCurrentModel();
    LogLine("split candidates_total=%d", static_cast<int>(candidates.size()));
    if (candidates.empty()) {
        LogLine("split no family-instance candidates found");
        OpenPluginReportLog();
        return 0;
    }

    std::vector<autobbox::core::SplitCandidate> selected;
    autobbox::core::SplitRunOptions options;
    bool cancelled = false;
    if (!autobbox::ui::PromptSplitDialog(
            candidates,
            selected,
            options,
            cancelled,
            [](const std::string &line) { LogPluginReportLine(line); })) {
        LogLine("split dialog status=%s", cancelled ? "cancelled" : "failed");
        OpenPluginReportLog();
        return 0;
    }
    if (cancelled) {
        LogLine("split cancelled by user");
        OpenPluginReportLog();
        return 0;
    }
    if (selected.empty()) {
        LogLine("split no instances selected");
        OpenPluginReportLog();
        return 0;
    }

    autobbox::application::ExecuteSplitInstancesTask(
        static_cast<int>(candidates.size()),
        selected,
        options,
        [&runtime](ProMdl mdl) { return ModelTag(mdl, runtime); },
        [](const std::string &line) { LogPluginReportLine(line); });
    OpenPluginReportLog();
    return 0;
}

} // namespace autobbox::main
