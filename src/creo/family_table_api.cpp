#include "autobbox/creo/family_table_api.h"

#include "autobbox/creo/model_info.h"
#include "autobbox/creo/parameter_api.h"

#include <ProFamtable.h>
#include <ProFaminstance.h>
#include <ProMdl.h>
#include <ProModelitem.h>
#include <ProParamval.h>
#include <ProParameter.h>
#include <ProUtil.h>

#include <cstring>
#include <unordered_map>

namespace autobbox::creo {

namespace {

std::unordered_map<std::wstring, ProMdl> g_immediate_generic_cache;

template <size_t N>
void CopyWStr(wchar_t (&dest)[N], const wchar_t *src)
{
    if (N == 0) {
        return;
    }

    size_t i = 0;
    if (src != nullptr) {
        while (i + 1 < N && src[i] != L'\0') {
            dest[i] = src[i];
            ++i;
        }
    }
    dest[i] = L'\0';
}

template <size_t N>
void AppendWStr(wchar_t (&dest)[N], const wchar_t *src)
{
    if (N == 0 || src == nullptr) {
        return;
    }

    size_t len = 0;
    while (len + 1 < N && dest[len] != L'\0') {
        ++len;
    }

    size_t i = 0;
    while (len + 1 < N && src[i] != L'\0') {
        dest[len++] = src[i++];
    }
    dest[len] = L'\0';
}

ProModelitem MdlAsModelitem(ProMdl mdl)
{
    ProModelitem item;
    std::memset(&item, 0, sizeof(item));
    ProMdlToModelitem(mdl, &item);
    return item;
}

bool ParamExists(ProMdl mdl, const wchar_t *param_name)
{
    if (mdl == nullptr || param_name == nullptr) {
        return false;
    }

    ProModelitem owner = MdlAsModelitem(mdl);
    ProParameter param;
    ProName pname = {0};
    CopyWStr(pname, param_name);
    return ProParameterInit(&owner, pname, &param) == PRO_TK_NO_ERROR;
}

ProError RemoveParamIfExists(ProMdl mdl, const wchar_t *param_name)
{
    if (mdl == nullptr || param_name == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    ProModelitem owner = MdlAsModelitem(mdl);
    ProParameter param;
    ProName pname = {0};
    CopyWStr(pname, param_name);
    ProError st = ProParameterInit(&owner, pname, &param);
    if (st == PRO_TK_E_NOT_FOUND) {
        return PRO_TK_E_NOT_FOUND;
    }
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }
    return ProParameterDelete(&param);
}

std::wstring GenericCacheKey(const ProName name, ProMdlType type)
{
    std::wstring key(name);
    key.push_back(L'|');
    key += std::to_wstring(static_cast<int>(type));
    return key;
}

bool IsWriteSuccess(ProError st)
{
    return st == PRO_TK_NO_ERROR || st == PRO_TK_NO_CHANGE || st == PRO_TK_E_FOUND;
}

bool IsDeleteSuccess(ProError st)
{
    return st == PRO_TK_NO_ERROR || st == PRO_TK_NO_CHANGE || st == PRO_TK_E_NOT_FOUND;
}

bool InitFamtableItemFromModelParameter(ProMdl mdl,
                                        const wchar_t *param_name,
                                        ProFamtableItem *out_item)
{
    if (mdl == nullptr || param_name == nullptr || out_item == nullptr) {
        return false;
    }

    ProModelitem owner = MdlAsModelitem(mdl);
    ProParameter param;
    ProName pname = {0};
    CopyWStr(pname, param_name);
    const ProError st = ProParameterInit(&owner, pname, &param);
    if (st != PRO_TK_NO_ERROR) {
        return false;
    }

    std::memset(out_item, 0, sizeof(*out_item));
    return ProParameterToFamtableItem(&param, out_item) == PRO_TK_NO_ERROR;
}

ProError TryLoadImmediateGenericByOrigin(ProMdl inst_mdl,
                                         const ProName gen_name,
                                         ProMdlType gen_type,
                                         ProMdl *generic_out)
{
    if (inst_mdl == nullptr || gen_name == nullptr || generic_out == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }
    *generic_out = nullptr;

    ProPath origin = {0};
    ProError st = ProMdlOriginGet(inst_mdl, origin);
    if (st != PRO_TK_NO_ERROR || origin[0] == L'\0') {
        return PRO_TK_E_NOT_FOUND;
    }

    ProPath dir = {0};
    st = ProFileMdlnameParse(origin, dir, nullptr, nullptr, nullptr);
    if (st != PRO_TK_NO_ERROR || dir[0] == L'\0') {
        return PRO_TK_E_NOT_FOUND;
    }

    const wchar_t *ext = nullptr;
    if (!autobbox::creo::MdlTypeToExt(gen_type, &ext)) {
        return PRO_TK_BAD_INPUTS;
    }

    ProPath full = {0};
    CopyWStr(full, dir);
    const size_t n = std::wcslen(full);
    if (n > 0 && full[n - 1] != L'\\' && full[n - 1] != L'/') {
        AppendWStr(full, L"\\");
    }
    AppendWStr(full, gen_name);
    AppendWStr(full, ext);
    return ProMdlFiletypeLoad(full, PRO_MDLFILE_UNUSED, PRO_B_FALSE, generic_out);
}

enum class InstanceResolveState {
    NotInstance,
    Resolved,
    Unresolved
};

InstanceResolveState ResolveImmediateGenericModel(ProMdl mdl, ProMdl *immediate_generic)
{
    if (immediate_generic != nullptr) {
        *immediate_generic = nullptr;
    }

    ProMdl generic = nullptr;
    ProError st = ProFaminstanceGenericGet(mdl, PRO_B_TRUE, &generic);
    if (st == PRO_TK_NO_ERROR && generic != nullptr) {
        if (generic == mdl ||
            autobbox::creo::ModelName(generic, L"") == autobbox::creo::ModelName(mdl, L"")) {
            return InstanceResolveState::NotInstance;
        }
        if (immediate_generic != nullptr) {
            *immediate_generic = generic;
        }
        return InstanceResolveState::Resolved;
    }

    if (st == PRO_TK_E_NOT_FOUND) {
        return InstanceResolveState::NotInstance;
    }

    ProName gen_name = {0};
    ProMdlType gen_type = PRO_MDL_UNUSED;
    st = ProFaminstanceImmediategenericinfoGet(mdl, gen_name, &gen_type);
    if (st != PRO_TK_NO_ERROR) {
        return InstanceResolveState::NotInstance;
    }
    if (gen_type == PRO_MDL_UNUSED) {
        return InstanceResolveState::Unresolved;
    }

    const std::wstring cache_key = GenericCacheKey(gen_name, gen_type);
    const auto it = g_immediate_generic_cache.find(cache_key);
    if (it != g_immediate_generic_cache.end() && it->second != nullptr) {
        if (immediate_generic != nullptr) {
            *immediate_generic = it->second;
        }
        return InstanceResolveState::Resolved;
    }

    st = TryLoadImmediateGenericByOrigin(mdl, gen_name, gen_type, &generic);
    if (st == PRO_TK_NO_ERROR && generic != nullptr) {
        if (generic == mdl ||
            autobbox::creo::ModelName(generic, L"") == autobbox::creo::ModelName(mdl, L"")) {
            return InstanceResolveState::NotInstance;
        }
        g_immediate_generic_cache[cache_key] = generic;
        if (immediate_generic != nullptr) {
            *immediate_generic = generic;
        }
        return InstanceResolveState::Resolved;
    }

    const ProMdlfileType gen_file_type = autobbox::creo::ToMdlFileType(gen_type);
    st = ProMdlnameInit(gen_name, gen_file_type, &generic);
    if (st != PRO_TK_NO_ERROR || generic == nullptr) {
        st = ProMdlnameRetrieve(gen_name, gen_file_type, &generic);
    }
    if (st == PRO_TK_NO_ERROR && generic != nullptr) {
        if (generic == mdl ||
            autobbox::creo::ModelName(generic, L"") == autobbox::creo::ModelName(mdl, L"")) {
            return InstanceResolveState::NotInstance;
        }
        g_immediate_generic_cache[cache_key] = generic;
        if (immediate_generic != nullptr) {
            *immediate_generic = generic;
        }
        return InstanceResolveState::Resolved;
    }

    return InstanceResolveState::Unresolved;
}

bool FamtableInstanceItemHasExplicitValue(ProFaminstance *inst, ProFamtableItem *item)
{
    if (inst == nullptr || item == nullptr) {
        return false;
    }

    ProBoolean is_default = PRO_B_FALSE;
    if (ProFaminstanceFamtableItemIsDefault(inst, item, &is_default) == PRO_TK_NO_ERROR) {
        return is_default != PRO_B_TRUE;
    }

    ProParamvalue oldv;
    std::memset(&oldv, 0, sizeof(oldv));
    return ProFaminstanceValueGet(inst, item, &oldv) == PRO_TK_NO_ERROR;
}

void SyncLoadedInstanceParameterCache(ProMdl inst_mdl,
                                      const wchar_t *param_name,
                                      ProParamvalue *value)
{
    if (inst_mdl == nullptr || param_name == nullptr || value == nullptr) {
        return;
    }

    ProModelitem owner = MdlAsModelitem(inst_mdl);
    ProParameter param;
    ProName pname = {0};
    CopyWStr(pname, param_name);
    if (ProParameterInit(&owner, pname, &param) == PRO_TK_NO_ERROR) {
        ProParameterValueWithUnitsSet(&param, value, nullptr);
    }
}

ProError SetStringOnCurrentInstanceFamtableLevel(ProMdl inst_mdl,
                                                 ProMdl immediate_generic,
                                                 const wchar_t *param_name,
                                                 const std::wstring &value,
                                                 bool recompute)
{
    if (inst_mdl == nullptr || immediate_generic == nullptr || param_name == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    ProFamtable ft;
    ProError st = ProFamtableInit(immediate_generic, &ft);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }

    ProName inst_name = {0};
    ProMdlNameGet(inst_mdl, inst_name);

    ProFaminstance inst;
    std::memset(&inst, 0, sizeof(inst));
    st = ProFaminstanceInit(inst_name, &ft, &inst);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }

