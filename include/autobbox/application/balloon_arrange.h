#pragma once

#include "autobbox/application/drawing_common.h"

#include <ProDrawing.h>
#include <ProDwgtable.h>
#include <ProToolkit.h>
#include <ProView.h>

#include <string>

namespace autobbox::application {

enum class BalloonArrangeLabelSource {
    ModelName,
    ParameterValue
};

struct BalloonArrangeOptions {
    BalloonArrangeLabelSource label_source = BalloonArrangeLabelSource::ParameterValue;
    std::wstring parameter_name = L"PTC_COMMON_NAME";
    bool fallback_to_model_name = true;
};

struct BalloonArrangeSummary {
    int sheet = 0;
    int selected_total = 0;
    int valid_views = 0;
    int views_arranged = 0;
    int views_failed = 0;
    int notes_reordered = 0;
    int notes_reorder_failed = 0;
    int custom_balloons_created = 0;
    int custom_balloons_updated = 0;
    int custom_balloons_failed = 0;
    ProError first_error = PRO_TK_NO_ERROR;
};

struct BalloonArrangeBomTableSelection {
    ProDwgtable table = {};
    int segment = PRO_VALUE_UNUSED;
    int selected_row = 0;
    int selected_column = 0;
};

BalloonArrangeSummary ExecuteArrangeSelectedBalloonsTask(ProDrawing drawing,
                                                        int sheet,
                                                        const BalloonArrangeOptions &options,
                                                        const Drawing3LogSink &log_sink);

BalloonArrangeSummary ExecuteArrangeBomTableNoteBalloonsTask(
    ProDrawing drawing,
    int sheet,
    const BalloonArrangeOptions &options,
    const BalloonArrangeBomTableSelection &bom_table,
    ProView target_view,
    const Drawing3LogSink &log_sink);

BalloonArrangeSummary ExecuteArrangeTraditionalBalloonsTask(ProDrawing drawing,
                                                            int sheet,
                                                            ProView target_view,
                                                            const Drawing3LogSink &log_sink);

BalloonArrangeSummary ExecuteOfficialBomBalloonCreatePocTask(
    ProDrawing drawing,
    int sheet,
    const BalloonArrangeBomTableSelection &bom_table,
    ProView target_view,
    const Drawing3LogSink &log_sink);

BalloonArrangeSummary ExecuteRebuildProblemTraditionalBalloonsTask(
    ProDrawing drawing,
    int sheet,
    const BalloonArrangeBomTableSelection &bom_table,
    ProView target_view,
    const Drawing3LogSink &log_sink);

} // namespace autobbox::application
