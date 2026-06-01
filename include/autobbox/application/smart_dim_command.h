#pragma once

#include "autobbox/application/drawing_common.h"
#include "autobbox/core/smart_dimension_types.h"

#include <ProDrawing.h>

namespace autobbox::application {

core::SmartDimensionLoopSummary RunSmartDimensionCommand(
    ProDrawing drawing,
    const Drawing3LogSink &log_sink);

} // namespace autobbox::application
