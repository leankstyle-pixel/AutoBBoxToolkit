#pragma once

#include "autobbox/core/drawing_export_types.h"

#include <functional>
#include <string>
#include <vector>

namespace autobbox::ui {

using DrawingExportDialogLogSink = std::function<void(const std::string &line)>;

bool PromptDrawingExportOptions(core::DrawingExportRequest &request,
                                const std::vector<core::DrawingExportSheetChoice> &sheet_choices,
                                bool &cancelled,
                                const DrawingExportDialogLogSink &log_sink);

} // namespace autobbox::ui
