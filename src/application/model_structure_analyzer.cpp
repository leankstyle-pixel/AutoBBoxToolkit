#include "autobbox/application/model_structure_analyzer.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/application/ft_discovery.h"
#include "autobbox/application/ft_reader.h"
#include "autobbox/creo/family_table_api.h"
#include "autobbox/creo/model_info.h"

#include <ProArray.h>
#include <ProAsmcomp.h>
#include <ProFeature.h>
#include <ProFamtable.h>
#include <ProFaminstance.h>
#include <ProDimension.h>
#include <ProMdl.h>
#include <ProMdlUnits.h>
#include <ProModelitem.h>
#include <ProParameter.h>
#include <ProParamDriver.h>
#include <ProRelSet.h>
#include <ProSelection.h>
#include <ProSolid.h>
#include <ProToolkit.h>
#include <ProUtil.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cwctype>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <set>
#include <vector>

namespace autobbox::application {

namespace {

void LogLine(const ModelStructureLogSink &log_sink, const char *fmt, ...)
{
    if (!log_sink || fmt == nullptr) {
        return;
    }
    char buffer[4096] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_sink(buffer);
}

std::wstring Trim(const std::wstring &value)
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

std::wstring Upper(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towupper(ch));
    });
    return value;
}

std::wstring FormatDouble(double value)
{
    wchar_t buf[96] = {0};
    std::swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%.6f", value);
    std::wstring out(buf);
    while (!out.empty() && out.back() == L'0') out.pop_back();
    if (!out.empty() && out.back() == L'.') out.pop_back();
    return out.empty() ? L"0" : out;
}

std::wstring StatusText(ProError st)
{
    return L"st=" + std::to_wstring(static_cast<int>(st));
}

std::wstring FeatureStatusLabel(ProFeatStatus st)
{
    switch (st) {
    case PRO_FEAT_ACTIVE: return L"正常";
    case PRO_FEAT_INACTIVE: return L"非活动";
    case PRO_FEAT_FAMTAB_SUPPRESSED: return L"族表抑制";
    case PRO_FEAT_SIMP_REP_SUPPRESSED: return L"简化表示抑制";
    case PRO_FEAT_PROG_SUPPRESSED: return L"程序抑制";
    case PRO_FEAT_SUPPRESSED: return L"抑制";
    case PRO_FEAT_UNREGENERATED: return L"未再生";
    default: return L"未知";
    }
}

std::wstring PathKey(const ProAsmcomppath &path, int depth)
{
    if (depth <= 0) {
        return L"ROOT";
    }
    std::wstring key;
    for (int i = 0; i < depth && i < PRO_MAX_ASSEM_LEVEL; ++i) {
        if (!key.empty()) key += L"/";
        key += std::to_wstring(path.comp_id_table[i]);
    }
    return key;
}

std::wstring PathText(const ProAsmcomppath &path)
{
    return PathKey(path, path.table_num);
}

const wchar_t *ParamTypeLabel(ProParamvalueType type)
{
    switch (type) {
    case PRO_PARAM_DOUBLE: return L"double";
    case PRO_PARAM_STRING: return L"string";
    case PRO_PARAM_INTEGER: return L"integer";
    case PRO_PARAM_BOOLEAN: return L"boolean";
    case PRO_PARAM_NOTE_ID: return L"NOTE_ID";
    case PRO_PARAM_VOID: return L"VOID";
    case PRO_PARAM_NOT_SET: return L"NOT_SET";
    default: return L"UNKNOWN";
    }
}

std::string JsonEscape(const std::wstring &value)
{
    const std::string utf8 = autobbox::common::WideToUtf8(value);
    std::string out;
    out.reserve(utf8.size() + 8);
    for (unsigned char ch : utf8) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                char buf[8] = {0};
                std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
                out += buf;
            } else {
                out.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return out;
}

std::string JsonStr(const std::wstring &value)
{
    return "\"" + JsonEscape(value) + "\"";
}

std::string JsonBool(bool value)
{
    return value ? "true" : "false";
}

std::string JsonNum(double value)
{
    char buf[96] = {0};
    std::snprintf(buf, sizeof(buf), "%.12g", value);
    return std::string(buf);
}

const ModelStructureNode *FindNodeByIndex(const ModelStructureReport &report, int node_index)
{
    for (const auto &node : report.nodes) {
        if (node.index == node_index) {
            return &node;
        }
    }
    return nullptr;
}

std::wstring ParamValueToText(ProParamvalue &pv, ProParamvalueType type)
{
    switch (type) {
    case PRO_PARAM_STRING: {
        ProLine s = {0};
        return ProParamvalueValueGet(&pv, type, s) == PRO_TK_NO_ERROR ? std::wstring(s) : L"<UNREADABLE>";
    }
    case PRO_PARAM_DOUBLE: {
        double v = 0.0;
        return ProParamvalueValueGet(&pv, type, &v) == PRO_TK_NO_ERROR ? FormatDouble(v) : L"<UNREADABLE>";
    }
    case PRO_PARAM_INTEGER:
    case PRO_PARAM_NOTE_ID: {
        int v = 0;
        return ProParamvalueValueGet(&pv, type, &v) == PRO_TK_NO_ERROR ? std::to_wstring(v) : L"<UNREADABLE>";
    }
    case PRO_PARAM_BOOLEAN: {
        short v = 0;
        return ProParamvalueValueGet(&pv, type, &v) == PRO_TK_NO_ERROR ? (v ? L"YES" : L"NO") : L"<UNREADABLE>";
    }
    case PRO_PARAM_VOID:
        return L"<VOID>";
    case PRO_PARAM_NOT_SET:
        return L"<NOT_SET>";
    default:
        return L"<UNKNOWN>";
    }
}

const wchar_t *FamTypeLabel(ProFamtabType type)
{
    switch (type) {
    case PRO_FAM_USER_PARAM: return L"PARAM";
    case PRO_FAM_SYSTEM_PARAM: return L"SYSTEM_PARAM";
    case PRO_FAM_DIMENSION: return L"DIM";
    case PRO_FAM_FEATURE: return L"FEAT";
    case PRO_FAM_ASMCOMP: return L"COMP";
    case PRO_FAM_UDF: return L"UDF";
    case PRO_FAM_ASMCOMP_MODEL: return L"COMP_MODEL";
    case PRO_FAM_EXTERNAL_REFERENCE: return L"EXT_REF";
    case PRO_FAM_MERGE_PART_REF: return L"MERGE_PART";
    default: return L"FAM_ITEM";
    }
}

const wchar_t *ConstraintTypeLabel(int type)
{
    switch (type) {
    case PRO_ASM_MATE: return L"MATE";
    case PRO_ASM_MATE_OFF: return L"MATE_OFF";
    case PRO_ASM_ALIGN: return L"ALIGN";
    case PRO_ASM_ALIGN_OFF: return L"ALIGN_OFF";
    case PRO_ASM_INSERT: return L"INSERT";
    case PRO_ASM_ORIENT: return L"ORIENT";
    case PRO_ASM_CSYS: return L"CSYS";
    case PRO_ASM_TANGENT: return L"TANGENT";
    case PRO_ASM_PNT_ON_SRF: return L"PNT_ON_SRF";
    case PRO_ASM_EDGE_ON_SRF: return L"EDGE_ON_SRF";
    case PRO_ASM_DEF_PLACEMENT: return L"DEF_PLACEMENT";
    case PRO_ASM_FIX: return L"FIX";
    case PRO_ASM_AUTO: return L"AUTO";
    case PRO_ASM_EXPLICIT: return L"EXPLICIT";
    default: return L"CONSTRAINT";
    }
}

std::wstring SelectionText(ProSelection sel)
{
    if (sel == nullptr) {
        return L"-";
    }
    ProModelitem item = {};
    const ProError st = ProSelectionModelitemGet(sel, &item);
    if (st != PRO_TK_NO_ERROR) {
        return StatusText(st);
    }
    std::wstring out = L"type=" + std::to_wstring(item.type) + L" id=" + std::to_wstring(item.id);
    if (item.owner != nullptr) {
        out += L" owner=" + autobbox::creo::ModelName(item.owner, L"");
    }
    return out;
}

ModelStructureReference BuildReferenceFromSelection(int node_index,
                                                    const std::wstring &source,
                                                    ProSelection sel)
{
    ModelStructureReference ref;
    ref.node_index = node_index;
    ref.source = source;
    ProModelitem item = {};
    if (sel != nullptr && ProSelectionModelitemGet(sel, &item) == PRO_TK_NO_ERROR) {
        ref.owner_model = item.owner == nullptr ? L"" : autobbox::creo::ModelName(item.owner, L"");
        ref.name = L"id=" + std::to_wstring(item.id);
        ref.type = item.type;
        ref.id = item.id;
        switch (item.type) {
        case PRO_SURFACE: ref.type_label = L"曲面"; break;
        case PRO_EDGE: ref.type_label = L"边"; break;
        case PRO_AXIS: ref.type_label = L"基准轴"; break;
        case PRO_CSYS: ref.type_label = L"坐标系"; break;
        case PRO_POINT: ref.type_label = L"基准点"; break;
        case PRO_FEATURE: ref.type_label = L"特征"; break;
        default: ref.type_label = L"type=" + std::to_wstring(item.type); break;
        }
        ref.unique = item.owner != nullptr && item.id != PRO_VALUE_UNUSED;
    }
    return ref;
}

