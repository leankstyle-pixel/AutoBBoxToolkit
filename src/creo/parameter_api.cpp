#include "autobbox/creo/parameter_api.h"

#include <ProParamDriver.h>
#include <ProParameter.h>
#include <ProParamval.h>
#include <ProUtil.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <unordered_map>

namespace autobbox::creo {

namespace {

void CopyWText(wchar_t *dest, size_t capacity, const wchar_t *src)
{
    if (dest == nullptr || capacity == 0) {
        return;
    }
    size_t i = 0;
    if (src != nullptr) {
        while (i + 1 < capacity && src[i] != L'\0') {
            dest[i] = src[i];
            ++i;
        }
    }
    dest[i] = L'\0';
}

ProError UpsertOwnerParameter(ProModelitem *owner,
                              const wchar_t *param_name,
                              ProParamvalue *value)
{
    if (owner == nullptr || param_name == nullptr || value == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    ProParameter param;
    ProName pname = {0};
    CopyWText(pname, sizeof(pname) / sizeof(pname[0]), param_name);

    ProError st = ProParameterInit(owner, pname, &param);
    if (st == PRO_TK_NO_ERROR) {
        return ProParameterValueWithUnitsSet(&param, value, nullptr);
    }
    return ProParameterWithUnitsCreate(owner, pname, value, nullptr, &param);
}

ProModelitem MdlAsModelitem(ProMdl mdl)
{
    ProModelitem item;
    std::memset(&item, 0, sizeof(item));
    ProMdlToModelitem(mdl, &item);
    return item;
}

std::wstring TrimWhitespace(const std::wstring &value)
{
    size_t begin = 0;
    while (begin < value.size() && std::iswspace(value[begin])) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::iswspace(value[end - 1])) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::wstring UppercaseAscii(const std::wstring &value)
{
    std::wstring out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towupper(ch));
    });
    return out;
}

std::wstring FormatDoubleValue(double value)
{
    wchar_t buf[128] = {0};
    std::swprintf(buf, sizeof(buf) / sizeof(buf[0]) - 1, L"%.6f", value);
    std::wstring text(buf);
    while (!text.empty() && text.back() == L'0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == L'.') {
        text.pop_back();
    }
    if (text.empty()) {
        text = L"0";
    }
    return text;
}

bool IsParameterDesignated(ProParameter *param)
{
    if (param == nullptr) {
        return false;
    }
    ProBoolean designated = PRO_B_FALSE;
    return ProParameterDesignationVerify(param, &designated) == PRO_TK_NO_ERROR &&
           designated == PRO_B_TRUE;
}

bool GetParameterDescription(ProParameter *param, std::wstring &description_out)
{
    description_out.clear();
    if (param == nullptr) {
        return false;
    }

    wchar_t *description = nullptr;
    const ProError st = ProParameterDescriptionGet(param, &description);
    if (st != PRO_TK_NO_ERROR || description == nullptr) {
        return false;
    }

    description_out.assign(description);
    ProWstringFree(description);
    return !description_out.empty();
}

bool IsParameterGovernedByTable(ProParameter *param)
{
    if (param == nullptr) {
        return false;
    }

    ProParamtableSet set = nullptr;
    const ProError st = ProParameterTablesetGet(param, &set);
    if (st != PRO_TK_NO_ERROR || set == nullptr) {
        return false;
    }

    ProParamtablesetFree(set);
    return true;
}

std::wstring DriverReadonlyReason(ProParameterDriver driver)
{
    switch (driver) {
    case PRO_PARAMDRIVER_PARAM:
        return L"参数由其他参数驱动";
    case PRO_PARAMDRIVER_FUNCTION:
        return L"参数由函数驱动";
    case PRO_PARAMDRIVER_RELATION:
        return L"参数由关系式驱动";
    case PRO_PARAMDRIVER_NONE:
    default:
        return L"参数只读";
    }
}

struct BomSnapshotCollectCtx {
    core::BomModelSnapshot *snapshot = nullptr;
};

struct ParamPreviewCollectCtx {
    std::unordered_map<std::wstring, size_t> *index_by_name = nullptr;
    std::vector<core::ParamPreviewEntry> *entries = nullptr;
};

