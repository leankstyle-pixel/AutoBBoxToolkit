#pragma once

#include "autobbox/application/drawing_common.h"
#include "autobbox/core/dwg3_types.h"

#include <ProDrawing.h>
#include <ProDwgtable.h>
#include <ProSolid.h>

#include <vector>

namespace autobbox::application {

std::vector<core::Dwg3SimprepOption> CollectDrawingViewSimprepOptions(ProSolid root_solid);
bool ActivateDrawingViewSimprepByName(ProSolid root_solid, const std::wstring &rep_name);
std::vector<core::Dwg3Candidate> CollectDrawingViewCandidates(ProMdl root);
std::vector<core::Dwg3Candidate> CollectDrawingViewCandidatesForSimprep(
    ProSolid root_solid,
    const core::Dwg3SimprepOption *option);
std::vector<core::Dwg3Candidate> CollectDrawingViewCandidatesFromBomTable(
    ProDwgtable table,
    int segment);
std::vector<core::Dwg3Candidate> CollectDrawingViewCandidatesFromBomTable(
    ProDwgtable table,
    int segment,
    const Drawing3LogSink &log_sink);
core::Dwg3ProjectionType ResolveDrawingProjectionType(ProDrawing drawing);
std::wstring JoinDwg3ViewLabels(core::Dwg3ViewMask mask);

void ExecuteDrawing3ViewsTask(ProDrawing drawing,
                              int sheet,
                              int candidates_total,
                              const std::vector<core::Dwg3Candidate> &selected,
                              core::Dwg3ViewMask selected_views,
                              bool quick_mode,
                              const core::Dwg3FrameOptions &frame_options,
                              const ProPoint3d start_point_screen,
                              const Drawing3ModelTagFormatter &format_model_tag,
                              const Drawing3LogSink &log_sink);

} // namespace autobbox::application
