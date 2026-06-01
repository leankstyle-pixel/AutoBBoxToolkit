#include "autobbox/application/sheetmetal_flat_batch.h"

#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"

#include <ProAsmcomppath.h>
#include <ProArray.h>
#include <ProElement.h>
#include <ProElemId.h>
#include <ProFamtable.h>
#include <ProFaminstance.h>
#include <ProFeature.h>
#include <ProFeatType.h>
#include <ProGeomitem.h>
#include <ProMdl.h>
#include <ProModelitem.h>
#include <ProMenu.h>
#include <ProRegularUnbend.h>
#include <ProParamval.h>
#include <ProReference.h>
#include <ProSelection.h>
#include <ProUIDialog.h>
#include <ProSheetmetal.h>
#include <ProSimprep.h>
#include <ProSimprepdata.h>
#include <ProSolid.h>
#include <ProSurface.h>
#include <ProSurfacedata.h>
#include <ProToolkit.h>

#include <algorithm>
#include <cwchar>
#include <cstdarg>
#include <cstdio>
#include <cwctype>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace autobbox::application {
namespace {

constexpr const wchar_t *kFlatRepPrefix = L"AB_FLAT_";

void LogLine(const SheetmetalFlatBatchLogSink &log_sink, const char *fmt, ...)
{
    if (!log_sink || fmt == nullptr) return;
    char buffer[4096] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_sink(buffer);
}

std::wstring UppercaseAscii(const std::wstring &value)
{
    std::wstring out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](wchar_t ch) {
        if (ch >= L'a' && ch <= L'z') return static_cast<wchar_t>(ch - L'a' + L'A');
        return ch;
    });
    return out;
}

bool IsAllowedNameChar(wchar_t ch)
{
    if (ch == L'\0' || std::iswcntrl(ch) != 0) return false;
    switch (ch) {
    case L'\\': case L'/': case L':': case L'*': case L'?': case L'"': case L'<': case L'>': case L'|': case L',': case L';':
        return false;
    default:
        return true;
    }
}

std::wstring SanitizeNameBase(const std::wstring &label)
{
    ProName probe = {0};
    const size_t max_chars = (sizeof(probe) / sizeof(probe[0])) - 1;
    const size_t prefix_len = std::wcslen(kFlatRepPrefix);
    const size_t reserve_for_suffix = 8;
    const size_t max_body = max_chars > prefix_len + reserve_for_suffix ? max_chars - prefix_len - reserve_for_suffix : max_chars;

    std::wstring out;
    for (wchar_t ch : label) {
        if (out.size() >= max_body) break;
        out.push_back(IsAllowedNameChar(ch) ? ch : L'_');
    }
    while (!out.empty() && (out.back() == L'_' || std::iswspace(out.back()) != 0)) out.pop_back();
    while (!out.empty() && (out.front() == L'_' || std::iswspace(out.front()) != 0)) out.erase(out.begin());
    if (out.empty()) out = L"SHEETMETAL";
    return std::wstring(kFlatRepPrefix) + out;
}

bool NameContainsFlat(const std::wstring &name)
{
    return UppercaseAscii(name).find(L"FLAT") != std::wstring::npos;
}

bool FlatNameMatchesTarget(const std::wstring &name_upper, const std::wstring &model_name)
{
    if (name_upper.find(L"FLAT") == std::wstring::npos) return false;

    const std::wstring expected_upper = UppercaseAscii(SanitizeNameBase(model_name));
    if (name_upper == expected_upper || name_upper.rfind(expected_upper + L"_", 0) == 0) return true;

    const size_t prefix_len = std::wcslen(kFlatRepPrefix);
    const std::wstring model_token = expected_upper.size() > prefix_len ? expected_upper.substr(prefix_len) : L"";
    return model_token.size() >= 3 && name_upper.find(model_token) != std::wstring::npos;
}

void CopyWToProName(ProName dest, const std::wstring &src)
{
    const size_t capacity = sizeof(ProName) / sizeof(wchar_t);
    size_t i = 0;
    while (i + 1 < capacity && i < src.size()) {
        dest[i] = src[i];
        ++i;
    }
    if (capacity > 0) dest[i] = L'\0';
}

std::wstring MakeUniqueName(const std::wstring &base, std::set<std::wstring> &existing)
{
    ProName probe = {0};
    const size_t max_chars = (sizeof(probe) / sizeof(probe[0])) - 1;
    std::wstring clean = base.empty() ? std::wstring(kFlatRepPrefix) + L"SHEETMETAL" : base;
    if (clean.size() > max_chars) clean.resize(max_chars);

    for (int i = 0; i < 10000; ++i) {
        std::wstring candidate = clean;
        if (i > 0) {
            const std::wstring suffix = L"_" + std::to_wstring(i);
            if (candidate.size() + suffix.size() > max_chars) candidate.resize(max_chars - suffix.size());
            candidate += suffix;
        }
        if (existing.insert(UppercaseAscii(candidate)).second) return candidate;
    }
    return clean;
}

bool IsSheetmetalModel(ProMdl mdl, ProMdl *generic_out = nullptr, bool *is_flat_instance_out = nullptr)
{
    if (generic_out != nullptr) *generic_out = nullptr;
    if (is_flat_instance_out != nullptr) *is_flat_instance_out = false;
    if (mdl == nullptr || autobbox::creo::ModelType(mdl) != PRO_MDL_PART) return false;

    ProBoolean is_flat_instance = PRO_B_FALSE;
    ProMdl generic = nullptr;
    const ProError st_flat = ProSmtMdlIsFlatStateInstance(mdl, &is_flat_instance, &generic);
    if (st_flat == PRO_TK_NO_ERROR && is_flat_instance == PRO_B_TRUE) {
        if (generic_out != nullptr) *generic_out = generic;
        if (is_flat_instance_out != nullptr) *is_flat_instance_out = true;
        return true;
    }

    ProDimension thickness = {};
    return ProSmtPartThicknessGet(reinterpret_cast<ProPart>(mdl), &thickness) == PRO_TK_NO_ERROR;
}

std::wstring TargetKey(ProMdl mdl)
{
    return UppercaseAscii(autobbox::creo::ModelName(mdl, L"")) + L":" + std::to_wstring(static_cast<int>(autobbox::creo::ModelType(mdl)));
}

std::wstring PathLabel(const ProAsmcomppath &path)
{
    std::wstring out;
    for (int i = 0; i < path.table_num; ++i) {
        if (!out.empty()) out += L"/";
        out += std::to_wstring(path.comp_id_table[i]);
    }
    return out.empty() ? L"<current>" : out;
}

bool TryReadSimprepName(ProSimprep *simprep, std::wstring &name_out)
{
    name_out.clear();
    if (simprep == nullptr) return false;
    ProSimprepdata *data = nullptr;
    if (ProSimprepdataGet(simprep, &data) != PRO_TK_NO_ERROR || data == nullptr) return false;
    ProName name = {0};
    const ProError st = ProSimprepdataNameGet(data, name);
    ProSimprepdataFree(&data);
    if (st != PRO_TK_NO_ERROR || name[0] == L'\0') return false;
    name_out.assign(name);
    return true;
}

ProError CollectSimprepNameAction(ProGeomitem *handle, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || handle == nullptr || app_data == nullptr) return PRO_TK_NO_ERROR;
    auto *names = reinterpret_cast<std::set<std::wstring> *>(app_data);
    ProSimprep *simprep = reinterpret_cast<ProSimprep *>(handle);
    std::wstring name;
    if (TryReadSimprepName(simprep, name) && !name.empty()) names->insert(UppercaseAscii(name));
    return PRO_TK_NO_ERROR;
}

std::set<std::wstring> CollectExistingSimprepNames(ProSolid owner)
{
    std::set<std::wstring> names;
    if (owner != nullptr) ProSolidSimprepVisit(owner, nullptr, CollectSimprepNameAction, &names);
    return names;
}

struct FindSimprepCtx {
    std::wstring wanted_upper;
    ProSimprep found = {};
    bool matched = false;
};

ProError FindSimprepAction(ProGeomitem *handle, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || handle == nullptr || app_data == nullptr) return PRO_TK_NO_ERROR;
    auto *ctx = reinterpret_cast<FindSimprepCtx *>(app_data);
    ProSimprep *simprep = reinterpret_cast<ProSimprep *>(handle);
    std::wstring name;
    if (TryReadSimprepName(simprep, name) && UppercaseAscii(name) == ctx->wanted_upper) {
        ctx->found = *simprep;
        ctx->matched = true;
        return PRO_TK_E_FOUND;
    }
    return PRO_TK_NO_ERROR;
}

bool FindSimprepByName(ProSolid owner, const std::wstring &name, ProSimprep &rep_out)
{
    rep_out = {};
    if (owner == nullptr || name.empty()) return false;
    FindSimprepCtx ctx;
    ctx.wanted_upper = UppercaseAscii(name);
    const ProError st = ProSolidSimprepVisit(owner, nullptr, FindSimprepAction, &ctx);
    if ((st == PRO_TK_E_FOUND || st == PRO_TK_NO_ERROR) && ctx.matched) {
        rep_out = ctx.found;
        return true;
    }
    return false;
}

struct MatchingSimprep {
    ProSimprep rep = {};
    std::wstring name;
};

struct MatchingSimprepCtx {
    const core::SheetmetalFlatTarget *target = nullptr;
    std::vector<MatchingSimprep> *matches = nullptr;
};

