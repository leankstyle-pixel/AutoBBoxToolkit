#include "autobbox/main/plugin_drawing_view_brush.h"

#include "autobbox/application/drawing_view_brush.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/drawing_view_brush_dialog.h"
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
    LogLine("ViewBrush window_get status=%d window=%d",
            static_cast<int>(st_get_window),
            window_id);
    if (st_get_window != PRO_TK_NO_ERROR) {
        return;
    }

    const ProError st_refresh = ProWindowRefresh(window_id);
    const ProError st_repaint = ProWindowRepaint(window_id);
    LogLine("ViewBrush window_refresh status=%d window=%d",
            static_cast<int>(st_refresh),
            window_id);
    LogLine("ViewBrush window_repaint status=%d window=%d",
            static_cast<int>(st_repaint),
            window_id);
}

void ShowBrushSummaryMessage(autobbox::application::DrawingViewBrushMode mode,
                             const autobbox::application::DrawingViewBrushOrientation &orientation,
                             const autobbox::application::DrawingViewBrushSummary &summary)
{
    wchar_t message[768] = {0};
    std::swprintf(message,
                  sizeof(message) / sizeof(message[0]),
                  L"\u89c6\u56fe\u5237\u5b8c\u6210\n\n\u6a21\u5f0f\uff1a%s\n\u76ee\u6807\u89c6\u5411\uff1a%s\n\n\u5df2\u5237\u89c6\u56fe\uff1a%d\n\u7531\u6295\u5f71/\u884d\u751f\u89c6\u56fe\u8bc6\u522b\u5230\u4e3b\u89c6\u56fe\uff1a%d\n\u8df3\u8fc7\u8f74\u6d4b\u56fe\uff1a%d\n\u8df3\u8fc7\u975e\u8f74\u6d4b\u56fe\uff1a%d\n\u8df3\u8fc7\u975e\u4e3b/General\u89c6\u56fe\uff1a%d\n\u8df3\u8fc7\u5176\u4ed6\u9875\uff1a%d\n\u5931\u8d25\uff1a%d\n\u56fe\u7eb8\u91cd\u751f\uff1a%s",
                  mode == autobbox::application::DrawingViewBrushMode::AxonometricView
                      ? L"\u8f74\u6d4b\u56fe"
                      : L"\u4e3b\u89c6\u56fe",
                  orientation.label.empty() ? L"custom / matrix" : orientation.label.c_str(),
                  summary.brushed,
                  summary.derived_resolved_to_main,
                  summary.skipped_axonometric,
                  summary.skipped_non_axonometric,
                  summary.skipped_non_general,
                  summary.skipped_other_sheet,
                  summary.failed,
                  summary.sheet_regenerated ? L"\u6210\u529f" : (summary.brushed > 0 ? L"\u5931\u8d25" : L"\u672a\u9700\u8981"));
    autobbox::ui::ShowSimpleMessageDialog(
        summary.failed == 0 && (summary.regenerate_error == PRO_TK_NO_ERROR || summary.brushed == 0)
            ? PROUIMESSAGE_INFO
            : PROUIMESSAGE_WARNING,
        L"\u89c6\u56fe\u5237",
        message);
}

} // namespace

