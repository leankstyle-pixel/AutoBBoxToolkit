#pragma once

#include "autobbox/core/dwg3_types.h"

#include <ProSolid.h>
#include <ProToolkit.h>

#include <functional>
#include <string>
#include <vector>

namespace autobbox::ui {

using Drawing3DialogLogSink = std::function<void(const std::string &line)>;

bool PromptDrawing3Targets(ProSolid root_solid,
                           std::vector<core::Dwg3Candidate> &selected,
                           int &candidates_total,
                           core::Dwg3ViewMask &view_mask,
                           bool &quick_mode,
                           core::Dwg3FrameOptions &frame_options,
                           bool &cancelled,
                           const Drawing3DialogLogSink &log_sink);

bool PromptDrawing3TargetsFromCandidates(ProSolid root_solid,
                                         const std::vector<core::Dwg3Candidate> &candidates,
                                         const std::wstring &source_label,
                                         std::vector<core::Dwg3Candidate> &selected,
                                         int &candidates_total,
                                         core::Dwg3ViewMask &view_mask,
                                         bool &quick_mode,
                                         core::Dwg3FrameOptions &frame_options,
                                         bool &cancelled,
                                         const Drawing3DialogLogSink &log_sink);

bool PromptDrawing3StartPoint(ProPoint3d start_point, bool &cancelled);

} // namespace autobbox::ui