ProError CollectMatchingFlatSimprepAction(ProGeomitem *handle, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || handle == nullptr || app_data == nullptr) return PRO_TK_NO_ERROR;
    auto *ctx = reinterpret_cast<MatchingSimprepCtx *>(app_data);
    if (ctx->matches == nullptr) return PRO_TK_NO_ERROR;

    ProSimprep *simprep = reinterpret_cast<ProSimprep *>(handle);

    std::wstring name;
    if (!TryReadSimprepName(simprep, name)) return PRO_TK_NO_ERROR;
    const std::wstring name_upper = UppercaseAscii(name);
    if (NameContainsFlat(name_upper) ||
        (ctx->target != nullptr && FlatNameMatchesTarget(name_upper, ctx->target->model_name))) {
        MatchingSimprep item;
        item.rep = *simprep;
        item.name = name;
        ctx->matches->push_back(item);
    }
    return PRO_TK_NO_ERROR;
}

std::vector<MatchingSimprep> CollectMatchingFlatSimpreps(ProSolid owner, const core::SheetmetalFlatTarget &target)
{
    std::vector<MatchingSimprep> matches;
    if (owner == nullptr) return matches;
    MatchingSimprepCtx ctx;
    ctx.target = &target;
    ctx.matches = &matches;
    ProSolidSimprepVisit(owner, nullptr, CollectMatchingFlatSimprepAction, &ctx);
    return matches;
}

std::vector<MatchingSimprep> CollectAllFlatNamedSimpreps(ProSolid owner)
{
    std::vector<MatchingSimprep> matches;
    if (owner == nullptr) return matches;
    MatchingSimprepCtx ctx;
    ctx.target = nullptr;
    ctx.matches = &matches;
    ProSolidSimprepVisit(owner, nullptr, CollectMatchingFlatSimprepAction, &ctx);
    return matches;
}

struct SimprepIdCtx {
    std::set<int> *ids = nullptr;
};

ProError CollectSimprepIdAction(ProGeomitem *handle, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || handle == nullptr || app_data == nullptr) return PRO_TK_NO_ERROR;
    auto *ctx = reinterpret_cast<SimprepIdCtx *>(app_data);
    if (ctx->ids != nullptr && handle->id > 0) ctx->ids->insert(handle->id);
    return PRO_TK_NO_ERROR;
}

std::set<int> CollectSimprepIds(ProSolid owner)
{
    std::set<int> ids;
    if (owner == nullptr) return ids;
    SimprepIdCtx ctx;
    ctx.ids = &ids;
    ProSolidSimprepVisit(owner, nullptr, CollectSimprepIdAction, &ctx);
    return ids;
}

struct FlatInstanceCtx {
    std::vector<std::wstring> *names = nullptr;
};

ProError VisitFamilyFlatInstance(ProFaminstance *inst, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || inst == nullptr || app_data == nullptr) return PRO_TK_NO_ERROR;
    auto *ctx = reinterpret_cast<FlatInstanceCtx *>(app_data);
    if (ctx->names == nullptr) return PRO_TK_NO_ERROR;
    ProBoolean is_flat = PRO_B_FALSE;
    if ((ProFaminstanceIsFlatState(inst, &is_flat) == PRO_TK_NO_ERROR && is_flat == PRO_B_TRUE) ||
        NameContainsFlat(inst->name)) {
        ctx->names->push_back(inst->name);
    }
    return PRO_TK_NO_ERROR;
}

struct FamilyInstanceNamesCtx {
    std::set<std::wstring> *names = nullptr;
};

ProError VisitFamilyInstanceName(ProFaminstance *inst, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || inst == nullptr || app_data == nullptr) return PRO_TK_NO_ERROR;
    auto *ctx = reinterpret_cast<FamilyInstanceNamesCtx *>(app_data);
    if (ctx->names != nullptr && inst->name[0] != L'\0') ctx->names->insert(UppercaseAscii(inst->name));
    return PRO_TK_NO_ERROR;
}

std::set<std::wstring> CollectExistingFamilyInstanceNames(ProFamtable *famtable)
{
    std::set<std::wstring> names;
    if (famtable == nullptr) return names;
    FamilyInstanceNamesCtx ctx;
    ctx.names = &names;
    ProFamtableInstanceVisit(famtable, VisitFamilyInstanceName, nullptr, &ctx);
    return names;
}

struct FlatPatternFeatureCtx {
    std::vector<int> *ids = nullptr;
    std::vector<ProFeature> *features = nullptr;
    int count = 0;
};

ProError VisitFlatPatternFeature(ProFeature *feature, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || feature == nullptr || app_data == nullptr) return PRO_TK_NO_ERROR;
    auto *ctx = reinterpret_cast<FlatPatternFeatureCtx *>(app_data);
    ProFeattype type = 0;
    if (ProFeatureTypeGet(feature, &type) == PRO_TK_NO_ERROR && type == PRO_FEAT_FLAT_PAT) {
        ++ctx->count;
        if (ctx->ids != nullptr) ctx->ids->push_back(feature->id);
        if (ctx->features != nullptr) ctx->features->push_back(*feature);
    }
    return PRO_TK_NO_ERROR;
}

int CountFlatPatternFeatures(ProMdl mdl)
{
    if (mdl == nullptr || autobbox::creo::ModelType(mdl) != PRO_MDL_PART) return 0;
    FlatPatternFeatureCtx ctx;
    ProSolidFeatVisit(reinterpret_cast<ProSolid>(mdl), VisitFlatPatternFeature, nullptr, &ctx);
    return ctx.count;
}

std::vector<int> CollectFlatPatternFeatureIds(ProMdl mdl)
{
    std::vector<int> ids;
    if (mdl == nullptr || autobbox::creo::ModelType(mdl) != PRO_MDL_PART) return ids;
    FlatPatternFeatureCtx ctx;
    ctx.ids = &ids;
    ProSolidFeatVisit(reinterpret_cast<ProSolid>(mdl), VisitFlatPatternFeature, nullptr, &ctx);
    return ids;
}

std::vector<ProFeature> CollectFlatPatternFeatures(ProMdl mdl)
{
    std::vector<ProFeature> features;
    if (mdl == nullptr || autobbox::creo::ModelType(mdl) != PRO_MDL_PART) return features;
    FlatPatternFeatureCtx ctx;
    ctx.features = &features;
    ProSolidFeatVisit(reinterpret_cast<ProSolid>(mdl), VisitFlatPatternFeature, nullptr, &ctx);
    return features;
}

void RefreshFamilyFlatStatus(core::SheetmetalFlatTarget &target)
{
    target.has_family_table = false;
    target.family_table_modifiable = false;
    target.family_flat_instances.clear();
    target.family_flat_create_supported = false;
    if (target.mdl == nullptr) return;

    ProFamtable famtable = {};
    if (ProFamtableInit(target.mdl, &famtable) != PRO_TK_NO_ERROR) return;
    const ProError st_check = ProFamtableCheck(&famtable);
    if (st_check != PRO_TK_NO_ERROR && st_check != PRO_TK_EMPTY) return;
    target.has_family_table = true;

    ProBoolean can_modify = PRO_B_FALSE;
    if (ProFamtableIsModifiable(&famtable, PRO_B_FALSE, &can_modify) == PRO_TK_NO_ERROR) {
        target.family_table_modifiable = (can_modify == PRO_B_TRUE);
    }
    target.family_flat_create_supported = target.family_table_modifiable;

    FlatInstanceCtx ctx;
    ctx.names = &target.family_flat_instances;
    ProFamtableInstanceVisit(&famtable, VisitFamilyFlatInstance, nullptr, &ctx);
}

void RefreshTargetStatus(core::SheetmetalFlatTarget &target, ProSolid /*simprep_owner*/)
{
    target.has_error = false;
    target.tool_simprep_name.clear();
    target.tool_simprep_names.clear();
    target.has_tool_simprep = false;
    target.simprep_applicable = (target.mdl != nullptr && autobbox::creo::ModelType(target.mdl) == PRO_MDL_PART);

    if (target.simprep_applicable) {
        std::set<std::wstring> names = CollectExistingSimprepNames(reinterpret_cast<ProSolid>(target.mdl));
        for (const std::wstring &name_upper : names) {
            if (FlatNameMatchesTarget(name_upper, target.model_name) || NameContainsFlat(name_upper)) {
                target.has_tool_simprep = true;
                target.tool_simprep_names.push_back(name_upper);
            }
        }
        if (!target.tool_simprep_names.empty()) target.tool_simprep_name = target.tool_simprep_names.front();
    }

    RefreshFamilyFlatStatus(target);
    const int flat_pattern_count = CountFlatPatternFeatures(target.mdl);

    std::wstring status;
    if (target.simprep_applicable) {
        status += target.has_tool_simprep ? L"简化表示: 已有" : L"简化表示: 未有";
    } else {
        status += L"简化表示: 不适用";
    }
    status += L"; 族表: ";
    if (!target.has_family_table) {
        status += L"未有";
    } else if (target.family_flat_instances.empty()) {
        status += L"未有 Flat/平整状态实例";
    } else {
        status += L"Flat/平整状态实例 ";
        for (size_t i = 0; i < target.family_flat_instances.size(); ++i) {
            if (i > 0) status += L", ";
            status += target.family_flat_instances[i];
        }
    }
    status += L"; 展平特征: ";
    status += flat_pattern_count > 0 ? (L"已有 " + std::to_wstring(flat_pattern_count)) : L"未有";
    if (!target.family_flat_create_supported) {
        status += L"; 创建平整状态: 族表不可修改";
    }
    target.status_text = status;
}

struct CollectCtx {
    core::SheetmetalFlatCollectResult *result = nullptr;
    std::map<std::wstring, size_t> index_by_key;
};

