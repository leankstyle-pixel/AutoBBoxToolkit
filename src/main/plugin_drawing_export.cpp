#include "autobbox/main/plugin_drawing_export.h"

#include "autobbox/application/drawing_export.h"
#include "autobbox/common/strings.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/drawing_export_dialog.h"
#include "autobbox/ui/message_dialog.h"

#include <ProMdl.h>
#include <ProToolkit.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>
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

std::wstring FirstFailedPath(const autobbox::core::DrawingExportResult &result)
{
    for (const autobbox::core::DrawingExportedFile &file : result.files) {
        if (file.status != PRO_TK_NO_ERROR) {
            return file.path;
        }
    }
    return std::wstring();
}

void ShowResultMessage(const autobbox::core::DrawingExportResult &result)
{
    wchar_t message[1024] = {0};
    if (result.success) {
        std::swprintf(message,
                      sizeof(message) / sizeof(message[0]),
                      L"\u5bfc\u51fa\u5b8c\u6210\uff1a%d/%d \u4e2a\u6587\u4ef6\u3002\n\u8f93\u51fa\u76ee\u5f55\uff1a%s",
                      result.exports_succeeded,
                      result.exports_attempted,
                      result.output_dir.c_str());
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_INFO,
            L"\u5de5\u7a0b\u56fe\u5bfc\u51fa",
            message);
        return;
    }

    const std::wstring failed_path = FirstFailedPath(result);
    std::swprintf(message,
                  sizeof(message) / sizeof(message[0]),
                  L"\u5bfc\u51fa\u672a\u5168\u90e8\u6210\u529f\uff1a%d/%d \u4e2a\u6587\u4ef6\u3002\n\u72b6\u6001\uff1a%d\n%s%s",
                  result.exports_succeeded,
                  result.exports_attempted,
                  static_cast<int>(result.status),
                  failed_path.empty() ? L"" : L"\u9996\u4e2a\u5931\u8d25\u6587\u4ef6\uff1a",
                  failed_path.empty() ? L"" : failed_path.c_str());
    autobbox::ui::ShowSimpleMessageDialog(
        PROUIMESSAGE_WARNING,
        L"\u5de5\u7a0b\u56fe\u5bfc\u51fa",
        message);
}

} // namespace

int RunPluginDrawingExportTask(const PluginDrawingExportRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=drawing-export reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("Run start: mode=drawing-export");

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR || current == nullptr) {
        LogLine("FAIL drawing-export reason=current-get");
        OpenPluginReportLog();
        return 0;
    }

    ProMdlType type = PRO_MDL_UNUSED;
    if (ProMdlTypeGet(current, &type) != PRO_TK_NO_ERROR || type != PRO_MDL_DRAWING) {
        LogLine("FAIL drawing-export reason=current-not-drawing");
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u5de5\u7a0b\u56fe\u5bfc\u51fa",
            L"\u8bf7\u5148\u6fc0\u6d3b\u4e00\u4e2a\u5de5\u7a0b\u56fe\u540e\u518d\u5bfc\u51fa\u3002");
        OpenPluginReportLog();
        return 0;
    }

    const ProError st_display = ProMdlDisplay(current);
    LogLine("DrawingExport display status=%d", static_cast<int>(st_display));
    if (st_display != PRO_TK_NO_ERROR) {
        OpenPluginReportLog();
        return 0;
    }

    autobbox::core::DrawingExportRequest request = {};
    request.format = autobbox::core::DrawingExportFormat::Pdf;
    request.dwg_mode = autobbox::core::DwgExportMode::MultiLayoutFile;
    std::vector<autobbox::core::DrawingExportSheetChoice> sheet_choices;
    int current_sheet = 1;
    const ProError st_sheets = autobbox::application::CollectDrawingExportSheets(
        current,
        sheet_choices,
        current_sheet,
        [](const std::string &line) { LogPluginReportLine(line); });
    if (st_sheets == PRO_TK_NO_ERROR) {
        request.selected_sheet = current_sheet;
        request.selected_sheets = {current_sheet};
    } else {
        LogLine("DrawingExport collect-sheets status=%d", static_cast<int>(st_sheets));
    }
    bool cancelled = false;
    if (!autobbox::ui::PromptDrawingExportOptions(
            request,
            sheet_choices,
            cancelled,
            [](const std::string &line) { LogPluginReportLine(line); })) {
        LogLine("DrawingExport selection_dialog status=%s", cancelled ? "cancelled" : "failed");
        if (!cancelled) {
            OpenPluginReportLog();
        }
        return 0;
    }

    const autobbox::core::DrawingExportResult result =
        autobbox::application::ExecuteDrawingExportTask(
            current,
            request,
            [](const std::string &line) { LogPluginReportLine(line); });

    ShowResultMessage(result);
    OpenPluginReportLog();
    return 0;
}

} // namespace autobbox::main