ProModelitem MdlAsModelitem(ProMdl mdl)
{
    ProModelitem item = {};
    if (mdl != nullptr) {
        ProMdlToModelitem(mdl, &item);
    }
    return item;
}

bool ParameterTableDriven(ProParameter *param)
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

bool ParameterDescription(ProParameter *param, std::wstring &out)
{
    out.clear();
    wchar_t *raw = nullptr;
    if (param == nullptr || ProParameterDescriptionGet(param, &raw) != PRO_TK_NO_ERROR || raw == nullptr) {
        return false;
    }
    out = raw;
    ProWstringFree(raw);
    return true;
}

std::wstring ReadStringParamValue(ProMdl mdl, const wchar_t *name)
{
    ProModelitem owner = MdlAsModelitem(mdl);
    ProName pname = {0};
    wcsncpy_s(pname, name, _TRUNCATE);
    ProParameter param = {};
    if (ProParameterInit(&owner, pname, &param) != PRO_TK_NO_ERROR) {
        return L"";
    }
    ProParamvalue pv = {};
    ProParamvalueType type = PRO_PARAM_NOT_SET;
    if (ProParameterValueWithUnitsGet(&param, &pv, nullptr) != PRO_TK_NO_ERROR ||
        ProParamvalueTypeGet(&pv, &type) != PRO_TK_NO_ERROR ||
        type != PRO_PARAM_STRING) {
        return L"";
    }
    return ParamValueToText(pv, type);
}

std::wstring ModelDisplayName(ProMdl mdl)
{
    ProMdlFileName display_name = {0};
    if (mdl != nullptr &&
        ProMdlDisplaynameGet(mdl, PRO_B_TRUE, display_name) == PRO_TK_NO_ERROR &&
        display_name[0] != L'\0') {
        return display_name;
    }
    return autobbox::creo::ModelName(mdl, L"");
}

std::wstring ModelOriginPath(ProMdl mdl)
{
    ProPath origin = {0};
    if (mdl != nullptr && ProMdlOriginGet(mdl, origin) == PRO_TK_NO_ERROR && origin[0] != L'\0') {
        return origin;
    }
    ProPath dir = {0};
    if (mdl != nullptr && ProMdlDirectoryPathGet(mdl, dir) == PRO_TK_NO_ERROR && dir[0] != L'\0') {
        ProMdlFileName display = {0};
        if (ProMdlDisplaynameGet(mdl, PRO_B_TRUE, display) == PRO_TK_NO_ERROR && display[0] != L'\0') {
            return autobbox::common::JoinPath(std::wstring(dir), display);
        }
        return dir;
    }
    return L"";
}

std::wstring ModelCustomName(ProMdl mdl)
{
    static const wchar_t *kCandidates[] = {
        L"CUSTOM_NAME",
        L"DISPLAY_NAME",
        L"PTC_COMMON_NAME",
        L"COMMON_NAME",
        L"DESCRIPTION"
    };
    for (const wchar_t *name : kCandidates) {
        std::wstring value = Trim(ReadStringParamValue(mdl, name));
        if (!value.empty()) {
            return value;
        }
    }
    return L"";
}

bool IsFamilyInstanceModel(ProMdl mdl)
{
    if (mdl == nullptr) {
        return false;
    }
    ProMdl generic = nullptr;
    if (ProFaminstanceGenericGet(mdl, PRO_B_FALSE, &generic) == PRO_TK_NO_ERROR && generic != nullptr) {
        return true;
    }
    ProName generic_name = {0};
    ProMdlType generic_type = PRO_MDL_UNUSED;
    return ProFaminstanceImmediategenericinfoGet(mdl, generic_name, &generic_type) == PRO_TK_NO_ERROR &&
           generic_name[0] != L'\0';
}

void ReadUnits(ModelStructureNode &node)
{
    ProUnitsystem system = {};
    if (ProMdlPrincipalunitsystemGet(node.mdl, &system) != PRO_TK_NO_ERROR) {
        return;
    }
    node.unit_system = system.name;
    ProUnititem unit = {};
    if (ProUnitsystemUnitGet(&system, PRO_UNITTYPE_LENGTH, &unit) == PRO_TK_NO_ERROR) {
        node.length_unit = unit.name;
    }
    unit = {};
    if (ProUnitsystemUnitGet(&system, PRO_UNITTYPE_MASS, &unit) == PRO_TK_NO_ERROR) {
        node.mass_unit = unit.name;
    }
}

struct ParamCtx {
    int node_index = -1;
    std::wstring owner_scope;
    std::vector<ModelStructureParameter> *out = nullptr;
};

ProError ParameterVisitAction(ProParameter *param, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || param == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }
    auto *ctx = reinterpret_cast<ParamCtx *>(app_data);
    if (ctx->out == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    ModelStructureParameter row;
    row.node_index = ctx->node_index;
    row.name = param->id;

    ProParamvalue pv = {};
    if (ProParameterValueWithUnitsGet(param, &pv, nullptr) == PRO_TK_NO_ERROR &&
        ProParamvalueTypeGet(&pv, &row.type) == PRO_TK_NO_ERROR) {
        row.value = ParamValueToText(pv, row.type);
    } else {
        row.value = L"<UNREADABLE>";
    }
    row.type_label = ParamTypeLabel(row.type);

    ProBoolean designated = PRO_B_FALSE;
    row.designated =
        ProParameterDesignationVerify(param, &designated) == PRO_TK_NO_ERROR && designated == PRO_B_TRUE;
    row.table_driven = ParameterTableDriven(param);
    ProParameterDrivertypeGet(param, &row.driver);
    ProParameterLockstatusGet(param, &row.lock_status);
    ParameterDescription(param, row.description);
    row.owner_scope = ctx->owner_scope.empty() ? (ctx->node_index == 0 ? L"顶层模型" : L"组件模型") : ctx->owner_scope;
    row.exists = true;
    row.writable = row.lock_status == PRO_PARAMLOCKSTATUS_UNLOCKED &&
                   row.driver == PRO_PARAMDRIVER_NONE &&
                   !row.table_driven &&
                   (row.type == PRO_PARAM_STRING || row.type == PRO_PARAM_INTEGER ||
                    row.type == PRO_PARAM_DOUBLE || row.type == PRO_PARAM_BOOLEAN);
    ctx->out->push_back(row);
    return PRO_TK_NO_ERROR;
}

struct DimensionCtx {
    int node_index = -1;
    int feature_id = -1;
    std::wstring feature_name;
    std::vector<ModelStructureDimension> *out = nullptr;
};

ProError DimensionVisitAction(ProDimension *dimension, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || dimension == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }
    auto *ctx = reinterpret_cast<DimensionCtx *>(app_data);
    if (ctx->out == nullptr) return PRO_TK_NO_ERROR;
    ModelStructureDimension dim;
    dim.node_index = ctx->node_index;
    dim.feature_id = ctx->feature_id;
    dim.feature_name = ctx->feature_name;
    ProName symbol = {0};
    if (ProDimensionSymbolGet(dimension, symbol) == PRO_TK_NO_ERROR) {
        dim.symbol = symbol;
    }
    ProDimensionValueGet(dimension, &dim.value);
    ProDimensiontype type = {};
    if (ProDimensionTypeGet(dimension, &type) == PRO_TK_NO_ERROR) {
        dim.type = static_cast<int>(type);
    }
    ctx->out->push_back(dim);
    return PRO_TK_NO_ERROR;
}

struct FeatureCtx {
    int node_index = -1;
    std::vector<ModelStructureFeatureInfo> *features = nullptr;
    std::vector<ModelStructureDimension> *dimensions = nullptr;
    std::vector<ModelStructureParameter> *parameters = nullptr;
};

ProError FeatureVisitAction(ProFeature *feature, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || feature == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }
    auto *ctx = reinterpret_cast<FeatureCtx *>(app_data);
    if (ctx->features == nullptr) return PRO_TK_NO_ERROR;
    ModelStructureFeatureInfo info;
    info.node_index = ctx->node_index;
    info.id = feature->id;
    ProName type_name = {0};
    if (ProFeatureTypenameGet(feature, type_name) == PRO_TK_NO_ERROR) {
        info.type_name = type_name;
    }
    ProModelitem item = *feature;
    ProMdlName tree_name = {0};
    if (ProFeatureMdltreeDisplaynameGet(&item, tree_name) == PRO_TK_NO_ERROR) {
        info.name = tree_name;
    }
    ProFeatStatus feat_status = PRO_FEAT_INVALID;
    if (ProFeatureStatusGet(feature, &feat_status) == PRO_TK_NO_ERROR) {
        info.status = static_cast<int>(feat_status);
        info.suppressed = feat_status == PRO_FEAT_SUPPRESSED ||
                          feat_status == PRO_FEAT_FAMTAB_SUPPRESSED ||
                          feat_status == PRO_FEAT_SIMP_REP_SUPPRESSED ||
                          feat_status == PRO_FEAT_PROG_SUPPRESSED;
    }
    ProBoolean visible = PRO_B_TRUE;
    if (ProFeatureVisibilityGet(feature, &visible) == PRO_TK_NO_ERROR) {
        info.visible = visible == PRO_B_TRUE;
    }
    ctx->features->push_back(info);

    if (ctx->parameters != nullptr) {
        ParamCtx pctx;
        pctx.node_index = ctx->node_index;
        pctx.owner_scope = L"特征:" + (info.name.empty() ? std::to_wstring(info.id) : info.name);
        pctx.out = ctx->parameters;
        ProParameterVisit(&item, nullptr, ParameterVisitAction, &pctx);
    }

    if (ctx->dimensions != nullptr) {
        DimensionCtx dctx;
        dctx.node_index = ctx->node_index;
        dctx.feature_id = info.id;
        dctx.feature_name = info.name;
        dctx.out = ctx->dimensions;
        ProFeatureDimensionVisit(feature, DimensionVisitAction, nullptr, &dctx);
    }
    return PRO_TK_NO_ERROR;
}