void AddTargetForModel(CollectCtx &ctx,
                       ProMdl raw_mdl,
                       const ProAsmcomppath *path,
                       bool current_model)
{
    if (ctx.result == nullptr || raw_mdl == nullptr) return;

    ++ctx.result->visited_components;
    ProMdl generic = nullptr;
    bool is_flat_instance = false;
    if (!IsSheetmetalModel(raw_mdl, &generic, &is_flat_instance)) {
        ++ctx.result->skipped_non_sheetmetal;
        return;
    }

    ProMdl target_mdl = (is_flat_instance && generic != nullptr) ? generic : raw_mdl;
    if (target_mdl == nullptr || autobbox::creo::ModelType(target_mdl) != PRO_MDL_PART) {
        ++ctx.result->skipped_unreadable;
        return;
    }

    const std::wstring key = TargetKey(target_mdl);
    size_t index = 0;
    auto found = ctx.index_by_key.find(key);
    if (found == ctx.index_by_key.end()) {
        core::SheetmetalFlatTarget target;
        target.item_name = "smtflat_" + std::to_string(ctx.result->targets.size());
        target.mdl = target_mdl;
        target.type = autobbox::creo::ModelType(target_mdl);
        target.model_name = autobbox::creo::ModelName(target_mdl, L"");
        target.current_model = current_model;
        target.from_flat_state_instance = is_flat_instance;
        target.flat_generic_mdl = generic;
        target.display_path = current_model ? L"<当前模型>" : L"";
        ctx.result->targets.push_back(target);
        index = ctx.result->targets.size() - 1;
        ctx.index_by_key.emplace(key, index);
    } else {
        index = found->second;
        ++ctx.result->duplicate_components;
    }

    if (path != nullptr) {
        core::SheetmetalFlatOccurrence occurrence;
        occurrence.path = *path;
        occurrence.depth = path->table_num;
        occurrence.path_label = PathLabel(*path);
        ctx.result->targets[index].occurrences.push_back(occurrence);
        if (ctx.result->targets[index].display_path.empty()) {
            ctx.result->targets[index].display_path = occurrence.path_label;
        } else if (ctx.result->targets[index].display_path.find(occurrence.path_label) == std::wstring::npos) {
            ctx.result->targets[index].display_path += L"; ";
            ctx.result->targets[index].display_path += occurrence.path_label;
        }
    }
}

ProError VisitAssemblyComponent(ProAsmcomppath *p_path,
                                ProSolid handle,
                                ProBoolean down,
                                ProAppData app_data)
{
    if (down != PRO_B_TRUE || app_data == nullptr || p_path == nullptr) return PRO_TK_NO_ERROR;
    auto *ctx = reinterpret_cast<CollectCtx *>(app_data);
    ProMdl mdl = reinterpret_cast<ProMdl>(handle);
    if (autobbox::creo::ModelType(mdl) == PRO_MDL_PART) {
        AddTargetForModel(*ctx, mdl, p_path, false);
    }
    return PRO_TK_NO_ERROR;
}

std::wstring FormatStatus(ProError st)
{
    return L"status=" + std::to_wstring(static_cast<int>(st));
}

void FinalizeActionSummary(core::SheetmetalFlatActionResult &result)
{
    result.summary_text = L"请求: " + std::to_wstring(result.requested) +
                          L"\n成功: " + std::to_wstring(result.succeeded) +
                          L"\n失败: " + std::to_wstring(result.failed) +
                          L"\n跳过: " + std::to_wstring(result.skipped);
}

int SelectedCount(const std::vector<core::SheetmetalFlatTarget> &targets)
{
    return static_cast<int>(std::count_if(targets.begin(), targets.end(), [](const auto &t) { return t.selected; }));
}

ProSolid CurrentAssemblyOwner()
{
    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR || current == nullptr) return nullptr;
    if (autobbox::creo::ModelType(current) != PRO_MDL_ASSEMBLY) return nullptr;
    return reinterpret_cast<ProSolid>(current);
}

struct TrueFlatInstanceCtx {
    std::set<std::wstring> *names = nullptr;
};

ProError VisitTrueFamilyFlatInstance(ProFaminstance *inst, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || inst == nullptr || app_data == nullptr) return PRO_TK_NO_ERROR;
    auto *ctx = reinterpret_cast<TrueFlatInstanceCtx *>(app_data);
    if (ctx->names == nullptr) return PRO_TK_NO_ERROR;
    ProBoolean is_flat = PRO_B_FALSE;
    if (ProFaminstanceIsFlatState(inst, &is_flat) == PRO_TK_NO_ERROR && is_flat == PRO_B_TRUE && inst->name[0] != L'\0') {
        ctx->names->insert(UppercaseAscii(inst->name));
    }
    return PRO_TK_NO_ERROR;
}

std::set<std::wstring> CollectTrueFamilyFlatInstanceNames(ProMdl mdl)
{
    std::set<std::wstring> names;
    if (mdl == nullptr) return names;
    ProFamtable famtable = {};
    if (ProFamtableInit(mdl, &famtable) != PRO_TK_NO_ERROR) return names;
    TrueFlatInstanceCtx ctx;
    ctx.names = &names;
    ProFamtableInstanceVisit(&famtable, VisitTrueFamilyFlatInstance, nullptr, &ctx);
    return names;
}

std::set<std::wstring> CollectFamilyFlatInstanceNames(ProMdl mdl)
{
    std::set<std::wstring> names;
    if (mdl == nullptr) return names;
    ProFamtable famtable = {};
    if (ProFamtableInit(mdl, &famtable) != PRO_TK_NO_ERROR) return names;
    std::vector<std::wstring> collected;
    FlatInstanceCtx ctx;
    ctx.names = &collected;
    ProFamtableInstanceVisit(&famtable, VisitFamilyFlatInstance, nullptr, &ctx);
    for (const auto &name : collected) {
        if (!name.empty()) names.insert(UppercaseAscii(name));
    }
    return names;
}

std::set<std::wstring> CollectFlatSimprepNames(ProMdl mdl)
{
    std::set<std::wstring> flat_names;
    if (mdl == nullptr || autobbox::creo::ModelType(mdl) != PRO_MDL_PART) return flat_names;
    const std::set<std::wstring> names = CollectExistingSimprepNames(reinterpret_cast<ProSolid>(mdl));
    for (const auto &name : names) {
        if (NameContainsFlat(name)) flat_names.insert(name);
    }
    return flat_names;
}

std::wstring TruncateToProName(const std::wstring &name)
{
    ProName probe = {0};
    const size_t max_chars = (sizeof(probe) / sizeof(probe[0])) - 1;
    std::wstring out = name;
    if (out.size() > max_chars) out.resize(max_chars);
    return out;
}

std::vector<std::wstring> FlatRepNameCandidates(const core::SheetmetalFlatTarget &target)
{
    std::vector<std::wstring> names;
    const std::wstring model = target.model_name.empty() ? L"SHEETMETAL" : target.model_name;
    names.push_back(TruncateToProName(model + L"_FLAT_REP"));
    names.push_back(TruncateToProName(model + L"_FLAT"));
    names.push_back(TruncateToProName(SanitizeNameBase(model)));
    names.push_back(TruncateToProName(UppercaseAscii(SanitizeNameBase(model))));
    std::set<std::wstring> seen;
    std::vector<std::wstring> unique;
    for (const auto &name : names) {
        if (!name.empty() && seen.insert(UppercaseAscii(name)).second) unique.push_back(name);
    }
    return unique;
}

ProError DeleteSimprepByName(ProSolid owner, const std::wstring &name, bool &deleted)
{
    deleted = false;
    if (owner == nullptr || name.empty()) return PRO_TK_BAD_INPUTS;
    ProName rep_name = {0};
    CopyWToProName(rep_name, name);
    ProSimprep rep = {};
    ProError st = ProSimprepInit(rep_name, PRO_VALUE_UNUSED, owner, &rep);
    if (st != PRO_TK_NO_ERROR) return st;
    st = ProSimprepDelete(&rep);
    if (st == PRO_TK_NO_ERROR || st == PRO_TK_E_NOT_FOUND) deleted = true;
    return st;
}

ProError DeleteSimprepById(ProSolid owner, int rep_id, bool &deleted)
{
    deleted = false;
    if (owner == nullptr || rep_id <= 0) return PRO_TK_BAD_INPUTS;
    ProSimprep rep = {};
    ProError st = ProSimprepInit(nullptr, rep_id, owner, &rep);
    if (st != PRO_TK_NO_ERROR) return st;
    st = ProSimprepDelete(&rep);
    if (st == PRO_TK_NO_ERROR || st == PRO_TK_E_NOT_FOUND) deleted = true;
    return st;
}

bool HasFlatRepByCandidateName(const core::SheetmetalFlatTarget &target)
{
    if (target.mdl == nullptr || autobbox::creo::ModelType(target.mdl) != PRO_MDL_PART) return false;
    ProSolid owner = reinterpret_cast<ProSolid>(target.mdl);
    for (const auto &candidate_name : FlatRepNameCandidates(target)) {
        ProName rep_name = {0};
        CopyWToProName(rep_name, candidate_name);
        ProSimprep rep = {};
        if (ProSimprepInit(rep_name, PRO_VALUE_UNUSED, owner, &rep) == PRO_TK_NO_ERROR) return true;
    }
    return false;
}

std::vector<std::wstring> SetDifference(const std::set<std::wstring> &after, const std::set<std::wstring> &before)
{
    std::vector<std::wstring> diff;
    for (const auto &name : after) {
        if (before.find(name) == before.end()) diff.push_back(name);
    }
    return diff;
}

std::vector<int> SetDifference(const std::set<int> &after, const std::set<int> &before)
{
    std::vector<int> diff;
    for (int id : after) {
        if (before.find(id) == before.end()) diff.push_back(id);
    }
    return diff;
}

std::wstring JoinNames(const std::vector<std::wstring> &names)
{
    std::wstring out;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) out += L", ";
        out += names[i];
    }
    return out;
}

ProError AddChildElement(ProElement parent, ProElement child)
{
    if (parent == nullptr || child == nullptr) return PRO_TK_BAD_INPUTS;
    return ProElemtreeElementAdd(parent, nullptr, child);
}

ProError AddIntElement(ProElement parent, ProElemId id, int value)
{
    ProElement elem = nullptr;
    ProError st = ProElementAlloc(id, &elem);
    if (st != PRO_TK_NO_ERROR) return st;
    st = ProElementIntegerSet(elem, value);
    if (st == PRO_TK_NO_ERROR) st = AddChildElement(parent, elem);
    return st;
}

