#pragma once

#include "autobbox/core/param_types.h"

#include <ProMdl.h>

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace autobbox::application {

using ParamToolModelTagFormatter = std::function<std::string(ProMdl mdl)>;
using ParamToolLogSink = std::function<void(const std::string &line)>;

bool ParseParamToolInputSpecs(const std::wstring &raw_text,
                              std::vector<core::ParamAddSpec> &specs_out,
                              std::vector<std::wstring> &errors_out);
core::ParamToolExecuteSummary ExecuteParamToolOperations(
    const std::vector<ProMdl> &models,
    const std::unordered_set<std::wstring> &delete_names,
    const std::vector<core::ParamAddSpec> &add_specs,
    const ParamToolModelTagFormatter &format_model_tag,
    const ParamToolLogSink &log_sink);

} // namespace autobbox::application
