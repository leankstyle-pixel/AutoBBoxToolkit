#pragma once

#include "autobbox/application/drawing_common.h"

#include <ProDrawing.h>

namespace autobbox::application {

struct DrawingArrangeSummary {
    int sheet = 0;
    int selected_total = 0;
    int valid_views = 0;
    int grouped_views = 0;
    int groups_total = 0;
    int groups_arranged = 0;
    int groups_skipped = 0;
    int moved_views = 0;
    int frames_deleted = 0;
    int frames_created = 0;
    int title_notes_deleted = 0;
    int title_notes_updated = 0;
    double page_scale = 1.0;
    bool page_scale_valid = false;
};

struct DrawingArrangeOptions {
    bool add_frame = true;
    bool update_model_title = true;
};

DrawingArrangeSummary ExecuteArrangeSelectedDrawingViewsTask(ProDrawing drawing,
                                                            int sheet,
                                                            const DrawingArrangeOptions &options,
                                                            const Drawing3LogSink &log_sink);

DrawingArrangeSummary ExecuteArrangeSelectedDrawingViewsTask(ProDrawing drawing,
                                                            int sheet,
                                                            const Drawing3LogSink &log_sink);

} // namespace autobbox::application