ProError AddBoolElement(ProElement parent, ProElemId id, ProBoolean value)
{
    ProElement elem = nullptr;
    ProError st = ProElementAlloc(id, &elem);
    if (st != PRO_TK_NO_ERROR) return st;
    st = ProElementBooleanSet(elem, value);
    if (st == PRO_TK_NO_ERROR) st = AddChildElement(parent, elem);
    return st;
}

ProError AddWstringElement(ProElement parent, ProElemId id, const std::wstring &value)
{
    ProElement elem = nullptr;
    ProError st = ProElementAlloc(id, &elem);
    if (st != PRO_TK_NO_ERROR) return st;
    std::wstring mutable_value = value;
    st = ProElementWstringSet(elem, mutable_value.empty() ? const_cast<wchar_t *>(L"") : &mutable_value[0]);
    if (st == PRO_TK_NO_ERROR) st = AddChildElement(parent, elem);
    return st;
}

ProError AddReferenceElement(ProElement parent, ProElemId id, ProSelection selection)
{
    if (selection == nullptr) return PRO_TK_BAD_INPUTS;
    ProElement elem = nullptr;
    ProError st = ProElementAlloc(id, &elem);
    if (st != PRO_TK_NO_ERROR) return st;
    ProReference ref = nullptr;
    st = ProSelectionToReference(selection, &ref);
    if (st == PRO_TK_NO_ERROR) st = ProElementReferenceSet(elem, ref);
    if (st == PRO_TK_NO_ERROR) st = AddChildElement(parent, elem);
    return st;
}

struct FixedSurfaceCtx {
    ProMdl mdl = nullptr;
    ProSurface best = nullptr;
    double best_area = -1.0;
    bool best_is_face = false;
};

ProError VisitCandidateFixedSurface(ProSurface surface, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || surface == nullptr || app_data == nullptr) return PRO_TK_NO_ERROR;
    auto *ctx = reinterpret_cast<FixedSurfaceCtx *>(app_data);
    if (ctx->mdl == nullptr) return PRO_TK_NO_ERROR;

    ProSrftype surface_type = PRO_SRF_NONE;
    if (ProSurfaceTypeGet(surface, &surface_type) != PRO_TK_NO_ERROR || surface_type != PRO_SRF_PLANE) return PRO_TK_NO_ERROR;

    ProBoolean is_datum = PRO_B_FALSE;
    if (ProSurfaceIsDatumPlane(reinterpret_cast<ProSolid>(ctx->mdl), surface, &is_datum) == PRO_TK_NO_ERROR &&
        is_datum == PRO_B_TRUE) {
        return PRO_TK_NO_ERROR;
    }

    ProSmtSurfType smt_type = PRO_SMT_SURF_NON_SMT;
    if (ProSmtSurfaceTypeGet(reinterpret_cast<ProPart>(ctx->mdl), surface, &smt_type) != PRO_TK_NO_ERROR ||
        smt_type == PRO_SMT_SURF_NON_SMT) {
        return PRO_TK_NO_ERROR;
    }

    double area = 0.0;
    if (ProSurfaceAreaEval(surface, &area) != PRO_TK_NO_ERROR) area = 0.0;
    const bool is_face = (smt_type == PRO_SMT_SURF_FACE);
    if (ctx->best == nullptr ||
        (is_face && !ctx->best_is_face) ||
        (is_face == ctx->best_is_face && area > ctx->best_area)) {
        ctx->best = surface;
        ctx->best_area = area;
        ctx->best_is_face = is_face;
    }
    return PRO_TK_NO_ERROR;
}

ProError FindFixedSurfaceSelection(ProMdl mdl, ProAsmcomppath *path, ProSelection *selection_out)
{
    if (selection_out != nullptr) *selection_out = nullptr;
    if (mdl == nullptr || selection_out == nullptr || autobbox::creo::ModelType(mdl) != PRO_MDL_PART) return PRO_TK_BAD_INPUTS;

    FixedSurfaceCtx ctx;
    ctx.mdl = mdl;
    ProSolidSurfaceVisit(reinterpret_cast<ProSolid>(mdl), VisitCandidateFixedSurface, nullptr, &ctx);
    if (ctx.best == nullptr) return PRO_TK_E_NOT_FOUND;

    int surface_id = -1;
    ProError st = ProSurfaceIdGet(ctx.best, &surface_id);
    if (st != PRO_TK_NO_ERROR || surface_id <= 0) return st == PRO_TK_NO_ERROR ? PRO_TK_E_NOT_FOUND : st;

    ProGeomitem geomitem = {};
    geomitem.owner = mdl;
    geomitem.id = surface_id;
    geomitem.type = PRO_SURFACE;
    return ProSelectionAlloc(path, &geomitem, selection_out);
}

ProError BuildFlatPatternElemtree(const core::SheetmetalFlatTarget &target,
                                  ProSelection fixed_surface_selection,
                                  ProElement *tree_out)
{
    if (tree_out != nullptr) *tree_out = nullptr;
    if (tree_out == nullptr || fixed_surface_selection == nullptr) return PRO_TK_BAD_INPUTS;

    ProElement root = nullptr;
    ProError st = ProElementAlloc(PRO_E_FEATURE_TREE, &root);
    if (st != PRO_TK_NO_ERROR) return st;

    std::set<std::wstring> existing_names;
    const std::wstring feature_name = MakeUniqueName(SanitizeNameBase(target.model_name), existing_names);

    if (st == PRO_TK_NO_ERROR) st = AddIntElement(root, PRO_E_FEATURE_TYPE, PRO_FEAT_FLAT_PAT);
    if (st == PRO_TK_NO_ERROR) st = AddWstringElement(root, PRO_E_STD_FEATURE_NAME, feature_name);
    if (st == PRO_TK_NO_ERROR) st = AddIntElement(root, PRO_E_SMT_UNBEND_TYPE, PRO_SMT_FLAT_PATTERN);
    if (st == PRO_TK_NO_ERROR) st = AddIntElement(root, PRO_E_SMT_UNBEND_SUB_TYPE, PRO_UNBEND_ALL);

    ProElement fixed_geom = nullptr;
    if (st == PRO_TK_NO_ERROR) st = ProElementAlloc(PRO_E_SMT_PRIMARY_FIXED_GEOM, &fixed_geom);
    if (st == PRO_TK_NO_ERROR) st = AddReferenceElement(fixed_geom, PRO_E_SMT_FIXED_REF, fixed_surface_selection);
    if (st == PRO_TK_NO_ERROR) st = AddIntElement(fixed_geom, PRO_E_SMT_FIXED_REF_SIDE, PRO_SMT_FIXED_SIDE_UNDEF);
    if (st == PRO_TK_NO_ERROR) st = AddChildElement(root, fixed_geom);

    if (st == PRO_TK_NO_ERROR) st = AddBoolElement(root, PRO_E_SMT_FLATTEN_FORM_WALLS, PRO_B_TRUE);
    if (st == PRO_TK_NO_ERROR) st = AddBoolElement(root, PRO_E_SMT_FLATTEN_ALL_FORMS, PRO_B_TRUE);
    if (st == PRO_TK_NO_ERROR) st = AddBoolElement(root, PRO_E_SMT_FLATTEN_PROJ_CUTS, PRO_B_TRUE);
    if (st == PRO_TK_NO_ERROR) st = AddBoolElement(root, PRO_E_SMT_MERGE_SAME_SIDES, PRO_B_TRUE);

    if (st != PRO_TK_NO_ERROR) {
        if (root != nullptr) ProElementFree(&root);
        return st;
    }
    *tree_out = root;
    return PRO_TK_NO_ERROR;
}

ProError CreateFlatPatternFeature(const core::SheetmetalFlatTarget &target, std::wstring &created_label, int *created_id_out = nullptr)
{
    created_label.clear();
    if (created_id_out != nullptr) *created_id_out = -1;
    if (target.mdl == nullptr || autobbox::creo::ModelType(target.mdl) != PRO_MDL_PART) return PRO_TK_BAD_INPUTS;

    ProAsmcomppath *path = nullptr;
    ProAsmcomppath occurrence_path = {};
    if (!target.current_model && !target.occurrences.empty()) {
        occurrence_path = target.occurrences.front().path;
        path = &occurrence_path;
    }

    ProSelection fixed_surface_selection = nullptr;
    ProSelection model_selection = nullptr;
    ProElement tree = nullptr;
    ProFeatureCreateOptions *create_options = nullptr;
    ProErrorlist errors = {};
    ProFeature created_feature = {};

    ProError st = FindFixedSurfaceSelection(target.mdl, path, &fixed_surface_selection);
    if (st == PRO_TK_NO_ERROR) st = BuildFlatPatternElemtree(target, fixed_surface_selection, &tree);

    ProModelitem model_item = {};
    if (st == PRO_TK_NO_ERROR) st = ProMdlToModelitem(target.mdl, &model_item);
    if (st == PRO_TK_NO_ERROR) st = ProSelectionAlloc(path, &model_item, &model_selection);
    if (st == PRO_TK_NO_ERROR) st = ProArrayAlloc(1, sizeof(ProFeatureCreateOptions), 1, reinterpret_cast<ProArray *>(&create_options));
    if (st == PRO_TK_NO_ERROR) create_options[0] = PRO_FEAT_CR_NO_OPTS;
    if (st == PRO_TK_NO_ERROR) {
        st = ProFeatureWithoptionsCreate(model_selection,
                                         tree,
                                         create_options,
                                         PRO_REGEN_NO_FLAGS,
                                         &created_feature,
                                         &errors);
    }

    if (st == PRO_TK_NO_ERROR) {
        created_label = L"Flat Pattern feature id " + std::to_wstring(created_feature.id);
        if (created_id_out != nullptr) *created_id_out = created_feature.id;
    }

    if (create_options != nullptr) ProArrayFree(reinterpret_cast<ProArray *>(&create_options));
    if (tree != nullptr) ProElementFree(&tree);
    if (model_selection != nullptr) ProSelectionFree(&model_selection);
    if (fixed_surface_selection != nullptr) ProSelectionFree(&fixed_surface_selection);
    return st;
}