int RunPluginDrawingViewBrushTask(const PluginDrawingViewBrushRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=view-brush reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("Run start: mode=view-brush");

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        autobbox::creo::ModelType(current) != PRO_MDL_DRAWING) {
        LogLine("FAIL view-brush reason=current-not-drawing");
        OpenPluginReportLog();
        return 0;
    }

    const ProError st_display = ProMdlDisplay(current);
    LogLine("ViewBrush display status=%d", static_cast<int>(st_display));
    if (st_display != PRO_TK_NO_ERROR) {
        OpenPluginReportLog();
        return 0;
    }

    const ProDrawing drawing = reinterpret_cast<ProDrawing>(current);
    int sheet = 0;
    const ProError st_sheet = ProDrawingCurrentSheetGet(drawing, &sheet);
    if (st_sheet != PRO_TK_NO_ERROR) {
        LogLine("FAIL view-brush reason=current-sheet status=%d", static_cast<int>(st_sheet));
        OpenPluginReportLog();
        return 0;
    }

    autobbox::ui::DrawingViewBrushDialogRequest request = {};
    bool cancelled = false;
    if (!autobbox::ui::PromptDrawingViewBrushOptions(
            request,
            cancelled,
            [](const std::string &line) { LogPluginReportLine(line); })) {
        LogLine("ViewBrush option_dialog status=%s", cancelled ? "cancelled" : "failed");
        if (!cancelled) {
            OpenPluginReportLog();
        }
        return 0;
    }

    const autobbox::application::DrawingViewBrushTargetSet targets =
        autobbox::application::CaptureDrawingViewBrushTargetsFromSelectionBuffer(
            drawing,
            sheet,
            request.mode,
            [](const std::string &line) { LogPluginReportLine(line); });
    LogLine("ViewBrush preselected mode=%d selected=%d target=%d derived_to_main=%d skip_axon=%d skip_non_axon=%d failed=%d last_error=%d",
            static_cast<int>(request.mode),
            targets.selected_total,
            targets.main_views_found,
            targets.derived_resolved_to_main,
            targets.skipped_axonometric,
            targets.skipped_non_axonometric,
            targets.failed,
            static_cast<int>(targets.last_error));
    if (targets.failed > 0) {
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_ERROR,
            L"\u89c6\u56fe\u5237",
            L"\u8bfb\u53d6\u5f53\u524d Creo \u9009\u62e9\u96c6\u5931\u8d25\uff0c\u8bf7\u91cd\u65b0\u70b9\u9009/\u6846\u9009\u5de5\u7a0b\u56fe\u89c6\u56fe\u540e\u518d\u6267\u884c\u3002");
        OpenPluginReportLog();
        return 0;
    }
    if (targets.selected_total == 0 || targets.main_views.empty()) {
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u89c6\u56fe\u5237",
            request.mode == autobbox::application::DrawingViewBrushMode::AxonometricView
                ? L"\u8bf7\u5148\u7528 Creo \u539f\u751f\u65b9\u5f0f\u70b9\u9009\u6216\u6846\u9009\u76ee\u6807\u8f74\u6d4b\u56fe\uff0c\u518d\u70b9\u51fb\u201c\u89c6\u56fe\u5237\u201d\u9009\u62e9\u8f74\u6d4b\u56fe\u6a21\u5f0f\u3002"
                : L"\u8bf7\u5148\u7528 Creo \u539f\u751f\u65b9\u5f0f\u70b9\u9009\u6216\u6846\u9009\u76ee\u6807\u4e3b\u89c6\u56fe/\u6295\u5f71\u89c6\u56fe\uff0c\u518d\u70b9\u51fb\u201c\u89c6\u56fe\u5237\u201d\u3002\n\n\u4e3b\u89c6\u56fe\u6a21\u5f0f\u4e0b\u8f74\u6d4b\u56fe\u4f1a\u88ab\u8df3\u8fc7\u3002");
        OpenPluginReportLog();
        return 0;
    }

    autobbox::application::DrawingViewBrushOrientation orientation = {};
    if (request.source == autobbox::application::DrawingViewBrushSource::ManualPreset) {
        orientation = autobbox::application::MakeDrawingViewBrushPresetOrientation(request.preset);
        LogLine("ViewBrush source=manual preset=%d valid=%d",
                static_cast<int>(request.preset),
                orientation.valid ? 1 : 0);
    } else {
        if (!autobbox::application::CaptureReferenceDrawingViewBrushOrientation(
                drawing,
                sheet,
                request.mode,
                orientation,
                cancelled,
                [](const std::string &line) { LogPluginReportLine(line); })) {
            LogLine("ViewBrush reference_capture status=%s", cancelled ? "cancelled" : "failed");
            if (!cancelled) {
                OpenPluginReportLog();
            }
            return 0;
        }
        LogLine("ViewBrush source=reference valid=%d", orientation.valid ? 1 : 0);
    }

    if (!orientation.valid) {
        LogLine("FAIL view-brush reason=invalid-orientation");
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_ERROR,
            L"\u89c6\u56fe\u5237",
            L"\u672a\u80fd\u83b7\u53d6\u6709\u6548\u89c6\u5411\u3002");
        OpenPluginReportLog();
        return 0;
    }

    const autobbox::application::DrawingViewBrushSummary summary =
        autobbox::application::ExecuteDrawingViewBrushForTargets(
            drawing,
            sheet,
            orientation,
            targets,
            [](const std::string &line) { LogPluginReportLine(line); });

    if (summary.brushed > 0) {
        RefreshCurrentWindow();
    }
    ShowBrushSummaryMessage(request.mode, orientation, summary);

    OpenPluginReportLog();
    return 0;
}

} // namespace autobbox::main
