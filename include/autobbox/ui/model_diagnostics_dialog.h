#pragma once

#include "autobbox/application/model_diagnostics.h"

#include <ProToolkit.h>

#include <functional>
#include <string>
#include <vector>

namespace autobbox::ui {

enum class ModelDiagnosticsDialogResult {
    Closed,
    RequestDeepCheck
};

using ModelDiagnosticsDialogLogSink = std::function<void(const std::string &line)>;
using ModelDiagnosticsLocateAction =
    std::function<ProError(const autobbox::application::ModelDiagnosticItem &item)>;
using ModelDiagnosticsOpenReportAction = std::function<void()>;

ModelDiagnosticsDialogResult PromptModelDiagnosticsDialog(
    const std::vector<autobbox::application::ModelDiagnosticItem> &items,
    bool deep_already_run,
    const ModelDiagnosticsLocateAction &locate_action,
    const ModelDiagnosticsOpenReportAction &open_report_action,
    const ModelDiagnosticsDialogLogSink &log_sink);

} // namespace autobbox::ui