ProError SuppressFlatPatternFeatures(ProMdl mdl,
                                     const std::vector<int> &feature_ids,
                                     int *suppressed_count_out = nullptr)
{
    if (suppressed_count_out != nullptr) *suppressed_count_out = 0;
    if (mdl == nullptr || autobbox::creo::ModelType(mdl) != PRO_MDL_PART) return PRO_TK_BAD_INPUTS;

    std::vector<int> to_suppress;
    for (int id : feature_ids) {
        if (id <= 0) continue;
        ProFeature feature = {};
        if (ProFeatureInit(reinterpret_cast<ProSolid>(mdl), id, &feature) != PRO_TK_NO_ERROR) continue;
        ProFeatStatus status = static_cast<ProFeatStatus>(PRO_FEAT_STAT_INVALID);
        if (ProFeatureStatusGet(&feature, &status) == PRO_TK_NO_ERROR && status == PRO_FEAT_SUPPRESSED) {
            continue;
        }
        to_suppress.push_back(id);
    }
    if (to_suppress.empty()) return PRO_TK_NO_ERROR;

    ProFeatureDeleteOptions suppress_opts[1] = {PRO_FEAT_DELETE_NO_OPTS};
    const ProError st = ProFeatureSuppress(reinterpret_cast<ProSolid>(mdl),
                                           to_suppress.data(),
                                           static_cast<int>(to_suppress.size()),
                                           suppress_opts,
                                           1);
    if (st == PRO_TK_NO_ERROR && suppressed_count_out != nullptr) {
        *suppressed_count_out = static_cast<int>(to_suppress.size());
    }
    return st;
}

ProError EnsureFlatPatternFeatureSuppressed(const core::SheetmetalFlatTarget &target,
                                            std::vector<int> &feature_ids_out,
                                            bool &created_feature_out,
                                            std::wstring &detail_out)
{
    feature_ids_out.clear();
    created_feature_out = false;
    detail_out.clear();
    if (target.mdl == nullptr || autobbox::creo::ModelType(target.mdl) != PRO_MDL_PART) return PRO_TK_BAD_INPUTS;

    feature_ids_out = CollectFlatPatternFeatureIds(target.mdl);
    if (feature_ids_out.empty()) {
        int created_id = -1;
        std::wstring created_label;
        const ProError create_st = CreateFlatPatternFeature(target, created_label, &created_id);
        if (create_st != PRO_TK_NO_ERROR) {
            detail_out = L"create flat pattern failed " + FormatStatus(create_st);
            return create_st;
        }
        created_feature_out = true;
        if (created_id > 0) feature_ids_out.push_back(created_id);
        if (feature_ids_out.empty()) feature_ids_out = CollectFlatPatternFeatureIds(target.mdl);
        detail_out = created_label;
    } else {
        detail_out = L"existing Flat Pattern feature count " + std::to_wstring(feature_ids_out.size());
    }

    int suppressed_count = 0;
    const ProError suppress_st = SuppressFlatPatternFeatures(target.mdl, feature_ids_out, &suppressed_count);
    if (suppress_st != PRO_TK_NO_ERROR) {
        detail_out += L"; suppress flat pattern failed " + FormatStatus(suppress_st);
        return suppress_st;
    }
    detail_out += L"; suppressed/reserved in generic " + std::to_wstring(suppressed_count);
    return PRO_TK_NO_ERROR;
}

ProError BuildFlatPatternFamtableItem(ProMdl mdl, int feature_id, ProFamtableItem &item_out)
{
    std::memset(&item_out, 0, sizeof(item_out));
    if (mdl == nullptr || feature_id <= 0 || autobbox::creo::ModelType(mdl) != PRO_MDL_PART) return PRO_TK_BAD_INPUTS;
    ProFeature feature = {};
    ProError st = ProFeatureInit(reinterpret_cast<ProSolid>(mdl), feature_id, &feature);
    if (st != PRO_TK_NO_ERROR) return st;
    ProModelitem model_item = feature;
    st = ProModelitemToFamtableItem(&model_item, &item_out);
    if (st == PRO_TK_NO_ERROR && item_out.type != PRO_FAM_FEATURE) return PRO_TK_BAD_INPUTS;
    return st;
}

ProError SetFamilyFeatureCell(ProFaminstance &inst, ProFamtableItem &item, bool yes)
{
    ProParamvalue value = {};
    ProLine line = {0};
    line[0] = yes ? L'Y' : L'N';
    line[1] = L'\0';
    ProError st = ProParamvalueSet(&value, line, PRO_PARAM_STRING);
    if (st != PRO_TK_NO_ERROR) return st;
    return ProFaminstanceValueSet(&inst, &item, &value);
}

ProError CreateFamilyFlatInstanceForTarget(core::SheetmetalFlatTarget &target,
                                           std::wstring &created_name_out,
                                           std::wstring &detail_out)
{
    created_name_out.clear();
    detail_out.clear();
    if (target.mdl == nullptr || autobbox::creo::ModelType(target.mdl) != PRO_MDL_PART) return PRO_TK_BAD_INPUTS;

    const std::set<std::wstring> existing_flat = CollectFamilyFlatInstanceNames(target.mdl);
    if (!existing_flat.empty()) {
        detail_out = L"flat-state family instance already exists: " +
                     JoinNames(std::vector<std::wstring>(existing_flat.begin(), existing_flat.end()));
        return PRO_TK_E_FOUND;
    }

    std::vector<int> flat_feature_ids;
    bool created_feature = false;
    std::wstring ensure_detail;
    ProError st = EnsureFlatPatternFeatureSuppressed(target, flat_feature_ids, created_feature, ensure_detail);
    if (st != PRO_TK_NO_ERROR) {
        detail_out = ensure_detail;
        return st;
    }
    if (flat_feature_ids.empty()) {
        detail_out = L"no Flat Pattern feature id available after ensure";
        return PRO_TK_E_NOT_FOUND;
    }

    ProFamtable famtable = {};
    st = ProFamtableInit(target.mdl, &famtable);
    if (st != PRO_TK_NO_ERROR) {
        detail_out = L"family table init failed " + FormatStatus(st);
        return st;
    }
    const ProError check_st = ProFamtableCheck(&famtable);
    if (check_st != PRO_TK_NO_ERROR && check_st != PRO_TK_EMPTY) {
        detail_out = L"family table check failed " + FormatStatus(check_st);
        return check_st;
    }
    ProBoolean can_modify = PRO_B_FALSE;
    st = ProFamtableIsModifiable(&famtable, PRO_B_FALSE, &can_modify);
    if (st != PRO_TK_NO_ERROR || can_modify != PRO_B_TRUE) {
        detail_out = L"family table is not modifiable " + FormatStatus(st);
        return st == PRO_TK_NO_ERROR ? PRO_TK_NOT_VALID : st;
    }

    ProFamtableItem flat_item = {};
    st = BuildFlatPatternFamtableItem(target.mdl, flat_feature_ids.front(), flat_item);
    if (st != PRO_TK_NO_ERROR) {
        detail_out = L"flat pattern family-table item conversion failed " + FormatStatus(st);
        return st;
    }
    st = ProFamtableItemAdd(&famtable, &flat_item);
    if (st != PRO_TK_NO_ERROR && st != PRO_TK_E_FOUND && st != PRO_TK_NO_CHANGE) {
        detail_out = L"add flat pattern column failed " + FormatStatus(st);
        return st;
    }

    std::set<std::wstring> existing_names = CollectExistingFamilyInstanceNames(&famtable);
    const std::wstring instance_name = MakeUniqueName(SanitizeNameBase(target.model_name), existing_names);
    ProName inst_name = {0};
    CopyWToProName(inst_name, instance_name);
    ProFaminstance inst = {};
    st = ProFaminstanceInit(inst_name, &famtable, &inst);
    if (st != PRO_TK_NO_ERROR) {
        detail_out = L"family instance init failed " + FormatStatus(st);
        return st;
    }
    st = ProFaminstanceAdd(&inst);
    if (st != PRO_TK_NO_ERROR && st != PRO_TK_E_FOUND) {
        detail_out = L"family instance add failed " + FormatStatus(st);
        return st;
    }

    st = SetFamilyFeatureCell(inst, flat_item, true);
    if (st != PRO_TK_NO_ERROR) {
        detail_out = L"set flat pattern feature column to YES failed " + FormatStatus(st);
        return st;
    }

    created_name_out = instance_name;
    detail_out = ensure_detail + L"; flat feature column=YES";
    ProMdlSave(target.mdl);
    return PRO_TK_NO_ERROR;
}

