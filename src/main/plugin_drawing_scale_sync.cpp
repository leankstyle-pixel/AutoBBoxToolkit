#include "autobbox/main/plugin_drawing_scale_sync.h"

#include "autobbox/application/drawing_page_scale_sync.h"
#include "autobbox/main/plugin_runtime_bridge.h"

#include <ProDrawing.h>
#include <ProMdl.h>
#include <ProToolkit.h>
#include <ProWindows.h>

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

void RefreshCurrentWindow()
{
    int window_id = 0;
    const ProError st_get_window = ProWindowCurrentGet(&window_id);
    LogLine("PageScaleSync window_get status=%d window=%d",
            static_cast<int>(st_get_window),
            window_id);
    if (st_get_window != PRO_TK_NO_ERROR) {
        return;
    }

    const ProError st_refresh = ProWindowRefresh(window_id);
    const ProError st_repaint = ProWindowRepaint(window_id);
    LogLine("PageScaleSync window_refresh status=%d window=%d",
            static_cast<int>(st_refresh),
            window_id);
    LogLine("PageScaleSync window_repaint status=%d window=%d",
            static_cast<int>(st_repaint),
            window_id);
}

} // namespace

int RunPluginDrawingScaleSyncTask(const PluginDrawingScaleSyncRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=page-scale-sync reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("Run start: mode=page-scale-sync");

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR || current == nullptr) {
        LogLine("FAIL page-scale-sync reason=current-get");
        OpenPluginReportLog();
        return 0;
    }
    if (ProMdlType type = PRO_MDL_UNUSED; ProMdlTypeGet(current, &type) != PRO_TK_NO_ERROR || type != PRO_MDL_DRAWING) {
        LogLine("FAIL page-scale-sync reason=current-not-drawing");
        OpenPluginReportLog();
        return 0;
    }

    const ProError st_display = ProMdlDisplay(current);
    LogLine("PageScaleSync display status=%d", static_cast<int>(st_display));
    if (st_display != PRO_TK_NO_ERROR) {
        OpenPluginReportLog();
        return 0;
    }

    const ProDrawing drawing = reinterpret_cast<ProDrawing>(current);
    int sheet = 0;
    const ProError st_sheet = ProDrawingCurrentSheetGet(drawing, &sheet);
    if (st_sheet != PRO_TK_NO_ERROR) {
        LogLine("FAIL page-scale-sync reason=current-sheet status=%d", static_cast<int>(st_sheet));
        OpenPluginReportLog();
        return 0;
    }

    const autobbox::application::DrawingPageScaleSyncSummary summary =
        autobbox::application::ExecuteDrawingPageScaleSyncTask(
            drawing,
            sheet,
            [](const std::string &line) { LogPluginReportLine(line); });

    if (summary.page_scale_valid && summary.views_on_sheet > 0) {
        RefreshCurrentWindow();
    }

    OpenPluginReportLog();
    return 0;
}

} // namespace autobbox::main
