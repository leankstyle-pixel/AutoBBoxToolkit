#include "autobbox/main/plugin_balloon_arrange.h"

#include "autobbox/application/balloon_arrange.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/balloon_arrange_dialog.h"
#include "autobbox/ui/message_dialog.h"

#include <ProDrawing.h>
#include <ProDwgtable.h>
#include <ProMdl.h>
#include <ProSelection.h>
#include <ProToolkit.h>
#include <ProWindows.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <string>

namespace autobbox::main {

namespace {

constexpr const wchar_t *kTitle = L"\u7403\u6807\u6574\u7406";

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
    LogLine("ArrangeBalloons window_get status=%d window=%d",
            static_cast<int>(st_get_window),
            window_id);
    if (st_get_window != PRO_TK_NO_ERROR) {
        return;
    }

    const ProError st_refresh = ProWindowRefresh(window_id);
    const ProError st_repaint = ProWindowRepaint(window_id);
    LogLine("ArrangeBalloons window_refresh status=%d window=%d",
            static_cast<int>(st_refresh),
            window_id);
    LogLine("ArrangeBalloons window_repaint status=%d window=%d",
            static_cast<int>(st_repaint),
            window_id);
}

void ShowSummaryMessage(const autobbox::application::BalloonArrangeSummary &summary)
{
    if (summary.notes_reordered > 0 && summary.notes_reorder_failed == 0) {
        wchar_t message[256] = {0};
        std::swprintf(message,
                      sizeof(message) / sizeof(message[0]),
                      L"\u5df2\u6574\u7406 %d \u4e2a\u5df2\u6709\u5b98\u65b9 BOM \u7403\u6807\u3002",
                      summary.notes_reordered);
        autobbox::ui::ShowSimpleMessageDialog(PROUIMESSAGE_INFO, kTitle, message);
        return;
    }

    if (summary.notes_reordered > 0 && summary.notes_reorder_failed > 0) {
        wchar_t message[256] = {0};
        std::swprintf(message,
                      sizeof(message) / sizeof(message[0]),
                      L"\u5df2\u6574\u7406 %d \u4e2a\u5b98\u65b9 BOM \u7403\u6807\uff0c%d \u4e2a\u79fb\u52a8\u5931\u8d25\u3002\u8be6\u60c5\u89c1 report\u3002",
                      summary.notes_reordered,
                      summary.notes_reorder_failed);
        autobbox::ui::ShowSimpleMessageDialog(PROUIMESSAGE_WARNING, kTitle, message);
        return;
    }

    if (summary.notes_reordered <= 0 && summary.notes_reorder_failed > 0) {
        wchar_t message[256] = {0};
        std::swprintf(message,
                      sizeof(message) / sizeof(message[0]),
                      L"\u627e\u5230 %d \u4e2a\u5b98\u65b9 BOM \u7403\u6807\uff0c\u4f46\u79fb\u52a8\u5931\u8d25\u3002\u8be6\u60c5\u89c1 report\u3002",
                      summary.notes_reorder_failed);
        autobbox::ui::ShowSimpleMessageDialog(PROUIMESSAGE_WARNING, kTitle, message);
        return;
    }

    if (summary.valid_views > 0 && summary.selected_total <= 0) {
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            kTitle,
            L"\u6240\u9009\u89c6\u56fe\u6ca1\u6709\u53ef\u6574\u7406\u7684\u5df2\u6709\u5b98\u65b9 BOM \u7403\u6807\u3002");
        return;
    }

    if ((summary.custom_balloons_created > 0 || summary.custom_balloons_updated > 0) &&
        summary.custom_balloons_failed == 0) {
        wchar_t message[256] = {0};
        std::swprintf(message,
                      sizeof(message) / sizeof(message[0]),
                      L"\u5df2\u66f4\u65b0 %d \u4e2a\u5df2\u6709 note \u7403\u6807\uff0c\u751f\u6210 %d \u4e2a\u81ea\u5b9a\u4e49 note \u7403\u6807\u3002",
                      summary.custom_balloons_updated,
                      summary.custom_balloons_created);
        autobbox::ui::ShowSimpleMessageDialog(PROUIMESSAGE_INFO, kTitle, message);
        return;
    }

