#include "autobbox/main/plugin_drawing3.h"

#include "autobbox/application/drawing3_views.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/drawing3_dialog.h"
#include "autobbox/ui/message_dialog.h"

#include <ProDrawing.h>
#include <ProDwgtable.h>
#include <ProMdl.h>
#include <ProSelection.h>
#include <ProSolid.h>
#include <ProToolkit.h>
#include <ProUIMessage.h>
#include <ProArray.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace autobbox::main {

namespace {

constexpr const wchar_t *kDrawing3Title = L"\u5efa\u89c6\u56fe";

enum class Drawing3SourceMode {
    BomTable,
    CurrentModel,
    Cancel
};

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

Drawing3SourceMode PromptDrawing3SourceMode()
{
    ProUIMessageButton choice = PRO_UI_MESSAGE_CANCEL;
    ProUIMessageButton *buttons = nullptr;
    if (ProArrayAlloc(0, sizeof(ProUIMessageButton), 1, reinterpret_cast<ProArray *>(&buttons)) != PRO_TK_NO_ERROR ||
        buttons == nullptr) {
        return Drawing3SourceMode::CurrentModel;
    }

    ProUIMessageButton button = PRO_UI_MESSAGE_YES;
    ProArrayObjectAdd(reinterpret_cast<ProArray *>(&buttons), PRO_VALUE_UNUSED, 1, &button);
    button = PRO_UI_MESSAGE_NO;
    ProArrayObjectAdd(reinterpret_cast<ProArray *>(&buttons), PRO_VALUE_UNUSED, 1, &button);
    button = PRO_UI_MESSAGE_CANCEL;
    ProArrayObjectAdd(reinterpret_cast<ProArray *>(&buttons), PRO_VALUE_UNUSED, 1, &button);

    const ProError st = ProUIMessageDialogDisplay(
        PROUIMESSAGE_INFO,
        const_cast<wchar_t *>(kDrawing3Title),
        const_cast<wchar_t *>(L"\u662f\uff1a\u9009\u62e9 BOM \u8868\u5e76\u6279\u91cf\u5efa\u89c6\u56fe\n\u5426\uff1a\u4f7f\u7528\u5f53\u524d\u5de5\u7a0b\u56fe\u6a21\u578b\n\u53d6\u6d88\uff1a\u9000\u51fa"),
        buttons,
        PRO_UI_MESSAGE_YES,
        &choice);
    ProArrayFree(reinterpret_cast<ProArray *>(&buttons));

    if (st != PRO_TK_NO_ERROR || choice == PRO_UI_MESSAGE_CANCEL) {
        return Drawing3SourceMode::Cancel;
    }
    return choice == PRO_UI_MESSAGE_YES ? Drawing3SourceMode::BomTable : Drawing3SourceMode::CurrentModel;
}

bool PromptBomTableSelection(ProDwgtable &table_out, int &segment_out)
{
    table_out = {};
    segment_out = PRO_VALUE_UNUSED;

    ProUIMessageButton choice = PRO_UI_MESSAGE_OK;
    ProUIMessageButton *buttons = nullptr;
    if (ProArrayAlloc(0, sizeof(ProUIMessageButton), 1, reinterpret_cast<ProArray *>(&buttons)) == PRO_TK_NO_ERROR &&
        buttons != nullptr) {
        ProUIMessageButton button = PRO_UI_MESSAGE_OK;
        ProArrayObjectAdd(reinterpret_cast<ProArray *>(&buttons), PRO_VALUE_UNUSED, 1, &button);
        ProUIMessageDialogDisplay(
            PROUIMESSAGE_INFO,
            const_cast<wchar_t *>(kDrawing3Title),
            const_cast<wchar_t *>(L"\u8bf7\u5728 BOM \u8868\u91cd\u590d\u533a\u57df\u5185\u4efb\u9009\u4e00\u4e2a\u5355\u5143\u683c\u3002"),
            buttons,
            PRO_UI_MESSAGE_OK,
            &choice);
        ProArrayFree(reinterpret_cast<ProArray *>(&buttons));
    }

    char filter[] = "table_cell";
    ProSelection *selections = nullptr;
    int selection_count = 0;
    const ProError st_select =
        ProSelect(filter, 1, nullptr, nullptr, nullptr, nullptr, &selections, &selection_count);
    LogLine("Drawing3 select-bom-table status=%d count=%d filter=%s",
            static_cast<int>(st_select),
            selection_count,
            filter);
    if (st_select != PRO_TK_NO_ERROR || selection_count < 1 || selections == nullptr) {
        // Keep this consistent with the existing BOM balloon selection flow:
        // interactive ProSelect table-cell handles are released by Creo after the
        // selection operation. Explicitly freeing here can invalidate the copied
        // ProDwgtable handle before follow-up table APIs use it.
        return false;
    }

    ProDwgtable table = {};
    int segment = PRO_VALUE_UNUSED;
    int row0 = 0;
    int col0 = 0;
    const ProError st_table = ProSelectionDwgtableGet(selections[0], &table);
    const ProError st_cell = ProSelectionDwgtblcellGet(selections[0], &segment, &row0, &col0);
    (void)ProSelectionUnhighlight(selections[0]);
    LogLine("Drawing3 select-bom-table parse table_status=%d cell_status=%d table_owner=%p segment=%d row=%d col=%d",
            static_cast<int>(st_table),
            static_cast<int>(st_cell),
            static_cast<void *>(table.owner),
            segment,
            row0 + 1,
            col0 + 1);

    if (st_table != PRO_TK_NO_ERROR || st_cell != PRO_TK_NO_ERROR || table.owner == nullptr) {
        return false;
    }

    table_out = table;
    segment_out = (segment <= 0) ? PRO_VALUE_UNUSED : segment;
    LogLine("Drawing3 select-bom-table normalized_segment=%d", segment_out);
    return true;
}

std::string ModelTag(ProMdl mdl, const PluginDrawing3Runtime &runtime)
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

} // namespace

