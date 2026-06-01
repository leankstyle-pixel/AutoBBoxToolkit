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

void CopyPoint(const ProPoint3d source, ProPoint3d target)
{
    target[0] = source[0];
    target[1] = source[1];
    target[2] = source[2];
}

void DrawPreviewSegment(const ProPoint3d start, const ProPoint3d end)
{
    ProPoint3d start_copy = {start[0], start[1], start[2]};
    ProPoint3d end_copy = {end[0], end[1], end[2]};
    ProGraphicsPenPosition(start_copy);
    ProGraphicsLineDraw(end_copy);
}

void DrawPreviewCrosshair(const ProPoint3d center)
{
    const double half = 8.0;
    ProPoint3d left = {center[0] - half, center[1], center[2]};
    ProPoint3d right = {center[0] + half, center[1], center[2]};
    ProPoint3d bottom = {center[0], center[1] - half, center[2]};
    ProPoint3d top = {center[0], center[1] + half, center[2]};

    DrawPreviewSegment(left, right);
    DrawPreviewSegment(bottom, top);
}

bool ButtonHas(ProMouseButton button, ProMouseButton mask)
{
    return (static_cast<int>(button) & static_cast<int>(mask)) != 0;
}

bool IsPlacementConfirm(ProMouseButton button)
{
    return ButtonHas(button, PRO_LEFT_BUTTON) ||
           button == PRO_LEFT_DOUBLECLICK ||
           button == PRO_LEFT_BUTTON_REL;
}

bool IsPlacementCancel(ProMouseButton button)
{
    return ButtonHas(button, PRO_RIGHT_BUTTON);
}

bool IsPlacementIgnore(ProMouseButton button)
{
    return button == PRO_NO_BUTTON ||
           button == PRO_MOUSE_MOVE ||
           ButtonHas(button, PRO_MIDDLE_BUTTON);
}

struct PreviewGraphicsModeGuard {
    explicit PreviewGraphicsModeGuard(const Drawing3LogSink &log_sink)
        : log_sink(log_sink)
    {
        const ProError status =
            ProGraphicsModeSet(PRO_DRAW_COMPLEMENT_MODE, &previous_mode);
        active = status == PRO_TK_NO_ERROR;
        LogLine(log_sink,
                "smart-dim preview graphics-mode status=%d active=%d previous=%d",
                static_cast<int>(status),
                active ? 1 : 0,
                active ? static_cast<int>(previous_mode) : -1);
    }

    ~PreviewGraphicsModeGuard()
    {
        if (!active) {
            return;
        }

        ProDrawMode ignored = PRO_DRAW_SET_MODE;
        const ProError status = ProGraphicsModeSet(previous_mode, &ignored);
        LogLine(log_sink,
                "smart-dim preview graphics-mode restore status=%d previous=%d",
                static_cast<int>(status),
                static_cast<int>(previous_mode));
    }

    const Drawing3LogSink &log_sink;
    ProDrawMode previous_mode = PRO_DRAW_SET_MODE;
    bool active = false;
};

bool FallbackPickPlacement(core::SmartDimensionPlacement &placement,
                           const Drawing3LogSink &log_sink)
{
    ProMouseButton button = PRO_NO_BUTTON;
    ProPoint3d pos = {0.0, 0.0, 0.0};
    const ProError pick_status = ProMousePickGet(PRO_ANY_BUTTON, &button, pos);
    LogLine(log_sink,
            "smart-dim preview fallback-pick status=%d button=%d loc=(%.3f,%.3f,%.3f)",
            static_cast<int>(pick_status),
            static_cast<int>(button),
            pos[0],
            pos[1],
            pos[2]);

    if (pick_status != PRO_TK_NO_ERROR) {
        placement.cancelled = true;
        placement.note = L"Mouse tracking failed and fallback pick failed.";
        return false;
    }

    if (!IsPlacementConfirm(button)) {
        placement.cancelled = true;
        placement.note = L"Placement cancelled by non-left button.";
        return false;
    }

    placement.location[0] = pos[0];
    placement.location[1] = pos[1];
    placement.location[2] = pos[2];
    placement.confirmed = true;
    placement.note = L"Dimension placement confirmed.";
    return true;
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
    //
    // Official graphics evidence:
    // - ProGraphic.h: ProGraphicsLineDraw draws from the current pen position
    //   to the supplied point.
    // - UgGraphPolyLineDrawUtil.c: rubber-band preview uses
    //   ProGraphicsModeSet(PRO_DRAW_COMPLEMENT_MODE), ProGraphicsPenPosition(),
    //   redraw-to-erase, and finally restores the graphics mode.

    LogLine(log_sink, "smart-dim preview begin-tracking");

    PreviewGraphicsModeGuard graphics_guard(log_sink);
    ProPoint3d previous_preview = {0.0, 0.0, 0.0};
    bool preview_drawn = false;

    auto erase_preview = [&]() {
        if (!preview_drawn) {
            return;
        }
        DrawPreviewCrosshair(previous_preview);
        preview_drawn = false;
    };

    while (true) {
        ProMouseButton button = PRO_NO_BUTTON;
        ProPoint3d pos = {0.0, 0.0, 0.0};
        const ProError track_status = ProMouseTrack(0, &button, pos);

        // Abort: Escape key or command-selection interruption
        if (track_status == PRO_TK_ABORT) {
            erase_preview();
            placement.cancelled = true;
            placement.note = L"Placement aborted.";
            LogLine(log_sink, "smart-dim preview aborted");
            break;
        }
        if (track_status != PRO_TK_NO_ERROR) {
            erase_preview();
            LogLine(log_sink,
                    "smart-dim preview error status=%d fallback=mouse-pick",
                    static_cast<int>(track_status));
            FallbackPickPlacement(placement, log_sink);
            break;
        }

        // Left button press: confirm placement at the current position.
        if (IsPlacementConfirm(button)) {
            erase_preview();
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

        // Right-click cancels.  Middle mouse/move events are ignored so the
        // preview loop is not terminated by harmless motion or view gestures.
        if (IsPlacementCancel(button)) {
            erase_preview();
            placement.cancelled = true;
            placement.note = L"Placement cancelled by non-left button.";
            LogLine(log_sink,
                    "smart-dim preview cancelled button=%d",
                    static_cast<int>(button));
            break;
        }

        if (!IsPlacementIgnore(button)) {
            LogLine(log_sink,
                    "smart-dim preview ignored button=%d loc=(%.3f,%.3f,%.3f)",
                    static_cast<int>(button),
                    pos[0],
                    pos[1],
                    pos[2]);
        }

        // Draw non-persistent preview at the current mouse position.  In
        // complement mode, redrawing the same crosshair erases it; this avoids
        // leaving persistent dimensions or auxiliary drawing entities.
        erase_preview();
        DrawPreviewCrosshair(pos);
        CopyPoint(pos, previous_preview);
        preview_drawn = true;
    }

    LogLine(log_sink,
            "smart-dim preview end confirmed=%d cancelled=%d",
            placement.confirmed ? 1 : 0,
            placement.cancelled ? 1 : 0);
    return placement;
}

} // namespace autobbox::application
