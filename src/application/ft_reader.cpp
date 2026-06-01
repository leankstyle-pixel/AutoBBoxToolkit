#include "autobbox/application/ft_reader.h"

#include "autobbox/application/ft_logger.h"
#include "autobbox/application/ft_support_matrix.h"
#include "autobbox/creo/parameter_api.h"

#include <ProAsmcomp.h>
#include <ProDimension.h>
#include <ProFamtable.h>
#include <ProFaminstance.h>
#include <ProFeature.h>
#include <ProMdl.h>
#include <ProModelitem.h>
#include <ProParamval.h>
#include <ProParameter.h>
#include <ProSolid.h>
#include <ProUtil.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <map>

namespace autobbox::application {
namespace {

void CopyW(wchar_t *dest, size_t cap, const std::wstring &src)
{
    if (dest == nullptr || cap == 0) return;
    wcsncpy_s(dest, cap, src.c_str(), _TRUNCATE);
}

std::wstring ParamValueTypeName(ProParamvalueType type)
{
    switch (type) {
    case PRO_PARAM_DOUBLE: return L"DOUBLE";
    case PRO_PARAM_STRING: return L"STRING";
    case PRO_PARAM_INTEGER: return L"INTEGER";
    case PRO_PARAM_BOOLEAN: return L"BOOLEAN";
    case PRO_PARAM_NOTE_ID: return L"NOTE_ID";
    case PRO_PARAM_VOID: return L"VOID";
    default: return L"UNKNOWN";
    }
}

std::wstring ParamValueToText(ProParamvalue *value)
{
    if (value == nullptr) return L"";
    ProParamvalueType type = PRO_PARAM_NOT_SET;
    if (ProParamvalueTypeGet(value, &type) != PRO_TK_NO_ERROR) {
        type = value->type;
    }
    switch (type) {
    case PRO_PARAM_DOUBLE: {
        double v = 0.0;
        if (ProParamvalueValueGet(value, PRO_PARAM_DOUBLE, &v) == PRO_TK_NO_ERROR) {
            wchar_t buf[128] = {0};
            std::swprintf(buf, 127, L"%.12g", v);
            return buf;
        }
        break;
    }
    case PRO_PARAM_STRING: {
        ProLine s = {0};
        if (ProParamvalueValueGet(value, PRO_PARAM_STRING, s) == PRO_TK_NO_ERROR) {
            return s;
        }
        break;
    }
    case PRO_PARAM_INTEGER:
    case PRO_PARAM_NOTE_ID: {
        int v = 0;
        if (ProParamvalueValueGet(value, type, &v) == PRO_TK_NO_ERROR) {
            return std::to_wstring(v);
        }
        break;
    }
    case PRO_PARAM_BOOLEAN: {
        ProBoolean v = PRO_B_FALSE;
        if (ProParamvalueValueGet(value, PRO_PARAM_BOOLEAN, &v) == PRO_TK_NO_ERROR) {
            return v == PRO_B_TRUE ? L"TRUE" : L"FALSE";
        }
        break;
    }
    case PRO_PARAM_VOID:
        return L"";
    default:
        break;
    }
    return L"<UNREADABLE>";
}

core::FtColumnCategory CategoryFromFamtabType(ProFamtabType type)
{
    switch (type) {
    case PRO_FAM_DIMENSION: return core::FtColumnCategory::Dimension;
    case PRO_FAM_USER_PARAM:
    case PRO_FAM_MP_USER_PARAM:
    case PRO_FAM_MP_SOURCE:
    case PRO_FAM_FEATURE_PARAM:
    case PRO_FAM_EDGE_PARAM:
    case PRO_FAM_SURFACE_PARAM:
    case PRO_FAM_CURVE_PARAM:
    case PRO_FAM_COMP_CURVE_PARAM:
    case PRO_FAM_QUILT_PARAM:
    case PRO_FAM_ANNOT_ELEM_PARAM:
    case PRO_FAM_CONNECTION_PARAM:
    case PRO_FAM_BODY_PARAM:
    case PRO_FAM_BODY_MP_PARAM:
        return core::FtColumnCategory::Parameter;
    case PRO_FAM_SYSTEM_PARAM: return core::FtColumnCategory::SystemParameter;
    case PRO_FAM_FEATURE: return core::FtColumnCategory::Feature;
    case PRO_FAM_ASMCOMP:
    case PRO_FAM_ASMCOMP_MODEL: return core::FtColumnCategory::AssemblyMember;
    case PRO_FAM_UDF: return core::FtColumnCategory::Udf;
    case PRO_FAM_EXTERNAL_REFERENCE: return core::FtColumnCategory::ReferenceModel;
    case PRO_FAM_MERGE_PART_REF: return core::FtColumnCategory::MergePart;
    default: return core::FtColumnCategory::Unknown;
    }
}

bool IsPatternQuantityFamtableName(const std::wstring &name)
{
    if (name.size() < 2) return false;
    if (name[0] != L'P' && name[0] != L'p') return false;
    return std::all_of(name.begin() + 1, name.end(), [](wchar_t ch) {
        return ch >= L'0' && ch <= L'9';
    });
}

std::wstring MakeColumnKey(core::FtColumnCategory cat, const std::wstring &name)
{
    return std::wstring(core::FtColumnCategoryName(cat)) + L":" + name;
}

std::wstring MakeNativeColumnLabel(core::FtColumnCategory cat, const std::wstring &name)
{
    if (cat == core::FtColumnCategory::Fixed) return name;
    return name;
}

bool TryAsmComponentFeatureFromFamtableItem(ProFamtableItem *item, ProModelitem &model_item)
{
    if (item == nullptr) return false;

    // Assembly component columns are displayed by Creo as compact ids such as
    // M376.  In practice the numeric part is the component feature id, so try
    // that first; the generic reverse converter may produce a less useful
    // model item for some family-table component/model columns.
    if (item->owner != nullptr && item->string[0] == L'M') {
        wchar_t *end = nullptr;
        const long feature_id = std::wcstol(item->string + 1, &end, 10);
        if (end != item->string + 1 && (end == nullptr || *end == L'\0') && feature_id > 0) {
            ProFeature feature = {};
            if (ProFeatureInit(reinterpret_cast<ProSolid>(item->owner), static_cast<int>(feature_id), &feature) == PRO_TK_NO_ERROR) {
                model_item = feature;
                return true;
            }
        }
    }

    if (ProFamtableItemToModelitem(item, &model_item) == PRO_TK_NO_ERROR) {
        return true;
    }

    return false;
}

struct FamItemFeatureLookupCtx {
    ProFamtableItem *target = nullptr;
    ProModelitem matched = {};
    bool found = false;
};

ProError MatchFeatureByFamtableItemVisit(ProFeature *feature, ProError status, ProAppData data)
{
    if (status != PRO_TK_NO_ERROR || feature == nullptr || data == nullptr) return PRO_TK_NO_ERROR;
    auto *ctx = reinterpret_cast<FamItemFeatureLookupCtx *>(data);
    if (ctx->target == nullptr) return PRO_TK_NO_ERROR;

    ProFamtableItem candidate = {};
    ProModelitem item = *feature;
    if (ProModelitemToFamtableItem(&item, &candidate) != PRO_TK_NO_ERROR) {
        return PRO_TK_NO_ERROR;
    }

    if ((candidate.type == ctx->target->type ||
         (candidate.type == PRO_FAM_ASMCOMP && ctx->target->type == PRO_FAM_ASMCOMP_MODEL) ||
         (candidate.type == PRO_FAM_ASMCOMP_MODEL && ctx->target->type == PRO_FAM_ASMCOMP)) &&
        std::wstring(candidate.string) == std::wstring(ctx->target->string)) {
        ctx->matched = item;
        ctx->found = true;
        return PRO_TK_E_FOUND;
    }
    return PRO_TK_NO_ERROR;
}

bool FindAsmComponentFeatureByFamtableColumn(ProFamtableItem *item, ProModelitem &model_item)
{
    if (item == nullptr || item->owner == nullptr || item->string[0] == L'\0') return false;
    FamItemFeatureLookupCtx ctx;
    ctx.target = item;
    const ProError st = ProSolidFeatVisit(reinterpret_cast<ProSolid>(item->owner),
                                          MatchFeatureByFamtableItemVisit,
                                          nullptr,
                                          &ctx);
    if ((st == PRO_TK_E_FOUND || st == PRO_TK_NO_ERROR) && ctx.found) {
        model_item = ctx.matched;
        return true;
    }
    return false;
}

std::wstring ReadAsmComponentModelCustomName(ProModelitem &model_item, const std::wstring &fam_string)
{
    ProAsmcomp asm_comp = model_item;
    ProMdl comp_mdl = nullptr;
    if (ProAsmcompMdlGet(&asm_comp, &comp_mdl) != PRO_TK_NO_ERROR || comp_mdl == nullptr) {
        return L"";
    }

    std::wstring custom_name;
    if ((autobbox::creo::ReadStringParamOnModel(comp_mdl, L"PTC_COMMON_NAME", custom_name) ||
         autobbox::creo::ReadStringParamOnModel(comp_mdl, L"COMMON_NAME", custom_name)) &&
        !custom_name.empty() &&
        custom_name != fam_string) {
        return custom_name;
    }
    return L"";
}

std::wstring ResolveNativeColumnDisplaySuffix(ProFamtableItem *item)
{
    if (item == nullptr) return L"";

    if (IsPatternQuantityFamtableName(item->string)) {
        ProModelitem model_item = {};
        if (ProFamtableItemToModelitem(item, &model_item) == PRO_TK_NO_ERROR) {
            ProDimension dim = model_item;
            ProName symbol = {0};
            if (ProDimensionSymbolGet(&dim, symbol) == PRO_TK_NO_ERROR && symbol[0] != L'\0') {
                return symbol;
            }

            ProName item_name = {0};
            if (ProModelitemNameGet(&model_item, item_name) == PRO_TK_NO_ERROR && item_name[0] != L'\0') {
                return item_name;
            }
        }
    }

    const bool looks_like_component_column =
        item->type == PRO_FAM_ASMCOMP ||
        item->type == PRO_FAM_ASMCOMP_MODEL ||
        (item->string[0] == L'M' && item->owner != nullptr);

    if (looks_like_component_column) {
        ProModelitem model_item = {};
        if (TryAsmComponentFeatureFromFamtableItem(item, model_item) ||
            FindAsmComponentFeatureByFamtableColumn(item, model_item)) {
            const std::wstring fam_string(item->string);

            ProMdlName tree_name = {0};
            if (ProFeatureMdltreeDisplaynameGet(&model_item, tree_name) == PRO_TK_NO_ERROR &&
                tree_name[0] != L'\0' &&
                std::wstring(tree_name) != fam_string) {
                return tree_name;
            }

            ProName item_name = {0};
            if (ProModelitemNameGet(&model_item, item_name) == PRO_TK_NO_ERROR &&
                item_name[0] != L'\0' &&
                std::wstring(item_name) != fam_string) {
                return item_name;
            }

            const std::wstring custom_name = ReadAsmComponentModelCustomName(model_item, fam_string);
            if (!custom_name.empty()) {
                return custom_name;
            }

            ProAsmcomp asm_comp = model_item;

            ProMdlfileType file_type = PRO_MDLFILE_UNUSED;
            ProFamilyMdlName mdl_name = {0};
            if (ProAsmcompMdlMdlnameGet(&asm_comp, &file_type, mdl_name) == PRO_TK_NO_ERROR &&
                mdl_name[0] != L'\0') {
                return mdl_name;
            }

            ProMdl comp_mdl = nullptr;
            if (ProAsmcompMdlGet(&asm_comp, &comp_mdl) == PRO_TK_NO_ERROR && comp_mdl != nullptr) {
                ProMdlFileName display_name = {0};
                if (ProMdlDisplaynameGet(comp_mdl, PRO_B_FALSE, display_name) == PRO_TK_NO_ERROR &&
                    display_name[0] != L'\0') {
                    return display_name;
                }

                ProMdlName short_name = {0};
                if (ProMdlMdlnameGet(comp_mdl, short_name) == PRO_TK_NO_ERROR && short_name[0] != L'\0') {
                    return short_name;
                }
            }
        }
    }

    ProParameter param = {};
    if (ProFamtableItemToParameter(item, &param) == PRO_TK_NO_ERROR) {
        const std::wstring name(param.id);
        if (!name.empty()) return name;
    }

    ProModelitem model_item = {};
    if (ProFamtableItemToModelitem(item, &model_item) == PRO_TK_NO_ERROR) {
        ProName item_name = {0};
        if (ProModelitemNameGet(&model_item, item_name) == PRO_TK_NO_ERROR) {
            const std::wstring name(item_name);
            if (!name.empty()) return name;
        }
    }

    return L"";
}

std::wstring MakeResolvedColumnLabel(const core::FtColumnCategory cat, ProFamtableItem *item, const std::wstring &name)
{
    if (cat == core::FtColumnCategory::Parameter && name == L"PTC_COMMON_NAME") {
        return L"\u516c\u7528\u540d\u79f0";
    }
    const std::wstring base = MakeNativeColumnLabel(cat, name);
    const std::wstring suffix = ResolveNativeColumnDisplaySuffix(item);
    if (suffix.empty() || suffix == base) return base;
    return base + L" " + suffix;
}

bool IsYesNoInstanceColumn(core::FtColumnCategory cat)
{
    return cat == core::FtColumnCategory::Feature ||
           cat == core::FtColumnCategory::AssemblyMember ||
           cat == core::FtColumnCategory::Udf ||
           cat == core::FtColumnCategory::ReferenceModel ||
           cat == core::FtColumnCategory::MergePart;
}

bool EqualsIgnoreCase(std::wstring a, std::wstring b)
{
    std::transform(a.begin(), a.end(), a.begin(), towupper);
    std::transform(b.begin(), b.end(), b.begin(), towupper);
    return a == b;
}

std::wstring ReadModelCommonName(ProMdl mdl)
{
    if (mdl == nullptr) return L"";
    std::wstring out;
    if (autobbox::creo::ReadStringParamOnModel(mdl, L"PTC_COMMON_NAME", out)) return out;
    return L"";
}

std::wstring NativeYesNoDisplayValue(core::FtColumnCategory cat, const std::wstring &raw)
{
    if (!IsYesNoInstanceColumn(cat)) return raw;
    if (raw.empty()) return raw;
    if (raw == L"1" || raw == L"X" || EqualsIgnoreCase(raw, L"Y") || EqualsIgnoreCase(raw, L"YES") || EqualsIgnoreCase(raw, L"TRUE")) return L"\u662f";
    if (raw == L"0" || EqualsIgnoreCase(raw, L"N") || EqualsIgnoreCase(raw, L"NO") || EqualsIgnoreCase(raw, L"FALSE")) return L"\u5426";
    // Non-boolean text is intentionally preserved: for component/model columns
    // Creo may store a family-table instance name here.
    return raw;
}

void AddFixedColumns(core::FtLevelNode &level)
{
    auto add = [&](const wchar_t *key, const wchar_t *label, bool editable, bool visible) {
        core::FtColumn c;
        c.column_key = key;
        c.column_display_name = label;
        c.column_category = core::FtColumnCategory::Fixed;
        c.value_type_name = L"STRING";
        c.value_type = PRO_PARAM_STRING;
        c.support_status = core::FtSupportStatus::Full;
        c.editable = editable;
        c.visible = visible;
        c.order_index = static_cast<int>(level.columns.size());
        level.columns.push_back(c);
    };
    add(L"INSTANCE_NAME", L"\u5b9e\u4f8b\u540d\u79f0", true, true);
    add(L"ROW_KIND", L"ROW_KIND", false, false);
    add(L"VERIFY_STATUS", L"VERIFY_STATUS", false, false);
    add(L"IS_LOCKED", L"IS_LOCKED", true, false);
    add(L"IS_EXT_LOCKED", L"IS_EXT_LOCKED", false, false);
    add(L"COMMENT", L"COMMENT", true, false);
    add(L"SUPPORT_STATUS", L"SUPPORT_STATUS", false, false);
}

struct ItemCtx { core::FtLevelNode *level = nullptr; };

ProError VisitItem(ProFamtableItem *item, ProError status, ProAppData data)
{
    if (status != PRO_TK_NO_ERROR || item == nullptr || data == nullptr) return PRO_TK_NO_ERROR;
    auto *ctx = reinterpret_cast<ItemCtx *>(data);
    if (ctx->level == nullptr) return PRO_TK_NO_ERROR;

    ProFamtableItem resolved_item = *item;
    if (resolved_item.owner == nullptr) {
        resolved_item.owner = ctx->level->generic_mdl;
    }

    core::FtColumnCategory cat = CategoryFromFamtabType(resolved_item.type);
    if (IsPatternQuantityFamtableName(resolved_item.string)) {
        // Creo displays pattern quantity family-table columns as P###.  They
        // are numeric dimension-like controls (IPAR_INT quantity dimensions),
        // not yes/no feature/component switches and not unsupported columns.
        cat = core::FtColumnCategory::Dimension;
    }
    core::FtColumn col;
    col.has_creo_item = true;
    col.famtab_type = resolved_item.type;
    col.famtab_string = resolved_item.string;
    col.creo_item_owner = resolved_item.owner;
    col.column_category = cat;
    col.column_display_name = MakeResolvedColumnLabel(cat, &resolved_item, col.famtab_string);
    col.column_key = MakeColumnKey(cat, col.famtab_string);
    col.support_status = SupportForColumnCategory(cat);
    col.editable = IsFtColumnEditable(cat, col.support_status);
    col.visible = true;
    col.order_index = static_cast<int>(ctx->level->columns.size());
    col.value_type = PRO_PARAM_NOT_SET;
    col.value_type_name = L"UNKNOWN";
    ctx->level->columns.push_back(col);
    return PRO_TK_NO_ERROR;
}

core::FtCell MakeCell(const core::FtColumn &col, const std::wstring &value)
{
    core::FtCell cell;
    cell.column_key = col.column_key;
    cell.value = value;
    cell.old_value = value;
    cell.editable = col.editable;
    cell.support_status = col.support_status;
    cell.value_type = col.value_type;
    return cell;
}

bool HasPtcCommonNameColumn(const core::FtLevelNode &level)
{
    for (const auto &col : level.columns) {
        if (col.column_category == core::FtColumnCategory::Parameter &&
            EqualsIgnoreCase(col.famtab_string, L"PTC_COMMON_NAME")) {
            return true;
        }
    }
    return false;
}

core::FtColumn MakePtcCommonNameColumn(const core::FtLevelNode &level)
{
    core::FtColumn col;
    col.column_key = MakeColumnKey(core::FtColumnCategory::Parameter, L"PTC_COMMON_NAME");
    col.column_display_name = L"公用名称";
    col.column_category = core::FtColumnCategory::Parameter;
    col.value_type = PRO_PARAM_STRING;
    col.value_type_name = L"STRING";
    col.support_status = core::FtSupportStatus::Full;
    col.editable = true;
    col.visible = true;
    col.required = false;
    col.has_creo_item = true;
    col.famtab_type = PRO_FAM_USER_PARAM;
    col.famtab_string = L"PTC_COMMON_NAME";
    col.order_index = static_cast<int>(level.columns.size());
    return col;
}

void AppendPtcCommonNameColumn(core::FtLevelNode &level)
{
    if (HasPtcCommonNameColumn(level)) return;
    core::FtColumn col = MakePtcCommonNameColumn(level);
    size_t insert_index = 0;
    if (!level.columns.empty() && level.columns.front().column_key == L"INSTANCE_NAME") {
        insert_index = 1;
    }
    if (insert_index > level.columns.size()) insert_index = level.columns.size();
    level.columns.insert(level.columns.begin() + static_cast<std::ptrdiff_t>(insert_index), col);
    for (size_t i = 0; i < level.columns.size(); ++i) {
        level.columns[i].order_index = static_cast<int>(i);
    }
    for (auto &row : level.rows) {
        std::wstring value;
        if (row.row_kind == core::FtRowKind::Generic) {
            value = ReadModelCommonName(level.generic_mdl);
        }
        core::FtCell cell = MakeCell(col, value);
        if (insert_index > row.cells.size()) {
            row.cells.push_back(cell);
        } else {
            row.cells.insert(row.cells.begin() + static_cast<std::ptrdiff_t>(insert_index), cell);
        }
    }
}

void FillFixedCells(core::FtRow &row, const core::FtLevelNode &level)
{
    for (const core::FtColumn &col : level.columns) {
        if (col.column_category != core::FtColumnCategory::Fixed) continue;
        std::wstring v;
        if (col.column_key == L"INSTANCE_NAME") v = row.instance_name;
        else if (col.column_key == L"COMMON_NAME") v = row.common_name;
        else if (col.column_key == L"ROW_KIND") v = row.row_kind == core::FtRowKind::Generic ? L"GENERIC" : L"INSTANCE";
        else if (col.column_key == L"VERIFY_STATUS") v = row.verify_status;
        else if (col.column_key == L"IS_LOCKED") v = row.is_locked ? L"TRUE" : L"FALSE";
        else if (col.column_key == L"IS_EXT_LOCKED") v = row.is_ext_locked ? L"TRUE" : L"FALSE";
        else if (col.column_key == L"COMMENT") v = L"";
        else if (col.column_key == L"SUPPORT_STATUS") v = L"FULL";
        row.cells.push_back(MakeCell(col, v));
    }
}

std::wstring ReadGenericValue(core::FtLevelNode &level, const core::FtColumn &col)
{
    if (!col.has_creo_item || col.famtab_string.empty()) return L"";
    ProFamtableItem item = {};
    item.type = col.famtab_type;
    item.owner = col.creo_item_owner != nullptr ? col.creo_item_owner : level.generic_mdl;
    CopyW(item.string, sizeof(item.string) / sizeof(item.string[0]), col.famtab_string);

    if (col.column_category == core::FtColumnCategory::Parameter || col.column_category == core::FtColumnCategory::SystemParameter) {
        ProParameter param;
        if (ProFamtableItemToParameter(&item, &param) == PRO_TK_NO_ERROR) {
            ProParamvalueType type = PRO_PARAM_NOT_SET;
            std::wstring text;
            if (autobbox::creo::ReadParameterDisplayValue(&param, type, text)) return text;
        }
    }
    if (col.column_category == core::FtColumnCategory::Dimension) {
        ProModelitem mi = {};
        if (ProFamtableItemToModelitem(&item, &mi) == PRO_TK_NO_ERROR) {
            ProDimension dim = mi;
            dim.owner = level.generic_mdl;
            double value = 0.0;
            if (ProDimensionValueGet(&dim, &value) == PRO_TK_NO_ERROR) {
                wchar_t buf[128] = {0};
                std::swprintf(buf, 127, L"%.12g", value);
                return buf;
            }
        }
    }
    return L"<GENERIC>";
}

void FillDynamicGenericCells(core::FtRow &row, core::FtLevelNode &level)
{
    for (const core::FtColumn &col : level.columns) {
        if (col.column_category == core::FtColumnCategory::Fixed) continue;
        row.cells.push_back(MakeCell(col, ReadGenericValue(level, col)));
    }
}

void FillDynamicInstanceCells(core::FtRow &row, core::FtLevelNode &level, ProFaminstance *inst)
{
    for (core::FtColumn &col : level.columns) {
        if (col.column_category == core::FtColumnCategory::Fixed) continue;
        ProFamtableItem item = {};
        item.type = col.famtab_type;
        item.owner = col.creo_item_owner != nullptr ? col.creo_item_owner : level.generic_mdl;
        CopyW(item.string, sizeof(item.string) / sizeof(item.string[0]), col.famtab_string);
        ProParamvalue value = {};
        ProError st = ProFaminstanceValueGet(inst, &item, &value);
        std::wstring text = (st == PRO_TK_NO_ERROR) ? NativeYesNoDisplayValue(col.column_category, ParamValueToText(&value)) : L"*";
        ProParamvalueType t = PRO_PARAM_NOT_SET;
        if (st == PRO_TK_NO_ERROR && ProParamvalueTypeGet(&value, &t) == PRO_TK_NO_ERROR) {
            col.value_type = t;
            col.value_type_name = ParamValueTypeName(t);
        }
        core::FtCell cell = MakeCell(col, text);
        cell.value_type = col.value_type;
        if ((col.value_type == PRO_PARAM_VOID || col.value_type == PRO_PARAM_NOTE_ID) &&
            !IsYesNoInstanceColumn(col.column_category)) {
            cell.editable = false;
            cell.support_status = core::FtSupportStatus::ReadOnly;
        }
        row.cells.push_back(cell);
    }
}

struct InstCtx { core::FtLevelNode *level = nullptr; };

ProError VisitInstanceForRead(ProFaminstance *inst, ProError status, ProAppData data)
{
    if (status != PRO_TK_NO_ERROR || inst == nullptr || data == nullptr) return PRO_TK_NO_ERROR;
    auto *ctx = reinterpret_cast<InstCtx *>(data);
    if (ctx->level == nullptr) return PRO_TK_NO_ERROR;

    core::FtRow row;
    row.row_kind = core::FtRowKind::Instance;
    row.instance_name = inst->name;
    row.original_instance_name = row.instance_name;
    // Do not retrieve/open every instance model while loading the manager.
    // PTC_COMMON_NAME is displayed as a native-like fixed column, but instance
    // values are populated only when the instance model is already edited or
    // when the user writes a value back.
    row.common_name.clear();
    int locked = 0;
    if (ProFaminstanceCheck(inst, &locked) == PRO_TK_NO_ERROR) row.is_locked = locked != 0;
    ProBoolean ext_locked = PRO_B_FALSE;
    if (ProFaminstanceIsExtLocked(inst, &ext_locked) == PRO_TK_NO_ERROR) row.is_ext_locked = ext_locked == PRO_B_TRUE;
    ProFaminstanceVerifyStatus verified = PRO_INST_FAILED_VERIFIED;
    if (ProFaminstanceIsVerified(inst, &verified) == PRO_TK_NO_ERROR) {
        row.verify_status = verified == PRO_INST_SUCCESS_VERIFIED ? L"VERIFIED" : L"FAILED";
    } else {
        row.verify_status = L"UNKNOWN";
    }
    FillFixedCells(row, *ctx->level);
    FillDynamicInstanceCells(row, *ctx->level, inst);
    ctx->level->rows.push_back(row);
    return PRO_TK_NO_ERROR;
}

core::FtLevelNode *FindLevelByPathLocal(std::vector<core::FtLevelNode> &levels, const std::wstring &path)
{
    for (auto &level : levels) {
        if (level.level_path == path) return &level;
    }
    return nullptr;
}

const core::FtLevelNode *FindLevelByPathLocal(const std::vector<core::FtLevelNode> &levels, const std::wstring &path)
{
    for (const auto &level : levels) {
        if (level.level_path == path) return &level;
    }
    return nullptr;
}

core::FtRow *FindRowByNameLocal(core::FtLevelNode &level, const std::wstring &name)
{
    for (auto &row : level.rows) {
        if (row.instance_name == name) return &row;
    }
    return nullptr;
}

core::FtColumn *FindColumnByKeyLocal(core::FtLevelNode &level, const std::wstring &key)
{
    for (auto &col : level.columns) {
        if (col.column_key == key) return &col;
    }
    return nullptr;
}

bool HasUnsavedCellEdit(const core::FtRow &row)
{
    for (const auto &cell : row.cells) {
        if (cell.changed ||
            cell.change_kind != core::FtChangeKind::None ||
            cell.clone_seeded) {
            return true;
        }
    }
    return false;
}

bool HasUnsavedRowEdit(const core::FtRow &row)
{
    return row.action != core::FtRowAction::Keep ||
           row.change_kind != core::FtChangeKind::None ||
           row.enhanced_clone ||
           HasUnsavedCellEdit(row);
}

bool HasUnsavedColumnEdit(const core::FtColumn &col)
{
    return col.change_kind != core::FtChangeKind::None;
}

bool HasUnsavedLevelState(const core::FtLevelNode &level)
{
    if (level.enhanced_clone || level.pending_resolve) {
        return true;
    }
    for (const auto &row : level.rows) {
        if (HasUnsavedRowEdit(row)) return true;
    }
    for (const auto &col : level.columns) {
        if (HasUnsavedColumnEdit(col)) return true;
    }
    return false;
}

bool ShouldPreservePendingLevelDuringIncrementalRead(const core::FtLevelNode &level)
{
    return level.pending_resolve &&
           (level.enhanced_clone || !level.rows.empty() || !level.columns.empty());
}

void RestoreUnsavedIncrementalEdits(core::FtWorkspace &workspace,
                                    const std::vector<core::FtLevelNode> &previous_levels)
{
    for (const auto &old_level : previous_levels) {
        if (ShouldPreservePendingLevelDuringIncrementalRead(old_level)) {
            continue;
        }

        core::FtLevelNode *level = FindLevelByPathLocal(workspace.level_nodes, old_level.level_path);
        if (level == nullptr) {
            bool has_unsaved = old_level.enhanced_clone;
            for (const auto &row : old_level.rows) has_unsaved = has_unsaved || HasUnsavedRowEdit(row);
            for (const auto &col : old_level.columns) has_unsaved = has_unsaved || HasUnsavedColumnEdit(col);
            if (has_unsaved) workspace.level_nodes.push_back(old_level);
            continue;
        }

        for (const auto &old_col : old_level.columns) {
            if (!HasUnsavedColumnEdit(old_col)) continue;
            core::FtColumn *col = FindColumnByKeyLocal(*level, old_col.column_key);
            if (col == nullptr) {
                level->columns.push_back(old_col);
            } else {
                *col = old_col;
            }
        }

        for (const auto &old_row : old_level.rows) {
            if (!HasUnsavedRowEdit(old_row)) continue;
            core::FtRow *row = FindRowByNameLocal(*level, old_row.instance_name);
            if (row == nullptr) {
                level->rows.push_back(old_row);
            } else {
                *row = old_row;
            }
        }
    }
}

std::vector<core::FtLevelNode> MergeIncrementalSnapshot(const std::vector<core::FtLevelNode> &current_levels,
                                                        const std::vector<core::FtLevelNode> &previous_levels,
                                                        const std::vector<core::FtLevelNode> &previous_snapshot)
{
    std::vector<core::FtLevelNode> snapshot = previous_snapshot;
    for (const auto &level : current_levels) {
        const core::FtLevelNode *previous_level = FindLevelByPathLocal(previous_levels, level.level_path);
        if (previous_level != nullptr && HasUnsavedLevelState(*previous_level)) {
            continue;
        }

        core::FtLevelNode *snap_level = FindLevelByPathLocal(snapshot, level.level_path);
        if (snap_level == nullptr) {
            snapshot.push_back(level);
        } else {
            *snap_level = level;
        }
    }
    return snapshot;
}

} // namespace

ProError ReadFamilyTableWorkspace(core::FtWorkspace &workspace, bool reset_snapshot)
{
    const std::vector<core::FtLevelNode> previous_levels = reset_snapshot ? std::vector<core::FtLevelNode>{} : workspace.level_nodes;
    const std::vector<core::FtLevelNode> previous_snapshot = reset_snapshot ? std::vector<core::FtLevelNode>{} : workspace.original_snapshot;
    const bool previous_dirty = workspace.dirty;

    for (core::FtLevelNode &level : workspace.level_nodes) {
        if (!reset_snapshot && ShouldPreservePendingLevelDuringIncrementalRead(level)) {
            continue;
        }
        if (level.pending_resolve && !level.has_family_table) {
            continue;
        }
        level.columns.clear();
        level.rows.clear();
        AddFixedColumns(level);

        if (!level.has_family_table) {
            core::FtRow generic;
            generic.row_kind = core::FtRowKind::Generic;
            generic.instance_name = level.generic_name;
            generic.original_instance_name = generic.instance_name;
            generic.common_name = ReadModelCommonName(level.generic_mdl);
            generic.verify_status = L"NO_FAMILY_TABLE";
            FillFixedCells(generic, level);
            level.rows.push_back(generic);
            FtLog(workspace, level.level_path, L"INFO", L"read", L"No family table on this level", PRO_TK_E_NOT_FOUND);
            continue;
        }

        ItemCtx item_ctx{&level};
        ProError st_items = ProFamtableItemVisit(&level.famtable, VisitItem, nullptr, &item_ctx);
        if (st_items != PRO_TK_NO_ERROR) {
            FtLog(workspace, level.level_path, L"WARN", L"read-columns", L"Failed to read family table columns", st_items);
        }

        core::FtRow generic;
        generic.row_kind = core::FtRowKind::Generic;
        generic.instance_name = level.generic_name;
        generic.original_instance_name = generic.instance_name;
        generic.common_name = ReadModelCommonName(level.generic_mdl);
        generic.verify_status = L"GENERIC";
        FillFixedCells(generic, level);
        FillDynamicGenericCells(generic, level);
        level.rows.push_back(generic);

        InstCtx inst_ctx{&level};
        ProError st_rows = ProFamtableInstanceVisit(&level.famtable, VisitInstanceForRead, nullptr, &inst_ctx);
        if (st_rows != PRO_TK_NO_ERROR) {
            FtLog(workspace, level.level_path, L"WARN", L"read-rows", L"Failed to read family table instances", st_rows);
        }
    }
    for (core::FtLevelNode &level : workspace.level_nodes) {
        if (level.pending_resolve || !level.has_family_table) continue;
        AppendPtcCommonNameColumn(level);
    }
    if (!reset_snapshot) {
        RestoreUnsavedIncrementalEdits(workspace, previous_levels);
        workspace.original_snapshot = MergeIncrementalSnapshot(workspace.level_nodes, previous_levels, previous_snapshot);
        workspace.dirty = previous_dirty;
    } else {
        workspace.original_snapshot = workspace.level_nodes;
        workspace.dirty = false;
    }
    FtLog(workspace, workspace.active_level_path, L"INFO", L"read", L"Family table workspace read complete", PRO_TK_NO_ERROR);
    return PRO_TK_NO_ERROR;
}

} // namespace autobbox::application