    if (summary.custom_balloons_created > 0 && summary.custom_balloons_failed > 0) {
        wchar_t message[256] = {0};
        std::swprintf(message,
                      sizeof(message) / sizeof(message[0]),
                      L"\u5df2\u751f\u6210 %d \u4e2a\u81ea\u5b9a\u4e49 note \u7403\u6807\uff0c%d \u4e2a\u5931\u8d25\u3002\u8be6\u60c5\u89c1 report\u3002",
                      summary.custom_balloons_created,
                      summary.custom_balloons_failed);
        autobbox::ui::ShowSimpleMessageDialog(PROUIMESSAGE_WARNING, kTitle, message);
        return;
    }

    if (summary.views_arranged > 0 && summary.views_failed == 0) {
        wchar_t message[256] = {0};
        std::swprintf(message,
                      sizeof(message) / sizeof(message[0]),
                      L"\u5df2\u6574\u7406 %d \u4e2a\u89c6\u56fe\u7684\u81ea\u5b9a\u4e49 note \u7403\u6807\uff0c\u4e8c\u6b21\u91cd\u6392 %d \u4e2a\u7403\u6807\u3002",
                      summary.views_arranged,
                      summary.notes_reordered);
        autobbox::ui::ShowSimpleMessageDialog(PROUIMESSAGE_INFO, kTitle, message);
        return;
    }

    if (summary.views_arranged > 0 && summary.views_failed > 0) {
        wchar_t message[256] = {0};
        std::swprintf(message,
                      sizeof(message) / sizeof(message[0]),
                      L"\u5df2\u6574\u7406 %d \u4e2a\u89c6\u56fe\uff0c%d \u4e2a\u89c6\u56fe\u5931\u8d25\u3002\u8be6\u60c5\u89c1 report\u3002",
                      summary.views_arranged,
                      summary.views_failed);
        autobbox::ui::ShowSimpleMessageDialog(PROUIMESSAGE_WARNING, kTitle, message);
        return;
    }

    if (summary.selected_total <= 0) {
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            kTitle,
            L"\u8bf7\u5148\u9009\u62e9\u76ee\u6807\u89c6\u56fe\uff0c\u5e76\u786e\u8ba4\u89c6\u56fe\u4e2d\u5df2\u6709\u5b98\u65b9 BOM \u7403\u6807\u3002");
        return;
    }

    if (summary.valid_views <= 0) {
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            kTitle,
            L"\u672a\u80fd\u8bc6\u522b BOM \u8868\u5143\u4ef6\u6216\u76ee\u6807\u89c6\u56fe\uff1b\u8bf7\u9009\u4e2d BOM \u8868\u91cd\u590d\u533a\u57df\u5185\u7684\u5355\u5143\u683c\u3002");
        return;
    }

    autobbox::ui::ShowSimpleMessageDialog(
        PROUIMESSAGE_WARNING,
        kTitle,
        L"\u5df2\u89e6\u53d1\u81ea\u5b9a\u4e49 note \u7403\u6807\u6574\u7406\uff0c\u4f46\u6240\u9009\u89c6\u56fe\u672a\u80fd\u6210\u529f\u751f\u6210\u6216\u66f4\u65b0\u3002\u8be6\u60c5\u89c1 report\u3002");
}

void ShowRebuildSummaryMessage(const autobbox::application::BalloonArrangeSummary &summary)
{
    if (summary.custom_balloons_created > 0 && summary.custom_balloons_failed == 0) {
        wchar_t message[256] = {0};
        std::swprintf(message,
                      sizeof(message) / sizeof(message[0]),
                      L"\u5df2\u91cd\u5efa %d \u4e2a\u95ee\u9898\u5b98\u65b9 BOM \u7403\u6807\u3002",
                      summary.custom_balloons_created);
        autobbox::ui::ShowSimpleMessageDialog(PROUIMESSAGE_INFO, kTitle, message);
        return;
    }
    if (summary.custom_balloons_created > 0 && summary.custom_balloons_failed > 0) {
        wchar_t message[256] = {0};
        std::swprintf(message,
                      sizeof(message) / sizeof(message[0]),
                      L"\u5df2\u91cd\u5efa %d \u4e2a\uff0c%d \u4e2a\u5931\u8d25\u6216\u5df2\u6062\u590d\u3002\u8be6\u60c5\u89c1 report\u3002",
                      summary.custom_balloons_created,
                      summary.custom_balloons_failed);
        autobbox::ui::ShowSimpleMessageDialog(PROUIMESSAGE_WARNING, kTitle, message);
        return;
    }
    if (summary.selected_total > 0 && summary.custom_balloons_failed <= 0) {
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_INFO,
            kTitle,
            L"\u672a\u53d1\u73b0\u9700\u8981\u91cd\u5efa\u7684\u9519\u8fb9\u6216\u957f\u5f15\u7ebf\u5b98\u65b9 BOM \u7403\u6807\u3002");
        return;
    }
    autobbox::ui::ShowSimpleMessageDialog(
        PROUIMESSAGE_WARNING,
        kTitle,
        L"\u672a\u80fd\u5b8c\u6210\u5b98\u65b9 BOM \u7403\u6807\u91cd\u5efa\uff1b\u8be6\u60c5\u89c1 report\u3002");
}

