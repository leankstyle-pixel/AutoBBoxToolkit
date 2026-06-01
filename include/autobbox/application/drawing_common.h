#pragma once

#include <ProMdl.h>

#include <functional>
#include <string>

namespace autobbox::application {

using Drawing3ModelTagFormatter = std::function<std::string(ProMdl mdl)>;
using Drawing3LogSink = std::function<void(const std::string &line)>;

} // namespace autobbox::application
