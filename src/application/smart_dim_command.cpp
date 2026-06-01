#include "autobbox/application/smart_dim_command.h"

#include "autobbox/application/dimension_create.h"
#include "autobbox/application/dimension_inference.h"
#include "autobbox/application/dimension_preview.h"
#include "autobbox/application/selection_capture.h"

#include <ProArray.h>
#include <ProDrawing.h>
#include <ProWindows.h>

#include <cstdarg>
#include <cstdio>
#include <vector>

namespace autobbox::application {

namespace {

void LogLine(const Drawing3LogSink &log_sink, const char *fmt, ...)
{
    if (!log_sink || fmt == nullptr) {
        return;
    }

    char buffer[2048] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_sink(buffer);
}

void RefreshCurrentWindow(const Drawing3LogSink &log_sink)
{
    int window_id = -1;
    const ProError get_status = ProWindowCurrentGet(&window_id);
    if (get_status != PRO_TK_NO_ERROR || window_id < 0) {
        LogLine(log_sink,
                "smart-dim window-refresh skipped status=%d window=%d",
                static_cast<int>(get_status),
                window_id);
        return;
    }

    const ProError refresh_status = ProWindowRefresh(window_id);
    const ProError repaint_status = ProWindowRepaint(window_id);
    LogLine(log_sink,
            "smart-dim window-refresh status=%d repaint=%d window=%d",
            static_cast<int>(refresh_status),
            static_cast<int>(repaint_status),
            window_id);
}

struct TangentEdgeDisplaySnapshot {
    ProView view = nullptr;
    ProDrawingViewDisplay original_display = {};
};

std::vector<TangentEdgeDisplaySnapshot> EnableSelectableTangentEdges(ProDrawing drawing,
                                                                     const Drawing3LogSink &log_sink)
{
    std::vector<TangentEdgeDisplaySnapshot> snapshots;
    ProView *views = nullptr;
    const ProError collect_status = ProDrawingViewsCollect(drawing, &views);
    if (!(collect_status == PRO_TK_NO_ERROR || collect_status == PRO_TK_E_NOT_FOUND) || views == nullptr) {
        LogLine(log_sink,
                "smart-dim tangent-display skipped collect status=%d views=%p",
                static_cast<int>(collect_status),
                views);
        return snapshots;
    }

    int view_count = 0;
    ProArraySizeGet(reinterpret_cast<ProArray>(views), &view_count);
    for (int i = 0; i < view_count; ++i) {
        ProDrawingViewDisplay display = {};
        const ProError get_status = ProDrawingViewDisplayGet(drawing, views[i], &display);
        if (get_status != PRO_TK_NO_ERROR) {
            LogLine(log_sink,
                    "smart-dim tangent-display get failed status=%d index=%d view=%p",
                    static_cast<int>(get_status),
                    i,
                    views[i]);
            continue;
        }

        // User-confirmed behavior: when a drawing view's tangent-edge display is
        // None, Creo does not let ProSelect pick tangent/silhouette edges.  The
        // official ProDrawingViewDisplay.tangent_edge_display field supports
        // PRO_TANEDGE_NONE and PRO_TANEDGE_SOLID, so temporarily make such edges
        // visible during the command and restore the exact view display at exit.
        if (display.tangent_edge_display != PRO_TANEDGE_NONE &&
            display.tangent_edge_display != PRO_TANEDGE_DEFAULT) {
            continue;
        }

        if (display.style == PRO_DISPSTYLE_SHADED) {
            LogLine(log_sink,
                    "smart-dim tangent-display skipped shaded view index=%d view=%p tangent=%d",
                    i,
                    views[i],
                    static_cast<int>(display.tangent_edge_display));
            continue;
        }

        TangentEdgeDisplaySnapshot snapshot = {};
        snapshot.view = views[i];
        snapshot.original_display = display;

        display.tangent_edge_display = PRO_TANEDGE_SOLID;
        const ProError set_status = ProDrawingViewDisplaySet(drawing, views[i], &display);
        LogLine(log_sink,
                "smart-dim tangent-display enable status=%d index=%d view=%p original=%d new=%d",
                static_cast<int>(set_status),
                i,
                views[i],
                static_cast<int>(snapshot.original_display.tangent_edge_display),
                static_cast<int>(display.tangent_edge_display));

        if (set_status == PRO_TK_NO_ERROR) {
            snapshots.push_back(snapshot);
        }
    }

    ProArrayFree(reinterpret_cast<ProArray *>(&views));

    if (!snapshots.empty()) {
        RefreshCurrentWindow(log_sink);
    }
    return snapshots;
}

void RestoreTangentEdgeDisplays(ProDrawing drawing,
                                const std::vector<TangentEdgeDisplaySnapshot> &snapshots,
                                const Drawing3LogSink &log_sink)
{
    for (const TangentEdgeDisplaySnapshot &snapshot : snapshots) {
        ProDrawingViewDisplay display = snapshot.original_display;
        const ProError restore_status =
            ProDrawingViewDisplaySet(drawing, snapshot.view, &display);
        LogLine(log_sink,
                "smart-dim tangent-display restore status=%d view=%p tangent=%d",
                static_cast<int>(restore_status),
                snapshot.view,
                static_cast<int>(display.tangent_edge_display));
    }

    if (!snapshots.empty()) {
        RefreshCurrentWindow(log_sink);
    }
}

} // namespace

core::SmartDimensionLoopSummary RunSmartDimensionCommand(ProDrawing drawing,
                                                         const Drawing3LogSink &log_sink)
{
    core::SmartDimensionLoopSummary summary = {};
    summary.last_stage = core::SmartDimensionStage::AwaitReferences;
    const std::vector<TangentEdgeDisplaySnapshot> tangent_display_snapshots =
        EnableSelectableTangentEdges(drawing, log_sink);

    while (true) {
        ++summary.cycles_started;

        // Do not create a real dimension during preview.
        // User-confirmed rule: ProDrawingDimensionCreate is called only after
        // the final left-button placement point has been picked.
        const core::SmartDimensionSelectionSet selections =
            CaptureSmartDimensionReferences(log_sink);
        if (selections.cancelled) {
            summary.last_stage = core::SmartDimensionStage::Finished;
            ++summary.cycles_cancelled;
            summary.last_note = L"Reference selection cancelled.";
            break;
        }
        if (!selections.complete) {
            summary.last_stage = core::SmartDimensionStage::Failed;
            summary.last_note = L"Failed to capture one reference.";
            break;
        }

        summary.last_stage = core::SmartDimensionStage::InferCandidate;
        const core::SmartDimensionInferenceResult inference =
            InferSmartDimensionCandidates(selections, log_sink);
        if (inference.candidates.empty()) {
            summary.last_stage = core::SmartDimensionStage::Failed;
            summary.last_note = L"No supported single-reference dimension candidate.";
            break;
        }

        summary.last_stage = core::SmartDimensionStage::AwaitPlacement;
        const core::SmartDimensionPlacement placement =
            CaptureSmartDimensionPlacement(selections, inference, log_sink);
        if (placement.cancelled || !placement.confirmed) {
            summary.last_stage = core::SmartDimensionStage::Finished;
            ++summary.cycles_cancelled;
            summary.last_note = L"Dimension placement cancelled.";
            break;
        }

        summary.last_stage = core::SmartDimensionStage::CreateDimension;
        core::SmartDimensionCreateInput create_input = {};
        create_input.drawing = drawing;
        create_input.selections = selections;
        create_input.candidate = inference.candidates.front();
        create_input.placement = placement;
        create_input.ready_for_official_create = true;

        const core::SmartDimensionCreateResult create_result =
            CreateSmartDimension(create_input, log_sink);
        summary.last_note = create_result.note;
        ++summary.cycles_completed;

        if (create_result.created) {
            ++summary.dimensions_created;
        }
        if (create_result.shown) {
            ++summary.dimensions_shown;
            RefreshCurrentWindow(log_sink);
        }

        summary.last_stage = core::SmartDimensionStage::ContinueLoop;
    }

    RestoreTangentEdgeDisplays(drawing, tangent_display_snapshots, log_sink);
    return summary;
}

} // namespace autobbox::application