bool PromptBomTableSelection(autobbox::application::BalloonArrangeBomTableSelection &table_out)
{
    table_out = {};
    autobbox::ui::ShowSimpleMessageDialog(
        PROUIMESSAGE_INFO,
        kTitle,
        L"\u8bf7\u5728 BOM \u8868\u91cd\u590d\u533a\u57df\u5185\u4efb\u9009\u4e00\u4e2a\u5355\u5143\u683c\u3002");

    char filter[] = "table_cell";
    ProSelection *selections = nullptr;
    int selection_count = 0;
    const ProError st_select =
        ProSelect(filter, 1, nullptr, nullptr, nullptr, nullptr, &selections, &selection_count);
    LogLine("ArrangeBalloons select-bom-table status=%d count=%d filter=%s",
            static_cast<int>(st_select),
            selection_count,
            filter);
    if (st_select != PRO_TK_NO_ERROR || selection_count < 1 || selections == nullptr) {
        return false;
    }

    ProDwgtable table = {};
    int segment = PRO_VALUE_UNUSED;
    int row0 = 0;
    int col0 = 0;
    const ProError st_table = ProSelectionDwgtableGet(selections[0], &table);
    const ProError st_cell = ProSelectionDwgtblcellGet(selections[0], &segment, &row0, &col0);
    (void)ProSelectionUnhighlight(selections[0]);
    LogLine("ArrangeBalloons select-bom-table parse table_status=%d cell_status=%d table_owner=%p segment=%d row=%d col=%d",
            static_cast<int>(st_table),
            static_cast<int>(st_cell),
            static_cast<void *>(table.owner),
            segment,
            row0 + 1,
            col0 + 1);
    if (st_table != PRO_TK_NO_ERROR || st_cell != PRO_TK_NO_ERROR || table.owner == nullptr) {
        return false;
    }

    table_out.table = table;
    table_out.segment = segment;
    table_out.selected_row = row0 + 1;
    table_out.selected_column = col0 + 1;
    return true;
}

bool PromptTargetViewSelection(ProView &view_out)
{
    view_out = nullptr;
    autobbox::ui::ShowSimpleMessageDialog(
        PROUIMESSAGE_INFO,
        kTitle,
        L"\u8bf7\u9009\u62e9\u8981\u6574\u7406\u5df2\u6709\u5b98\u65b9 BOM \u7403\u6807\u7684\u5de5\u7a0b\u56fe\u89c6\u56fe\u3002");

    char filter[] = "dwg_view";
    ProSelection *selections = nullptr;
    int selection_count = 0;
    const ProError st_select =
        ProSelect(filter, 1, nullptr, nullptr, nullptr, nullptr, &selections, &selection_count);
    LogLine("ArrangeBalloons select-target-view status=%d count=%d filter=%s",
            static_cast<int>(st_select),
            selection_count,
            filter);
    if (st_select != PRO_TK_NO_ERROR || selection_count < 1 || selections == nullptr) {
        return false;
    }

    const ProError st_view = ProSelectionViewGet(selections[0], &view_out);
    (void)ProSelectionUnhighlight(selections[0]);
    LogLine("ArrangeBalloons select-target-view parse status=%d view=%p",
            static_cast<int>(st_view),
            static_cast<void *>(view_out));
    return st_view == PRO_TK_NO_ERROR && view_out != nullptr;
}


} // namespace