bool CreateSheetmetalFlatPatternFeatures(std::vector<core::SheetmetalFlatTarget> &targets,
                                         core::SheetmetalFlatAction action,
                                         core::SheetmetalFlatActionResult &result,
                                         const SheetmetalFlatBatchLogSink &log_sink,
                                         const SheetmetalFlatBatchCompletionSink &completion_sink)
{
    result = {};
    result.action = action;
    result.requested = SelectedCount(targets);

    for (auto &target : targets) {
        if (!target.selected) continue;
        if (target.mdl == nullptr || autobbox::creo::ModelType(target.mdl) != PRO_MDL_PART) {
            ++result.failed;
            target.has_error = true;
            target.status_text = L"Target is not a part model.";
            continue;
        }

        const std::set<std::wstring> existing = CollectFamilyFlatInstanceNames(target.mdl);
        if (!existing.empty()) {
            ++result.skipped;
            target.has_error = false;
            target.status_text = L"Skipped: flat-state family instance already exists: " +
                                 JoinNames(std::vector<std::wstring>(existing.begin(), existing.end()));
            LogLine(log_sink,
                    "sheetmetal-flat create-family-instance skip model=%s reason=already-has-flat-family count=%d",
                    autobbox::common::WToA(target.model_name.c_str()).c_str(),
                    static_cast<int>(existing.size()));
            continue;
        }

        std::wstring created_name;
        std::wstring detail;
        const ProError st = CreateFamilyFlatInstanceForTarget(target, created_name, detail);
        if (st == PRO_TK_NO_ERROR) {
            ++result.succeeded;
            target.has_error = false;
            target.status_text = L"Created flat instance: " + created_name + L"; " + detail;
            LogLine(log_sink,
                    "sheetmetal-flat create-family-instance ok model=%s instance=%s detail=%s",
                    autobbox::common::WToA(target.model_name.c_str()).c_str(),
                    autobbox::common::WToA(created_name.c_str()).c_str(),
                    autobbox::common::WToA(detail.c_str()).c_str());
        } else {
            ++result.failed;
            target.has_error = true;
            target.status_text = L"Create flat instance failed " + FormatStatus(st) +
                                 (detail.empty() ? L"" : L"; " + detail);
            LogLine(log_sink,
                    "sheetmetal-flat create-family-instance failed model=%s status=%d detail=%s",
                    autobbox::common::WToA(target.model_name.c_str()).c_str(),
                    static_cast<int>(st),
                    autobbox::common::WToA(detail.c_str()).c_str());
        }
        RefreshTargetStatus(target, nullptr);
    }

    FinalizeActionSummary(result);
    if (completion_sink) completion_sink(result);
    return result.failed == 0;
}

enum class FlatQueuePhase {
    StartTarget,
    AfterPreview,
    AfterCommand,
    Verify,
};

struct FlatCreationQueue {
    bool active = false;
    core::SheetmetalFlatAction action = core::SheetmetalFlatAction::CreateModelFlatRep;
    std::vector<core::SheetmetalFlatTarget> targets;
    size_t index = 0;
    int verify_attempts = 0;
    FlatQueuePhase phase = FlatQueuePhase::StartTarget;
    std::set<std::wstring> before_names;
    std::set<int> before_ids;
    core::SheetmetalFlatActionResult result;
    ProMdl restore_mdl = nullptr;
    ProUITimerID timer_id = nullptr;
    bool timer_created = false;
    SheetmetalFlatBatchLogSink log_sink;
    SheetmetalFlatBatchCompletionSink completion_sink;
};

FlatCreationQueue g_flat_queue;

const char *QueueActionName(core::SheetmetalFlatAction action)
{
    return action == core::SheetmetalFlatAction::CreateFamilyFlat ? "family-flat" : "model-flat-rep";
}

std::wstring QueueActionDoneLabel(core::SheetmetalFlatAction action)
{
    return action == core::SheetmetalFlatAction::CreateFamilyFlat ? L"flat-state family instance" : L"flat-state model representation";
}

void FlatCreationTimerAction(char *dialog, ProUITimerID timer_id, ProAppData appdata);

bool EnsureFlatQueueTimer()
{
    if (g_flat_queue.timer_created && g_flat_queue.timer_id != nullptr) return true;
    ProName timer_name = {0};
    CopyWToProName(timer_name, L"ABSMTFLATQUEUE");
    const ProError st = ProUITimerCreate(FlatCreationTimerAction, nullptr, timer_name, &g_flat_queue.timer_id);
    if (st == PRO_TK_NO_ERROR) {
        g_flat_queue.timer_created = true;
        return true;
    }
    LogLine(g_flat_queue.log_sink, "sheetmetal-flat queue timer-create failed status=%d", static_cast<int>(st));
    return false;
}

bool ScheduleFlatQueue(int duration_ms)
{
    if (!EnsureFlatQueueTimer()) return false;
    const int duration = std::max(100, duration_ms);
    const ProError st = ProUIDialogTimerStart(const_cast<char *>("main_dlg_cur"),
                                             g_flat_queue.timer_id,
                                             duration,
                                             PRO_B_FALSE);
    if (st != PRO_TK_NO_ERROR) {
        LogLine(g_flat_queue.log_sink, "sheetmetal-flat queue timer-start failed status=%d", static_cast<int>(st));
        return false;
    }
    return true;
}

ProError LoadOfficialMacro(const std::wstring &macro, const char *tag)
{
    if (macro.empty()) return PRO_TK_BAD_INPUTS;
    std::wstring mutable_macro = macro;
    const ProError st = ProMacroLoad(&mutable_macro[0]);
    LogLine(g_flat_queue.log_sink, "sheetmetal-flat queue macro tag=%s status=%d", tag == nullptr ? "" : tag, static_cast<int>(st));
    return st;
}

std::wstring OfficialPreviewMacro()
{
    return L"~ Command `ProCmdSmtFlatPatView`  1;\n";
}

std::wstring OfficialCreateMacro(core::SheetmetalFlatAction action)
{
    if (action == core::SheetmetalFlatAction::CreateFamilyFlat) {
        return L"~ Command `ProCmdSmtFlatViewMakeInst@context_main_dlg_cur|layGr_accs_proe_win`;\n";
    }
    return L"~ Command `ProCmdSmtFlatViewMakeConf@context_main_dlg_cur|layGr_accs_proe_win`;\n";
}

std::wstring OfficialCreateFallbackMacro(core::SheetmetalFlatAction action)
{
    if (action == core::SheetmetalFlatAction::CreateFamilyFlat) {
        return L"~ Command `ProCmdSmtFlatViewMakeInst`;\n";
    }
    return L"~ Command `ProCmdSmtFlatViewMakeConf`;\n";
}

std::wstring OfficialOduiAcceptMacro()
{
    return L"~ Activate `Odui_Dlg_00` `stdbtn_1`;\n";
}

void CompleteFlatQueue()
{
    if (!g_flat_queue.active) return;
    if (g_flat_queue.restore_mdl != nullptr) {
        ProMdlDisplay(g_flat_queue.restore_mdl);
    }
    FinalizeActionSummary(g_flat_queue.result);
    LogLine(g_flat_queue.log_sink,
            "sheetmetal-flat queue complete action=%s requested=%d succeeded=%d failed=%d skipped=%d",
            QueueActionName(g_flat_queue.action),
            g_flat_queue.result.requested,
            g_flat_queue.result.succeeded,
            g_flat_queue.result.failed,
            g_flat_queue.result.skipped);
    auto completion = g_flat_queue.completion_sink;
    const core::SheetmetalFlatActionResult final_result = g_flat_queue.result;
    g_flat_queue.active = false;
    g_flat_queue.targets.clear();
    g_flat_queue.before_names.clear();
    g_flat_queue.before_ids.clear();
    g_flat_queue.log_sink = nullptr;
    g_flat_queue.completion_sink = nullptr;
    if (completion) completion(final_result);
}

void FailCurrentQueueTarget(core::SheetmetalFlatTarget &target, const std::wstring &message)
{
    ++g_flat_queue.result.failed;
    target.has_error = true;
    target.status_text = message;
    LogLine(g_flat_queue.log_sink,
            "sheetmetal-flat queue target-failed action=%s model=%s message=%s",
            QueueActionName(g_flat_queue.action),
            autobbox::common::WToA(target.model_name.c_str()).c_str(),
            autobbox::common::WToA(message.c_str()).c_str());
    ++g_flat_queue.index;
    g_flat_queue.phase = FlatQueuePhase::StartTarget;
    ScheduleFlatQueue(200);
}

bool StartOfficialFlatCreationQueue(std::vector<core::SheetmetalFlatTarget> &targets,
                                    core::SheetmetalFlatAction action,
                                    core::SheetmetalFlatActionResult &result,
                                    const SheetmetalFlatBatchLogSink &log_sink,
                                    const SheetmetalFlatBatchCompletionSink &completion_sink)
{
    result = {};
    result.action = action;
    result.requested = SelectedCount(targets);
    if (g_flat_queue.active) {
        result.failed = result.requested;
        result.summary_text = L"Another sheetmetal flat creation queue is already running.";
        LogLine(log_sink, "sheetmetal-flat queue start rejected reason=already-active action=%s", QueueActionName(action));
        return false;
    }

    ProUITimerID existing_timer_id = g_flat_queue.timer_id;
    const bool existing_timer_created = g_flat_queue.timer_created;
    g_flat_queue = FlatCreationQueue{};
    g_flat_queue.timer_id = existing_timer_id;
    g_flat_queue.timer_created = existing_timer_created;
    g_flat_queue.active = true;
    g_flat_queue.action = action;
    g_flat_queue.log_sink = log_sink;
    g_flat_queue.completion_sink = completion_sink;
    g_flat_queue.result = result;
    ProMdlCurrentGet(&g_flat_queue.restore_mdl);

    for (const auto &target : targets) {
        if (target.selected) g_flat_queue.targets.push_back(target);
    }
    if (g_flat_queue.targets.empty()) {
        g_flat_queue.active = false;
        result.summary_text = L"No selected targets.";
        return false;
    }

    if (!EnsureFlatQueueTimer() || !ScheduleFlatQueue(100)) {
        result.failed = result.requested;
        result.summary_text = L"Failed to start Creo timer for sheetmetal flat creation queue.";
        g_flat_queue.active = false;
        return false;
    }

    result.summary_text = L"Queued " + std::to_wstring(result.requested) + L" target(s) for official Creo flat-state creation. See report for final verification.";
    LogLine(log_sink,
            "sheetmetal-flat queue start action=%s requested=%d",
            QueueActionName(action),
            result.requested);
    return true;
}

