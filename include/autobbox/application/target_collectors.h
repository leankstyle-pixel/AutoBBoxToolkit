#pragma once

#include "autobbox/core/bom_types.h"
#include "autobbox/core/split_types.h"

#include <ProToolkit.h>

#include <string>
#include <vector>

namespace autobbox::application {

std::vector<ProMdl> CollectTargetsFromCurrentModel(ProBoolean parts,
                                                   ProBoolean assemblies,
                                                   ProBoolean top_level_only);
std::vector<core::SplitCandidate> CollectSplitCandidatesFromCurrentModel();
std::vector<core::Dwg3SimprepOption> CollectBomSimprepOptions();
std::vector<core::BomTarget> CollectBomTargets(const core::BomToolState &state);
std::wstring BuildBomAvailableLabel(const core::BomAvailableParam &entry);

} // namespace autobbox::application
