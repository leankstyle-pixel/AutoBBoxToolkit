#pragma once

#include "autobbox/core/bom_types.h"

#include <functional>
#include <string>

namespace autobbox::application {

using BomModelTagFormatter = std::function<std::string(ProMdl mdl)>;
using BomLogSink = std::function<void(const std::string &line)>;

core::BomUpdateSummary ApplyBomDraftsToModels(
    core::BomToolState &state,
    const BomModelTagFormatter &format_model_tag,
    const BomLogSink &log_sink);

} // namespace autobbox::application
