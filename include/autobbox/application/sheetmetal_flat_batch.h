#pragma once

#include "autobbox/core/sheetmetal_flat_batch_types.h"

#include <functional>
#include <string>
#include <vector>

namespace autobbox::application {

using SheetmetalFlatBatchLogSink = std::function<void(const std::string &line)>;
using SheetmetalFlatBatchCompletionSink = std::function<void(const core::SheetmetalFlatActionResult &result)>;

core::SheetmetalFlatCollectResult CollectSheetmetalFlatTargets();

std::wstring BuildSheetmetalFlatCollectSummary(const core::SheetmetalFlatCollectResult &result);

bool CreateSheetmetalFlatSimpreps(std::vector<core::SheetmetalFlatTarget> &targets,
                                  core::SheetmetalFlatActionResult &result,
                                  const SheetmetalFlatBatchLogSink &log_sink,
                                  const SheetmetalFlatBatchCompletionSink &completion_sink = nullptr);

bool CreateSheetmetalFamilyFlatStates(std::vector<core::SheetmetalFlatTarget> &targets,
                                      core::SheetmetalFlatActionResult &result,
                                      const SheetmetalFlatBatchLogSink &log_sink,
                                      const SheetmetalFlatBatchCompletionSink &completion_sink = nullptr);

bool DeleteSheetmetalFlatObjects(std::vector<core::SheetmetalFlatTarget> &targets,
                                 core::SheetmetalFlatActionResult &result,
                                 const SheetmetalFlatBatchLogSink &log_sink);

} // namespace autobbox::application
