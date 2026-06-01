#pragma once

#include "autobbox/core/split_types.h"

#include <ProMdl.h>

#include <functional>
#include <string>
#include <vector>

namespace autobbox::application {

using SplitModelTagFormatter = std::function<std::string(ProMdl mdl)>;
using SplitLogSink = std::function<void(const std::string &line)>;

void ExecuteSplitInstancesTask(int candidates_total,
                               const std::vector<core::SplitCandidate> &selected,
                               const core::SplitRunOptions &options,
                               const SplitModelTagFormatter &format_model_tag,
                               const SplitLogSink &log_sink);

} // namespace autobbox::application
