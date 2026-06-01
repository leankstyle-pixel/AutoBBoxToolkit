#pragma once

#include <ProMdl.h>
#include <ProToolkit.h>

#include <string>

namespace autobbox::creo {

std::wstring ModelName(ProMdl mdl, const wchar_t *fallback = L"<unknown>");
ProMdlType ModelType(ProMdl mdl);
bool IsPartOrAsm(ProMdl mdl);
std::string DefaultModelTag(ProMdl mdl);
ProMdlfileType ToMdlFileType(ProMdlType type);
bool MdlTypeToExt(ProMdlType type, const wchar_t **ext_out);

} // namespace autobbox::creo
