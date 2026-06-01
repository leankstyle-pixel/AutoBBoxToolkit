#include "autobbox/application/ft_column_ops.h"

#include "autobbox/application/ft_support_matrix.h"

#include <ProArray.h>
#include <ProAsmcomp.h>
#include <ProAsmcomppath.h>
#include <ProDimension.h>
#include <ProFeature.h>
#include <ProMdl.h>
#include <ProModelitem.h>
#include <ProSolid.h>
#include <ProUdf.h>

#include <algorithm>
#include <cwctype>
#include <cstring>
#include <vector>

namespace autobbox::application {
namespace {

void Reindex(core::FtLevelNode &level)
{
    for (size_t i = 0; i < level.columns.size(); ++i) level.columns[i].order_index = static_cast<int>(i);
}

std::wstring CategoryPrefix(core::FtColumnCategory category)
{
    return std::wstring(core::FtColumnCategoryName(category));
}

ProFamtabType FamtabTypeForCategory(core::FtColumnCategory category)
{
    switch (category) {
    case core::FtColumnCategory::Dimension: return PRO_FAM_DIMENSION;
    case core::FtColumnCategory::Parameter: return PRO_FAM_USER_PARAM;
    case core::FtColumnCategory::SystemParameter: return PRO_FAM_SYSTEM_PARAM;
    case core::FtColumnCategory::Feature: return PRO_FAM_FEATURE;
    case core::FtColumnCategory::AssemblyMember: return PRO_FAM_ASMCOMP;
    case core::FtColumnCategory::Udf: return PRO_FAM_UDF;
    case core::FtColumnCategory::ReferenceModel: return PRO_FAM_EXTERNAL_REFERENCE;
    case core::FtColumnCategory::MergePart: return PRO_FAM_MERGE_PART_REF;
    default: return PRO_FAM_TYPE_UNUSED;
    }
}

ProParamvalueType ValueTypeForCategory(core::FtColumnCategory category)
{
    switch (category) {
    case core::FtColumnCategory::Dimension:
        return PRO_PARAM_DOUBLE;
    case core::FtColumnCategory::Feature:
    case core::FtColumnCategory::AssemblyMember:
    case core::FtColumnCategory::Udf:
    case core::FtColumnCategory::ReferenceModel:
    case core::FtColumnCategory::MergePart:
        return PRO_PARAM_STRING;
    case core::FtColumnCategory::Parameter:
    case core::FtColumnCategory::SystemParameter:
    default:
        return PRO_PARAM_STRING;
    }
}

std::wstring ValueTypeNameForCategory(core::FtColumnCategory category)
{
    return category == core::FtColumnCategory::Dimension ? L"DOUBLE" : L"STRING";
}

bool CategoryCanBeAddedFromReplica(core::FtColumnCategory category)
{
    switch (category) {
    case core::FtColumnCategory::Dimension:
    case core::FtColumnCategory::Parameter:
    case core::FtColumnCategory::SystemParameter:
    case core::FtColumnCategory::Feature:
    case core::FtColumnCategory::AssemblyMember:
    case core::FtColumnCategory::Udf:
    case core::FtColumnCategory::ReferenceModel:
    case core::FtColumnCategory::MergePart:
        return true;
    default:
        return false;
    }
}

std::wstring Trim(std::wstring text)
{
    const auto is_space = [](wchar_t ch) { return std::iswspace(ch) != 0; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), [&](wchar_t ch) { return !is_space(ch); }));
    text.erase(std::find_if(text.rbegin(), text.rend(), [&](wchar_t ch) { return !is_space(ch); }).base(), text.end());
    return text;
}

std::wstring Upper(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
    return text;
}

bool TryParseFeatureIdRef(const std::wstring &text, int &feature_id_out)
{
    std::wstring trimmed = Trim(text);
    if (trimmed.empty()) {
        return false;
    }

    const std::wstring upper = Upper(trimmed);
    if (upper.rfind(L"FEAT:", 0) == 0) {
        trimmed = Trim(trimmed.substr(5));
    } else if (upper.rfind(L"FEATURE:", 0) == 0) {
        trimmed = Trim(trimmed.substr(8));
    }

    if (trimmed.empty()) {
        return false;
    }

    size_t start = 0;
    if (trimmed[0] == L'+' || trimmed[0] == L'-') {
        start = 1;
    }
    if (start >= trimmed.size() ||
        !std::all_of(trimmed.begin() + static_cast<std::ptrdiff_t>(start),
                     trimmed.end(),
                     [](wchar_t ch) { return std::iswdigit(ch) != 0; })) {
        return false;
    }

    try {
        feature_id_out = std::stoi(trimmed);
        return true;
    } catch (...) {
        return false;
    }
}

