#include "autobbox/main/plugin_drawing_arrange.h"

#include "autobbox/application/drawing_arrange.h"
#include "autobbox/common/strings.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/drawing_arrange_dialog.h"
#include "autobbox/ui/message_dialog.h"

#include <ProDrawing.h>
#include <ProMdl.h>
#include <ProToolkit.h>
#include <ProWindows.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>

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
    LogLine("ArrangeDwgViews window_get status=%d window=%d",
            static_cast<int>(st_get_window),
            window_id);
    if (st_get_window != PRO_TK_NO_ERROR) {
        return;
    }

    const ProError st_refresh = ProWindowRefresh(window_id);
    const ProError st_repaint = ProWindowRepaint(window_id);
    LogLine("ArrangeDwgViews window_refresh status=%d window=%d",
            static_cast<int>(st_refresh),
            window_id);
    LogLine("ArrangeDwgViews window_repaint status=%d window=%d",
            static_cast<int>(st_repaint),
            window_id);
}

} // namespace

int RunPluginDrawingArrangeTask(const PluginDrawingArrangeRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=arrange-dwgviews reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("Run start: mode=arrange-dwgviews");

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR || current == nullptr) {
        LogLine("FAIL arrange-dwgviews reason=current-get");
        OpenPluginReportLog();
        return 0;
    }

    ProMdlType type = PRO_MDL_UNUSED;
    if (ProMdlTypeGet(current, &type) != PRO_TK_NO_ERROR || type != PRO_MDL_DRAWING) {
        LogLine("FAIL arrange-dwgviews reason=current-not-drawing");
        OpenPluginReportLog();
        return 0;
    }

    const ProError st_display = ProMdlDisplay(current);
    LogLine("ArrangeDwgViews display status=%d", static_cast<int>(st_display));
    if (st_display != PRO_TK_NO_ERROR) {
        OpenPluginReportLog();
        return 0;
    }

    const ProDrawing drawing = reinterpret_cast<ProDrawing>(current);
    int sheet = 0;
    const ProError st_sheet = ProDrawingCurrentSheetGet(drawing, &sheet);
    if (st_sheet != PRO_TK_NO_ERROR) {
        LogLine("FAIL arrange-dwgviews reason=current-sheet status=%d", static_cast<int>(st_sheet));
        OpenPluginReportLog();
        return 0;
    }

    autobbox::application::DrawingArrangeOptions options;
    bool cancelled = false;
    std::wstring option_error;
    if (!autobbox::ui::PromptDrawingArrangeOptionsDialog(options, cancelled, option_error)) {
        if (cancelled) {
            LogLine("CANCEL arrange-dwgviews reason=options-dialog");
        } else {
            LogLine("FAIL arrange-dwgviews reason=options-dialog error=%s",
                    autobbox::common::WToA(option_error.c_str()).c_str());
            autobbox::ui::ShowSimpleMessageDialog(
                PROUIMESSAGE_ERROR,
                L"视图整理",
                option_error.empty() ? L"无法读取视图整理选项。" : option_error.c_str());
        }
        return 0;
    }
    LogLine("ArrangeDwgViews options add_frame=%d update_model_title=%d",
            options.add_frame ? 1 : 0,
            options.update_model_title ? 1 : 0);

    const autobbox::application::DrawingArrangeSummary summary =
        autobbox::application::ExecuteArrangeSelectedDrawingViewsTask(
            drawing,
            sheet,
            options,
            [](const std::string &line) { LogPluginReportLine(line); });

    if (summary.groups_arranged > 0) {
        RefreshCurrentWindow();
        wchar_t message[256] = {0};
        std::swprintf(message,
                      sizeof(message) / sizeof(message[0]),
                      L"已整理 %d 组视图，移动 %d 个视图。\n图框：新增 %d，删除旧图框 %d。\n名称/数量：更新 %d 处。",
                      summary.groups_arranged,
                      summary.moved_views,
                      summary.frames_created,
                      summary.frames_deleted,
                      summary.title_notes_updated);
        autobbox::ui::ShowSimpleMessageDialog(PROUIMESSAGE_INFO, L"视图整理", message);
    } else if (summary.valid_views < 2) {
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"视图整理",
            L"请先在当前页框选至少两个工程图视图，再点击“视图整理”。");
    } else {
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"视图整理",
            L"已触发整理命令，但当前选中视图未能稳定识别成可整理的视图组。");
    }

    OpenPluginReportLog();
    return 0;
}

} // namespace autobbox::main
