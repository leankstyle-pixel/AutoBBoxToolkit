#include "autobbox/application/dimension_preview.h"

#include <ProGraphic.h>

#include <cstdarg>
#include <cstdio>

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

} // namespace

core::SmartDimensionPlacement CaptureSmartDimensionPlacement(
    const core::SmartDimensionSelectionSet &selections,
    const core::SmartDimensionInferenceResult &inference,
    const Drawing3LogSink &log_sink)
{
    core::SmartDimensionPlacement placement = {};
    placement.view = selections.first.view;

    if (!selections.complete || inference.candidates.empty()) {
        placement.cancelled = true;
        placement.note = L"Missing reference or candidate for placement.";
        return placement;
    }

    // Official API evidence (protkdoc/api/1561.html):
    // ProMouseTrack reports the current mouse position immediately, regardless
    // of button state.  It returns PRO_TK_ABORT on Escape or when the user
    // selects another command.  We loop to provide real-time placement preview
    // via non-persistent graphics, then confirm on left-click.
    //
    // This replaces ProMousePickGet which blocks without any visual feedback.
    // User-confirmed rule: preview must use only non-persistent graphics; no
    // dimension entities or auxiliary lines may be created before the final
    // left-click placement.

    LogLine(log_sink, "smart-dim preview begin-tracking");

    while (true) {
        ProMouseButton button = PRO_NO_BUTTON;
        ProPoint3d pos = {0.0, 0.0, 0.0};
        const ProError track_status = ProMouseTrack(0, &button, pos);

        // Abort: Escape key or command-selection interruption
        if (track_status == PRO_TK_ABORT) {
            placement.cancelled = true;
            placement.note = L"Placement aborted.";
            LogLine(log_sink, "smart-dim preview aborted");
            break;
        }
        if (track_status != PRO_TK_NO_ERROR) {
            placement.cancelled = true;
            placement.note = L"Mouse tracking error.";
            LogLine(log_sink,
                    "smart-dim preview error status=%d",
                    static_cast<int>(track_status));
            break;
        }

        // Left button press: confirm placement at the current position
        if (button == PRO_LEFT_BUTTON) {
            placement.location[0] = pos[0];
            placement.location[1] = pos[1];
            placement.location[2] = pos[2];
            placement.confirmed = true;
            placement.note = L"Dimension placement confirmed.";
            LogLine(log_sink,
                    "smart-dim preview confirmed loc=(%.3f,%.3f,%.3f)",
                    pos[0],
                    pos[1],
                    pos[2]);
            break;
        }

        // Any non-left button press: cancel
        if (button != PRO_NO_BUTTON) {
            placement.cancelled = true;
            placement.note = L"Placement cancelled by non-left button.";
            LogLine(log_sink,
                    "smart-dim preview cancelled button=%d",
                    static_cast<int>(button));
            break;
        }

        // Draw non-persistent preview at the current mouse position.
        // ProGraphicsLineDraw creates overlay graphics that disappear on the
        // next window repaint — no dimension entity is produced.
        // We draw a simple crosshair to indicate where the dimension will land.
        const double half = 8.0;
        {
            ProPoint3d a = {pos[0] - half, pos[1], pos[2]};
            ProPoint3d b = {pos[0] + half, pos[1], pos[2]};
            ProGraphicsLineDraw(a);
            ProGraphicsLineDraw(b);
        }
        {
            ProPoint3d a = {pos[0], pos[1] - half, pos[2]};
            ProPoint3d b = {pos[0], pos[1] + half, pos[2]};
            ProGraphicsLineDraw(a);
            ProGraphicsLineDraw(b);
        }
    }

    LogLine(log_sink,
            "smart-dim preview end confirmed=%d cancelled=%d",
            placement.confirmed ? 1 : 0,
            placement.cancelled ? 1 : 0);
    return placement;
}

} // namespace autobbox::application
