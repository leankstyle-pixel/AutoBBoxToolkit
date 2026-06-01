#include "autobbox/application/ordinate_mode.h"

#include <ProArray.h>
#include <ProDimension.h>

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

ProError CreateOrdinateWithOfficialApi(ProDrawing drawing,
                                       ProSelection *surface_array,
                                       ProSelection baseline,
                                       ProDimension **dimensions)
{
    return ProDimensionAutoOrdinateCreate(drawing, surface_array, baseline, dimensions);
}

} // namespace

core::SmartDimensionCreateResult TryCreateAutoOrdinateDimension(
    const core::SmartDimensionCreateInput &input,
    const Drawing3LogSink &log_sink)
{
    core::SmartDimensionCreateResult result = {};
    result.note = L"V1 范围不启用单手自动 ordinate 创建，仅保留官方 API 骨架。";

    if (!input.ready_for_official_create) {
        LogLine(log_sink, "smart-dim ordinate skipped reason=not-ready");
        return result;
    }

    ProSelection *surface_array = nullptr;
    ProDimension *dimensions = nullptr;
    const ProSelection baseline =
        input.selections.first.selection; // VERIFY_WITH_OFFICIAL_DOC: baseline packing.
    result.create_status =
        CreateOrdinateWithOfficialApi(input.drawing, surface_array, baseline, &dimensions);
    LogLine(log_sink,
            "smart-dim ordinate create status=%d",
            static_cast<int>(result.create_status));
    return result;
}

} // namespace autobbox::application
