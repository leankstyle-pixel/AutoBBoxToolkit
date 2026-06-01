#pragma once

#include "autobbox/application/drawing_common.h"
#include "autobbox/core/smart_dimension_types.h"

namespace autobbox::application {

core::SmartDimensionCreateResult CreateSmartDimension(
    const core::SmartDimensionCreateInput &input,
    const Drawing3LogSink &log_sink);

ProError MoveSmartDimension(ProDrawing drawing,
                            ProDimension *dimension,
                            const core::SmartDimensionPlacement &placement,
                            const Drawing3LogSink &log_sink);

ProError DeleteSmartDimension(ProDimension *dimension,
                              const Drawing3LogSink &log_sink);

} // namespace autobbox::application