int RunPluginBalloonArrangeTask(const PluginBalloonArrangeRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=arrange-balloons reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("Run start: mode=arrange-balloons");

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR || current == nullptr) {
        LogLine("FAIL arrange-balloons reason=current-get");
        OpenPluginReportLog();
        return 0;
    }

    ProMdlType type = PRO_MDL_UNUSED;
    if (ProMdlTypeGet(current, &type) != PRO_TK_NO_ERROR || type != PRO_MDL_DRAWING) {
        LogLine("FAIL arrange-balloons reason=current-not-drawing");
        OpenPluginReportLog();
        return 0;
    }

    const ProError st_display = ProMdlDisplay(current);
    LogLine("ArrangeBalloons display status=%d", static_cast<int>(st_display));
    if (st_display != PRO_TK_NO_ERROR) {
        OpenPluginReportLog();
        return 0;
    }

    const ProDrawing drawing = reinterpret_cast<ProDrawing>(current);
    int sheet = 0;
    const ProError st_sheet = ProDrawingCurrentSheetGet(drawing, &sheet);
    if (st_sheet != PRO_TK_NO_ERROR) {
        LogLine("FAIL arrange-balloons reason=current-sheet status=%d", static_cast<int>(st_sheet));
        OpenPluginReportLog();
        return 0;
    }

    ProView target_view = nullptr;
    if (!PromptTargetViewSelection(target_view)) {
        LogLine("SKIP arrange-balloons reason=target-view-not-selected");
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            kTitle,
            L"\u672a\u9009\u5230\u76ee\u6807\u89c6\u56fe\uff0c\u5df2\u53d6\u6d88\u7403\u6807\u6574\u7406\u3002");
        OpenPluginReportLog();
        return 0;
    }

    const autobbox::application::BalloonArrangeSummary summary =
        autobbox::application::ExecuteArrangeTraditionalBalloonsTask(
            drawing,
            sheet,
            target_view,
            [](const std::string &line) { LogPluginReportLine(line); });

    if (summary.views_arranged > 0 || summary.notes_reordered > 0) {
        RefreshCurrentWindow();
    }
    ShowSummaryMessage(summary);

    OpenPluginReportLog();
    return 0;
}

int RunPluginRebuildBalloonsTask(const PluginBalloonArrangeRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=rebuild-balloons reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("Run start: mode=rebuild-balloons");

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR || current == nullptr) {
        LogLine("FAIL rebuild-balloons reason=current-get");
        OpenPluginReportLog();
        return 0;
    }

    ProMdlType type = PRO_MDL_UNUSED;
    if (ProMdlTypeGet(current, &type) != PRO_TK_NO_ERROR || type != PRO_MDL_DRAWING) {
        LogLine("FAIL rebuild-balloons reason=current-not-drawing");
        OpenPluginReportLog();
        return 0;
    }

    const ProError st_display = ProMdlDisplay(current);
    LogLine("RebuildBalloons display status=%d", static_cast<int>(st_display));
    if (st_display != PRO_TK_NO_ERROR) {
        OpenPluginReportLog();
        return 0;
    }

    const ProDrawing drawing = reinterpret_cast<ProDrawing>(current);
    int sheet = 0;
    const ProError st_sheet = ProDrawingCurrentSheetGet(drawing, &sheet);
    if (st_sheet != PRO_TK_NO_ERROR) {
        LogLine("FAIL rebuild-balloons reason=current-sheet status=%d", static_cast<int>(st_sheet));
        OpenPluginReportLog();
        return 0;
    }

    ProView target_view = nullptr;
    if (!PromptTargetViewSelection(target_view)) {
        LogLine("SKIP rebuild-balloons reason=target-view-not-selected");
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            kTitle,
            L"\u672a\u9009\u5230\u76ee\u6807\u89c6\u56fe\uff0c\u5df2\u53d6\u6d88\u5b98\u65b9\u7403\u6807\u91cd\u5efa\u3002");
        OpenPluginReportLog();
        return 0;
    }

    autobbox::application::BalloonArrangeBomTableSelection bom_table = {};
    if (!PromptBomTableSelection(bom_table)) {
        LogLine("SKIP rebuild-balloons reason=bom-table-not-selected");
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            kTitle,
            L"\u672a\u9009\u5230 BOM \u8868\u91cd\u590d\u533a\u57df\u5355\u5143\u683c\uff0c\u5df2\u53d6\u6d88\u5b98\u65b9\u7403\u6807\u91cd\u5efa\u3002");
        OpenPluginReportLog();
        return 0;
    }

    const autobbox::application::BalloonArrangeSummary summary =
        autobbox::application::ExecuteRebuildProblemTraditionalBalloonsTask(
            drawing,
            sheet,
            bom_table,
            target_view,
            [](const std::string &line) { LogPluginReportLine(line); });

    if (summary.views_arranged > 0 || summary.custom_balloons_created > 0) {
        RefreshCurrentWindow();
    }
    ShowRebuildSummaryMessage(summary);

    OpenPluginReportLog();
    return 0;
}

} // namespace autobbox::main

