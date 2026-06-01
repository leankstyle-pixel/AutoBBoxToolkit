#pragma once

#include "autobbox/application/drawing_common.h"
#include "autobbox/core/smart_dimension_types.h"

namespace autobbox::application {

core::SmartDimensionCreateResult TryCreateAutoOrdinateDimension(
    const core::SmartDimensionCreateInput &input,
    const Drawing3LogSink &log_sink);

} // namespace autobbox::application