struct FamilyCtx {
    ModelStructureFamilyTable *table = nullptr;
};

ProError FamItemAction(ProFamtableItem *item, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || item == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }
    auto *ctx = reinterpret_cast<FamilyCtx *>(app_data);
    if (ctx->table == nullptr) {
        return PRO_TK_NO_ERROR;
    }
    ModelStructureFamilyColumn col;
    col.key = item->string;
    col.type = static_cast<int>(item->type);
    col.type_label = FamTypeLabel(item->type);
    ctx->table->columns.push_back(col);
    return PRO_TK_NO_ERROR;
}

ProError FamInstanceAction(ProFaminstance *inst, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || inst == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }
    auto *ctx = reinterpret_cast<FamilyCtx *>(app_data);
    if (ctx->table == nullptr) {
        return PRO_TK_NO_ERROR;
    }
    ModelStructureFamilyRow row;
    row.instance_name = inst->name;
    int locked = 0;
    if (ProFaminstanceCheck(inst, &locked) == PRO_TK_NO_ERROR) {
        row.locked = locked != 0;
    }
    ProBoolean ext_locked = PRO_B_FALSE;
    if (ProFaminstanceIsExtLocked(inst, &ext_locked) == PRO_TK_NO_ERROR) {
        row.ext_locked = ext_locked == PRO_B_TRUE;
    }
    ProFaminstanceVerifyStatus verify = PRO_INST_NOT_VERIFIED;
    if (ProFaminstanceIsVerified(inst, &verify) == PRO_TK_NO_ERROR) {
        row.verify_status = static_cast<int>(verify);
    }

    for (const ModelStructureFamilyColumn &col : ctx->table->columns) {
        ModelStructureFamilyCell cell;
        cell.column_key = col.key;
        ProFamtableItem item = {};
        item.type = static_cast<ProFamtabType>(col.type);
        wcsncpy_s(item.string, col.key.c_str(), _TRUNCATE);
        item.owner = inst->famtab.owner;
        ProParamvalue value = {};
        cell.status = ProFaminstanceValueGet(inst, &item, &value);
        if (cell.status == PRO_TK_NO_ERROR &&
            ProParamvalueTypeGet(&value, &cell.type) == PRO_TK_NO_ERROR) {
            cell.value = ParamValueToText(value, cell.type);
        } else {
            cell.value = StatusText(cell.status);
        }
        row.cells.push_back(cell);
    }
    ctx->table->rows.push_back(row);
    return PRO_TK_NO_ERROR;
}

struct RelsetCtx {
    ProRelset relset = {};
    bool found = false;
};

ProError RelsetVisitAction(ProRelset *relset, ProAppData app_data)
{
    if (relset == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }
    ProModelitem item = {};
    if (ProRelsetToModelitem(relset, &item) == PRO_TK_NO_ERROR &&
        item.id != PRO_RELSET_POST_REGEN_ID) {
        auto *ctx = reinterpret_cast<RelsetCtx *>(app_data);
        ctx->relset = *relset;
        ctx->found = true;
        return PRO_TK_E_FOUND;
    }
    return PRO_TK_NO_ERROR;
}

bool FindMainRelset(ProMdl mdl, ProRelset &relset, ProError &visit_status)
{
    relset = {};
    RelsetCtx ctx;
    visit_status = ProSolidRelsetVisit(mdl, RelsetVisitAction, &ctx);
    if (ctx.found) {
        relset = ctx.relset;
        return true;
    }
    ProModelitem owner = MdlAsModelitem(mdl);
    return ProModelitemToRelset(&owner, &relset) == PRO_TK_NO_ERROR;
}

bool IsCommentLine(const std::wstring &trimmed)
{
    return trimmed.rfind(L"/*", 0) == 0 || trimmed.rfind(L"//", 0) == 0 || trimmed.rfind(L"!", 0) == 0;
}

bool IsConditionalLine(const std::wstring &upper)
{
    return upper.rfind(L"IF", 0) == 0 || upper.rfind(L"ELSE", 0) == 0 || upper.rfind(L"ENDIF", 0) == 0;
}

void ReadRelations(int node_index, ProMdl mdl, ModelStructureReport &report)
{
    ModelStructureRelationSet set;
    set.node_index = node_index;
    ProRelset relset = {};
    if (!FindMainRelset(mdl, relset, set.visit_status)) {
        report.relation_sets.push_back(set);
        return;
    }
    set.has_main_relset = true;
    ProWstring *raw_lines = nullptr;
    set.read_status = ProArrayAlloc(0, sizeof(ProWstring), 1, reinterpret_cast<ProArray *>(&raw_lines));
    if (set.read_status == PRO_TK_NO_ERROR) {
        set.read_status = ProRelsetRelationsGet(&relset, &raw_lines);
    }
    if (set.read_status == PRO_TK_NO_ERROR) {
        int count = 0;
        if (ProArraySizeGet(reinterpret_cast<ProArray>(raw_lines), &count) == PRO_TK_NO_ERROR) {
            for (int i = 0; i < count; ++i) {
                ModelStructureRelationLine line;
                line.line_number = i + 1;
                line.text = raw_lines[i] == nullptr ? L"" : raw_lines[i];
                const std::wstring trimmed = Trim(line.text);
                const std::wstring upper = Upper(trimmed);
                line.blank = trimmed.empty();
                line.comment = IsCommentLine(trimmed);
                line.conditional = IsConditionalLine(upper);
                set.lines.push_back(line);
            }
        }
    }
    if (raw_lines != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&raw_lines));
    }
    report.relation_sets.push_back(set);
}

void ReadParameters(ModelStructureNode &node, ModelStructureReport &report)
{
    ProModelitem owner = MdlAsModelitem(node.mdl);
    ParamCtx ctx;
    ctx.node_index = node.index;
    ctx.out = &report.parameters;
    const size_t before = report.parameters.size();
    ProParameterVisit(&owner, nullptr, ParameterVisitAction, &ctx);
    node.parameter_count = static_cast<int>(report.parameters.size() - before);
}

void ReadFeaturesAndDimensions(ModelStructureNode &node, ModelStructureReport &report)
{
    if (node.mdl == nullptr || !autobbox::creo::IsPartOrAsm(node.mdl)) {
        return;
    }
    FeatureCtx ctx;
    ctx.node_index = node.index;
    ctx.features = &report.features;
    ctx.dimensions = &report.dimensions;
    ctx.parameters = &report.parameters;
    const size_t feature_before = report.features.size();
    const size_t dim_before = report.dimensions.size();
    const size_t param_before = report.parameters.size();
    ProSolidFeatVisit(reinterpret_cast<ProSolid>(node.mdl), FeatureVisitAction, nullptr, &ctx);
    node.feature_count = static_cast<int>(report.features.size() - feature_before);
    node.dimension_count = static_cast<int>(report.dimensions.size() - dim_before);
    node.parameter_count += static_cast<int>(report.parameters.size() - param_before);
    for (size_t i = dim_before; i < report.dimensions.size(); ++i) {
        report.dimensions[i].unit = node.length_unit;
    }
}

void MarkFamilyTableDimensions(ModelStructureNode &node, ModelStructureReport &report)
{
    const ModelStructureFamilyTable *table = nullptr;
    for (const auto &candidate : report.family_tables) {
        if (candidate.node_index == node.index) {
            table = &candidate;
            break;
        }
    }
    if (table == nullptr || !table->has_family_table) {
        return;
    }
    std::set<std::wstring> dimension_keys;
    for (const auto &col : table->columns) {
        if (col.type == PRO_FAM_DIMENSION) {
            dimension_keys.insert(Upper(col.key));
        }
    }
    if (dimension_keys.empty()) {
        return;
    }
    for (auto &dim : report.dimensions) {
        if (dim.node_index == node.index && dimension_keys.find(Upper(dim.symbol)) != dimension_keys.end()) {
            dim.family_table_column = true;
        }
    }
}