bool TryParseAssemblyMemberIdRef(const std::wstring &text, int &member_id_out)
{
    std::wstring trimmed = Trim(text);
    if (trimmed.empty()) {
        return false;
    }

    const std::wstring upper = Upper(trimmed);
    if (upper.rfind(L"MEMBER:", 0) == 0) {
        trimmed = Trim(trimmed.substr(7));
    } else if (upper.rfind(L"ASM:", 0) == 0) {
        trimmed = Trim(trimmed.substr(4));
    } else if (upper.rfind(L"COMP:", 0) == 0) {
        trimmed = Trim(trimmed.substr(5));
    } else if (upper.rfind(L"COMPONENT:", 0) == 0) {
        trimmed = Trim(trimmed.substr(10));
    } else if (upper.rfind(L"FEAT:", 0) == 0) {
        trimmed = Trim(trimmed.substr(5));
    } else if (upper.rfind(L"FEATURE:", 0) == 0) {
        trimmed = Trim(trimmed.substr(8));
    }

    if (trimmed.empty()) {
        return false;
    }

    size_t start = 0;
    if (trimmed[0] == L'+' || trimmed[0] == L'-') {
        start = 1;
    }
    if (start >= trimmed.size() ||
        !std::all_of(trimmed.begin() + static_cast<std::ptrdiff_t>(start),
                     trimmed.end(),
                     [](wchar_t ch) { return std::iswdigit(ch) != 0; })) {
        return false;
    }

    try {
        member_id_out = std::stoi(trimmed);
        return true;
    } catch (...) {
        return false;
    }
}

bool TryParseUdfIdRef(const std::wstring &text, int &udf_id_out)
{
    std::wstring trimmed = Trim(text);
    if (trimmed.empty()) {
        return false;
    }

    const std::wstring upper = Upper(trimmed);
    if (upper.rfind(L"UDF:", 0) == 0) {
        trimmed = Trim(trimmed.substr(4));
    } else if (upper.rfind(L"GROUP:", 0) == 0) {
        trimmed = Trim(trimmed.substr(6));
    } else if (upper.rfind(L"FEAT:", 0) == 0) {
        trimmed = Trim(trimmed.substr(5));
    } else if (upper.rfind(L"FEATURE:", 0) == 0) {
        trimmed = Trim(trimmed.substr(8));
    }

    if (trimmed.empty()) {
        return false;
    }

    size_t start = 0;
    if (trimmed[0] == L'+' || trimmed[0] == L'-') {
        start = 1;
    }
    if (start >= trimmed.size() ||
        !std::all_of(trimmed.begin() + static_cast<std::ptrdiff_t>(start),
                     trimmed.end(),
                     [](wchar_t ch) { return std::iswdigit(ch) != 0; })) {
        return false;
    }

    try {
        udf_id_out = std::stoi(trimmed);
        return true;
    } catch (...) {
        return false;
    }
}

bool ResolveModelitemFamtableName(const ProModelitem &model_item,
                                  ProFamtabType expected_type,
                                  std::wstring &resolved_name,
                                  std::wstring &error_out)
{
    error_out.clear();

    ProModelitem item = model_item;
    ProFamtableItem fam_item = {};
    const ProError fam_st = ProModelitemToFamtableItem(&item, &fam_item);
    if (fam_st != PRO_TK_NO_ERROR || fam_item.string[0] == L'\0') {
        error_out = L"Cannot convert model item to family-table item";
        return false;
    }

    const bool type_ok =
        expected_type == PRO_FAM_TYPE_UNUSED ||
        fam_item.type == expected_type ||
        (expected_type == PRO_FAM_ASMCOMP && fam_item.type == PRO_FAM_ASMCOMP_MODEL);
    if (!type_ok) {
        error_out = L"Resolved family-table item type does not match requested column category";
        return false;
    }

    resolved_name = fam_item.string;
    return true;
}

bool EqualsLookupText(const std::wstring &lhs, const std::wstring &rhs)
{
    return !Trim(lhs).empty() && !Trim(rhs).empty() && Upper(Trim(lhs)) == Upper(Trim(rhs));
}

std::wstring BuildResolvedDisplayName(const std::wstring &resolved_name, const std::wstring &display_name)
{
    if (display_name.empty() || display_name == resolved_name) {
        return resolved_name;
    }
    return resolved_name + L" (" + display_name + L")";
}

