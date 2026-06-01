#include "autobbox/main/plugin_sheetmetal_flat_batch.h"

#include "autobbox/application/sheetmetal_flat_batch.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/message_dialog.h"
#include "autobbox/ui/sheetmetal_flat_batch_dialog.h"

#include <ProMdl.h>
#include <ProToolkit.h>
#include <ProUIMessage.h>

#include <cstdarg>
#include <cstdio>
#include <string>

namespace autobbox::main {
namespace {

void LogLine(const char *fmt, ...)
{
    if (fmt == nullptr) return;
    char buffer[4096] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    LogPluginReportLine(buffer);
}

struct TaskGuard {
    explicit TaskGuard(bool *task_running) : task_running(task_running) {}
    ~TaskGuard()
    {
        EndPluginReportSession();
        if (task_running != nullptr) *task_running = false;
    }
    bool *task_running = nullptr;
};

} // namespace

int RunPluginSheetmetalFlatBatchTask(const PluginSheetmetalFlatBatchRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=sheetmetal-flat-batch reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) *runtime.task_running = true;
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("===== Run begin mode=sheetmetal-flat-batch =====");

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        !autobbox::creo::IsPartOrAsm(current)) {
        LogLine("FAIL sheetmetal-flat-batch reason=current-not-part-or-assembly");
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"??????",
            L"??????????????????????????????");
        OpenPluginReportLog();
        return 0;
    }

    core::SheetmetalFlatCollectResult collect_result = autobbox::application::CollectSheetmetalFlatTargets();
    LogLine("sheetmetal-flat-batch collect targets=%d visited=%d duplicates=%d skip_non_sheet=%d skip_unreadable=%d",
            static_cast<int>(collect_result.targets.size()),
            collect_result.visited_components,
            collect_result.duplicate_components,
            collect_result.skipped_non_sheetmetal,
            collect_result.skipped_unreadable);

    if (collect_result.targets.empty()) {
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_INFO,
            L"??????",
            L"???????????????????????????");
        OpenPluginReportLog();
        return 0;
    }

    bool cancelled = false;
    bool deferred_action_requested = false;
    core::SheetmetalFlatAction deferred_action = core::SheetmetalFlatAction::CreateModelFlatRep;
    if (!autobbox::ui::PromptSheetmetalFlatBatchDialog(
            collect_result,
            cancelled,
            deferred_action_requested,
            deferred_action,
            [](const std::string &line) { LogPluginReportLine(line); })) {
        LogLine("sheetmetal-flat-batch dialog status=%s", cancelled ? "cancelled" : "failed");
        if (!cancelled) OpenPluginReportLog();
        return 0;
    }

    bool queued_official_flat_creation = false;
    if (deferred_action_requested) {
        core::SheetmetalFlatActionResult action_result;
        auto completion = [](const core::SheetmetalFlatActionResult &result) {
            LogPluginReportLine("sheetmetal-flat-batch async complete requested=" + std::to_string(result.requested) +
                                " succeeded=" + std::to_string(result.succeeded) +
                                " failed=" + std::to_string(result.failed) +
                                " skipped=" + std::to_string(result.skipped));
            OpenPluginReportLog();
        };
        if (deferred_action == core::SheetmetalFlatAction::CreateFamilyFlat) {
            queued_official_flat_creation = autobbox::application::CreateSheetmetalFamilyFlatStates(
                collect_result.targets,
                action_result,
                [](const std::string &line) { LogPluginReportLine(line); },
                completion);
        } else if (deferred_action == core::SheetmetalFlatAction::CreateModelFlatRep) {
            queued_official_flat_creation = autobbox::application::CreateSheetmetalFlatSimpreps(
                collect_result.targets,
                action_result,
                [](const std::string &line) { LogPluginReportLine(line); },
                completion);
        }
        LogLine("sheetmetal-flat-batch queued action=%d requested=%d status=%s",
                static_cast<int>(deferred_action),
                action_result.requested,
                queued_official_flat_creation ? "queued" : "failed-to-queue");
    }

    LogLine("===== Run end mode=sheetmetal-flat-batch =====");
    if (!queued_official_flat_creation) OpenPluginReportLog();
    return 0;
}

} // namespace autobbox::main