    ProModelitem gen_owner = MdlAsModelitem(immediate_generic);
    if (!ParamExists(immediate_generic, param_name)) {
        st = SetStringParamOnOwner(&gen_owner, param_name, value);
        if (!IsWriteSuccess(st)) {
            return st;
        }
    }

    ProFamtableItem item;
    if (!InitFamtableItemFromModelParameter(immediate_generic, param_name, &item)) {
        return PRO_TK_BAD_INPUTS;
    }

    st = ProFamtableItemAdd(&ft, &item);
    if (!(st == PRO_TK_NO_ERROR || st == PRO_TK_NO_CHANGE || st == PRO_TK_E_FOUND)) {
        return st;
    }

    if (!recompute) {
        if (FamtableInstanceItemHasExplicitValue(&inst, &item)) {
            return PRO_TK_NO_CHANGE;
        }
    }

    ProLine sval = {0};
    CopyWStr(sval, value.c_str());
    ProParamvalue pv;
    std::memset(&pv, 0, sizeof(pv));
    ProParamvalueSet(&pv, sval, PRO_PARAM_STRING);
    st = ProFaminstanceValueSet(&inst, &item, &pv);
    if (!IsWriteSuccess(st)) {
        return st;
    }

    SyncLoadedInstanceParameterCache(inst_mdl, param_name, &pv);
    return st;
}

ProError SetDoubleOnCurrentInstanceFamtableLevel(ProMdl inst_mdl,
                                                 ProMdl immediate_generic,
                                                 const wchar_t *param_name,
                                                 double value,
                                                 bool recompute)
{
    if (inst_mdl == nullptr || immediate_generic == nullptr || param_name == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    ProFamtable ft;
    ProError st = ProFamtableInit(immediate_generic, &ft);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }

    ProName inst_name = {0};
    ProMdlNameGet(inst_mdl, inst_name);

    ProFaminstance inst;
    std::memset(&inst, 0, sizeof(inst));
    st = ProFaminstanceInit(inst_name, &ft, &inst);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }

