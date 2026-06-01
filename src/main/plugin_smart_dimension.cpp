#include "autobbox/main/plugin_smart_dimension.h"

#include "autobbox/application/smart_dim_command.h"
#include "autobbox/ui/message_dialog.h"

#include "autobbox/main/plugin_runtime_bridge.h"

#include <ProDrawing.h>
#include <ProMdl.h>
#include <ProToolkit.h>

#include <cstdarg>
#include <cstdio>

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

int RunPluginSmartDimensionTask(const PluginSmartDimensionRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=smart-dimension reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("Run start: mode=smart-dimension");

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR || current == nullptr) {
        LogLine("FAIL smart-dimension reason=current-get");
        OpenPluginReportLog();
        return 0;
    }

    ProMdlType type = PRO_MDL_UNUSED;
    if (ProMdlTypeGet(current, &type) != PRO_TK_NO_ERROR || type != PRO_MDL_DRAWING) {
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"智能尺寸标注",
            L"请先激活工程图，再执行智能尺寸标注。");
        OpenPluginReportLog();
        return 0;
    }

    const autobbox::core::SmartDimensionLoopSummary summary =
        autobbox::application::RunSmartDimensionCommand(
            reinterpret_cast<ProDrawing>(current),
            [](const std::string &line) { LogPluginReportLine(line); });

    if (summary.dimensions_created > 0) {
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_INFO,
            L"智能尺寸标注",
            L"已完成本轮智能尺寸创建。");
    } else {
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_INFO,
            L"智能尺寸标注",
            summary.last_note.empty()
                ? L"已接入智能尺寸标注第一版骨架。"
                : summary.last_note.c_str());
    }

    OpenPluginReportLog();
    return 0;
}

} // namespace autobbox::main
