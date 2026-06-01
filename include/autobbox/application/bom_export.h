#pragma once

#include "autobbox/core/bom_types.h"

#include <string>

namespace autobbox::application {

const core::BomAvailableParam *FindBomAvailableParam(const core::BomToolState &state, const std::wstring &name);
const core::BomModelSnapshot *FindBomSnapshot(const core::BomToolState &state, ProMdl mdl);
core::BomCellView BuildBomCellView(const core::BomToolState &state,
                                   const core::BomRow &row,
                                   const core::BomAvailableParam &column);
core::BomRenderStats BuildBomRenderStats(const core::BomToolState &state);
std::wstring BuildBomSummaryText(const core::BomToolState &state);

bool ExportBomCsv(const core::BomToolState &state,
                  std::wstring &file_path_out);
bool ExportBomExcel(const core::BomToolState &state,
                    std::wstring &file_path_out);

} // namespace autobbox::application
