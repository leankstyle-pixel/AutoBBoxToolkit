#pragma once

#include "autobbox/application/model_structure_analyzer.h"

#include <functional>
#include <string>

namespace autobbox::ui {

enum class ModelStructureAnalyzerDialogResult {
    Closed,
    RequestRefresh
};

ModelStructureAnalyzerDialogResult PromptModelStructureAnalyzerDialog(
    const autobbox::application::ModelStructureReport &report,
    const std::function<void(const std::string &line)> &log_sink);

} // namespace autobbox::ui
