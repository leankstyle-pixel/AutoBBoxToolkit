#pragma once

#include <ProMdl.h>
#include <ProModelitem.h>
#include <ProParameter.h>
#include <ProToolkit.h>

#include "autobbox/core/bom_types.h"
#include "autobbox/core/param_types.h"

#include <string>
#include <vector>

namespace autobbox::creo {

ProError SetStringParamOnOwner(ProModelitem *owner, const wchar_t *param_name, const std::wstring &value);
ProError SetDoubleParamOnOwner(ProModelitem *owner, const wchar_t *param_name, double value);
ProError SetIntegerParamOnOwner(ProModelitem *owner, const wchar_t *param_name, int value);
ProError SetBooleanParamOnOwner(ProModelitem *owner, const wchar_t *param_name, short value);
ProError SetStringParamOnModel(ProMdl mdl, const wchar_t *param_name, const std::wstring &value);
bool ParameterExistsOnModel(ProMdl mdl, const wchar_t *param_name);
bool ReadStringParamOnModel(ProMdl mdl, const wchar_t *param_name, std::wstring &value_out);
bool ReadParamDisplayValueOnModel(ProMdl mdl, const wchar_t *param_name, std::wstring &value_out);
std::wstring NormalizeParameterName(const std::wstring &name);
bool IsWritableParameterType(ProParamvalueType type);
bool ReadParameterDisplayValue(ProParameter *param, ProParamvalueType &type_out, std::wstring &value_out);
core::BomModelSnapshot CollectBomModelSnapshot(ProMdl mdl);
std::vector<core::ParamPreviewEntry> CollectParameterPreview(const std::vector<ProMdl> &models);
bool SetModelParameterFromSpec(ProMdl mdl, const core::ParamAddSpec &spec);

} // namespace autobbox::creo