std::wstring BestModelitemDisplayName(const ProModelitem &model_item)
{
    ProModelitem item = model_item;

    ProMdlName mdltree_name = {0};
    if (ProFeatureMdltreeDisplaynameGet(&item, mdltree_name) == PRO_TK_NO_ERROR && mdltree_name[0] != L'\0') {
        return mdltree_name;
    }

    ProName item_name = {0};
    if (ProModelitemNameGet(&item, item_name) == PRO_TK_NO_ERROR && item_name[0] != L'\0') {
        return item_name;
    }

    return L"";
}

bool ModelitemMatchesRequestedName(const ProModelitem &model_item, const std::wstring &requested_name, std::wstring &matched_display_name)
{
    matched_display_name = BestModelitemDisplayName(model_item);
    if (EqualsLookupText(matched_display_name, requested_name)) {
        return true;
    }

    ProModelitem item = model_item;
    ProName item_name = {0};
    if (ProModelitemNameGet(&item, item_name) == PRO_TK_NO_ERROR &&
        item_name[0] != L'\0' &&
        EqualsLookupText(item_name, requested_name)) {
        if (matched_display_name.empty()) {
            matched_display_name = item_name;
        }
        return true;
    }

    return false;
}

enum class FeatureLookupMode {
    Any,
    ComponentLike,
};

struct FeatureNameLookupContext {
    std::wstring requested_name;
    ProFamtabType expected_type = PRO_FAM_TYPE_UNUSED;
    FeatureLookupMode mode = FeatureLookupMode::Any;
    ProFeature matched_feature = {};
    std::wstring resolved_name;
    std::wstring display_name;
    std::wstring convert_error;
    bool found = false;
};

