#pragma once

#include "autobbox/application/drawing_common.h"
#include "autobbox/core/smart_dimension_types.h"

namespace autobbox::application {

core::SmartDimensionPlacement CaptureSmartDimensionPlacement(
    const core::SmartDimensionSelectionSet &selections,
    const core::SmartDimensionInferenceResult &inference,
    const Drawing3LogSink &log_sink);

} // namespace autobbox::application
