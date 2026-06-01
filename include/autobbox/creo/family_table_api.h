#pragma once

#include <ProMdl.h>
#include <ProToolkit.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>

namespace autobbox::creo {

bool IsFamilyInstanceQuick(ProMdl mdl);
ProError DeleteParamWithFamtableSupport(ProMdl mdl, const wchar_t *param_name);
ProError SetStringParamWithFamtableSupport(ProMdl mdl,
                                          const wchar_t *param_name,
                                          const std::wstring &value,
                                          bool recompute);
ProError SetDoubleParamWithFamtableSupport(ProMdl mdl,
                                          const wchar_t *param_name,
                                          double value,
                                          bool recompute);
ULONGLONG PreheatImmediateGenericCache(const std::vector<ProMdl> &models);

} // namespace autobbox::creo