int RunPluginDrawing3Task(const PluginDrawing3Runtime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=dwg3 reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("Run start: mode=dwg3");

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        autobbox::creo::ModelType(current) != PRO_MDL_DRAWING) {
        LogLine("FAIL drawing3 reason=current-not-drawing");
        OpenPluginReportLog();
        return 0;
    }

    const ProDrawing drawing = reinterpret_cast<ProDrawing>(current);
    ProSolid root_solid = nullptr;
    ProError st = ProDrawingCurrentsolidGet(drawing, &root_solid);
    if (st != PRO_TK_NO_ERROR || root_solid == nullptr) {
        LogLine("FAIL drawing3 reason=current-solid status=%d", static_cast<int>(st));
        OpenPluginReportLog();
        return 0;
    }

    int sheet = 1;
    ProDrawingCurrentSheetGet(drawing, &sheet);
    LogLine("Drawing3 current_sheet=%d root=%s",
            sheet,
            ModelTag(reinterpret_cast<ProMdl>(root_solid), runtime).c_str());

    bool cancelled = false;
    std::vector<autobbox::core::Dwg3Candidate> selected;
    int candidates_total = 0;
    autobbox::core::Dwg3ViewMask selected_views = 0;
    bool quick_mode = true;
    autobbox::core::Dwg3FrameOptions frame_options;

    const Drawing3SourceMode source_mode = PromptDrawing3SourceMode();
    LogLine("Drawing3 source_mode=%s",
            source_mode == Drawing3SourceMode::BomTable
                ? "bom-table"
                : (source_mode == Drawing3SourceMode::CurrentModel ? "current-model" : "cancel"));
    if (source_mode == Drawing3SourceMode::Cancel) {
        LogLine("Drawing3 source selection cancelled");
        return 0;
    }

    bool target_prompt_ok = false;
    if (source_mode == Drawing3SourceMode::BomTable) {
        ProDwgtable bom_table = {};
        int bom_segment = PRO_VALUE_UNUSED;
        if (!PromptBomTableSelection(bom_table, bom_segment)) {
            LogLine("SKIP drawing3 reason=bom-table-not-selected");
            autobbox::ui::ShowSimpleMessageDialog(
                PROUIMESSAGE_WARNING,
                kDrawing3Title,
                L"\u672a\u9009\u5230 BOM \u8868\u5355\u5143\u683c\uff0c\u5df2\u53d6\u6d88\u5efa\u89c6\u56fe\u3002");
            OpenPluginReportLog();
            return 0;
        }

        std::vector<autobbox::core::Dwg3Candidate> bom_candidates =
            autobbox::application::CollectDrawingViewCandidatesFromBomTable(
                bom_table,
                bom_segment,
                [](const std::string &line) { LogPluginReportLine(line); });
        LogLine("Drawing3 bom candidates=%d segment=%d",
                static_cast<int>(bom_candidates.size()),
                bom_segment);
        if (bom_candidates.empty()) {
            LogLine("SKIP drawing3 reason=bom-table-no-models");
            autobbox::ui::ShowSimpleMessageDialog(
                PROUIMESSAGE_WARNING,
                kDrawing3Title,
                L"BOM \u8868\u5185\u672a\u89e3\u6790\u5230\u53ef\u5efa\u89c6\u56fe\u7684\u96f6\u4ef6\u6216\u7ec4\u4ef6\u6a21\u578b\u3002");
            OpenPluginReportLog();
            return 0;
        }

        target_prompt_ok = autobbox::ui::PromptDrawing3TargetsFromCandidates(
            root_solid,
            bom_candidates,
            L"\u5df2\u4ece BOM \u8868\u8bfb\u53d6\u6a21\u578b\uff0c\u8bf7\u52fe\u9009\u9700\u8981\u5efa\u89c6\u56fe\u7684\u6a21\u578b\uff1a",
            selected,
            candidates_total,
            selected_views,
            quick_mode,
            frame_options,
            cancelled,
            [](const std::string &line) { LogPluginReportLine(line); });
    } else {
        target_prompt_ok = autobbox::ui::PromptDrawing3Targets(
            root_solid,
            selected,
            candidates_total,
            selected_views,
            quick_mode,
            frame_options,
            cancelled,
            [](const std::string &line) { LogPluginReportLine(line); });
    }

    if (!target_prompt_ok) {
        LogLine("Drawing3 selection_dialog status=%s", cancelled ? "cancelled" : "failed");
        if (!cancelled) {
            OpenPluginReportLog();
        }
        return 0;
    }

    LogLine("Drawing3 selected_total=%d", static_cast<int>(selected.size()));
    LogLine("Drawing3 candidates_total=%d", candidates_total);
    if (selected.empty()) {
        LogLine("Drawing3 no models selected");
        OpenPluginReportLog();
        return 0;
    }
    LogLine("Drawing3 selected_views=%s",
            autobbox::common::WToA(autobbox::application::JoinDwg3ViewLabels(selected_views).c_str()).c_str());
    LogLine("Drawing3 quick_mode=%d", quick_mode ? 1 : 0);
    LogLine("Drawing3 frame_mode=%s symbol=%s version=%d",
            frame_options.mode == autobbox::core::Dwg3FrameMode::Symbol ? "symbol" : "auto",
            autobbox::common::WToA(frame_options.symbol_file_name.c_str()).c_str(),
            frame_options.symbol_version);
    if (selected_views == 0) {
        LogLine("Drawing3 no views selected");
        OpenPluginReportLog();
        return 0;
    }

    st = ProMdlDisplay(current);
    LogLine("Drawing3 display status=%d", static_cast<int>(st));

    ProPoint3d start_point = {0.0, 0.0, 0.0};
    if (!autobbox::ui::PromptDrawing3StartPoint(start_point, cancelled)) {
        LogLine("Drawing3 start-point status=%s", cancelled ? "cancelled" : "failed");
        if (!cancelled) {
            OpenPluginReportLog();
        }
        return 0;
    }

    autobbox::application::ExecuteDrawing3ViewsTask(
        drawing,
        sheet,
        candidates_total,
        selected,
        selected_views,
        quick_mode,
        frame_options,
        start_point,
        [&runtime](ProMdl mdl) { return ModelTag(mdl, runtime); },
        [](const std::string &line) { LogPluginReportLine(line); });
    OpenPluginReportLog();
    return 0;
}

} // namespace autobbox::main