    ProModelitem gen_owner = MdlAsModelitem(immediate_generic);
    if (!ParamExists(immediate_generic, param_name)) {
        st = SetDoubleParamOnOwner(&gen_owner, param_name, value);
        if (!IsWriteSuccess(st)) {
            return st;
        }
    }

    ProFamtableItem item;
    if (!InitFamtableItemFromModelParameter(immediate_generic, param_name, &item)) {
        return PRO_TK_BAD_INPUTS;
    }

    st = ProFamtableItemAdd(&ft, &item);
    if (!(st == PRO_TK_NO_ERROR || st == PRO_TK_NO_CHANGE || st == PRO_TK_E_FOUND)) {
        return st;
    }

    if (!recompute) {
        if (FamtableInstanceItemHasExplicitValue(&inst, &item)) {
            return PRO_TK_NO_CHANGE;
        }
    }

    ProParamvalue pv;
    std::memset(&pv, 0, sizeof(pv));
    ProParamvalueSet(&pv, &value, PRO_PARAM_DOUBLE);
    st = ProFaminstanceValueSet(&inst, &item, &pv);
    if (!IsWriteSuccess(st)) {
        return st;
    }

    SyncLoadedInstanceParameterCache(inst_mdl, param_name, &pv);
    return st;
}

} // namespace

