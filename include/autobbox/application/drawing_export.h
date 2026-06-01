#pragma once

#include "autobbox/core/drawing_export_types.h"

#include <ProMdl.h>

#include <functional>
#include <string>
#include <vector>

namespace autobbox::application {

using DrawingExportLogSink = std::function<void(const std::string &line)>;

core::DrawingExportResult ExecuteDrawingExportTask(
    ProMdl drawing_model,
    const core::DrawingExportRequest &request,
    const DrawingExportLogSink &log_sink);

ProError CollectDrawingExportSheets(
    ProMdl drawing_model,
    std::vector<core::DrawingExportSheetChoice> &sheets_out,
    int &current_sheet_out,
    const DrawingExportLogSink &log_sink);

const wchar_t *DrawingExportFormatExtension(core::DrawingExportFormat format);
const char *DrawingExportFormatLogName(core::DrawingExportFormat format);

} // namespace autobbox::application
