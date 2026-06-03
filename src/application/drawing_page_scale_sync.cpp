#include "autobbox/application/drawing_page_scale_sync.h"

#include "autobbox/common/strings.h"

#include <ProArray.h>
#include <ProDrawing.h>

#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>

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

std::vector<int> TargetSheets(const DrawingPageScaleSyncOptions &options)
{
    std::vector<int> sheets;
    if (options.scope == DrawingPageScaleSyncScope::AllSheets) {
        for (int sheet = 1; sheet <= options.sheet_count; ++sheet) {
            sheets.push_back(sheet);
        }
    } else if (options.current_sheet > 0) {
        sheets.push_back(options.current_sheet);
    }
    return sheets;
}

void ProcessSheetViews(ProDrawing drawing,
                       int sheet,
                       double target_scale,
                       ProView *views,
                       int view_count,
                       DrawingPageScaleSyncSummary &summary,
                       const DrawingPageScaleLogSink &log_sink)
{
    LogLine(log_sink,
            "PageScaleSync sheet-start sheet=%d target_scale=%.6f collected=%d",
            sheet,
            target_scale,
            view_count);

    double old_page_scale = 0.0;
    const ProError st_old_page_scale = ProDrawingScaleGet(drawing, nullptr, sheet, &old_page_scale);
    const bool has_old_page_scale = st_old_page_scale == PRO_TK_NO_ERROR && IsFinitePositive(old_page_scale);

    const ProError st_page_set = ProDrawingScaleSet(drawing, nullptr, sheet, target_scale);
    if (st_page_set == PRO_TK_NO_ERROR) {
        ++summary.sheet_scale_ok_count;
        LogLine(log_sink,
                "OK   sheet-scale sheet=%d old_scale=%s target_scale=%.6f status=%d",
                sheet,
                FormatScaleValue(old_page_scale, has_old_page_scale).c_str(),
                target_scale,
                static_cast<int>(st_page_set));
    } else {
        ++summary.sheet_scale_fail_count;
        LogLine(log_sink,
                "FAIL sheet-scale sheet=%d old_scale=%s target_scale=%.6f status=%d scale_get_status=%d",
                sheet,
                FormatScaleValue(old_page_scale, has_old_page_scale).c_str(),
                target_scale,
                static_cast<int>(st_page_set),
                static_cast<int>(st_old_page_scale));
    }

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
            continue;
        }

        ++summary.views_on_sheet;

        double old_scale = 0.0;
        const ProError st_old_scale = ProDrawingViewScaleGet(drawing, view, &old_scale);
        const bool has_old_scale = st_old_scale == PRO_TK_NO_ERROR && IsFinitePositive(old_scale);

        const ProError st_set = ProDrawingViewScaleSet(drawing, view, target_scale);
        if (st_set == PRO_TK_NO_ERROR) {
            ++summary.ok_count;
            LogLine(log_sink,
                    "OK   view=%s sheet=%d old_scale=%s target_scale=%.6f status=%d",
                    view_name.c_str(),
                    view_sheet,
                    FormatScaleValue(old_scale, has_old_scale).c_str(),
                    target_scale,
                    static_cast<int>(st_set));
        } else {
            ++summary.fail_count;
            LogLine(log_sink,
                    "FAIL view=%s sheet=%d old_scale=%s target_scale=%.6f status=%d scale_get_status=%d",
                    view_name.c_str(),
                    view_sheet,
                    FormatScaleValue(old_scale, has_old_scale).c_str(),
                    target_scale,
                    static_cast<int>(st_set),
                    static_cast<int>(st_old_scale));
        }
    }

    const ProError st_regen = ProDwgSheetRegenerate(drawing, sheet);
    LogLine(log_sink,
            "PageScaleSync sheet-regenerate sheet=%d status=%d",
            sheet,
            static_cast<int>(st_regen));
}

} // namespace

DrawingPageScaleSyncSummary ExecuteDrawingPageScaleSyncTask(ProDrawing drawing,
                                                           int sheet,
                                                           const DrawingPageScaleLogSink &log_sink)
{
    double page_scale = 0.0;
    ProError st = PRO_TK_BAD_INPUTS;
    if (drawing != nullptr) {
        st = ProDrawingScaleGet(drawing, nullptr, sheet, &page_scale);
    }
    if (st != PRO_TK_NO_ERROR || !IsFinitePositive(page_scale)) {
        LogLine(log_sink,
                "FAIL page-scale-sync reason=page-scale-get sheet=%d status=%d value=%.6f",
                sheet,
                static_cast<int>(st),
                page_scale);
        DrawingPageScaleSyncSummary summary = {};
        summary.sheet = sheet;
        return summary;
    }

    DrawingPageScaleSyncOptions options = {};
    options.scope = DrawingPageScaleSyncScope::CurrentSheet;
    options.current_sheet = sheet;
    options.sheet_count = sheet;
    options.target_scale = page_scale;
    return ExecuteDrawingPageScaleSyncTask(drawing, options, log_sink);
}

