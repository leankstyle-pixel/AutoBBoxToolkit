#pragma once

#include "autobbox/core/bom_types.h"

#include <functional>
#include <vector>

namespace autobbox::application {

using BomTargetCollector = std::function<std::vector<core::BomTarget>(const core::BomToolState &state)>;

using BomAvailableLabelBuilder = std::function<std::wstring(const core::BomAvailableParam &entry)>;

void RefreshBomState(core::BomToolState &state,
                     const BomTargetCollector &collect_targets,
                     const BomAvailableLabelBuilder &build_available_label);
const wchar_t *ParamAddTypeMenuLabel(ProParamvalueType type);
bool ParamAddTypeFromMenuLabel(const std::wstring &label, ProParamvalueType &type_out);
const wchar_t *BoolMenuLabel(short bool_value);
bool BoolMenuValueToShort(const std::wstring &label, short &value_out);
bool ParseParamAddDialogSpec(const std::wstring &name_text,
                             const std::wstring &type_label,
                             const std::wstring &value_text,
                             core::ParamAddSpec &spec_out,
                             std::wstring &error_out);
bool BuildBomInlineCreateSpec(const core::BomToolState &state,
                              core::ParamAddSpec &spec_out,
                              std::wstring &error_out);
bool ValidateBomCustomParamSpec(const core::BomToolState &state,
                                const core::ParamAddSpec &spec,
                                std::wstring &error_out);
std::vector<std::wstring> SyncVisibleBomColumnsFromChecked(core::BomToolState &state);
std::vector<std::wstring> AddCheckedBomColumns(core::BomToolState &state);
std::vector<std::wstring> AddCustomBomAvailableParams(core::BomToolState &state,
                                                      const std::vector<core::ParamAddSpec> &specs);
std::vector<std::wstring> AddCustomBomColumns(core::BomToolState &state,
                                              const std::vector<core::ParamAddSpec> &specs);
std::vector<std::wstring> ClearCheckedBomAvailableParams(core::BomToolState &state);
bool RemoveCustomBomAvailableParam(core::BomToolState &state,
                                   const std::wstring &param_name,
                                   std::wstring &error_out);
bool UpdateCustomBomAvailableParam(core::BomToolState &state,
                                   const std::wstring &old_param_name,
                                   const core::ParamAddSpec &spec,
                                   std::wstring &error_out);
bool MoveSelectedBomColumnsLeft(core::BomToolState &state, std::wstring &error_out);
bool MoveSelectedBomColumnsRight(core::BomToolState &state, std::wstring &error_out);
std::vector<std::wstring> RemoveSelectedBomColumns(core::BomToolState &state);

} // namespace autobbox::application