void ReadFamilyTableForModel(ModelStructureNode &node,
                             ProMdl table_mdl,
                             bool source_is_selected_model,
                             bool source_is_generic,
                             int generation,
                             const std::wstring &selected_instance_name,
                             const std::wstring &immediate_generic_name,
                             const std::wstring &top_generic_name,
                             ProError immediate_generic_status,
                             ProError top_generic_status,
                             ModelStructureReport &report)
{
    ModelStructureFamilyTable table;
    table.node_index = node.index;
    table.source_is_selected_model = source_is_selected_model;
    table.source_is_generic = source_is_generic;
    table.generation = generation;
    table.table_model_name = autobbox::creo::ModelName(table_mdl, L"");
    table.table_model_type_label = ModelStructureModelTypeLabel(autobbox::creo::ModelType(table_mdl));
    table.selected_instance_name = selected_instance_name;
    table.immediate_generic_name = immediate_generic_name;
    table.top_generic_name = top_generic_name;
    table.immediate_generic_status = immediate_generic_status;
    table.top_generic_status = top_generic_status;
    table.init_status = ProFamtableInit(table_mdl, &table.famtable);
    if (table.init_status == PRO_TK_NO_ERROR) {
        table.check_status = ProFamtableCheck(&table.famtable);
        table.has_family_table = table.check_status == PRO_TK_NO_ERROR || table.check_status == PRO_TK_EMPTY;
    }
    if (table.has_family_table) {
        FamilyCtx ctx;
        ctx.table = &table;
        table.item_visit_status = ProFamtableItemVisit(&table.famtable, FamItemAction, nullptr, &ctx);
        table.instance_visit_status = ProFamtableInstanceVisit(&table.famtable, FamInstanceAction, nullptr, &ctx);
    }
    report.family_tables.push_back(std::move(table));
}

void ReadFamilyTableLegacy(ModelStructureNode &node, ModelStructureReport &report)
{
    const size_t before = report.family_tables.size();

    ProMdl top_generic = nullptr;
    const ProError top_st = ProFaminstanceGenericGet(node.mdl, PRO_B_FALSE, &top_generic);
    const std::wstring top_name = top_st == PRO_TK_NO_ERROR ? autobbox::creo::ModelName(top_generic, L"") : L"";

    ReadFamilyTableForModel(node,
                            node.mdl,
                            true,
                            false,
                            0,
                            node.family_instance ? node.model_name : L"",
                            L"",
                            top_name,
                            PRO_TK_GENERAL_ERROR,
                            top_st,
                            report);

    if (node.family_instance) {
        ProMdl cursor = node.mdl;
        std::set<std::wstring> visited_generics;
        for (int generation = 1; generation <= 8; ++generation) {
            ProMdl generic = nullptr;
            ProName immediate_name_buf = {0};
            ProMdlType immediate_type = PRO_MDL_UNUSED;
            const ProError info_st = ProFaminstanceImmediategenericinfoGet(cursor, immediate_name_buf, &immediate_type);
            const std::wstring immediate_name_from_info = info_st == PRO_TK_NO_ERROR ? std::wstring(immediate_name_buf) : L"";
            const ProError generic_st = ProFaminstanceGenericGet(cursor, PRO_B_TRUE, &generic);
            if (generic_st != PRO_TK_NO_ERROR || generic == nullptr) {
                ModelStructureFamilyTable placeholder;
                placeholder.node_index = node.index;
                placeholder.source_is_selected_model = false;
                placeholder.source_is_generic = true;
                placeholder.generation = generation;
                placeholder.selected_instance_name = node.model_name;
                placeholder.immediate_generic_name = immediate_name_from_info;
                placeholder.top_generic_name = top_name;
                placeholder.immediate_generic_status = generic_st;
                placeholder.top_generic_status = top_st;
                placeholder.init_status = PRO_TK_GENERAL_ERROR;
                placeholder.check_status = PRO_TK_GENERAL_ERROR;
                report.family_tables.push_back(std::move(placeholder));
                break;
            }
            const std::wstring generic_name = autobbox::creo::ModelName(generic, L"");
            const std::wstring generic_key = Upper(generic_name) + L":" + std::to_wstring(static_cast<int>(autobbox::creo::ModelType(generic)));
            if (!visited_generics.insert(generic_key).second) {
                break;
            }
            ReadFamilyTableForModel(node,
                                    generic,
                                    false,
                                    true,
                                    generation,
                                    node.model_name,
                                    generic_name.empty() ? immediate_name_from_info : generic_name,
                                    top_name,
                                    generic_st,
                                    top_st,
                                    report);
            if (!IsFamilyInstanceModel(generic)) {
                break;
            }
            cursor = generic;
        }
    }

    node.has_family_table = false;
    node.family_column_count = 0;
    node.family_row_count = 0;
    for (size_t i = before; i < report.family_tables.size(); ++i) {
        const auto &table = report.family_tables[i];
        if (table.has_family_table) {
            node.has_family_table = true;
        }
        node.family_column_count += static_cast<int>(table.columns.size());
        node.family_row_count += static_cast<int>(table.rows.size());
    }
}

void AppendWorkspaceFamilyTables(ModelStructureNode &node,
                                 const core::FtWorkspace &workspace,
                                 ModelStructureReport &report)
{
    const size_t before = report.family_tables.size();
    for (const auto &level : workspace.level_nodes) {
        ModelStructureFamilyTable table;
        table.node_index = node.index;
        table.source_is_selected_model = level.level_depth == 0;
        table.source_is_generic = level.level_depth > 0 || level.generic_name != node.model_name;
        table.generation = level.level_depth;
        table.table_model_name = level.generic_name;
        table.table_model_type_label = ModelStructureModelTypeLabel(level.model_type);
        table.selected_instance_name = node.family_instance ? node.model_name : L"";
        table.immediate_generic_name = level.parent_generic_name;
        table.top_generic_name = workspace.level_nodes.empty() ? L"" : workspace.level_nodes.front().generic_name;
        table.level_path = level.level_path;
        table.parent_generic_name = level.parent_generic_name;
        table.parent_instance_name = level.parent_instance_name;
        table.level_depth = level.level_depth;
        table.has_family_table = level.has_family_table;
        table.init_status = level.generic_mdl == nullptr ? PRO_TK_GENERAL_ERROR : PRO_TK_NO_ERROR;
        table.check_status = level.has_family_table ? PRO_TK_NO_ERROR : PRO_TK_E_NOT_FOUND;
        table.item_visit_status = PRO_TK_NO_ERROR;
        table.instance_visit_status = PRO_TK_NO_ERROR;

        for (const auto &src_col : level.columns) {
            if (!src_col.visible) {
                continue;
            }
            ModelStructureFamilyColumn col;
            col.key = src_col.column_key.empty() ? src_col.column_display_name : src_col.column_key;
            col.type = static_cast<int>(src_col.famtab_type);
            col.type_label = src_col.column_display_name.empty() ? core::FtColumnCategoryName(src_col.column_category)
                                                                 : src_col.column_display_name;
            table.columns.push_back(std::move(col));
        }

        for (const auto &src_row : level.rows) {
            ModelStructureFamilyRow row;
            row.instance_name = src_row.instance_name;
            row.row_kind = src_row.row_kind == core::FtRowKind::Generic ? L"GENERIC" : L"INSTANCE";
            row.common_name = src_row.common_name;
            row.verify_status_text = src_row.verify_status;
            row.locked = src_row.is_locked;
            row.ext_locked = src_row.is_ext_locked;
            row.verify_status = 0;
            for (const auto &src_cell : src_row.cells) {
                const auto col_it = std::find_if(level.columns.begin(),
                                                 level.columns.end(),
                                                 [&](const core::FtColumn &c) {
                                                     return c.column_key == src_cell.column_key && c.visible;
                                                 });
                if (col_it == level.columns.end()) {
                    continue;
                }
                ModelStructureFamilyCell cell;
                cell.column_key = src_cell.column_key;
                cell.type = src_cell.value_type;
                cell.value = src_cell.value;
                cell.status = PRO_TK_NO_ERROR;
                cell.editable = src_cell.editable;
                row.cells.push_back(std::move(cell));
            }
            table.rows.push_back(std::move(row));
        }
        report.family_tables.push_back(std::move(table));
    }

    node.has_family_table = false;
    node.family_column_count = 0;
    node.family_row_count = 0;
    for (size_t i = before; i < report.family_tables.size(); ++i) {
        const auto &table = report.family_tables[i];
        if (table.has_family_table) {
            node.has_family_table = true;
        }
        node.family_column_count += static_cast<int>(table.columns.size());
        node.family_row_count += static_cast<int>(table.rows.size());
    }
}

void ReadFamilyTable(ModelStructureNode &node, ModelStructureReport &report)
{
    core::FtWorkspace workspace;
    const ProError discover_st = DiscoverFamilyTableWorkspaceDeep(node.mdl, workspace);
    if (discover_st == PRO_TK_NO_ERROR && !workspace.level_nodes.empty()) {
        ReadFamilyTableWorkspace(workspace, true);
        AppendWorkspaceFamilyTables(node, workspace, report);
        return;
    }
    ReadFamilyTableLegacy(node, report);
}

ProError InitAsmcompForPath(const ProAsmcomppath &component_path,
                            ProAsmcomp *comp,
                            ProAsmcomppath *owner_path)
{
    if (comp == nullptr || owner_path == nullptr || component_path.table_num <= 0) {
        return PRO_TK_BAD_INPUTS;
    }
    *owner_path = component_path;
    owner_path->table_num = std::max(0, component_path.table_num - 1);
    ProMdl owner_mdl = reinterpret_cast<ProMdl>(component_path.owner);
    if (owner_path->table_num > 0) {
        ProAsmcomppath path_copy = *owner_path;
        ProError mdl_st = ProAsmcomppathMdlGet(&path_copy, &owner_mdl);
        if (mdl_st != PRO_TK_NO_ERROR) {
            return mdl_st;
        }
    }
    if (owner_mdl == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }
    const int feat_id = component_path.comp_id_table[component_path.table_num - 1];
    ProFeature feat = {};
    const ProError st = ProFeatureInit(reinterpret_cast<ProSolid>(owner_mdl), feat_id, &feat);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }
    std::memcpy(comp, &feat, sizeof(ProAsmcomp));
    return PRO_TK_NO_ERROR;
}

