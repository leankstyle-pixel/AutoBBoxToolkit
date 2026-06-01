#include "autobbox/main/plugin_relations.h"

#include "autobbox/application/relations.h"
#include "autobbox/application/target_collectors.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/relations_text_dialog.h"

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

std::string ModelTag(ProMdl mdl, const PluginRelationsRuntime &runtime)
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

ProBoolean ValueOrDefault(const ProBoolean *value, ProBoolean fallback)
{
    return value != nullptr ? *value : fallback;
}

const char *ModeToken(PluginRelationsMode mode)
{
    return mode == PluginRelationsMode::Clean ? "clean-relations" : "add-relations";
}

} // namespace

int RunPluginRelationsTask(PluginRelationsMode mode,
                           const PluginRelationsRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=%s reason=already-running", ModeToken(mode));
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();

    const ProBoolean parts = ValueOrDefault(runtime.parts, PRO_B_TRUE);
    const ProBoolean assemblies = ValueOrDefault(runtime.assemblies, PRO_B_TRUE);
    const ProBoolean surface = ValueOrDefault(runtime.surface, PRO_B_FALSE);
    const ProBoolean curve = ValueOrDefault(runtime.curve, PRO_B_FALSE);
    const ProBoolean recompute = ValueOrDefault(runtime.recompute, PRO_B_TRUE);
    const ProBoolean top_level_only = ValueOrDefault(runtime.top_level_only, PRO_B_FALSE);

    std::vector<ProMdl> models =
        autobbox::application::CollectTargetsFromCurrentModel(parts, assemblies, top_level_only);

    LogLine("===== Run begin mode=%s =====", ModeToken(mode));
    LogLine("targets=%d parts=%d asms=%d top2=%d surface_ignored=%d curve_ignored=%d recompute_ignored=%d",
            static_cast<int>(models.size()),
            static_cast<int>(parts),
            static_cast<int>(assemblies),
            static_cast<int>(top_level_only),
            static_cast<int>(surface),
            static_cast<int>(curve),
            static_cast<int>(recompute));

    if (mode == PluginRelationsMode::Clean) {
        autobbox::application::ExecuteCleanRelationsTask(
            models,
            [&runtime](ProMdl mdl) { return ModelTag(mdl, runtime); },
            [](const std::string &line) { LogPluginReportLine(line); });
        OpenPluginReportLog();
        return 0;
    }

    if (models.empty()) {
        LogLine("Add relations skipped: no targets");
        OpenPluginReportLog();
        return 0;
    }

    std::wstring raw_text;
    bool cancelled = false;
    if (!autobbox::ui::PromptRelationsTextDialog(
            raw_text,
            cancelled,
            [](const std::string &line) { LogPluginReportLine(line); })) {
        LogLine("Add relations status=%s", cancelled ? "cancelled" : "dialog-failed");
        OpenPluginReportLog();
        return 0;
    }

    autobbox::application::ExecuteAddRelationsTask(
        models,
        raw_text,
        [&runtime](ProMdl mdl) { return ModelTag(mdl, runtime); },
        [](const std::string &line) { LogPluginReportLine(line); });
    OpenPluginReportLog();
    return 0;
}

} // namespace autobbox::main