bool IsFamilyInstanceQuick(ProMdl mdl)
{
    ProMdl generic = nullptr;
    ProError st = ProFaminstanceGenericGet(mdl, PRO_B_TRUE, &generic);
    if (st == PRO_TK_NO_ERROR) {
        if (generic == mdl) {
            return false;
        }
        if (generic != nullptr &&
            autobbox::creo::ModelName(generic, L"") == autobbox::creo::ModelName(mdl, L"")) {
            return false;
        }
        return true;
    }

    ProName gen_name = {0};
    ProMdlType gen_type = PRO_MDL_UNUSED;
    st = ProFaminstanceImmediategenericinfoGet(mdl, gen_name, &gen_type);
    return st == PRO_TK_NO_ERROR && gen_type != PRO_MDL_UNUSED;
}

ProError DeleteParamWithFamtableSupport(ProMdl mdl, const wchar_t *param_name)
{
    const ProError st = RemoveParamIfExists(mdl, param_name);
    return IsDeleteSuccess(st) ? PRO_TK_NO_ERROR : st;
}

ProError SetStringParamWithFamtableSupport(ProMdl mdl,
                                          const wchar_t *param_name,
                                          const std::wstring &value,
                                          bool recompute)
{
    const bool is_instance = IsFamilyInstanceQuick(mdl);
    ProMdl immediate_generic = nullptr;
    const InstanceResolveState inst_state = ResolveImmediateGenericModel(mdl, &immediate_generic);
    if (inst_state == InstanceResolveState::Resolved && immediate_generic != nullptr) {
        ProError st = SetStringOnCurrentInstanceFamtableLevel(
            mdl, immediate_generic, param_name, value, recompute);
        if (IsWriteSuccess(st) || st == PRO_TK_NO_CHANGE) {
            return st;
        }
    } else if (inst_state == InstanceResolveState::Unresolved && is_instance) {
        // Fall back to local write when the immediate generic cannot be resolved.
    }

    if (!recompute && ParamExists(mdl, param_name)) {
        return PRO_TK_NO_CHANGE;
    }

    ProModelitem owner = MdlAsModelitem(mdl);
    return SetStringParamOnOwner(&owner, param_name, value);
}

ProError SetDoubleParamWithFamtableSupport(ProMdl mdl,
                                          const wchar_t *param_name,
                                          double value,
                                          bool recompute)
{
    const bool is_instance = IsFamilyInstanceQuick(mdl);
    ProMdl immediate_generic = nullptr;
    const InstanceResolveState inst_state = ResolveImmediateGenericModel(mdl, &immediate_generic);
    if (inst_state == InstanceResolveState::Resolved && immediate_generic != nullptr) {
        ProError st = SetDoubleOnCurrentInstanceFamtableLevel(
            mdl, immediate_generic, param_name, value, recompute);
        if (IsWriteSuccess(st) || st == PRO_TK_NO_CHANGE) {
            return st;
        }
    } else if (inst_state == InstanceResolveState::Unresolved && is_instance) {
        // Fall back to local write when the immediate generic cannot be resolved.
    }

    if (!recompute && ParamExists(mdl, param_name)) {
        return PRO_TK_NO_CHANGE;
    }

    ProModelitem owner = MdlAsModelitem(mdl);
    return SetDoubleParamOnOwner(&owner, param_name, value);
}

ULONGLONG PreheatImmediateGenericCache(const std::vector<ProMdl> &models)
{
    const ULONGLONG t0 = GetTickCount64();
    for (ProMdl mdl : models) {
        if (mdl == nullptr || !IsFamilyInstanceQuick(mdl)) {
            continue;
        }
        ProMdl generic = nullptr;
        ResolveImmediateGenericModel(mdl, &generic);
    }
    return GetTickCount64() - t0;
}

} // namespace autobbox::creo
