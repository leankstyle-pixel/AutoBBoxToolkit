#pragma once

#include "autobbox/application/drawing_common.h"

#include <ProDrawing.h>
#include <ProToolkit.h>
#include <ProUtil.h>
#include <ProView.h>

#include <string>
#include <vector>

namespace autobbox::application {

enum class DrawingViewBrushSource {
    ReferenceView,
    ManualPreset
};

enum class DrawingViewBrushMode {
    MainView,
    AxonometricView
};

enum class DrawingViewBrushPreset {
    Front,
    Back,
    Right,
    Left,
    Top,
    Bottom,
    Iso,
    IsoZUpNE,
    IsoZUpNW,
    IsoZUpSW,
    IsoZUpSE,
    IsoYUpNE,
    IsoYUpNW,
    IsoYUpSW,
    IsoYUpSE
};

struct DrawingViewBrushOrientation {
    ProMatrix matrix = {{0}};
    std::wstring label;
    bool valid = false;
};

struct DrawingViewBrushSummary {
    int sheet = 0;
    int selected_total = 0;
    int valid_views = 0;
    int main_views_found = 0;
    int brushed = 0;
    int derived_resolved_to_main = 0;
    int skipped_axonometric = 0;
    int skipped_non_axonometric = 0;
    int skipped_non_general = 0;
    int skipped_other_sheet = 0;
    int skipped_duplicate = 0;
    int failed = 0;
    bool sheet_regenerated = false;
    ProError regenerate_error = PRO_TK_NO_ERROR;
    ProError last_error = PRO_TK_NO_ERROR;
};

struct DrawingViewBrushTargetSet {
    int sheet = 0;
    int selected_total = 0;
    int valid_views = 0;
    int main_views_found = 0;
    int derived_resolved_to_main = 0;
    int skipped_axonometric = 0;
    int skipped_non_axonometric = 0;
    int skipped_non_general = 0;
    int skipped_other_sheet = 0;
    int skipped_duplicate = 0;
    int failed = 0;
    ProError last_error = PRO_TK_NO_ERROR;
    std::vector<ProView> main_views;
};

DrawingViewBrushOrientation MakeDrawingViewBrushPresetOrientation(DrawingViewBrushPreset preset);

bool CaptureReferenceDrawingViewBrushOrientation(ProDrawing drawing,
                                                 int sheet,
                                                 DrawingViewBrushMode mode,
                                                 DrawingViewBrushOrientation &orientation,
                                                 bool &cancelled,
                                                 const Drawing3LogSink &log_sink);

DrawingViewBrushSummary ExecuteDrawingViewBrushTask(ProDrawing drawing,
                                                    int sheet,
                                                    const DrawingViewBrushOrientation &orientation,
                                                    const Drawing3LogSink &log_sink);

DrawingViewBrushSummary ExecuteDrawingViewBrushFromSelectionBuffer(
    ProDrawing drawing,
    int sheet,
    const DrawingViewBrushOrientation &orientation,
    const Drawing3LogSink &log_sink);

DrawingViewBrushTargetSet CaptureDrawingViewBrushTargetsFromSelectionBuffer(
    ProDrawing drawing,
    int sheet,
    DrawingViewBrushMode mode,
    const Drawing3LogSink &log_sink);

DrawingViewBrushSummary ExecuteDrawingViewBrushForTargets(
    ProDrawing drawing,
    int sheet,
    const DrawingViewBrushOrientation &orientation,
    const DrawingViewBrushTargetSet &targets,
    const Drawing3LogSink &log_sink);

} // namespace autobbox::application
