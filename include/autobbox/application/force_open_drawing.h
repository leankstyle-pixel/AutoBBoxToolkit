#pragma once

#include <ProToolkit.h>

#include <functional>
#include <string>

namespace autobbox::application {

using ForceOpenDrawingLogSink = std::function<void(const std::string &)>;

struct ForceOpenDrawingResult {
    ProError status = PRO_TK_GENERAL_ERROR;
    ProError selection_status = PRO_TK_GENERAL_ERROR;
    ProError model_display_status = PRO_TK_GENERAL_ERROR;
    ProError drawing_load_status = PRO_TK_GENERAL_ERROR;
    ProError drawing_display_status = PRO_TK_GENERAL_ERROR;
    bool cancelled = false;
    bool drawing_path_local_missing = false;
    int alias_created_count = 0;
    std::wstring selected_model_name;
    std::wstring selected_model_path;
    std::wstring drawing_path;
};

ForceOpenDrawingResult ExecuteForceOpenDrawingTask(const ForceOpenDrawingLogSink &log_sink);

} // namespace autobbox::application
