#include "autobbox/creo/model_info.h"

#include "autobbox/common/strings.h"

#include <ProMdl.h>

#include <cstdio>

namespace autobbox::creo {

std::wstring ModelName(ProMdl mdl, const wchar_t *fallback)
{
    ProName name = {0};
    if (mdl != nullptr && ProMdlNameGet(mdl, name) == PRO_TK_NO_ERROR) {
        return std::wstring(name);
    }
    return fallback == nullptr ? std::wstring() : std::wstring(fallback);
}

ProMdlType ModelType(ProMdl mdl)
{
    ProMdlType type = PRO_MDL_UNUSED;
    ProMdlTypeGet(mdl, &type);
    return type;
}

bool IsPartOrAsm(ProMdl mdl)
{
    const ProMdlType type = ModelType(mdl);
    return type == PRO_MDL_PART || type == PRO_MDL_ASSEMBLY;
}

std::string DefaultModelTag(ProMdl mdl)
{
    char buffer[256] = {0};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%s(type=%d)",
                  autobbox::common::WToA(ModelName(mdl).c_str()).c_str(),
                  static_cast<int>(ModelType(mdl)));
    return std::string(buffer);
}

ProMdlfileType ToMdlFileType(ProMdlType type)
{
    return static_cast<ProMdlfileType>(type);
}

bool MdlTypeToExt(ProMdlType type, const wchar_t **ext_out)
{
    if (ext_out == nullptr) {
        return false;
    }

    switch (type) {
    case PRO_MDL_PART:
        *ext_out = L".prt";
        return true;
    case PRO_MDL_ASSEMBLY:
        *ext_out = L".asm";
        return true;
    case PRO_MDL_DRAWING:
        *ext_out = L".drw";
        return true;
    default:
        return false;
    }
}

} // namespace autobbox::creo
