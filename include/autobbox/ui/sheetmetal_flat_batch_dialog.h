#pragma once

#include "autobbox/core/sheetmetal_flat_batch_types.h"

#include <functional>
#include <string>

namespace autobbox::ui {

using SheetmetalFlatBatchDialogLogSink = std::function<void(const std::string &line)>;

bool PromptSheetmetalFlatBatchDialog(core::SheetmetalFlatCollectResult &collect_result,
                                     bool &cancelled,
                                     bool &deferred_action_requested,
                                     core::SheetmetalFlatAction &deferred_action,
                                     const SheetmetalFlatBatchDialogLogSink &log_sink);

} // namespace autobbox::ui