DrawingPageScaleSyncSummary ExecuteDrawingPageScaleSyncTask(
    ProDrawing drawing,
    const DrawingPageScaleSyncOptions &options,
    const DrawingPageScaleLogSink &log_sink)
{
    DrawingPageScaleSyncSummary summary = {};
    summary.sheet = options.current_sheet;
    summary.sheet_count = options.sheet_count;
    summary.page_scale = options.target_scale;
    summary.page_scale_valid = IsFinitePositive(options.target_scale);

    if (drawing == nullptr) {
        LogLine(log_sink, "FAIL page-scale-sync reason=null-drawing");
        return summary;
    }
    if (!summary.page_scale_valid) {
        LogLine(log_sink,
                "FAIL page-scale-sync reason=invalid-target-scale value=%.6f",
                options.target_scale);
        return summary;
    }

    const std::vector<int> sheets = TargetSheets(options);
    if (sheets.empty()) {
        LogLine(log_sink,
                "FAIL page-scale-sync reason=no-target-sheets scope=%d current_sheet=%d sheet_count=%d",
                static_cast<int>(options.scope),
                options.current_sheet,
                options.sheet_count);
        return summary;
    }

    ProView *views = nullptr;
    ProError st = ProDrawingViewsCollect(drawing, &views);
    if (st != PRO_TK_NO_ERROR && st != PRO_TK_E_NOT_FOUND) {
        LogLine(log_sink,
                "FAIL page-scale-sync reason=collect-views current_sheet=%d status=%d",
                options.current_sheet,
                static_cast<int>(st));
        return summary;
    }

    int view_count = 0;
    if (views != nullptr) {
        ProArraySizeGet(views, &view_count);
    }
    summary.views_total = view_count;

    LogLine(log_sink,
            "PageScaleSync start scope=%d current_sheet=%d sheet_count=%d target_scale=%.6f collected=%d targets=%zu",
            static_cast<int>(options.scope),
            options.current_sheet,
            options.sheet_count,
            options.target_scale,
            view_count,
            sheets.size());

    int original_sheet = 0;
    const ProError st_original_sheet = ProDrawingCurrentSheetGet(drawing, &original_sheet);
    LogLine(log_sink,
            "PageScaleSync original-sheet status=%d sheet=%d",
            static_cast<int>(st_original_sheet),
            original_sheet);

    for (int target_sheet : sheets) {
        const ProError st_current = ProDrawingCurrentSheetSet(drawing, target_sheet);
        LogLine(log_sink,
                "PageScaleSync set-current-sheet sheet=%d status=%d",
                target_sheet,
                static_cast<int>(st_current));
        if (st_current != PRO_TK_NO_ERROR) {
            ++summary.sheet_scale_fail_count;
            continue;
        }

        ++summary.sheets_processed;
        ProcessSheetViews(
            drawing,
            target_sheet,
            options.target_scale,
            views,
            view_count,
            summary,
            log_sink);
    }

    if (views != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&views));
    }

    if (st_original_sheet == PRO_TK_NO_ERROR && original_sheet > 0 && original_sheet != sheets.back()) {
        const ProError st_restore = ProDrawingCurrentSheetSet(drawing, original_sheet);
        LogLine(log_sink,
                "PageScaleSync restore-sheet sheet=%d status=%d",
                original_sheet,
                static_cast<int>(st_restore));
    }

    if (summary.views_on_sheet == 0) {
        LogLine(log_sink,
                "PageScaleSync current_sheet=%d no syncable views",
                options.current_sheet);
    }

    LogLine(log_sink,
            "Summary mode=page-scale-sync scope=%d sheet=%d sheet_count=%d processed=%d sheet_scale_ok=%d sheet_scale_fail=%d collected=%d on_sheet=%d ok=%d fail=%d skip=%d target_scale=%.6f",
            static_cast<int>(options.scope),
            summary.sheet,
            summary.sheet_count,
            summary.sheets_processed,
            summary.sheet_scale_ok_count,
            summary.sheet_scale_fail_count,
            summary.views_total,
            summary.views_on_sheet,
            summary.ok_count,
            summary.fail_count,
            summary.skip_count,
            summary.page_scale);

    return summary;
}

} // namespace autobbox::application
