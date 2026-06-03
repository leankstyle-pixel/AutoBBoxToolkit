#pragma once

#include <ProDrawing.h>

#include <functional>
#include <string>

namespace autobbox::application {

using DrawingPageScaleLogSink = std::function<void(const std::string &line)>;

enum class DrawingPageScaleSyncScope {
    CurrentSheet,
    AllSheets
};

struct DrawingPageScaleSyncOptions {
    DrawingPageScaleSyncScope scope = DrawingPageScaleSyncScope::CurrentSheet;
    int current_sheet = 0;
    int sheet_count = 0;
    double target_scale = 1.0;
};

struct DrawingPageScaleSyncSummary {
    int sheet = 0;
    int sheet_count = 0;
    int sheets_processed = 0;
    int sheet_scale_ok_count = 0;
    int sheet_scale_fail_count = 0;
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

DrawingPageScaleSyncSummary ExecuteDrawingPageScaleSyncTask(
    ProDrawing drawing,
    const DrawingPageScaleSyncOptions &options,
    const DrawingPageScaleLogSink &log_sink);

} // namespace autobbox::application