void FlatCreationTimerAction(char *, ProUITimerID, ProAppData)
{
    if (!g_flat_queue.active) return;
    if (g_flat_queue.index >= g_flat_queue.targets.size()) {
        CompleteFlatQueue();
        return;
    }

    auto &target = g_flat_queue.targets[g_flat_queue.index];
    if (target.mdl == nullptr || autobbox::creo::ModelType(target.mdl) != PRO_MDL_PART) {
        FailCurrentQueueTarget(target, L"Target is not a part model.");
        return;
    }

    if (g_flat_queue.phase == FlatQueuePhase::StartTarget) {
        RefreshTargetStatus(target, nullptr);
        if (g_flat_queue.action == core::SheetmetalFlatAction::CreateFamilyFlat) {
            const std::set<std::wstring> existing = CollectFamilyFlatInstanceNames(target.mdl);
            if (!existing.empty()) {
                ++g_flat_queue.result.skipped;
                target.status_text = L"Skipped: true flat-state family instance already exists.";
                target.has_error = false;
                LogLine(g_flat_queue.log_sink,
                        "sheetmetal-flat queue skip action=family-flat model=%s reason=already-has-true-flat-family count=%d",
                        autobbox::common::WToA(target.model_name.c_str()).c_str(),
                        static_cast<int>(existing.size()));
                ++g_flat_queue.index;
                ScheduleFlatQueue(100);
                return;
            }
            g_flat_queue.before_names = existing;
            g_flat_queue.before_ids.clear();
        } else {
            const std::set<std::wstring> existing = CollectFlatSimprepNames(target.mdl);
            g_flat_queue.before_ids = CollectSimprepIds(reinterpret_cast<ProSolid>(target.mdl));
            if (!existing.empty() || HasFlatRepByCandidateName(target)) {
                ++g_flat_queue.result.skipped;
                target.status_text = L"Skipped: flat-state model representation already exists.";
                target.has_error = false;
                LogLine(g_flat_queue.log_sink,
                        "sheetmetal-flat queue skip action=model-flat-rep model=%s reason=already-has-flat-rep count=%d",
                        autobbox::common::WToA(target.model_name.c_str()).c_str(),
                        static_cast<int>(existing.size()));
                ++g_flat_queue.index;
                ScheduleFlatQueue(100);
                return;
            }
            g_flat_queue.before_names = existing;
        }

        std::vector<int> flat_feature_ids;
        bool created_feature = false;
        std::wstring ensure_detail;
        const ProError ensure_st = EnsureFlatPatternFeatureSuppressed(target, flat_feature_ids, created_feature, ensure_detail);
        if (ensure_st != PRO_TK_NO_ERROR) {
            FailCurrentQueueTarget(target, L"Ensure Flat Pattern feature failed " + FormatStatus(ensure_st) +
                                           (ensure_detail.empty() ? L"" : L"; " + ensure_detail));
            return;
        }
        LogLine(g_flat_queue.log_sink,
                "sheetmetal-flat queue ensure-flat-feature action=%s model=%s created=%d ids=%d detail=%s",
                QueueActionName(g_flat_queue.action),
                autobbox::common::WToA(target.model_name.c_str()).c_str(),
                created_feature ? 1 : 0,
                static_cast<int>(flat_feature_ids.size()),
                autobbox::common::WToA(ensure_detail.c_str()).c_str());

        const ProError display_st = ProMdlDisplay(target.mdl);
        if (display_st != PRO_TK_NO_ERROR) {
            FailCurrentQueueTarget(target, L"ProMdlDisplay failed " + FormatStatus(display_st));
            return;
        }
        const ProError macro_st = LoadOfficialMacro(OfficialPreviewMacro(), "preview");
        if (macro_st != PRO_TK_NO_ERROR) {
            FailCurrentQueueTarget(target, L"Failed to load official flat preview command " + FormatStatus(macro_st));
            return;
        }
        LogLine(g_flat_queue.log_sink,
                "sheetmetal-flat queue target-start action=%s model=%s index=%d total=%d",
                QueueActionName(g_flat_queue.action),
                autobbox::common::WToA(target.model_name.c_str()).c_str(),
                static_cast<int>(g_flat_queue.index + 1),
                static_cast<int>(g_flat_queue.targets.size()));
        g_flat_queue.phase = FlatQueuePhase::AfterPreview;
        ScheduleFlatQueue(1800);
        return;
    }

    if (g_flat_queue.phase == FlatQueuePhase::AfterPreview) {
        ProError macro_st = LoadOfficialMacro(OfficialCreateMacro(g_flat_queue.action), "create");
        if (macro_st != PRO_TK_NO_ERROR) {
            macro_st = LoadOfficialMacro(OfficialCreateFallbackMacro(g_flat_queue.action), "create-fallback");
        }
        if (macro_st != PRO_TK_NO_ERROR) {
            FailCurrentQueueTarget(target, L"Failed to load official flat create command " + FormatStatus(macro_st));
            return;
        }
        g_flat_queue.phase = FlatQueuePhase::AfterCommand;
        ScheduleFlatQueue(900);
        return;
    }

    if (g_flat_queue.phase == FlatQueuePhase::AfterCommand) {
        LoadOfficialMacro(OfficialOduiAcceptMacro(), "odui-ok");
        g_flat_queue.verify_attempts = 0;
        g_flat_queue.phase = FlatQueuePhase::Verify;
        ScheduleFlatQueue(3000);
        return;
    }

    if (g_flat_queue.phase == FlatQueuePhase::Verify) {
        std::set<std::wstring> after_names = g_flat_queue.action == core::SheetmetalFlatAction::CreateFamilyFlat
                                                ? CollectFamilyFlatInstanceNames(target.mdl)
                                                : CollectFlatSimprepNames(target.mdl);
        const std::vector<std::wstring> created = SetDifference(after_names, g_flat_queue.before_names);
        std::set<int> after_ids;
        std::vector<int> created_ids;
        if (g_flat_queue.action == core::SheetmetalFlatAction::CreateModelFlatRep) {
            after_ids = CollectSimprepIds(reinterpret_cast<ProSolid>(target.mdl));
            created_ids = SetDifference(after_ids, g_flat_queue.before_ids);
            ProMdl current = nullptr;
            if (ProMdlCurrentGet(&current) == PRO_TK_NO_ERROR && current != nullptr &&
                autobbox::creo::ModelType(current) == PRO_MDL_PART &&
                TargetKey(current) == TargetKey(target.mdl)) {
                const std::set<int> current_ids = CollectSimprepIds(reinterpret_cast<ProSolid>(current));
                for (int id : current_ids) after_ids.insert(id);
                created_ids = SetDifference(after_ids, g_flat_queue.before_ids);
            }
        }
        if (!created.empty() || !created_ids.empty()) {
            ProMdlSave(target.mdl);
            ++g_flat_queue.result.succeeded;
            target.has_error = false;
            std::wstring verified_label = !created.empty()
                                              ? JoinNames(created)
                                              : (L"id " + std::to_wstring(created_ids.front()));
            target.status_text = L"Verified created " + QueueActionDoneLabel(g_flat_queue.action) + L": " + verified_label;
            LogLine(g_flat_queue.log_sink,
                    "sheetmetal-flat queue verified action=%s model=%s names=%s ids=%d",
                    QueueActionName(g_flat_queue.action),
                    autobbox::common::WToA(target.model_name.c_str()).c_str(),
                    autobbox::common::WToA(JoinNames(created).c_str()).c_str(),
                    static_cast<int>(created_ids.size()));
            ++g_flat_queue.index;
            g_flat_queue.phase = FlatQueuePhase::StartTarget;
            ScheduleFlatQueue(300);
            return;
        }
        ++g_flat_queue.verify_attempts;
        if (g_flat_queue.verify_attempts <= 10) {
            ScheduleFlatQueue(1000);
            return;
        }
        FailCurrentQueueTarget(target, L"Timed out: official command finished but no new verified flat-state object was detected.");
        return;
    }
}

} // namespace

core::SheetmetalFlatCollectResult CollectSheetmetalFlatTargets()
{
    core::SheetmetalFlatCollectResult result;
    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR || current == nullptr) return result;

    const ProMdlType current_type = autobbox::creo::ModelType(current);
    result.current_is_part = (current_type == PRO_MDL_PART);
    result.current_is_assembly = (current_type == PRO_MDL_ASSEMBLY);

    CollectCtx ctx;
    ctx.result = &result;
    ProSolid simprep_owner = nullptr;

    if (current_type == PRO_MDL_PART) {
        simprep_owner = reinterpret_cast<ProSolid>(current);
        AddTargetForModel(ctx, current, nullptr, true);
    } else if (current_type == PRO_MDL_ASSEMBLY) {
        simprep_owner = reinterpret_cast<ProSolid>(current);
        ProSolidDispCompVisit(simprep_owner, VisitAssemblyComponent, nullptr, &ctx);
    }

    std::sort(result.targets.begin(), result.targets.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.model_name < rhs.model_name;
    });
    for (size_t i = 0; i < result.targets.size(); ++i) {
        result.targets[i].item_name = "smtflat_" + std::to_string(i);
        RefreshTargetStatus(result.targets[i], simprep_owner);
    }
    return result;
}

std::wstring BuildSheetmetalFlatCollectSummary(const core::SheetmetalFlatCollectResult &result)
{
    std::wstring text = L"目标: " + std::to_wstring(result.targets.size()) +
                        L"  已访问: " + std::to_wstring(result.visited_components) +
                        L"  重复引用: " + std::to_wstring(result.duplicate_components);
    if (result.skipped_non_sheetmetal > 0 || result.skipped_unreadable > 0) {
        text += L"  跳过: " + std::to_wstring(result.skipped_non_sheetmetal + result.skipped_unreadable);
    }
    return text;
}

