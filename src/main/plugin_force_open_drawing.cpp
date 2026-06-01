#include "autobbox/main/plugin_force_open_drawing.h"

#include "autobbox/application/force_open_drawing.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/message_dialog.h"

#include <ProToolkit.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <string>

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

const wchar_t *FailureStage(const autobbox::application::ForceOpenDrawingResult &result)
{
    if (result.selection_status != PRO_TK_NO_ERROR) {
        return L"\u9009\u62e9\u6a21\u578b";
    }
    if (result.model_display_status != PRO_TK_NO_ERROR) {
        return L"\u663e\u793a\u6a21\u578b";
    }
    if (result.drawing_path_local_missing || result.drawing_load_status != PRO_TK_NO_ERROR) {
        return L"\u6253\u5f00\u540c\u540d\u5de5\u7a0b\u56fe";
    }
    if (result.drawing_display_status != PRO_TK_NO_ERROR) {
        return L"\u663e\u793a\u5de5\u7a0b\u56fe";
    }
    return L"\u5f3a\u5236\u6253\u5f00\u5de5\u7a0b\u56fe";
}

ProError FailureStatus(const autobbox::application::ForceOpenDrawingResult &result)
{
    if (result.selection_status != PRO_TK_NO_ERROR) {
        return result.selection_status;
    }
    if (result.model_display_status != PRO_TK_NO_ERROR) {
        return result.model_display_status;
    }
    if (result.drawing_path_local_missing || result.drawing_load_status != PRO_TK_NO_ERROR) {
        return result.drawing_load_status;
    }
    if (result.drawing_display_status != PRO_TK_NO_ERROR) {
        return result.drawing_display_status;
    }
    return result.status;
}

void ShowFailureMessage(const autobbox::application::ForceOpenDrawingResult &result)
{
    wchar_t message[1536] = {0};
    const wchar_t *stage = FailureStage(result);
    const ProError status = FailureStatus(result);

    if (result.drawing_path_local_missing) {
        std::swprintf(message,
                      sizeof(message) / sizeof(message[0]),
                      L"%s\u5931\u8d25\uff1a\u627e\u4e0d\u5230\u540c\u540d\u5de5\u7a0b\u56fe\u3002\n\u6a21\u578b\uff1a%s\n\u5de5\u7a0b\u56fe\uff1a%s",
                      stage,
                      result.selected_model_path.c_str(),
                      result.drawing_path.c_str());
    } else {
        std::swprintf(message,
                      sizeof(message) / sizeof(message[0]),
                      L"%s\u5931\u8d25\uff0cCreo \u72b6\u6001\u7801\uff1a%d\n\u6a21\u578b\uff1a%s\n\u5de5\u7a0b\u56fe\uff1a%s",
                      stage,
                      static_cast<int>(status),
                      result.selected_model_path.c_str(),
                      result.drawing_path.c_str());
    }

    autobbox::ui::ShowSimpleMessageDialog(
        PROUIMESSAGE_WARNING,
        L"\u5f3a\u5236\u6253\u5f00\u5de5\u7a0b\u56fe",
        message);
}

} // namespace

int RunPluginForceOpenDrawingTask(const PluginForceOpenDrawingRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=force-open-drawing reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("Run start: mode=force-open-drawing");

    const autobbox::application::ForceOpenDrawingResult result =
        autobbox::application::ExecuteForceOpenDrawingTask(
            [](const std::string &line) { LogPluginReportLine(line); });

    if (result.cancelled) {
        LogLine("ForceOpenDrawing cancelled");
        return 0;
    }

    if (result.status != PRO_TK_NO_ERROR) {
        LogLine("ForceOpenDrawing failed status=%d", static_cast<int>(result.status));
        ShowFailureMessage(result);
        OpenPluginReportLog();
        return 0;
    }

    LogLine("ForceOpenDrawing completed");
    return 0;
}

} // namespace autobbox::main