ProError InitAsmcompForNode(const ModelStructureReport &,
                            const ModelStructureNode &node,
                            ProAsmcomp *comp,
                            ProAsmcomppath *owner_path)
{
    if (!node.has_component_path) {
        return PRO_TK_BAD_INPUTS;
    }
    return InitAsmcompForPath(node.component_path, comp, owner_path);
}

void ReadComponentStateAndConstraints(ModelStructureNode &node, ModelStructureReport &report)
{
    if (!node.has_component_path) {
        return;
    }
    ProMatrix trf = {{0}};
    node.transform_status = ProAsmcomppathTrfGet(&node.component_path, PRO_B_TRUE, trf);
    if (node.transform_status == PRO_TK_NO_ERROR) {
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                node.transform[r][c] = trf[r][c];
            }
        }
    }

    ProAsmcomp comp = {};
    ProAsmcomppath owner_path = {};
    node.component_init_status = InitAsmcompForNode(report, node, &comp, &owner_path);
    if (node.component_init_status != PRO_TK_NO_ERROR) {
        return;
    }
    ProBoolean flag = PRO_B_FALSE;
    node.packaged_status = ProAsmcompIsPackaged(&comp, &flag);
    node.packaged = node.packaged_status == PRO_TK_NO_ERROR && flag == PRO_B_TRUE;
    flag = PRO_B_FALSE;
    node.underconstrained_status = ProAsmcompIsUnderconstrained(&comp, &flag);
    node.underconstrained = node.underconstrained_status == PRO_TK_NO_ERROR && flag == PRO_B_TRUE;
    flag = PRO_B_FALSE;
    node.frozen_status = ProAsmcompIsFrozen(&comp, &flag);
    node.frozen = node.frozen_status == PRO_TK_NO_ERROR && flag == PRO_B_TRUE;

    ProFeature *comp_feature = reinterpret_cast<ProFeature *>(&comp);
    ProFeatStatus feat_status = PRO_FEAT_INVALID;
    if (ProFeatureStatusGet(comp_feature, &feat_status) == PRO_TK_NO_ERROR) {
        node.suppressed = feat_status == PRO_FEAT_SUPPRESSED ||
                          feat_status == PRO_FEAT_FAMTAB_SUPPRESSED ||
                          feat_status == PRO_FEAT_SIMP_REP_SUPPRESSED ||
                          feat_status == PRO_FEAT_PROG_SUPPRESSED;
    }
    ProBoolean visible = PRO_B_TRUE;
    if (ProFeatureVisibilityGet(comp_feature, &visible) == PRO_TK_NO_ERROR) {
        node.hidden = visible != PRO_B_TRUE;
    }

    ProAsmcompconstraint *constraints = nullptr;
    const ProError constraints_st = ProAsmcompConstraintsWithComppathGet(&comp, &owner_path, &constraints);
    if (constraints_st != PRO_TK_NO_ERROR || constraints == nullptr) {
        ModelStructureConstraint detail;
        detail.node_index = node.index;
        detail.read_status = constraints_st;
        report.constraints.push_back(detail);
        return;
    }
    int count = 0;
    if (ProArraySizeGet(reinterpret_cast<ProArray>(constraints), &count) != PRO_TK_NO_ERROR) {
        count = 0;
    }
    for (int i = 0; i < count; ++i) {
        ModelStructureConstraint detail;
        detail.node_index = node.index;
        detail.index = i + 1;
        detail.read_status = PRO_TK_NO_ERROR;
        ProAsmcompConstrType type = PRO_ASM_UNDEF;
        if (ProAsmcompconstraintTypeGet(constraints[i], &type) == PRO_TK_NO_ERROR) {
            detail.type = static_cast<int>(type);
            detail.type_label = ConstraintTypeLabel(detail.type);
        }
        ProSelection asm_ref = nullptr;
        ProSelection comp_ref = nullptr;
        ProDatumside asm_orient = PRO_DATUM_SIDE_NONE;
        ProDatumside comp_orient = PRO_DATUM_SIDE_NONE;
        detail.asm_ref_status = ProAsmcompconstraintAsmreferenceGet(constraints[i], &asm_ref, &asm_orient);
        detail.comp_ref_status = ProAsmcompconstraintCompreferenceGet(constraints[i], &comp_ref, &comp_orient);
        detail.asm_orientation = static_cast<int>(asm_orient);
        detail.comp_orientation = static_cast<int>(comp_orient);
        detail.asm_reference = SelectionText(asm_ref);
        detail.comp_reference = SelectionText(comp_ref);
        if (asm_ref != nullptr) {
            report.references.push_back(BuildReferenceFromSelection(node.index, L"assembly_constraint", asm_ref));
        }
        if (comp_ref != nullptr) {
            report.references.push_back(BuildReferenceFromSelection(node.index, L"component_constraint", comp_ref));
        }
        if (asm_ref != nullptr) ProSelectionFree(&asm_ref);
        if (comp_ref != nullptr) ProSelectionFree(&comp_ref);
        detail.offset_status = ProAsmcompconstraintOffsetGet(constraints[i], &detail.offset);
        detail.attributes_status = ProAsmcompconstraintAttributesGet(constraints[i], &detail.attributes);
        report.constraints.push_back(std::move(detail));
    }
    node.constraint_count = count;
    ProAsmcompconstraintArrayFree(constraints);
}

void ReadSelectedOccurrenceConstraints(const ProAsmcomppath &path,
                                       ModelStructureReport &report)
{
    if (path.table_num <= 0) {
        return;
    }
    ProAsmcomp comp = {};
    ProAsmcomppath owner_path = {};
    const ProError init_st = InitAsmcompForPath(path, &comp, &owner_path);
    if (init_st != PRO_TK_NO_ERROR) {
        ModelStructureConstraint detail;
        detail.node_index = -1;
        detail.read_status = init_st;
        report.selected_occurrence_constraints.push_back(detail);
        return;
    }

    ProAsmcompconstraint *constraints = nullptr;
    const ProError constraints_st = ProAsmcompConstraintsWithComppathGet(&comp, &owner_path, &constraints);
    if (constraints_st != PRO_TK_NO_ERROR || constraints == nullptr) {
        ModelStructureConstraint detail;
        detail.node_index = -1;
        detail.read_status = constraints_st;
        report.selected_occurrence_constraints.push_back(detail);
        return;
    }

    int count = 0;
    if (ProArraySizeGet(reinterpret_cast<ProArray>(constraints), &count) != PRO_TK_NO_ERROR) {
        count = 0;
    }
    for (int i = 0; i < count; ++i) {
        ModelStructureConstraint detail;
        detail.node_index = -1;
        detail.index = i + 1;
        detail.read_status = PRO_TK_NO_ERROR;
        ProAsmcompConstrType type = PRO_ASM_UNDEF;
        if (ProAsmcompconstraintTypeGet(constraints[i], &type) == PRO_TK_NO_ERROR) {
            detail.type = static_cast<int>(type);
            detail.type_label = ConstraintTypeLabel(detail.type);
        }
        ProSelection asm_ref = nullptr;
        ProSelection comp_ref = nullptr;
        ProDatumside asm_orient = PRO_DATUM_SIDE_NONE;
        ProDatumside comp_orient = PRO_DATUM_SIDE_NONE;
        detail.asm_ref_status = ProAsmcompconstraintAsmreferenceGet(constraints[i], &asm_ref, &asm_orient);
        detail.comp_ref_status = ProAsmcompconstraintCompreferenceGet(constraints[i], &comp_ref, &comp_orient);
        detail.asm_orientation = static_cast<int>(asm_orient);
        detail.comp_orientation = static_cast<int>(comp_orient);
        detail.asm_reference = SelectionText(asm_ref);
        detail.comp_reference = SelectionText(comp_ref);
        if (asm_ref != nullptr) {
            report.references.push_back(BuildReferenceFromSelection(-1, L"selected_occurrence_assembly_constraint", asm_ref));
        }
        if (comp_ref != nullptr) {
            report.references.push_back(BuildReferenceFromSelection(-1, L"selected_occurrence_component_constraint", comp_ref));
        }
        if (asm_ref != nullptr) ProSelectionFree(&asm_ref);
        if (comp_ref != nullptr) ProSelectionFree(&comp_ref);
        detail.offset_status = ProAsmcompconstraintOffsetGet(constraints[i], &detail.offset);
        detail.attributes_status = ProAsmcompconstraintAttributesGet(constraints[i], &detail.attributes);
        report.selected_occurrence_constraints.push_back(std::move(detail));
    }
    ProAsmcompconstraintArrayFree(constraints);
}

