#pragma once

#include <ProMdl.h>

#include <functional>
#include <string>
#include <vector>

namespace autobbox::application {

using RelationModelTagFormatter = std::function<std::string(ProMdl mdl)>;
using RelationLogSink = std::function<void(const std::string &line)>;

void ExecuteCleanRelationsTask(const std::vector<ProMdl> &models,
                               const RelationModelTagFormatter &format_model_tag,
                               const RelationLogSink &log_sink);
void ExecuteAddRelationsTask(const std::vector<ProMdl> &models,
                             const std::wstring &raw_text,
                             const RelationModelTagFormatter &format_model_tag,
                             const RelationLogSink &log_sink);

} // namespace autobbox::application