ProError CollectBomSnapshotVisitAction(ProParameter *handle,
                                       ProError status,
                                       ProAppData data)
{
    if (status != PRO_TK_NO_ERROR || handle == nullptr || data == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    BomSnapshotCollectCtx *ctx = reinterpret_cast<BomSnapshotCollectCtx *>(data);
    if (ctx == nullptr || ctx->snapshot == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    const std::wstring name = NormalizeParameterName(std::wstring(handle->id));
    if (name.empty()) {
        return PRO_TK_NO_ERROR;
    }

    core::BomModelParamInfo info;
    info.exists = true;
    if (!ReadParameterDisplayValue(handle, info.type, info.display_value)) {
        info.display_value = L"<UNREADABLE>";
    }

    if (ProParameterDrivertypeGet(handle, &info.driver) != PRO_TK_NO_ERROR) {
        info.readonly_reason = L"无法读取驱动状态";
    }
    if (ProParameterLockstatusGet(handle, &info.lock_status) != PRO_TK_NO_ERROR) {
        info.lock_status = PRO_PARAMLOCKSTATUS_LOCKED;
        if (info.readonly_reason.empty()) {
            info.readonly_reason = L"无法读取锁定状态";
        }
    }

    if (!IsWritableParameterType(info.type)) {
        info.readonly_reason = L"参数类型首版不支持回写";
    } else if (info.lock_status != PRO_PARAMLOCKSTATUS_UNLOCKED) {
        info.readonly_reason = L"参数已锁定";
    } else if (info.driver != PRO_PARAMDRIVER_NONE) {
        info.readonly_reason = DriverReadonlyReason(info.driver);
    } else if (info.readonly_reason.empty()) {
        info.writable = true;
    }

    ctx->snapshot->params[name] = info;
    return PRO_TK_NO_ERROR;
}

ProError CollectParamPreviewVisitAction(ProParameter *handle,
                                        ProError status,
                                        ProAppData data)
{
    if (status != PRO_TK_NO_ERROR || handle == nullptr || data == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    ParamPreviewCollectCtx *ctx = reinterpret_cast<ParamPreviewCollectCtx *>(data);
    if (ctx == nullptr || ctx->index_by_name == nullptr || ctx->entries == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    const std::wstring name = NormalizeParameterName(std::wstring(handle->id));
    if (name.empty()) {
        return PRO_TK_NO_ERROR;
    }

    ProParamvalueType type = PRO_PARAM_NOT_SET;
    std::wstring sample_value;
    if (!ReadParameterDisplayValue(handle, type, sample_value)) {
        sample_value = L"<UNREADABLE>";
    }

    const bool designated = IsParameterDesignated(handle);
    const bool table_source = IsParameterGovernedByTable(handle);
    std::wstring description;
    GetParameterDescription(handle, description);

    auto found = ctx->index_by_name->find(name);
    if (found == ctx->index_by_name->end()) {
        core::ParamPreviewEntry entry;
        entry.name = name;
        entry.hit_count = 1;
        entry.types.insert(type);
        entry.sample_value = sample_value;
        entry.designated_count = designated ? 1 : 0;
        entry.table_source_count = table_source ? 1 : 0;
        entry.sample_description = description;
        const size_t new_index = ctx->entries->size();
        ctx->entries->push_back(entry);
        (*ctx->index_by_name)[name] = new_index;
        return PRO_TK_NO_ERROR;
    }

    core::ParamPreviewEntry &entry = (*ctx->entries)[found->second];
    ++entry.hit_count;
    entry.types.insert(type);
    if (designated) {
        ++entry.designated_count;
    }
    if (table_source) {
        ++entry.table_source_count;
    }
    if (entry.sample_value.empty() || entry.sample_value == L"<UNREADABLE>") {
        entry.sample_value = sample_value;
    }
    if (entry.sample_description.empty() && !description.empty()) {
        entry.sample_description = description;
    }
    return PRO_TK_NO_ERROR;
}

} // namespace

ProError SetStringParamOnOwner(ProModelitem *owner, const wchar_t *param_name, const std::wstring &value)
{
    ProLine sval = {0};
    CopyWText(sval, sizeof(sval) / sizeof(sval[0]), value.c_str());

    ProParamvalue pv;
    std::memset(&pv, 0, sizeof(pv));
    ProParamvalueSet(&pv, sval, PRO_PARAM_STRING);
    return UpsertOwnerParameter(owner, param_name, &pv);
}

ProError SetDoubleParamOnOwner(ProModelitem *owner, const wchar_t *param_name, double value)
{
    ProParamvalue pv;
    std::memset(&pv, 0, sizeof(pv));
    ProParamvalueSet(&pv, &value, PRO_PARAM_DOUBLE);
    return UpsertOwnerParameter(owner, param_name, &pv);
}

ProError SetIntegerParamOnOwner(ProModelitem *owner, const wchar_t *param_name, int value)
{
    ProParamvalue pv;
    std::memset(&pv, 0, sizeof(pv));
    ProParamvalueSet(&pv, &value, PRO_PARAM_INTEGER);
    return UpsertOwnerParameter(owner, param_name, &pv);
}

ProError SetBooleanParamOnOwner(ProModelitem *owner, const wchar_t *param_name, short value)
{
    ProParamvalue pv;
    std::memset(&pv, 0, sizeof(pv));
    ProParamvalueSet(&pv, &value, PRO_PARAM_BOOLEAN);
    return UpsertOwnerParameter(owner, param_name, &pv);
}

ProError SetStringParamOnModel(ProMdl mdl, const wchar_t *param_name, const std::wstring &value)
{
    if (mdl == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    ProModelitem owner = MdlAsModelitem(mdl);
    return SetStringParamOnOwner(&owner, param_name, value);
}

bool ParameterExistsOnModel(ProMdl mdl, const wchar_t *param_name)
{
    if (mdl == nullptr || param_name == nullptr || param_name[0] == L'\0') {
        return false;
    }

    ProModelitem owner = MdlAsModelitem(mdl);
    ProName pname = {0};
    CopyWText(pname,
              sizeof(pname) / sizeof(pname[0]),
              NormalizeParameterName(param_name).c_str());

    ProParameter param;
    std::memset(&param, 0, sizeof(param));
    return ProParameterInit(&owner, pname, &param) == PRO_TK_NO_ERROR;
}

bool ReadStringParamOnModel(ProMdl mdl, const wchar_t *param_name, std::wstring &value_out)
{
    value_out.clear();
    if (mdl == nullptr || param_name == nullptr || param_name[0] == L'\0') {
        return false;
    }

    ProModelitem owner = MdlAsModelitem(mdl);
    ProName pname = {0};
    CopyWText(pname,
              sizeof(pname) / sizeof(pname[0]),
              NormalizeParameterName(param_name).c_str());

    ProParameter param;
    std::memset(&param, 0, sizeof(param));
    if (ProParameterInit(&owner, pname, &param) != PRO_TK_NO_ERROR) {
        return false;
    }

    ProParamvalue pv;
    std::memset(&pv, 0, sizeof(pv));
    ProParamvalueType type = PRO_PARAM_NOT_SET;
    if (ProParameterValueWithUnitsGet(&param, &pv, nullptr) != PRO_TK_NO_ERROR ||
        ProParamvalueTypeGet(&pv, &type) != PRO_TK_NO_ERROR ||
        type != PRO_PARAM_STRING) {
        return false;
    }

    ProLine value = {0};
    if (ProParamvalueValueGet(&pv, type, value) != PRO_TK_NO_ERROR) {
        return false;
    }

    value_out.assign(value);
    return true;
}

bool ReadParamDisplayValueOnModel(ProMdl mdl, const wchar_t *param_name, std::wstring &value_out)
{
    value_out.clear();
    if (mdl == nullptr || param_name == nullptr || param_name[0] == L'\0') {
        return false;
    }

    ProModelitem owner = MdlAsModelitem(mdl);
    ProName pname = {0};
    CopyWText(pname,
              sizeof(pname) / sizeof(pname[0]),
              NormalizeParameterName(param_name).c_str());

    ProParameter param;
    std::memset(&param, 0, sizeof(param));
    if (ProParameterInit(&owner, pname, &param) != PRO_TK_NO_ERROR) {
        return false;
    }

    ProParamvalueType type = PRO_PARAM_NOT_SET;
    return ReadParameterDisplayValue(&param, type, value_out);
}

std::wstring NormalizeParameterName(const std::wstring &name)
{
    return UppercaseAscii(TrimWhitespace(name));
}

bool IsWritableParameterType(ProParamvalueType type)
{
    return type == PRO_PARAM_STRING ||
           type == PRO_PARAM_INTEGER ||
           type == PRO_PARAM_DOUBLE ||
           type == PRO_PARAM_BOOLEAN;
}

bool ReadParameterDisplayValue(ProParameter *param,
                               ProParamvalueType &type_out,
                               std::wstring &value_out)
{
    type_out = PRO_PARAM_NOT_SET;
    value_out.clear();
    if (param == nullptr) {
        return false;
    }

    ProParamvalue pv;
    std::memset(&pv, 0, sizeof(pv));
    if (ProParameterValueWithUnitsGet(param, &pv, nullptr) != PRO_TK_NO_ERROR ||
        ProParamvalueTypeGet(&pv, &type_out) != PRO_TK_NO_ERROR) {
        return false;
    }

    switch (type_out) {
    case PRO_PARAM_STRING: {
        ProLine s = {0};
        if (ProParamvalueValueGet(&pv, type_out, s) != PRO_TK_NO_ERROR) {
            return false;
        }
        value_out = std::wstring(s);
        if (value_out.empty()) {
            value_out = L"\"\"";
        }
        return true;
    }
    case PRO_PARAM_DOUBLE: {
        double d = 0.0;
        if (ProParamvalueValueGet(&pv, type_out, &d) != PRO_TK_NO_ERROR) {
            return false;
        }
        value_out = FormatDoubleValue(d);
        return true;
    }
    case PRO_PARAM_INTEGER:
    case PRO_PARAM_NOTE_ID: {
        int i = 0;
        if (ProParamvalueValueGet(&pv, type_out, &i) != PRO_TK_NO_ERROR) {
            return false;
        }
        value_out = std::to_wstring(i);
        return true;
    }
    case PRO_PARAM_BOOLEAN: {
        short b = 0;
        if (ProParamvalueValueGet(&pv, type_out, &b) != PRO_TK_NO_ERROR) {
            return false;
        }
        value_out = (b != 0) ? L"YES" : L"NO";
        return true;
    }
    case PRO_PARAM_VOID:
        value_out = L"<VOID>";
        return true;
    case PRO_PARAM_NOT_SET:
        value_out = L"<NOT_SET>";
        return true;
    default:
        value_out = L"<UNKNOWN>";
        return true;
    }
}

core::BomModelSnapshot CollectBomModelSnapshot(ProMdl mdl)
{
    core::BomModelSnapshot snapshot;
    snapshot.mdl = mdl;
    if (mdl == nullptr) {
        return snapshot;
    }

    ProModelitem owner = MdlAsModelitem(mdl);
    BomSnapshotCollectCtx ctx;
    ctx.snapshot = &snapshot;
    ProParameterVisit(&owner, nullptr, CollectBomSnapshotVisitAction, &ctx);
    return snapshot;
}

std::vector<core::ParamPreviewEntry> CollectParameterPreview(const std::vector<ProMdl> &models)
{
    std::vector<core::ParamPreviewEntry> entries;
    std::unordered_map<std::wstring, size_t> index_by_name;
    ParamPreviewCollectCtx ctx;
    ctx.index_by_name = &index_by_name;
    ctx.entries = &entries;

    for (ProMdl mdl : models) {
        ProModelitem owner = MdlAsModelitem(mdl);
        ProParameterVisit(&owner, nullptr, CollectParamPreviewVisitAction, &ctx);
    }

    std::sort(entries.begin(), entries.end(), [](const core::ParamPreviewEntry &lhs, const core::ParamPreviewEntry &rhs) {
        return lhs.name < rhs.name;
    });
    return entries;
}

bool SetModelParameterFromSpec(ProMdl mdl, const core::ParamAddSpec &spec)
{
    if (mdl == nullptr || spec.name.empty()) {
        return false;
    }

    ProModelitem owner = MdlAsModelitem(mdl);
    ProError st = PRO_TK_BAD_INPUTS;
    switch (spec.type) {
    case PRO_PARAM_STRING:
        st = SetStringParamOnOwner(&owner, spec.name.c_str(), spec.string_value);
        break;
    case PRO_PARAM_INTEGER:
        st = SetIntegerParamOnOwner(&owner, spec.name.c_str(), spec.int_value);
        break;
    case PRO_PARAM_DOUBLE:
        st = SetDoubleParamOnOwner(&owner, spec.name.c_str(), spec.double_value);
        break;
    case PRO_PARAM_BOOLEAN:
        st = SetBooleanParamOnOwner(&owner, spec.name.c_str(), spec.bool_value);
        break;
    default:
        return false;
    }
    return st == PRO_TK_NO_ERROR || st == PRO_TK_NO_CHANGE;
}

} // namespace autobbox::creo