void FillSelectedOccurrence(ProMdl selected_model,
                            const ProAsmcomppath *selected_path,
                            ModelStructureReport &report)
{
    if (selected_path == nullptr || selected_path->table_num <= 0) {
        return;
    }
    auto &occ = report.selected_occurrence;
    occ.has_selection_path = true;
    occ.selected_model_name = autobbox::creo::ModelName(selected_model, L"");
    occ.selected_model_type_label = ModelStructureModelTypeLabel(autobbox::creo::ModelType(selected_model));
    occ.selected_display_name = ModelDisplayName(selected_model);
    occ.selected_model_path = ModelOriginPath(selected_model);
    occ.context_root_model = autobbox::creo::ModelName(reinterpret_cast<ProMdl>(selected_path->owner), L"");
    occ.context_root_type_label = ModelStructureModelTypeLabel(autobbox::creo::ModelType(reinterpret_cast<ProMdl>(selected_path->owner)));
    occ.occurrence_path = PathText(*selected_path);
    occ.component_feature_id = selected_path->comp_id_table[selected_path->table_num - 1];
    occ.level = selected_path->table_num;
    occ.parent_occurrence_path = PathKey(*selected_path, selected_path->table_num - 1);

    ProAsmcomppath parent_path = *selected_path;
    parent_path.table_num = std::max(0, selected_path->table_num - 1);
    ProMdl parent_mdl = reinterpret_cast<ProMdl>(selected_path->owner);
    if (parent_path.table_num > 0) {
        ProAsmcomppath copy = parent_path;
        ProAsmcomppathMdlGet(&copy, &parent_mdl);
    }
    occ.parent_assembly = autobbox::creo::ModelName(parent_mdl, L"");

    ProMatrix trf = {{0}};
    occ.transform_status = ProAsmcomppathTrfGet(const_cast<ProAsmcomppath *>(selected_path), PRO_B_TRUE, trf);
    if (occ.transform_status == PRO_TK_NO_ERROR) {
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                occ.transform[r][c] = trf[r][c];
            }
        }
    }

    ProAsmcomp comp = {};
    ProAsmcomppath owner_path = {};
    if (InitAsmcompForPath(*selected_path, &comp, &owner_path) == PRO_TK_NO_ERROR) {
        ProBoolean flag = PRO_B_FALSE;
        if (ProAsmcompIsPackaged(&comp, &flag) == PRO_TK_NO_ERROR) occ.packaged = flag == PRO_B_TRUE;
        flag = PRO_B_FALSE;
        if (ProAsmcompIsUnderconstrained(&comp, &flag) == PRO_TK_NO_ERROR) occ.underconstrained = flag == PRO_B_TRUE;
        flag = PRO_B_FALSE;
        if (ProAsmcompIsFrozen(&comp, &flag) == PRO_TK_NO_ERROR) occ.frozen = flag == PRO_B_TRUE;
        ProFeature *comp_feature = reinterpret_cast<ProFeature *>(&comp);
        ProFeatStatus feat_status = PRO_FEAT_INVALID;
        if (ProFeatureStatusGet(comp_feature, &feat_status) == PRO_TK_NO_ERROR) {
            occ.suppressed = feat_status == PRO_FEAT_SUPPRESSED ||
                             feat_status == PRO_FEAT_FAMTAB_SUPPRESSED ||
                             feat_status == PRO_FEAT_SIMP_REP_SUPPRESSED ||
                             feat_status == PRO_FEAT_PROG_SUPPRESSED;
        }
        ProBoolean visible = PRO_B_TRUE;
        if (ProFeatureVisibilityGet(comp_feature, &visible) == PRO_TK_NO_ERROR) {
            occ.hidden = visible != PRO_B_TRUE;
        }
    }

    ReadSelectedOccurrenceConstraints(*selected_path, report);
}

struct TraverseCtx {
    ModelStructureReport *report = nullptr;
    std::map<std::wstring, int> index_by_path;
};

ProError ComponentVisitAction(ProAsmcomppath *path, ProSolid handle, ProBoolean down, ProAppData app_data)
{
    if (down != PRO_B_TRUE || path == nullptr || handle == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }
    auto *ctx = reinterpret_cast<TraverseCtx *>(app_data);
    if (ctx->report == nullptr || path->table_num <= 0) {
        return PRO_TK_NO_ERROR;
    }
    ModelStructureNode node;
    node.index = static_cast<int>(ctx->report->nodes.size());
    node.depth = path->table_num;
    node.mdl = reinterpret_cast<ProMdl>(handle);
    node.model_type = autobbox::creo::ModelType(node.mdl);
    if (!autobbox::creo::IsPartOrAsm(node.mdl)) {
        return PRO_TK_NO_ERROR;
    }
    node.model_name = autobbox::creo::ModelName(node.mdl, L"<unknown>");
    node.display_name = ModelDisplayName(node.mdl);
    node.custom_name = ModelCustomName(node.mdl);
    node.model_path = ModelOriginPath(node.mdl);
    node.family_instance = IsFamilyInstanceModel(node.mdl);
    node.model_type_label = ModelStructureModelTypeLabel(node.model_type);
    node.component_path = *path;
    node.has_component_path = true;
    node.component_feature_id = path->comp_id_table[path->table_num - 1];
    node.occurrence_path = PathText(*path);

    const std::wstring parent_key = PathKey(*path, path->table_num - 1);
    const auto parent = ctx->index_by_path.find(parent_key);
    node.parent_index = parent == ctx->index_by_path.end() ? 0 : parent->second;
    if (node.parent_index >= 0 && node.parent_index < static_cast<int>(ctx->report->nodes.size())) {
        node.parent_assembly = ctx->report->nodes[static_cast<size_t>(node.parent_index)].model_name;
    }

    ctx->index_by_path[PathText(*path)] = node.index;
    ctx->report->nodes.push_back(node);
    return PRO_TK_NO_ERROR;
}

} // namespace

const wchar_t *ModelStructureModelTypeLabel(ProMdlType type)
{
    switch (type) {
    case PRO_MDL_PART: return L"PRT";
    case PRO_MDL_ASSEMBLY: return L"ASM";
    case PRO_MDL_DRAWING: return L"DRW";
    default: return L"MDL";
    }
}

ModelStructureReport CollectModelStructureAnalysis(ProMdl current,
                                                   const ModelStructureLogSink &log_sink)
{
    return CollectModelStructureAnalysis(current, nullptr, log_sink);
}

ModelStructureReport CollectModelStructureAnalysis(ProMdl current,
                                                   const ProAsmcomppath *selected_path,
                                                   const ModelStructureLogSink &log_sink)
{
    return CollectModelStructureAnalysis(current, current, selected_path, log_sink);
}

ModelStructureReport CollectModelStructureAnalysis(ProMdl current,
                                                   ProMdl selected_model,
                                                   const ProAsmcomppath *selected_path,
                                                   const ModelStructureLogSink &log_sink)
{
    ModelStructureReport report;
    report.root = current;
    if (current == nullptr || !autobbox::creo::IsPartOrAsm(current)) {
        return report;
    }
    report.root_name = autobbox::creo::ModelName(current, L"<unknown>");

    ModelStructureNode root;
    root.index = 0;
    root.parent_index = -1;
    root.depth = 0;
    root.mdl = current;
    root.model_type = autobbox::creo::ModelType(current);
    root.model_name = report.root_name;
    root.display_name = ModelDisplayName(current);
    root.custom_name = ModelCustomName(current);
    root.model_path = ModelOriginPath(current);
    root.family_instance = IsFamilyInstanceModel(current);
    root.model_type_label = ModelStructureModelTypeLabel(root.model_type);
    root.occurrence_path = L"ROOT";
    report.nodes.push_back(root);
    FillSelectedOccurrence(selected_model == nullptr ? current : selected_model, selected_path, report);

    if (root.model_type == PRO_MDL_ASSEMBLY) {
        TraverseCtx ctx;
        ctx.report = &report;
        ctx.index_by_path[L"ROOT"] = 0;
        const ProError visit_st = ProSolidDispCompVisit(
            reinterpret_cast<ProSolid>(current),
            ComponentVisitAction,
            nullptr,
            &ctx);
        LogLine(log_sink, "model-structure assembly-visit status=%d nodes=%d",
                static_cast<int>(visit_st),
                static_cast<int>(report.nodes.size()));
    }

    std::map<std::wstring, int> occurrence_count_by_name;
    for (const ModelStructureNode &node : report.nodes) {
        occurrence_count_by_name[node.model_name]++;
    }

    for (ModelStructureNode &node : report.nodes) {
        node.duplicate_assembly = occurrence_count_by_name[node.model_name] > 1;
        ReadUnits(node);
        ReadComponentStateAndConstraints(node, report);
        ReadParameters(node, report);
        ReadFamilyTable(node, report);
        ReadFeaturesAndDimensions(node, report);
        MarkFamilyTableDimensions(node, report);
        ReadRelations(node.index, node.mdl, report);
        if (!report.relation_sets.empty() && report.relation_sets.back().node_index == node.index) {
            node.relation_line_count = static_cast<int>(report.relation_sets.back().lines.size());
        }
    }

    LogLine(log_sink,
            "model-structure summary nodes=%d constraints=%d params=%d family_tables=%d relation_sets=%d",
            static_cast<int>(report.nodes.size()),
            static_cast<int>(report.constraints.size()),
            static_cast<int>(report.parameters.size()),
            static_cast<int>(report.family_tables.size()),
            static_cast<int>(report.relation_sets.size()));
    return report;
}