bool CreateSheetmetalFlatSimpreps(std::vector<core::SheetmetalFlatTarget> &targets,
                                  core::SheetmetalFlatActionResult &result,
                                  const SheetmetalFlatBatchLogSink &log_sink,
                                  const SheetmetalFlatBatchCompletionSink &completion_sink)
{
    result = {};
    result.action = core::SheetmetalFlatAction::CreateModelFlatRep;
    result.requested = SelectedCount(targets);
    result.skipped = result.requested;
    result.summary_text = L"创建展平表示功能已取消；请使用创建展平实例。删除按钮仍会尝试删除已有展平表示。";
    LogLine(log_sink, "sheetmetal-flat create-model-flat-rep disabled requested=%d", result.requested);
    if (completion_sink) completion_sink(result);
    return false;
}

bool CreateSheetmetalFamilyFlatStates(std::vector<core::SheetmetalFlatTarget> &targets,
                                      core::SheetmetalFlatActionResult &result,
                                      const SheetmetalFlatBatchLogSink &log_sink,
                                      const SheetmetalFlatBatchCompletionSink &completion_sink)
{
    return CreateSheetmetalFlatPatternFeatures(targets,
                                               core::SheetmetalFlatAction::CreateFamilyFlat,
                                               result,
                                               log_sink,
                                               completion_sink);
}

bool DeleteSheetmetalFlatObjects(std::vector<core::SheetmetalFlatTarget> &targets,
                                 core::SheetmetalFlatActionResult &result,
                                 const SheetmetalFlatBatchLogSink &log_sink)
{
    result = {};
    result.action = core::SheetmetalFlatAction::DeleteSelected;
    result.requested = SelectedCount(targets);

    ProSolid assembly_owner = CurrentAssemblyOwner();
    bool assembly_flat_reps_deleted = false;
    bool assembly_delete_failed = false;
    std::wstring assembly_delete_status;

    if (assembly_owner != nullptr && result.requested > 0) {
        const std::vector<MatchingSimprep> flat_reps = CollectAllFlatNamedSimpreps(assembly_owner);
        for (const auto &flat_rep : flat_reps) {
            ProSimprep rep = flat_rep.rep;
            const ProError st = ProSimprepDelete(&rep);
            if (st == PRO_TK_NO_ERROR || st == PRO_TK_E_NOT_FOUND) {
                assembly_flat_reps_deleted = true;
                assembly_delete_status += L"Deleted simprep whose name contains Flat from assembly: " + flat_rep.name + L"; ";
                LogLine(log_sink,
                        "sheetmetal-flat delete-assembly-simprep rep=%s status=%d",
                        autobbox::common::WToA(flat_rep.name.c_str()).c_str(),
                        static_cast<int>(st));
            } else {
                assembly_delete_failed = true;
                assembly_delete_status += L"Delete simprep whose name contains Flat from assembly failed: " + flat_rep.name + L" status " + FormatStatus(st) + L"; ";
            }
        }
        if (assembly_flat_reps_deleted) ProMdlSave(reinterpret_cast<ProMdl>(assembly_owner));
    }

    bool assembly_status_attached = false;
    for (auto &target : targets) {
        if (!target.selected) continue;
        bool touched = false;
        bool row_failed = false;
        std::wstring row_status;

        if (!assembly_status_attached && (!assembly_delete_status.empty() || assembly_delete_failed)) {
            assembly_status_attached = true;
            touched = touched || assembly_flat_reps_deleted;
            row_failed = row_failed || assembly_delete_failed;
            row_status += assembly_delete_status;
        }

        if (target.mdl != nullptr && autobbox::creo::ModelType(target.mdl) == PRO_MDL_PART) {
            ProSolid part_owner = reinterpret_cast<ProSolid>(target.mdl);
            for (const auto &candidate_name : FlatRepNameCandidates(target)) {
                bool deleted_by_name = false;
                const ProError st = DeleteSimprepByName(part_owner, candidate_name, deleted_by_name);
                if (deleted_by_name) {
                    touched = true;
                    row_status += L"Deleted flat representation by name: " + candidate_name + L"; ";
                    LogLine(log_sink,
                            "sheetmetal-flat delete-model-simprep-by-name model=%s rep=%s status=%d",
                            autobbox::common::WToA(target.model_name.c_str()).c_str(),
                            autobbox::common::WToA(candidate_name.c_str()).c_str(),
                            static_cast<int>(st));
                } else if (st != PRO_TK_NO_ERROR) {
                    // Candidate names are only heuristics. A "not found"/bad-name status
                    // must not make the whole row fail when deletion by id or family-table
                    // instance succeeds later.
                    LogLine(log_sink,
                            "sheetmetal-flat delete-model-simprep-by-name miss model=%s rep=%s status=%d",
                            autobbox::common::WToA(target.model_name.c_str()).c_str(),
                            autobbox::common::WToA(candidate_name.c_str()).c_str(),
                            static_cast<int>(st));
                }
            }

            const std::set<int> remaining_rep_ids = CollectSimprepIds(part_owner);
            for (int rep_id : remaining_rep_ids) {
                ProSimprep rep = {};
                const ProError init_st = ProSimprepInit(nullptr, rep_id, part_owner, &rep);
                if (init_st != PRO_TK_NO_ERROR) continue;

                std::wstring rep_name;
                const bool has_name = TryReadSimprepName(&rep, rep_name);
                if (has_name && !NameContainsFlat(rep_name) && !FlatNameMatchesTarget(UppercaseAscii(rep_name), target.model_name)) {
                    continue;
                }

                bool deleted_by_id = false;
                const ProError st = DeleteSimprepById(part_owner, rep_id, deleted_by_id);
                if (st == PRO_TK_NO_ERROR || st == PRO_TK_E_NOT_FOUND) {
                    if (deleted_by_id) {
                        touched = true;
                        row_status += has_name
                                          ? (L"Deleted flat representation by id/name: " + std::to_wstring(rep_id) + L"/" + rep_name + L"; ")
                                          : (L"Deleted unnamed flat representation by id: " + std::to_wstring(rep_id) + L"; ");
                        LogLine(log_sink,
                                "sheetmetal-flat delete-model-simprep-by-id model=%s id=%d name=%s status=%d",
                                autobbox::common::WToA(target.model_name.c_str()).c_str(),
                                rep_id,
                                has_name ? autobbox::common::WToA(rep_name.c_str()).c_str() : "",
                                static_cast<int>(st));
                    }
                } else {
                    row_failed = true;
                    row_status += L"Delete flat representation by id failed: " + std::to_wstring(rep_id) + L" status " + FormatStatus(st) + L"; ";
                }
            }

            const std::vector<MatchingSimprep> part_reps = CollectAllFlatNamedSimpreps(part_owner);
            for (const auto &flat_rep : part_reps) {
                ProSimprep rep = flat_rep.rep;
                const ProError st = ProSimprepDelete(&rep);
                if (st == PRO_TK_NO_ERROR || st == PRO_TK_E_NOT_FOUND) {
                    touched = true;
                    row_status += L"Deleted simprep whose name contains Flat from part: " + flat_rep.name + L"; ";
                    LogLine(log_sink,
                            "sheetmetal-flat delete-model-simprep model=%s rep=%s status=%d",
                            autobbox::common::WToA(target.model_name.c_str()).c_str(),
                            autobbox::common::WToA(flat_rep.name.c_str()).c_str(),
                            static_cast<int>(st));
                } else {
                    row_failed = true;
                    row_status += L"Delete simprep whose name contains Flat from part failed: " + flat_rep.name + L" status " + FormatStatus(st) + L"; ";
                }
            }
            if ((touched || !part_reps.empty()) && !row_failed) ProMdlSave(target.mdl);
        }

        if (!target.family_flat_instances.empty() && target.mdl != nullptr) {
            ProFamtable famtable = {};
            ProError st_fam = ProFamtableInit(target.mdl, &famtable);
            if (st_fam == PRO_TK_NO_ERROR) {
                std::vector<std::wstring> instance_names = target.family_flat_instances;
                std::sort(instance_names.begin(), instance_names.end());
                instance_names.erase(std::unique(instance_names.begin(), instance_names.end()), instance_names.end());
                for (const auto &inst_name_w : instance_names) {
                    ProName inst_name = {0};
                    CopyWToProName(inst_name, inst_name_w);
                    ProFaminstance inst = {};
                    ProError st = ProFaminstanceInit(inst_name, &famtable, &inst);
                    if (st == PRO_TK_NO_ERROR) {
                        ProFaminstanceErase(&inst);
                        st = ProFaminstanceRemove(&inst);
                    }
                    if (st == PRO_TK_NO_ERROR || st == PRO_TK_E_NOT_FOUND) {
                        touched = true;
                        row_status += L"Deleted family instance whose name contains Flat: " + inst_name_w + L"; ";
                        LogLine(log_sink,
                                "sheetmetal-flat delete-family-flat model=%s instance=%s status=%d",
                                autobbox::common::WToA(target.model_name.c_str()).c_str(),
                                autobbox::common::WToA(inst_name_w.c_str()).c_str(),
                                static_cast<int>(st));
                    } else {
                        row_failed = true;
                        row_status += L"Delete family instance failed: " + inst_name_w + L" status " + FormatStatus(st) + L"; ";
                    }
                }
                if (touched) ProMdlSave(target.mdl);
            } else {
                row_failed = true;
                row_status += L"Family table init failed " + FormatStatus(st_fam) + L"; ";
            }
        }

        RefreshTargetStatus(target, nullptr);
        if (touched && !row_failed) {
            ++result.succeeded;
            target.status_text = row_status.empty() ? L"Done" : row_status;
            target.has_error = false;
        } else if (row_failed) {
            ++result.failed;
            target.status_text = row_status.empty() ? L"Done" : row_status;
            target.has_error = true;
        } else {
            ++result.skipped;
            target.status_text = L"Skip: no deletable flat instance or flat representation";
            target.has_error = false;
        }
    }
    FinalizeActionSummary(result);
    return result.failed == 0;
}

} // namespace autobbox::application
