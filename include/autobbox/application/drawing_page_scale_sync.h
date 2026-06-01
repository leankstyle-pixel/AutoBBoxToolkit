#pragma once

#include <ProDrawing.h>

#include <functional>
#include <string>

namespace autobbox::application {

using DrawingPageScaleLogSink = std::function<void(const std::string &line)>;

struct DrawingPageScaleSyncSummary {
    int sheet = 0;
    int views_total = 0;
    int views_on_sheet = 0;
    int ok_count = 0;
    int fail_count = 0;
    int skip_count = 0;
    double page_scale = 1.0;
    bool page_scale_valid = false;
};

DrawingPageScaleSyncSummary ExecuteDrawingPageScaleSyncTask(ProDrawing drawing,
                                                           int sheet,
                                                           const DrawingPageScaleLogSink &log_sink);

} // namespace autobbox::application