std::string BuildModelStructureJson(const ModelStructureReport &report)
{
    const ModelStructureNode *root = report.nodes.empty() ? nullptr : &report.nodes.front();
    std::ostringstream os;
    os << "{\n";
    os << "  \"schema\": \"autobbox.semantic_model_structure.v1\",\n";
    os << "  \"package_name\": " << JsonStr(report.root_name + L"_semantic_model_structure") << ",\n";
    os << "  \"root_model\": " << JsonStr(report.root_name) << ",\n";
    os << "  \"root\": {\n";
    os << "    \"model_name\": " << JsonStr(root == nullptr ? report.root_name : root->model_name) << ",\n";
    os << "    \"model_type\": " << JsonStr(root == nullptr ? L"" : root->model_type_label) << ",\n";
    os << "    \"unit_system\": " << JsonStr(root == nullptr ? L"" : root->unit_system) << ",\n";
    os << "    \"length_unit\": " << JsonStr(root == nullptr ? L"" : root->length_unit) << ",\n";
    os << "    \"mass_unit\": " << JsonStr(root == nullptr ? L"" : root->mass_unit) << "\n";
    os << "  },\n";
    os << "  \"summary\": {\n";
    os << "    \"nodes\": " << report.nodes.size() << ",\n";
    os << "    \"constraints\": " << report.constraints.size() << ",\n";
    os << "    \"selected_occurrence_constraints\": " << report.selected_occurrence_constraints.size() << ",\n";
    os << "    \"parameters\": " << report.parameters.size() << ",\n";
    os << "    \"references\": " << report.references.size() << ",\n";
    os << "    \"features\": " << report.features.size() << ",\n";
    os << "    \"dimensions\": " << report.dimensions.size() << "\n";
    os << "  },\n";

    const auto &occ = report.selected_occurrence;
    os << "  \"selected_occurrence\": {\n";
    os << "    \"has_selection_path\": " << JsonBool(occ.has_selection_path) << ",\n";
    os << "    \"selected_model_name\": " << JsonStr(occ.selected_model_name) << ",\n";
    os << "    \"selected_model_type\": " << JsonStr(occ.selected_model_type_label) << ",\n";
    os << "    \"selected_display_name\": " << JsonStr(occ.selected_display_name) << ",\n";
    os << "    \"selected_model_path\": " << JsonStr(occ.selected_model_path) << ",\n";
    os << "    \"context_root_model\": " << JsonStr(occ.context_root_model) << ",\n";
    os << "    \"context_root_type\": " << JsonStr(occ.context_root_type_label) << ",\n";
    os << "    \"parent_assembly\": " << JsonStr(occ.parent_assembly) << ",\n";
    os << "    \"occurrence_path\": " << JsonStr(occ.occurrence_path) << ",\n";
    os << "    \"parent_occurrence_path\": " << JsonStr(occ.parent_occurrence_path) << ",\n";
    os << "    \"component_feature_id\": " << occ.component_feature_id << ",\n";
    os << "    \"level\": " << occ.level << ",\n";
    os << "    \"packaged\": " << JsonBool(occ.packaged) << ",\n";
    os << "    \"underconstrained\": " << JsonBool(occ.underconstrained) << ",\n";
    os << "    \"frozen\": " << JsonBool(occ.frozen) << ",\n";
    os << "    \"suppressed\": " << JsonBool(occ.suppressed) << ",\n";
    os << "    \"hidden\": " << JsonBool(occ.hidden) << ",\n";
    os << "    \"transform_status\": " << static_cast<int>(occ.transform_status) << ",\n";
    os << "    \"transform\": [";
    for (int r = 0; r < 4; ++r) {
        os << (r == 0 ? "" : ", ") << "[";
        for (int c = 0; c < 4; ++c) {
            os << (c == 0 ? "" : ", ") << JsonNum(occ.transform[r][c]);
        }
        os << "]";
    }
    os << "],\n";
    os << "    \"constraints\": [\n";
    for (size_t i = 0; i < report.selected_occurrence_constraints.size(); ++i) {
        const auto &c = report.selected_occurrence_constraints[i];
        os << "      {\"constraint_index\": " << c.index
           << ", \"constraint_type\": " << JsonStr(c.type_label)
           << ", \"constraint_reference_1\": " << JsonStr(c.asm_reference)
           << ", \"constraint_reference_2\": " << JsonStr(c.comp_reference)
           << ", \"offset\": " << JsonNum(c.offset)
           << ", \"attributes\": " << c.attributes
           << ", \"read_status\": " << static_cast<int>(c.read_status) << "}"
           << (i + 1 < report.selected_occurrence_constraints.size() ? "," : "") << "\n";
    }
    os << "    ]\n";
    os << "  },\n";

    os << "  \"model_structure\": [\n";
    for (size_t i = 0; i < report.nodes.size(); ++i) {
        const auto &n = report.nodes[i];
        os << "    {\n";
        os << "      \"index\": " << n.index << ",\n";
        os << "      \"model_name\": " << JsonStr(n.model_name) << ",\n";
        os << "      \"model_type\": " << JsonStr(n.model_type_label) << ",\n";
        os << "      \"unit_system\": " << JsonStr(n.unit_system) << ",\n";
        os << "      \"length_unit\": " << JsonStr(n.length_unit) << ",\n";
        os << "      \"mass_unit\": " << JsonStr(n.mass_unit) << ",\n";
        os << "      \"component_model_name\": " << JsonStr(n.model_name) << ",\n";
        os << "      \"component_display_name\": " << JsonStr(n.display_name) << ",\n";
        os << "      \"custom_name\": " << JsonStr(n.custom_name) << ",\n";
        os << "      \"component_path\": " << JsonStr(n.occurrence_path) << ",\n";
        os << "      \"model_file_path\": " << JsonStr(n.model_path) << ",\n";
        os << "      \"component_feature_id\": " << n.component_feature_id << ",\n";
        os << "      \"parent_assembly\": " << JsonStr(n.parent_assembly) << ",\n";
        os << "      \"level\": " << n.depth << ",\n";
        os << "      \"suppressed\": " << JsonBool(n.suppressed) << ",\n";
        os << "      \"hidden\": " << JsonBool(n.hidden) << ",\n";
        os << "      \"family_table_instance\": " << JsonBool(n.family_instance) << ",\n";
        os << "      \"repeated_assembly\": " << JsonBool(n.duplicate_assembly) << ",\n";
        os << "      \"counts\": {\"parameters\": " << n.parameter_count
           << ", \"constraints\": " << n.constraint_count
           << ", \"features\": " << n.feature_count
           << ", \"dimensions\": " << n.dimension_count
           << ", \"family_columns\": " << n.family_column_count
           << ", \"family_rows\": " << n.family_row_count << "}\n";
        os << "    }" << (i + 1 < report.nodes.size() ? "," : "") << "\n";
    }
    os << "  ],\n";

    os << "  \"parameters\": [\n";
    for (size_t i = 0; i < report.parameters.size(); ++i) {
        const auto &p = report.parameters[i];
        os << "    {\"node_index\": " << p.node_index
           << ", \"name\": " << JsonStr(p.name)
           << ", \"type\": " << JsonStr(p.type_label)
           << ", \"value\": " << JsonStr(p.value)
           << ", \"owner_object\": " << JsonStr(p.owner_scope)
           << ", \"exists\": " << JsonBool(p.exists)
           << ", \"modifiable\": " << JsonBool(p.writable)
           << ", \"relation_driven\": " << JsonBool(p.driver != PRO_PARAMDRIVER_NONE)
           << ", \"family_table_driven\": " << JsonBool(p.table_driven)
           << ", \"designated\": " << JsonBool(p.designated)
           << ", \"description\": " << JsonStr(p.description) << "}"
           << (i + 1 < report.parameters.size() ? "," : "") << "\n";
    }
    os << "  ],\n";

    os << "  \"references\": [\n";
    for (size_t i = 0; i < report.references.size(); ++i) {
        const auto &r = report.references[i];
        os << "    {\"node_index\": " << r.node_index
           << ", \"source\": " << JsonStr(r.source)
           << ", \"owner\": " << JsonStr(r.owner_model)
           << ", \"reference_name\": " << JsonStr(r.name)
           << ", \"reference_type\": " << JsonStr(r.type_label)
           << ", \"modelitem_type\": " << r.type
           << ", \"id\": " << r.id
           << ", \"unique\": " << JsonBool(r.unique) << "}"
           << (i + 1 < report.references.size() ? "," : "") << "\n";
    }
    os << "  ],\n";

    os << "  \"features\": [\n";
    for (size_t i = 0; i < report.features.size(); ++i) {
        const auto &f = report.features[i];
        os << "    {\"node_index\": " << f.node_index
           << ", \"feature_name\": " << JsonStr(f.name)
           << ", \"feature_type\": " << JsonStr(f.type_name)
           << ", \"feature_id\": " << f.id
           << ", \"feature_status\": " << JsonStr(FeatureStatusLabel(static_cast<ProFeatStatus>(f.status)))
           << ", \"suppressed\": " << JsonBool(f.suppressed)
           << ", \"hidden\": " << JsonBool(!f.visible) << "}"
           << (i + 1 < report.features.size() ? "," : "") << "\n";
    }
    os << "  ],\n";

    os << "  \"dimensions\": [\n";
    for (size_t i = 0; i < report.dimensions.size(); ++i) {
        const auto &d = report.dimensions[i];
        os << "    {\"node_index\": " << d.node_index
           << ", \"feature_name\": " << JsonStr(d.feature_name)
           << ", \"feature_id\": " << d.feature_id
           << ", \"dimension_symbol\": " << JsonStr(d.symbol)
           << ", \"dimension_value\": " << JsonNum(d.value)
           << ", \"dimension_unit\": " << JsonStr(d.unit)
           << ", \"is_pattern_quantity\": " << JsonBool(d.pattern_quantity)
           << ", \"is_family_table_column\": " << JsonBool(d.family_table_column)
           << ", \"requires_user_input_control\": false}"
           << (i + 1 < report.dimensions.size() ? "," : "") << "\n";
    }
    os << "  ],\n";

    os << "  \"assembly_rules\": [\n";
    for (size_t i = 0; i < report.constraints.size(); ++i) {
        const auto &c = report.constraints[i];
        const ModelStructureNode *node = FindNodeByIndex(report, c.node_index);
        const ModelStructureNode *parent = node == nullptr ? nullptr : FindNodeByIndex(report, node->parent_index);
        os << "    {\"node_index\": " << c.node_index
           << ", \"constraint_index\": " << c.index
           << ", \"model_to_assemble\": " << JsonStr(node == nullptr ? L"" : node->model_name)
           << ", \"target_model\": " << JsonStr(parent == nullptr ? L"" : parent->model_name)
           << ", \"constraint_type\": " << JsonStr(c.type_label)
           << ", \"constraint_reference_1\": " << JsonStr(c.asm_reference)
           << ", \"constraint_reference_2\": " << JsonStr(c.comp_reference)
           << ", \"offset\": " << JsonNum(c.offset)
           << ", \"allow_auto_constraint\": " << JsonBool(c.type_label == L"AUTO")
           << ", \"assembled_name\": " << JsonStr(node == nullptr ? L"" : (node->custom_name.empty() ? node->display_name : node->custom_name))
           << ", \"post_assembly_parameters\": []"
           << ", \"read_status\": " << c.read_status << "}"
           << (i + 1 < report.constraints.size() ? "," : "") << "\n";
    }
    os << "  ],\n";

    os << "  \"family_tables\": [\n";
    for (size_t i = 0; i < report.family_tables.size(); ++i) {
        const auto &ft = report.family_tables[i];
        const ModelStructureNode *node = FindNodeByIndex(report, ft.node_index);
        os << "    {\"node_index\": " << ft.node_index
           << ", \"node_model_name\": " << JsonStr(node == nullptr ? L"" : node->model_name)
           << ", \"node_occurrence_path\": " << JsonStr(node == nullptr ? L"" : node->occurrence_path)
           << ", \"source_is_selected_model\": " << JsonBool(ft.source_is_selected_model)
           << ", \"source_is_generic\": " << JsonBool(ft.source_is_generic)
           << ", \"generation\": " << ft.generation
           << ", \"level_path\": " << JsonStr(ft.level_path)
           << ", \"level_depth\": " << ft.level_depth
           << ", \"parent_generic_name\": " << JsonStr(ft.parent_generic_name)
           << ", \"parent_instance_name\": " << JsonStr(ft.parent_instance_name)
           << ", \"table_model_name\": " << JsonStr(ft.table_model_name)
           << ", \"table_model_type\": " << JsonStr(ft.table_model_type_label)
           << ", \"selected_instance_name\": " << JsonStr(ft.selected_instance_name)
           << ", \"immediate_generic_name\": " << JsonStr(ft.immediate_generic_name)
           << ", \"top_generic_name\": " << JsonStr(ft.top_generic_name)
           << ", \"init_status\": " << static_cast<int>(ft.init_status)
           << ", \"check_status\": " << static_cast<int>(ft.check_status)
           << ", \"immediate_generic_status\": " << static_cast<int>(ft.immediate_generic_status)
           << ", \"top_generic_status\": " << static_cast<int>(ft.top_generic_status)
           << ", \"has_family_table\": " << JsonBool(ft.has_family_table)
           << ", \"columns\": [";
        for (size_t c = 0; c < ft.columns.size(); ++c) {
            os << "{\"key\": " << JsonStr(ft.columns[c].key)
               << ", \"type\": " << JsonStr(ft.columns[c].type_label) << "}"
               << (c + 1 < ft.columns.size() ? "," : "");
        }
        os << "], \"instances\": [";
        for (size_t r = 0; r < ft.rows.size(); ++r) {
            os << "{\"name\": " << JsonStr(ft.rows[r].instance_name)
               << ", \"row_kind\": " << JsonStr(ft.rows[r].row_kind)
               << ", \"common_name\": " << JsonStr(ft.rows[r].common_name)
               << ", \"verify_status\": " << JsonStr(ft.rows[r].verify_status_text)
               << ", \"locked\": " << JsonBool(ft.rows[r].locked)
               << ", \"externally_locked\": " << JsonBool(ft.rows[r].ext_locked)
               << ", \"cells\": [";
            for (size_t c = 0; c < ft.rows[r].cells.size(); ++c) {
                os << "{\"column\": " << JsonStr(ft.rows[r].cells[c].column_key)
                   << ", \"value\": " << JsonStr(ft.rows[r].cells[c].value)
                   << ", \"editable\": " << JsonBool(ft.rows[r].cells[c].editable) << "}"
                   << (c + 1 < ft.rows[r].cells.size() ? "," : "");
            }
            os << "]}" << (r + 1 < ft.rows.size() ? "," : "");
        }
        os << "]}" << (i + 1 < report.family_tables.size() ? "," : "") << "\n";
    }
    os << "  ],\n";

    os << "  \"relations\": [\n";
    bool first_relation = true;
    for (const auto &rs : report.relation_sets) {
        for (const auto &line : rs.lines) {
            if (!first_relation) os << ",\n";
            first_relation = false;
            os << "    {\"node_index\": " << rs.node_index
               << ", \"line_number\": " << line.line_number
               << ", \"text\": " << JsonStr(line.text)
               << ", \"comment\": " << JsonBool(line.comment)
               << ", \"conditional\": " << JsonBool(line.conditional) << "}";
        }
    }
    os << "\n  ],\n";

    os << "  \"udf\": {\n";
    os << "    \"status\": \"not_collected\",\n";
    os << "    \"udf_file_path\": \"\",\n";
    os << "    \"udf_name\": \"\",\n";
    os << "    \"placement_model\": \"\",\n";
    os << "    \"udf_refs\": [],\n";
    os << "    \"udf_dims\": [],\n";
    os << "    \"note\": \"UDF placement data is reserved; no UDF-specific APIs were invoked in this export.\"\n";
    os << "  },\n";
    os << "  \"business_rules\": {\n";
    os << "    \"allow_surface_feature_check\": false,\n";
    os << "    \"rename_after_create\": false,\n";
    os << "    \"exported_parameters\": [";
    bool first_exported = true;
    std::set<std::wstring> exported_param_keys;
    for (const auto &p : report.parameters) {
        if (!p.designated || p.name.empty()) {
            continue;
        }
        const std::wstring key = std::to_wstring(p.node_index) + L":" + Upper(p.name);
        if (!exported_param_keys.insert(key).second) {
            continue;
        }
        if (!first_exported) {
            os << ", ";
        }
        first_exported = false;
        os << "{\"node_index\": " << p.node_index
           << ", \"parameter_name\": " << JsonStr(p.name)
           << ", \"parameter_type\": " << JsonStr(p.type_label) << "}";
    }
    os << "],\n";
    os << "    \"assembly_rules_source\": \"existing_creo_constraints\",\n";
    os << "    \"post_assembly_parameters_source\": \"reserved\"\n";
    os << "  }\n";
    os << "}\n";
    return os.str();
}