ProError MatchFeatureByNameVisit(ProFeature *feature, ProError status, ProAppData data)
{
    if (status != PRO_TK_NO_ERROR || feature == nullptr || data == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    auto *ctx = reinterpret_cast<FeatureNameLookupContext *>(data);
    ProBoolean is_component_like = PRO_B_FALSE;
    if (ctx->mode == FeatureLookupMode::ComponentLike) {
        if (ProFeatureIsComponentLike(feature, &is_component_like) != PRO_TK_NO_ERROR ||
            is_component_like != PRO_B_TRUE) {
            return PRO_TK_NO_ERROR;
        }
    }

    ProModelitem model_item = *feature;
    std::wstring matched_display_name;
    if (!ModelitemMatchesRequestedName(model_item, ctx->requested_name, matched_display_name)) {
        return PRO_TK_NO_ERROR;
    }

    std::wstring resolved_name;
    std::wstring error_text;
    if (!ResolveModelitemFamtableName(model_item, ctx->expected_type, resolved_name, error_text)) {
        ctx->convert_error = error_text;
        return PRO_TK_NO_ERROR;
    }

    ctx->matched_feature = *feature;
    ctx->resolved_name = resolved_name;
    ctx->display_name = matched_display_name.empty() ? resolved_name : matched_display_name;
    ctx->found = true;
    return static_cast<ProError>(1);
}

bool ResolveFeatureBackedItemByName(core::FtLevelNode &level,
                                    const std::wstring &requested_name,
                                    ProFamtabType expected_type,
                                    FeatureLookupMode mode,
                                    std::wstring &resolved_name,
                                    std::wstring &display_name,
                                    std::wstring &error_out)
{
    if (level.generic_mdl == nullptr) {
        error_out = L"Current level model is not loaded";
        return false;
    }

    FeatureNameLookupContext ctx;
    ctx.requested_name = Trim(requested_name);
    ctx.expected_type = expected_type;
    ctx.mode = mode;
    const ProError visit_st = ProSolidFeatVisit(reinterpret_cast<ProSolid>(level.generic_mdl),
                                                MatchFeatureByNameVisit,
                                                nullptr,
                                                &ctx);
    if (!ctx.found) {
        if (!ctx.convert_error.empty()) {
            error_out = ctx.convert_error;
            return false;
        }
        if (visit_st != PRO_TK_NO_ERROR && visit_st != PRO_TK_CONTINUE) {
            error_out = L"Failed to scan model features by display name";
            return false;
        }
        return false;
    }

    resolved_name = ctx.resolved_name;
    display_name = BuildResolvedDisplayName(resolved_name, ctx.display_name);
    return true;
}

struct FeatureDimensionFindContext {
    ProDimension dimension = {};
    std::wstring symbol;
    bool found = false;
};

ProError AcceptIparIntDimension(ProDimension *dimension, ProAppData)
{
    if (dimension == nullptr) {
        return PRO_TK_CONTINUE;
    }

    ProDimensiontype type = PRODIMTYPE_UNKNOWN;
    if (ProDimensionTypeGet(dimension, &type) != PRO_TK_NO_ERROR) {
        return PRO_TK_CONTINUE;
    }
    return type == PRODIMTYPE_IPAR_INT ? PRO_TK_NO_ERROR : PRO_TK_CONTINUE;
}

ProError CaptureFirstFeatureDimension(ProDimension *dimension, ProError status, ProAppData data)
{
    if (status != PRO_TK_NO_ERROR || dimension == nullptr || data == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    auto *ctx = reinterpret_cast<FeatureDimensionFindContext *>(data);
    ctx->dimension = *dimension;
    ctx->found = true;

    ProName symbol = {0};
    if (ProDimensionSymbolGet(dimension, symbol) == PRO_TK_NO_ERROR) {
        ctx->symbol = symbol;
    } else {
        ctx->symbol.clear();
    }

    return static_cast<ProError>(1);
}

bool ResolveDimensionAddSpec(core::FtLevelNode &level,
                             const std::wstring &requested_name,
                             std::wstring &resolved_name,
                             std::wstring &display_name,
                             std::wstring &error_out)
{
    resolved_name = Trim(requested_name);
    display_name = resolved_name;
    error_out.clear();

    int feature_id = -1;
    if (!TryParseFeatureIdRef(resolved_name, feature_id)) {
        return !resolved_name.empty();
    }

    if (level.generic_mdl == nullptr) {
        error_out = L"Current level model is not loaded";
        return false;
    }

    ProFeature feature = {};
    const ProError init_st = ProFeatureInit(reinterpret_cast<ProSolid>(level.generic_mdl), feature_id, &feature);
    if (init_st != PRO_TK_NO_ERROR) {
        error_out = L"Cannot initialize feature id " + std::to_wstring(feature_id);
        return false;
    }

    FeatureDimensionFindContext ctx;
    const ProError visit_st = ProFeatureDimensionVisit(&feature,
                                                       CaptureFirstFeatureDimension,
                                                       AcceptIparIntDimension,
                                                       &ctx);
    if (!ctx.found) {
        error_out = visit_st == PRO_TK_NO_ERROR || visit_st == PRO_TK_CONTINUE
            ? L"Feature " + std::to_wstring(feature_id) + L" has no IPAR_INT quantity dimension"
            : L"Failed to visit dimensions on feature " + std::to_wstring(feature_id);
        return false;
    }

    ProModelitem model_item = ctx.dimension;
    if (!ResolveModelitemFamtableName(model_item, PRO_FAM_DIMENSION, resolved_name, error_out)) {
        error_out = L"Cannot convert quantity dimension on feature " + std::to_wstring(feature_id) + L" to family-table item";
        return false;
    }

    display_name = ctx.symbol.empty() || ctx.symbol == resolved_name
        ? resolved_name
        : resolved_name + L" (" + ctx.symbol + L")";
    return true;
}

bool ResolveFeatureAddSpec(core::FtLevelNode &level,
                           const std::wstring &requested_name,
                           std::wstring &resolved_name,
                           std::wstring &display_name,
                           std::wstring &error_out)
{
    resolved_name = Trim(requested_name);
    display_name = resolved_name;
    error_out.clear();

    int feature_id = -1;
    if (!TryParseFeatureIdRef(resolved_name, feature_id)) {
        std::wstring name_resolved = resolved_name;
        std::wstring name_display = display_name;
        std::wstring name_error;
        if (ResolveFeatureBackedItemByName(level, resolved_name, PRO_FAM_FEATURE, FeatureLookupMode::Any, name_resolved, name_display, name_error)) {
            resolved_name = name_resolved;
            display_name = name_display;
            return true;
        }
        return !resolved_name.empty();
    }

    if (level.generic_mdl == nullptr) {
        error_out = L"Current level model is not loaded";
        return false;
    }

    ProFeature feature = {};
    const ProError init_st = ProFeatureInit(reinterpret_cast<ProSolid>(level.generic_mdl), feature_id, &feature);
    if (init_st != PRO_TK_NO_ERROR) {
        error_out = L"Cannot initialize feature id " + std::to_wstring(feature_id);
        return false;
    }

    ProModelitem model_item = feature;
    if (!ResolveModelitemFamtableName(model_item, PRO_FAM_FEATURE, resolved_name, error_out)) {
        error_out = L"Cannot convert feature " + std::to_wstring(feature_id) + L" to family-table item";
        return false;
    }

    display_name = BuildResolvedDisplayName(resolved_name, BestModelitemDisplayName(model_item));
    return true;
}

bool ResolveAssemblyMemberAddSpec(core::FtLevelNode &level,
                                  const std::wstring &requested_name,
                                  std::wstring &resolved_name,
                                  std::wstring &display_name,
                                  std::wstring &error_out)
{
    resolved_name = Trim(requested_name);
    display_name = resolved_name;
    error_out.clear();

    int member_id = -1;
    if (!TryParseAssemblyMemberIdRef(resolved_name, member_id)) {
        std::wstring name_resolved = resolved_name;
        std::wstring name_display = display_name;
        std::wstring name_error;
        if (ResolveFeatureBackedItemByName(level, resolved_name, PRO_FAM_ASMCOMP, FeatureLookupMode::ComponentLike, name_resolved, name_display, name_error)) {
            resolved_name = name_resolved;
            display_name = name_display;
            return true;
        }
        return !resolved_name.empty();
    }

    if (level.generic_mdl == nullptr) {
        error_out = L"Current level model is not loaded";
        return false;
    }

    ProMdlType mdl_type = PRO_MDL_UNUSED;
    if (ProMdlTypeGet(level.generic_mdl, &mdl_type) != PRO_TK_NO_ERROR || mdl_type != PRO_MDL_ASSEMBLY) {
        error_out = L"Assembly member columns can only be resolved from an assembly generic";
        return false;
    }

    ProAsmcomppath path = {};
    ProIdTable ids = {0};
    const ProError path_st = ProAsmcomppathInit(reinterpret_cast<ProSolid>(level.generic_mdl), ids, 0, &path);
    if (path_st != PRO_TK_NO_ERROR) {
        error_out = L"Cannot initialize root assembly component path";
        return false;
    }

    ProAsmitem asm_item = {};
    ProName name = {0};
    const ProError init_st = ProAsmcompAsmitemInit(level.generic_mdl, member_id, PRO_SUB_ASSEMBLY, name, &path, &asm_item);
    if (init_st != PRO_TK_NO_ERROR) {
        error_out = L"Cannot initialize assembly member id " + std::to_wstring(member_id);
        return false;
    }

    if (!ResolveModelitemFamtableName(asm_item.item, PRO_FAM_ASMCOMP, resolved_name, error_out)) {
        error_out = L"Cannot convert assembly member " + std::to_wstring(member_id) + L" to family-table item";
        return false;
    }

    display_name = BuildResolvedDisplayName(resolved_name, BestModelitemDisplayName(asm_item.item));
    return true;
}

bool ResolveUdfAddSpec(core::FtLevelNode &level,
                       const std::wstring &requested_name,
                       std::wstring &resolved_name,
                       std::wstring &display_name,
                       std::wstring &error_out)
{
    resolved_name = Trim(requested_name);
    display_name = resolved_name;
    error_out.clear();

    int udf_id = -1;
    if (!TryParseUdfIdRef(resolved_name, udf_id)) {
        ProGroup *groups_by_name = nullptr;
        const ProError collect_name_st = level.generic_mdl == nullptr
            ? PRO_TK_BAD_INPUTS
            : ProSolidGroupsCollect(reinterpret_cast<ProSolid>(level.generic_mdl), &groups_by_name);
        if (collect_name_st == PRO_TK_NO_ERROR && groups_by_name != nullptr) {
            int group_count = 0;
            if (ProArraySizeGet(reinterpret_cast<ProArray>(groups_by_name), &group_count) == PRO_TK_NO_ERROR) {
                for (int i = 0; i < group_count; ++i) {
                    ProName udf_name = {0};
                    ProName instance_name = {0};
                    if (ProUdfNameGet(&groups_by_name[i], udf_name, instance_name) != PRO_TK_NO_ERROR) {
                        continue;
                    }
                    if (!EqualsLookupText(udf_name, resolved_name)) {
                        continue;
                    }
                    ProModelitem model_item = {};
                    std::memcpy(&model_item, &groups_by_name[i], sizeof(model_item));
                    std::wstring name_error;
                    std::wstring name_resolved;
                    if (ResolveModelitemFamtableName(model_item, PRO_FAM_UDF, name_resolved, name_error)) {
                        resolved_name = name_resolved;
                        display_name = BuildResolvedDisplayName(resolved_name, udf_name);
                        ProArrayFree(reinterpret_cast<ProArray *>(&groups_by_name));
                        return true;
                    }
                }
            }
            ProArrayFree(reinterpret_cast<ProArray *>(&groups_by_name));
        }
        return !resolved_name.empty();
    }

    if (level.generic_mdl == nullptr) {
        error_out = L"Current level model is not loaded";
        return false;
    }

    ProGroup *groups = nullptr;
    const ProError collect_st = ProSolidGroupsCollect(reinterpret_cast<ProSolid>(level.generic_mdl), &groups);
    if (collect_st != PRO_TK_NO_ERROR || groups == nullptr) {
        error_out = L"Cannot collect groups/UDFs from current generic";
        return false;
    }

    int group_count = 0;
    if (ProArraySizeGet(reinterpret_cast<ProArray>(groups), &group_count) != PRO_TK_NO_ERROR) {
        ProArrayFree(reinterpret_cast<ProArray *>(&groups));
        error_out = L"Cannot inspect collected groups/UDFs";
        return false;
    }

    bool found = false;
    bool found_non_udf = false;
    ProGroup udf_group = {};
    ProName udf_name = {0};
    for (int i = 0; i < group_count; ++i) {
        if (groups[i].id != udf_id) {
            continue;
        }
        found = true;
        ProName instance_name = {0};
        if (ProUdfNameGet(&groups[i], udf_name, instance_name) != PRO_TK_NO_ERROR) {
            found_non_udf = true;
            continue;
        }
        udf_group = groups[i];
        found_non_udf = false;
        break;
    }
    ProArrayFree(reinterpret_cast<ProArray *>(&groups));

    if (!found) {
        error_out = L"Cannot find UDF/group id " + std::to_wstring(udf_id);
        return false;
    }
    if (found_non_udf) {
        error_out = L"Group " + std::to_wstring(udf_id) + L" exists but is not a UDF";
        return false;
    }

    ProModelitem model_item = {};
    std::memcpy(&model_item, &udf_group, sizeof(model_item));
    if (!ResolveModelitemFamtableName(model_item, PRO_FAM_UDF, resolved_name, error_out)) {
        error_out = L"Cannot convert UDF " + std::to_wstring(udf_id) + L" to family-table item";
        return false;
    }

    display_name = udf_name[0] == L'\0' ? resolved_name : resolved_name + L" (" + std::wstring(udf_name) + L")";
    return true;
}

bool ResolveFeatureBackedFamtableSpec(core::FtLevelNode &level,
                                      const std::wstring &requested_name,
                                      ProFamtabType expected_type,
                                      const wchar_t *label,
                                      std::wstring &resolved_name,
                                      std::wstring &display_name,
                                      std::wstring &error_out)
{
    resolved_name = Trim(requested_name);
    display_name = resolved_name;
    error_out.clear();

    int feature_id = -1;
    if (!TryParseFeatureIdRef(resolved_name, feature_id)) {
        std::wstring name_resolved = resolved_name;
        std::wstring name_display = display_name;
        std::wstring name_error;
        if (ResolveFeatureBackedItemByName(level, resolved_name, expected_type, FeatureLookupMode::Any, name_resolved, name_display, name_error)) {
            resolved_name = name_resolved;
            display_name = name_display;
            return true;
        }
        return !resolved_name.empty();
    }

    if (level.generic_mdl == nullptr) {
        error_out = L"Current level model is not loaded";
        return false;
    }

    ProFeature feature = {};
    const ProError init_st = ProFeatureInit(reinterpret_cast<ProSolid>(level.generic_mdl), feature_id, &feature);
    if (init_st != PRO_TK_NO_ERROR) {
        error_out = std::wstring(L"Cannot initialize ") + label + L" feature id " + std::to_wstring(feature_id);
        return false;
    }

    ProModelitem model_item = feature;
    if (!ResolveModelitemFamtableName(model_item, expected_type, resolved_name, error_out)) {
        error_out = std::wstring(L"Cannot convert ") + label + L" feature " + std::to_wstring(feature_id) + L" to family-table item";
        return false;
    }

    display_name = BuildResolvedDisplayName(resolved_name, BestModelitemDisplayName(model_item));
    return true;
}

bool AddColumnCells(core::FtLevelNode &level, const core::FtColumn &col)
{
    for (auto &row : level.rows) {
        core::FtCell cell;
        cell.column_key = col.column_key;
        cell.value = L"";
        cell.old_value = L"";
        cell.editable = row.row_kind == core::FtRowKind::Instance && col.editable;
        cell.support_status = col.support_status;
        cell.change_kind = core::FtChangeKind::New;
        cell.value_type = col.value_type;
        row.cells.push_back(cell);
    }
    return true;
}

} // namespace

bool AddFtColumn(core::FtLevelNode &level, const FtAddColumnSpec &spec, std::wstring &error_out)
{
    error_out.clear();
    if (spec.object_name.empty()) {
        error_out = L"Object name/id is empty";
        return false;
    }
    if (!CategoryCanBeAddedFromReplica(spec.category)) {
        error_out = L"This item type is visible in the native-style Add Item flow, but automatic add is not safe yet. Use Tools > Open Native for this type.";
        return false;
    }
    if (spec.category == core::FtColumnCategory::Parameter ||
        spec.category == core::FtColumnCategory::SystemParameter) {
        const std::wstring param_name = Trim(spec.object_name);
        if (param_name.empty()) {
            error_out = L"Parameter name is empty";
            return false;
        }
        const std::wstring key = CategoryPrefix(spec.category) + L":" + param_name;
        for (const auto &col : level.columns) {
            if (col.column_key == key ||
                (spec.category == core::FtColumnCategory::Parameter && col.column_key == L"PARAM:" + param_name)) {
                error_out = L"Column already exists";
                return false;
            }
        }

        core::FtColumn col;
        col.column_key = key;
        col.column_display_name = key;
        col.column_category = spec.category;
        col.value_type = PRO_PARAM_STRING;
        col.value_type_name = L"STRING";
        col.support_status = core::FtSupportStatus::Full;
        col.editable = IsFtColumnEditable(col.column_category, col.support_status);
        col.visible = true;
        col.required = false;
        col.has_creo_item = true;
        col.famtab_type = spec.category == core::FtColumnCategory::SystemParameter
            ? PRO_FAM_SYSTEM_PARAM
            : PRO_FAM_USER_PARAM;
        col.famtab_string = param_name;
        col.change_kind = core::FtChangeKind::New;

        int insert_index = spec.insert_index;
        if (insert_index < 0 || insert_index > static_cast<int>(level.columns.size())) insert_index = static_cast<int>(level.columns.size());
        level.columns.insert(level.columns.begin() + insert_index, col);
        Reindex(level);
        AddColumnCells(level, col);
        return true;
    }

    std::wstring resolved_name = Trim(spec.object_name);
    std::wstring display_name = resolved_name;
    if (spec.category == core::FtColumnCategory::Dimension &&
        !ResolveDimensionAddSpec(level, spec.object_name, resolved_name, display_name, error_out)) {
        if (error_out.empty()) {
            error_out = L"Cannot resolve dimension item";
        }
        return false;
    }
    if (spec.category == core::FtColumnCategory::Feature &&
        !ResolveFeatureAddSpec(level, spec.object_name, resolved_name, display_name, error_out)) {
        if (error_out.empty()) {
            error_out = L"Cannot resolve feature item";
        }
        return false;
    }
    if (spec.category == core::FtColumnCategory::AssemblyMember &&
        !ResolveAssemblyMemberAddSpec(level, spec.object_name, resolved_name, display_name, error_out)) {
        if (error_out.empty()) {
            error_out = L"Cannot resolve assembly member item";
        }
        return false;
    }
    if (spec.category == core::FtColumnCategory::Udf &&
        !ResolveUdfAddSpec(level, spec.object_name, resolved_name, display_name, error_out)) {
        if (error_out.empty()) {
            error_out = L"Cannot resolve UDF item";
        }
        return false;
    }
    if (spec.category == core::FtColumnCategory::ReferenceModel &&
        !ResolveFeatureBackedFamtableSpec(level, spec.object_name, PRO_FAM_EXTERNAL_REFERENCE, L"reference-model", resolved_name, display_name, error_out)) {
        if (error_out.empty()) {
            error_out = L"Cannot resolve reference model item";
        }
        return false;
    }
    if (spec.category == core::FtColumnCategory::MergePart &&
        !ResolveFeatureBackedFamtableSpec(level, spec.object_name, PRO_FAM_MERGE_PART_REF, L"merge-part", resolved_name, display_name, error_out)) {
        if (error_out.empty()) {
            error_out = L"Cannot resolve merge part item";
        }
        return false;
    }

    const std::wstring key = CategoryPrefix(spec.category) + L":" + resolved_name;
    for (const auto &col : level.columns) {
        if (col.column_key == key) {
            error_out = L"Column already exists";
            return false;
        }
    }

    core::FtColumn col;
    col.column_key = key;
    col.column_display_name = display_name.empty() ? key : display_name;
    col.column_category = spec.category;
    col.value_type = ValueTypeForCategory(spec.category);
    col.value_type_name = ValueTypeNameForCategory(spec.category);
    col.support_status = core::FtSupportStatus::Full;
    col.editable = IsFtColumnEditable(col.column_category, col.support_status);
    col.visible = true;
    col.required = false;
    col.has_creo_item = true;
    col.famtab_type = FamtabTypeForCategory(spec.category);
    col.famtab_string = resolved_name;
    col.change_kind = core::FtChangeKind::New;

    if (col.famtab_type == PRO_FAM_TYPE_UNUSED) {
        error_out = L"Unsupported family-table item type";
        return false;
    }

    int insert_index = spec.insert_index;
    if (insert_index < 0 || insert_index > static_cast<int>(level.columns.size())) insert_index = static_cast<int>(level.columns.size());
    level.columns.insert(level.columns.begin() + insert_index, col);
    Reindex(level);
    AddColumnCells(level, col);
    return true;
}

bool AddFtParameterColumn(core::FtLevelNode &level, const std::wstring &param_name, int insert_index, std::wstring &error_out)
{
    const std::wstring trimmed_name = Trim(param_name);
    if (trimmed_name.empty()) { error_out = L"Parameter name is empty"; return false; }
    const std::wstring key = L"PARAM:" + trimmed_name;
    for (const auto &col : level.columns) {
        if (col.column_key == key || col.column_key == L"PARAM:" + trimmed_name) { error_out = L"Column already exists"; return false; }
    }
    core::FtColumn col;
    col.column_key = key;
    col.column_display_name = key;
    col.column_category = core::FtColumnCategory::Parameter;
    col.value_type = PRO_PARAM_STRING;
    col.value_type_name = L"STRING";
    col.support_status = core::FtSupportStatus::Full;
    col.editable = true;
    col.visible = true;
    col.required = false;
    col.has_creo_item = true;
    col.famtab_type = PRO_FAM_USER_PARAM;
    col.famtab_string = trimmed_name;
    col.change_kind = core::FtChangeKind::New;
    if (insert_index < 0 || insert_index > static_cast<int>(level.columns.size())) insert_index = static_cast<int>(level.columns.size());
    level.columns.insert(level.columns.begin() + insert_index, col);
    Reindex(level);
    AddColumnCells(level, col);
    return true;
}

bool DeleteFtColumn(core::FtLevelNode &level, const std::wstring &column_key, std::wstring &error_out)
{
    auto it = std::find_if(level.columns.begin(), level.columns.end(), [&](const core::FtColumn &c){ return c.column_key == column_key; });
    if (it == level.columns.end()) { error_out = L"Column not found"; return false; }
    if (it->column_category == core::FtColumnCategory::Fixed) { error_out = L"Fixed columns cannot be deleted"; return false; }
    if (it->support_status != core::FtSupportStatus::Full) { error_out = L"Only FULL columns can be deleted automatically"; return false; }
    level.columns.erase(it);
    for (auto &row : level.rows) {
        row.cells.erase(std::remove_if(row.cells.begin(), row.cells.end(), [&](const core::FtCell &c){ return c.column_key == column_key; }), row.cells.end());
    }
    Reindex(level);
    return true;
}

bool MoveFtColumn(core::FtLevelNode &level, const std::wstring &column_key, int new_index, std::wstring &error_out)
{
    auto it = std::find_if(level.columns.begin(), level.columns.end(), [&](const core::FtColumn &c){ return c.column_key == column_key; });
    if (it == level.columns.end()) { error_out = L"Column not found"; return false; }
    if (it->column_category == core::FtColumnCategory::Fixed) { error_out = L"Fixed columns cannot be moved"; return false; }
    core::FtColumn col = *it;
    level.columns.erase(it);
    if (new_index < 0) new_index = 0;
    if (new_index > static_cast<int>(level.columns.size())) new_index = static_cast<int>(level.columns.size());
    level.columns.insert(level.columns.begin() + new_index, col);
    Reindex(level);
    return true;
}

} // namespace autobbox::application

