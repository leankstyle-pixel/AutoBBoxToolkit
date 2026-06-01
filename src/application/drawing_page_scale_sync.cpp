#include "autobbox/application/drawing_page_scale_sync.h"

#include "autobbox/common/strings.h"

#include <ProArray.h>
#include <ProDrawing.h>

#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <string>

namespace autobbox::application {

namespace {

void LogLine(const DrawingPageScaleLogSink &log_sink, const char *fmt, ...)
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

bool IsFinitePositive(double value)
{
    return value > 0.0 && std::isfinite(value);
}

std::string ViewName(ProDrawing drawing, ProView view)
{
    if (drawing == nullptr || view == nullptr) {
        return "<unknown>";
    }

    ProName name = {0};
    if (ProDrawingViewNameGet(drawing, view, name) == PRO_TK_NO_ERROR && name[0] != L'\0') {
        return autobbox::common::WToA(name);
    }
    return "<unnamed>";
}

std::string FormatScaleValue(double scale, bool has_value)
{
    if (!has_value) {
        return "<unavailable>";
    }

    char buffer[64] = {0};
    std::snprintf(buffer, sizeof(buffer), "%.6f", scale);
    return buffer;
}

} // namespace

DrawingPageScaleSyncSummary ExecuteDrawingPageScaleSyncTask(ProDrawing drawing,
                                                           int sheet,
                                                           const DrawingPageScaleLogSink &log_sink)
{
    DrawingPageScaleSyncSummary summary = {};
    summary.sheet = sheet;

    if (drawing == nullptr) {
        LogLine(log_sink, "FAIL page-scale-sync reason=null-drawing");
        return summary;
    }

    double page_scale = 0.0;
    ProError st = ProDrawingScaleGet(drawing, nullptr, sheet, &page_scale);
    if (st != PRO_TK_NO_ERROR || !IsFinitePositive(page_scale)) {
        LogLine(log_sink,
                "FAIL page-scale-sync reason=page-scale-get sheet=%d status=%d value=%.6f",
                sheet,
                static_cast<int>(st),
                page_scale);
        return summary;
    }

    summary.page_scale = page_scale;
    summary.page_scale_valid = true;

    ProView *views = nullptr;
    st = ProDrawingViewsCollect(drawing, &views);
    if (st != PRO_TK_NO_ERROR && st != PRO_TK_E_NOT_FOUND) {
        LogLine(log_sink,
                "FAIL page-scale-sync reason=collect-views sheet=%d status=%d",
                sheet,
                static_cast<int>(st));
        return summary;
    }

    int view_count = 0;
    if (views != nullptr) {
        ProArraySizeGet(views, &view_count);
    }
    summary.views_total = view_count;

    LogLine(log_sink,
            "PageScaleSync start sheet=%d target_scale=%.6f collected=%d",
            sheet,
            page_scale,
            view_count);

    for (int i = 0; i < view_count; ++i) {
        ProView view = views[i];
        const std::string view_name = ViewName(drawing, view);

        int view_sheet = 0;
        const ProError st_sheet = ProDrawingViewSheetGet(drawing, view, &view_sheet);
        if (st_sheet != PRO_TK_NO_ERROR) {
            ++summary.skip_count;
            LogLine(log_sink,
                    "SKIP view=%s reason=sheet-get status=%d",
                    view_name.c_str(),
                    static_cast<int>(st_sheet));
            continue;
        }

        if (view_sheet != sheet) {
            ++summary.skip_count;
            continue;
        }

        ++summary.views_on_sheet;

        double old_scale = 0.0;
        const ProError st_old_scale = ProDrawingViewScaleGet(drawing, view, &old_scale);
        const bool has_old_scale = st_old_scale == PRO_TK_NO_ERROR && IsFinitePositive(old_scale);

        const ProError st_set = ProDrawingViewScaleSet(drawing, view, page_scale);
        if (st_set == PRO_TK_NO_ERROR) {
            ++summary.ok_count;
            LogLine(log_sink,
                    "OK   view=%s sheet=%d old_scale=%s target_scale=%.6f status=%d",
                    view_name.c_str(),
                    view_sheet,
                    FormatScaleValue(old_scale, has_old_scale).c_str(),
                    page_scale,
                    static_cast<int>(st_set));
        } else {
            ++summary.fail_count;
            LogLine(log_sink,
                    "FAIL view=%s sheet=%d old_scale=%s target_scale=%.6f status=%d scale_get_status=%d",
                    view_name.c_str(),
                    view_sheet,
                    FormatScaleValue(old_scale, has_old_scale).c_str(),
                    page_scale,
                    static_cast<int>(st_set),
                    static_cast<int>(st_old_scale));
        }
    }

    if (views != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&views));
    }

    if (summary.views_on_sheet == 0) {
        LogLine(log_sink,
                "PageScaleSync current_sheet=%d no syncable views",
                sheet);
    }

    LogLine(log_sink,
            "Summary mode=page-scale-sync sheet=%d collected=%d on_sheet=%d ok=%d fail=%d skip=%d target_scale=%.6f",
            summary.sheet,
            summary.views_total,
            summary.views_on_sheet,
            summary.ok_count,
            summary.fail_count,
            summary.skip_count,
            summary.page_scale);

    return summary;
}

} // namespace autobbox::application