bool SaveModelStructureJson(const ModelStructureReport &report,
                            const std::wstring &path,
                            std::string &error_out)
{
    error_out.clear();
    if (path.empty()) {
        error_out = "empty output path";
        return false;
    }
    const std::string json = BuildModelStructureJson(report);
    FILE *fp = nullptr;
#if defined(_MSC_VER)
    if (_wfopen_s(&fp, path.c_str(), L"wb") != 0) {
        fp = nullptr;
    }
#else
    fp = std::fopen(autobbox::common::WideToUtf8(path).c_str(), "wb");
#endif
    if (fp == nullptr) {
        error_out = "open output file failed";
        return false;
    }
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    std::fwrite(bom, 1, sizeof(bom), fp);
    const size_t written = std::fwrite(json.data(), 1, json.size(), fp);
    std::fclose(fp);
    if (written != json.size()) {
        error_out = "write output file failed";
        return false;
    }
    return true;
}

std::wstring BuildModelStructureSummary(const ModelStructureReport &report)
{
    int family_tables = 0;
    int relation_sets = 0;
    for (const ModelStructureFamilyTable &ft : report.family_tables) {
        if (ft.has_family_table) ++family_tables;
    }
    for (const ModelStructureRelationSet &rs : report.relation_sets) {
        if (rs.has_main_relset) ++relation_sets;
    }
    return L"结构分析：" + std::to_wstring(report.nodes.size()) +
           L" 个节点，约束 " + std::to_wstring(report.constraints.size()) +
           L" 条，参数 " + std::to_wstring(report.parameters.size()) +
           L" 个，族表 " + std::to_wstring(family_tables) +
           L" 个，关系式集 " + std::to_wstring(relation_sets) + L" 个";
}

} // namespace autobbox::application
