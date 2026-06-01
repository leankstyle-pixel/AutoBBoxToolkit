#include "autobbox/main/plugin_quick_simprep.h"

#include "autobbox/application/quick_simprep.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/message_dialog.h"
#include "autobbox/ui/quick_simprep_dialog.h"

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
    if (fmt == nullptr) {
        return;
    }

    char buffer[4096] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    LogPluginReportLine(buffer);
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

int RunPluginQuickSimprepTask(const PluginQuickSimprepRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=quick-simprep reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("===== Run begin mode=quick-simprep =====");

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        autobbox::creo::ModelType(current) != PRO_MDL_ASSEMBLY) {
        LogLine("FAIL quick-simprep reason=current-not-assembly");
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u5feb\u901f\u7b80\u5316\u8868\u793a",
            L"\u8bf7\u5148\u6253\u5f00\u88c5\u914d\uff0c\u518d\u521b\u5efa\u7b80\u5316\u8868\u793a\u3002");
        OpenPluginReportLog();
        return 0;
    }

    core::QuickSimprepCollectResult collect_result =
        autobbox::application::CollectQuickSimprepCategories();
    LogLine("quick-simprep collect categories=%d direct=%d grouped=%d skip_missing=%d skip_unreadable=%d",
            static_cast<int>(collect_result.categories.size()),
            collect_result.direct_component_count,
            collect_result.grouped_component_count,
            collect_result.skipped_missing_common_name,
            collect_result.skipped_unreadable_common_name);

    if (collect_result.categories.empty()) {
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_INFO,
            L"\u5feb\u901f\u7b80\u5316\u8868\u793a",
            L"\u5f53\u524d\u88c5\u914d\u7684\u76f4\u63a5\u7ec4\u4ef6\u4e2d\u672a\u627e\u5230\u975e\u7a7a PTC_COMMON_NAME\u3002");
        OpenPluginReportLog();
        return 0;
    }

    bool cancelled = false;
    if (!autobbox::ui::PromptQuickSimprepDialog(
            collect_result,
            cancelled,
            [](const std::string &line) { LogPluginReportLine(line); })) {
        LogLine("quick-simprep dialog status=%s", cancelled ? "cancelled" : "failed");
        if (!cancelled) {
            OpenPluginReportLog();
        }
        return 0;
    }

    LogLine("===== Run end mode=quick-simprep =====");
    OpenPluginReportLog();
    return 0;
}

} // namespace autobbox::main
