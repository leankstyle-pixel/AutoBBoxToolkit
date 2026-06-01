#include "autobbox/application/model_diagnostics.h"

#include "autobbox/application/model_metrics.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"

#include <ProArray.h>
#include <ProFeature.h>
#include <ProMdl.h>
#include <ProModelitem.h>
#include <ProSelbuffer.h>
#include <ProSelection.h>
#include <ProSolid.h>
#include <ProToolkit.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace autobbox::application {

namespace {

struct DiagnosticTarget {
    ProMdl mdl = nullptr;
    ProAsmcomppath component_path = {};
    bool has_component_path = false;
};

struct CollectCtx {
    std::vector<DiagnosticTarget> targets;
    std::unordered_set<std::uintptr_t> seen;
};

void LogLine(const ModelDiagnosticsLogSink &log_sink, const char *fmt, ...)
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

const wchar_t *ModelTypeLabel(ProMdlType type)
{
    switch (type) {
    case PRO_MDL_PART:
        return L"PRT";
    case PRO_MDL_ASSEMBLY:
        return L"ASM";
    default:
        return L"MDL";
    }
}

std::wstring RegenStatusText(int regen_status)
{
    switch (regen_status) {
    case PRO_SOLID_REGENERATED:
        return L"已再生";
    case PRO_SOLID_NEEDS_REGENERATION:
        return L"需要重新生成";
    case PRO_SOLID_FAILED_REGENERATION:
        return L"重新生成失败";
    case PRO_SOLID_CONNECT_FAILED:
        return L"机构连接失败";
    default:
        return L"未知再生状态";
    }
}

std::wstring JoinReason(const std::vector<std::wstring> &parts)
{
    std::wstring out;
    for (const std::wstring &part : parts) {
        if (part.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += L"；";
        }
        out += part;
    }
    return out;
}

ProError ComponentVisitAction(ProAsmcomppath *p_path,
                              ProSolid handle,
                              ProBoolean down,
                              ProAppData app_data)
{
    if (down != PRO_B_TRUE || app_data == nullptr || handle == nullptr) {
        return PRO_TK_NO_ERROR;
    }
    ProMdl mdl = reinterpret_cast<ProMdl>(handle);
    if (!autobbox::creo::IsPartOrAsm(mdl)) {
        return PRO_TK_NO_ERROR;
    }

    CollectCtx *ctx = reinterpret_cast<CollectCtx *>(app_data);
    const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(mdl);
    if (!ctx->seen.insert(key).second) {
        return PRO_TK_NO_ERROR;
    }

    DiagnosticTarget target;
    target.mdl = mdl;
    if (p_path != nullptr && p_path->table_num > 0) {
        target.component_path = *p_path;
        target.has_component_path = true;
    }
    ctx->targets.push_back(target);
    return PRO_TK_NO_ERROR;
}

std::vector<DiagnosticTarget> CollectTargets()
{
    std::vector<DiagnosticTarget> result;
    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        !autobbox::creo::IsPartOrAsm(current)) {
        return result;
    }

    DiagnosticTarget root;
    root.mdl = current;
    result.push_back(root);

    if (autobbox::creo::ModelType(current) != PRO_MDL_ASSEMBLY) {
        return result;
    }

    CollectCtx ctx;
    ctx.targets = result;
    ctx.seen.insert(reinterpret_cast<std::uintptr_t>(current));
    ProSolidDispCompVisit(reinterpret_cast<ProSolid>(current), ComponentVisitAction, nullptr, &ctx);
    return ctx.targets;
}

int ProArrayIntSize(int *array)
{
    if (array == nullptr) {
        return 0;
    }
    int size = 0;
    if (ProArraySizeGet(reinterpret_cast<ProArray>(array), &size) != PRO_TK_NO_ERROR) {
        return 0;
    }
    return size;
}

int ProArrayUIntSize(unsigned int *array)
{
    if (array == nullptr) {
        return 0;
    }
    int size = 0;
    if (ProArraySizeGet(reinterpret_cast<ProArray>(array), &size) != PRO_TK_NO_ERROR) {
        return 0;
    }
    return size;
}

std::wstring SafeFeatureTreeName(ProFeature &feature)
{
    ProModelitem item = feature;
    ProMdlName tree_name = {0};
    if (ProFeatureMdltreeDisplaynameGet(&item, tree_name) == PRO_TK_NO_ERROR &&
        tree_name[0] != L'\0') {
        return tree_name;
    }
    return std::wstring();
}

std::wstring SafeFeatureTypeName(ProFeature &feature)
{
    ProName type_name = {0};
    if (ProFeatureTypenameGet(&feature, type_name) == PRO_TK_NO_ERROR &&
        type_name[0] != L'\0') {
        return type_name;
    }
    return std::wstring();
}

ModelDiagnosticFeature BuildFeatureInfo(ProSolid solid,
                                        int id,
                                        const wchar_t *kind,
                                        unsigned int status_flags = 0)
{
    ModelDiagnosticFeature info;
    info.id = id;
    info.status_flags = status_flags;
    info.kind = kind == nullptr ? std::wstring() : std::wstring(kind);

    ProFeature feature = {};
    if (solid != nullptr && ProFeatureInit(solid, id, &feature) == PRO_TK_NO_ERROR) {
        info.type_name = SafeFeatureTypeName(feature);
        info.tree_name = SafeFeatureTreeName(feature);
    }
    return info;
}

std::vector<ModelDiagnosticFeature> BuildFeatureList(ProSolid solid,
                                                     int *ids,
                                                     const wchar_t *kind)
{
    std::vector<ModelDiagnosticFeature> out;
    const int count = ProArrayIntSize(ids);
    out.reserve(static_cast<size_t>(std::max(0, count)));
    for (int i = 0; i < count; ++i) {
        out.push_back(BuildFeatureInfo(solid, ids[i], kind));
    }
    return out;
}

std::wstring FeatureFlagsText(unsigned int flags)
{
    std::vector<std::wstring> parts;
    if ((flags & PRO_FEAT_STAT_FAILED) != 0U) {
        parts.push_back(L"失败特征");
    }
    if ((flags & PRO_FEAT_STAT_CHILD_OF_FAILED) != 0U) {
        parts.push_back(L"失败子项");
    }
    if ((flags & PRO_FEAT_STAT_CHILD_OF_EXT_FAILED) != 0U) {
        parts.push_back(L"外部失败子项");
    }
    if ((flags & PRO_FEAT_STAT_UNREGENERATED) != 0U) {
        parts.push_back(L"未重新生成");
    }
    if (parts.empty()) {
        parts.push_back(L"特征状态异常");
    }
    return JoinReason(parts);
}

bool HasProblemFeatureFlags(unsigned int flags)
{
    return (flags & (PRO_FEAT_STAT_FAILED |
                     PRO_FEAT_STAT_CHILD_OF_FAILED |
                     PRO_FEAT_STAT_CHILD_OF_EXT_FAILED |
                     PRO_FEAT_STAT_UNREGENERATED)) != 0U;
}

bool HasErrorFeatureFlags(unsigned int flags)
{
    return (flags & (PRO_FEAT_STAT_FAILED | PRO_FEAT_STAT_CHILD_OF_EXT_FAILED)) != 0U;
}

std::wstring FeatureBrief(const ModelDiagnosticFeature &feature)
{
    if (feature.id < 0) {
        return L"";
    }
    std::wstring out = L"#" + std::to_wstring(feature.id);
    if (!feature.tree_name.empty()) {
        out += L" ";
        out += feature.tree_name;
    } else if (!feature.type_name.empty()) {
        out += L" ";
        out += feature.type_name;
    }
    if (!feature.kind.empty()) {
        out += L" ";
        out += feature.kind;
    }
    return out;
}

std::wstring FirstFeatureBrief(const ModelDiagnosticItem &item)
{
    if (!item.failed_features.empty()) {
        return FeatureBrief(item.failed_features.front());
    }
    if (!item.child_failed_features.empty()) {
        return FeatureBrief(item.child_failed_features.front());
    }
    if (!item.external_child_failed_features.empty()) {
        return FeatureBrief(item.external_child_failed_features.front());
    }
    if (!item.flagged_features.empty()) {
        return FeatureBrief(item.flagged_features.front());
    }
    return L"";
}

void ReadFailedFeatureLists(ModelDiagnosticItem &item)
{
    int *failed = nullptr;
    int *child = nullptr;
    int *external_child = nullptr;
    const ProSolid solid = reinterpret_cast<ProSolid>(item.mdl);
    item.failed_features_status =
        ProSolidFailedfeaturesList(solid, &failed, &child, &external_child);
    if (item.failed_features_status == PRO_TK_NO_ERROR) {
        item.failed_features = BuildFeatureList(solid, failed, L"失败特征");
        item.child_failed_features = BuildFeatureList(solid, child, L"失败子项");
        item.external_child_failed_features = BuildFeatureList(solid, external_child, L"外部失败子项");
    }
    if (failed != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&failed));
    }
    if (child != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&child));
    }
    if (external_child != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&external_child));
    }
}

void ReadStatusFlagFeatures(ModelDiagnosticItem &item)
{
    int *ids = nullptr;
    unsigned int *flags = nullptr;
    const ProSolid solid = reinterpret_cast<ProSolid>(item.mdl);
    item.status_flags_status = ProSolidFeatstatusflagsGet(solid, &ids, &flags);
    if (item.status_flags_status == PRO_TK_NO_ERROR) {
        const int id_count = ProArrayIntSize(ids);
        const int flag_count = ProArrayUIntSize(flags);
        const int count = std::min(id_count, flag_count);
        item.flagged_features.clear();
        item.flagged_features.reserve(static_cast<size_t>(std::max(0, count)));
        for (int i = 0; i < count; ++i) {
            if (!HasProblemFeatureFlags(flags[i])) {
                continue;
            }
            const std::wstring kind = FeatureFlagsText(flags[i]);
            item.flagged_features.push_back(BuildFeatureInfo(solid, ids[i], kind.c_str(), flags[i]));
        }
    }
    if (ids != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&ids));
    }
    if (flags != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&flags));
    }
}

std::wstring FailureSetKey(const ModelDiagnosticItem &item)
{
    std::vector<int> ids;
    for (const ModelDiagnosticFeature &f : item.failed_features) {
        ids.push_back(1000000 + f.id);
    }
    for (const ModelDiagnosticFeature &f : item.child_failed_features) {
        ids.push_back(2000000 + f.id);
    }
    for (const ModelDiagnosticFeature &f : item.external_child_failed_features) {
        ids.push_back(3000000 + f.id);
    }
    for (const ModelDiagnosticFeature &f : item.flagged_features) {
        ids.push_back(4000000 + f.id);
    }
    std::sort(ids.begin(), ids.end());
    std::wstring key;
    for (int id : ids) {
        if (!key.empty()) {
            key += L",";
        }
        key += std::to_wstring(id);
    }
    return key;
}

std::wstring GeometrySnapshot(ProMdl mdl)
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    const bool has_bbox = ComputeBBoxAxes(mdl, false, false, x, y, z);
    wchar_t buffer[256] = {0};
    std::swprintf(buffer,
                  sizeof(buffer) / sizeof(buffer[0]),
                  L"bbox=%d:%.6f/%.6f/%.6f",
                  has_bbox ? 1 : 0,
                  x,
                  y,
                  z);
    return buffer;
}

std::wstring StatusFlagsKey(ProMdl mdl)
{
    int *ids = nullptr;
    unsigned int *flags = nullptr;
    std::wstring key;
    const ProError st = ProSolidFeatstatusflagsGet(
        reinterpret_cast<ProSolid>(mdl),
        &ids,
        &flags);
    key = L"flag_st=" + std::to_wstring(static_cast<int>(st));
    if (st == PRO_TK_NO_ERROR) {
        std::vector<std::wstring> parts;
        const int count = std::min(ProArrayIntSize(ids), ProArrayUIntSize(flags));
        parts.reserve(static_cast<size_t>(std::max(0, count)));
        for (int i = 0; i < count; ++i) {
            if (!HasProblemFeatureFlags(flags[i])) {
                continue;
            }
            parts.push_back(std::to_wstring(ids[i]) + L":" + std::to_wstring(flags[i]));
        }
        std::sort(parts.begin(), parts.end());
        for (const std::wstring &part : parts) {
            key += L"," + part;
        }
    }
    if (ids != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&ids));
    }
    if (flags != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&flags));
    }
    return key;
}

std::wstring ModelStateSnapshot(ProMdl mdl)
{
    ProSolidRegenerationStatus regen = PRO_SOLID_REGENERATED;
    const ProError regen_st =
        ProSolidRegenerationstatusGet(reinterpret_cast<ProSolid>(mdl), &regen);

    ProBoolean modified = PRO_B_FALSE;
    const ProError modified_st = ProMdlModificationVerify(mdl, &modified);

    ModelDiagnosticItem temp;
    temp.mdl = mdl;
    ReadFailedFeatureLists(temp);

    return L"regen_st=" + std::to_wstring(static_cast<int>(regen_st)) +
           L";regen=" + std::to_wstring(static_cast<int>(regen)) +
           L";modified_st=" + std::to_wstring(static_cast<int>(modified_st)) +
           L";modified=" + std::to_wstring(modified == PRO_B_TRUE ? 1 : 0) +
           L";failed=" + FailureSetKey(temp) +
           L";" + StatusFlagsKey(mdl) +
           L";" + GeometrySnapshot(mdl);
}

bool IsSuccessfulRegen(ProError st)
{
    return st == PRO_TK_NO_ERROR ||
           st == PRO_TK_UNATTACHED_FEATS ||
           st == PRO_TK_NO_CHANGE;
}

ProError SafeRegenerate(ProMdl mdl)
{
    if (mdl == nullptr || !autobbox::creo::IsPartOrAsm(mdl)) {
        return PRO_TK_BAD_INPUTS;
    }
    const int flags = PRO_REGEN_NO_RESOLVE_MODE | PRO_REGEN_UNDO_IF_FAIL;
    ProError st = ProSolidRegenerate(reinterpret_cast<ProSolid>(mdl), flags);
    if (st == PRO_TK_REGEN_AGAIN) {
        st = ProSolidRegenerate(reinterpret_cast<ProSolid>(mdl), flags);
    }
    return st;
}

void ApplyDeepCheck(ModelDiagnosticItem &item)
{
    item.deep_checked = true;
    const std::wstring key_before = ModelStateSnapshot(item.mdl);

    item.deep_regen_first = SafeRegenerate(item.mdl);
    const std::wstring key_after_first = ModelStateSnapshot(item.mdl);

    item.deep_regen_second = SafeRegenerate(item.mdl);
    const std::wstring key_after_second = ModelStateSnapshot(item.mdl);

    item.unstable_after_regen =
        IsSuccessfulRegen(item.deep_regen_first) &&
        IsSuccessfulRegen(item.deep_regen_second) &&
        (key_before != key_after_first || key_after_first != key_after_second);

    item.deep_summary = L"深度检测：第一次=" + std::to_wstring(static_cast<int>(item.deep_regen_first)) +
                        L"，第二次=" + std::to_wstring(static_cast<int>(item.deep_regen_second));
    if (item.unstable_after_regen) {
        if (key_before != key_after_first && key_after_first != key_after_second) {
            item.deep_summary += L"，重生前后和两次重生结果均不一致";
        } else if (key_before != key_after_first) {
            item.deep_summary += L"，重生前后结果不一致";
        } else {
            item.deep_summary += L"，两次重生结果不一致";
        }
    }
}

void ClassifyItem(ModelDiagnosticItem &item)
{
    std::vector<std::wstring> reasons;
    item.severity = ModelDiagnosticSeverity::Info;
    item.reason.clear();
    item.suggestion.clear();

    if (item.regen_status_get == PRO_TK_NO_ERROR) {
        if (item.regen_status == PRO_SOLID_FAILED_REGENERATION) {
            item.severity = ModelDiagnosticSeverity::Error;
            reasons.push_back(L"模型重新生成失败");
        } else if (item.regen_status == PRO_SOLID_NEEDS_REGENERATION) {
            item.severity = std::max(item.severity, ModelDiagnosticSeverity::Warning);
            reasons.push_back(L"模型需要重新生成");
        } else if (item.regen_status == PRO_SOLID_CONNECT_FAILED) {
            item.severity = std::max(item.severity, ModelDiagnosticSeverity::Warning);
            reasons.push_back(L"机构连接失败");
        }
    } else {
        item.severity = std::max(item.severity, ModelDiagnosticSeverity::Warning);
        reasons.push_back(L"无法读取再生状态");
    }

    if (!item.failed_features.empty()) {
        item.severity = ModelDiagnosticSeverity::Error;
        reasons.push_back(L"存在失败特征 " + std::to_wstring(item.failed_features.size()) + L" 个");
        item.suggestion = L"优先修复第一个失败特征，检查草绘、尺寸、父项引用和外部参照。";
    }
    if (!item.child_failed_features.empty()) {
        item.severity = std::max(item.severity, ModelDiagnosticSeverity::Warning);
        reasons.push_back(L"存在失败子项 " + std::to_wstring(item.child_failed_features.size()) + L" 个");
        if (item.suggestion.empty()) {
            item.suggestion = L"先修复父失败特征，再处理子特征。";
        }
    }
    if (!item.external_child_failed_features.empty()) {
        item.severity = std::max(item.severity, ModelDiagnosticSeverity::Warning);
        reasons.push_back(L"存在外部失败子项 " + std::to_wstring(item.external_child_failed_features.size()) + L" 个");
        if (item.suggestion.empty()) {
            item.suggestion = L"检查外部参照、骨架/发布几何和装配上下文。";
        }
    }
    if (!item.flagged_features.empty()) {
        bool has_error_flags = false;
        bool has_unregen = false;
        for (const ModelDiagnosticFeature &feature : item.flagged_features) {
            has_error_flags = has_error_flags || HasErrorFeatureFlags(feature.status_flags);
            has_unregen = has_unregen || ((feature.status_flags & PRO_FEAT_STAT_UNREGENERATED) != 0U);
        }
        item.severity = has_error_flags
                            ? ModelDiagnosticSeverity::Error
                            : std::max(item.severity, ModelDiagnosticSeverity::Warning);
        std::wstring detail = L"特征状态异常 " +
                              std::to_wstring(item.flagged_features.size()) +
                              L" 个";
        if (has_unregen) {
            detail += L"（含未重新生成特征）";
        }
        reasons.push_back(detail);
        if (item.suggestion.empty()) {
            item.suggestion = has_error_flags
                                  ? L"定位首个异常特征，先修复失败/外部失败引用，再重新生成。"
                                  : L"定位未重新生成特征，检查父项引用、关系式、族表/程序和外部参照。";
        }
    }
    if (item.severity == ModelDiagnosticSeverity::Warning &&
        item.failed_features.empty() &&
        item.child_failed_features.empty() &&
        item.external_child_failed_features.empty() &&
        item.flagged_features.empty()) {
        if (item.status_flags_status != PRO_TK_NO_ERROR) {
            reasons.push_back(L"未能读取具体特征状态（返回码 " +
                              std::to_wstring(static_cast<int>(item.status_flags_status)) +
                              L"）");
        } else {
            reasons.push_back(L"Creo 未返回具体失败特征，可能是关系式/程序/族表或装配上下文导致的待重生");
        }
    }

    if (item.suggestion.empty() && item.severity == ModelDiagnosticSeverity::Warning) {
        item.suggestion = L"执行深度检测或在 Creo 中重新生成模型，确认状态是否稳定。";
    }
    if (item.suggestion.empty()) {
        item.suggestion = L"未发现再生异常。";
    }
    item.reason = JoinReason(reasons);
    if (item.reason.empty()) {
        item.reason = L"未发现异常";
    }
}

bool SeverityLessForSort(const ModelDiagnosticItem &lhs, const ModelDiagnosticItem &rhs)
{
    if (lhs.severity != rhs.severity) {
        return static_cast<int>(lhs.severity) > static_cast<int>(rhs.severity);
    }
    return lhs.model_name < rhs.model_name;
}

std::wstring FeaturesForReport(const std::vector<ModelDiagnosticFeature> &features)
{
    std::wstring out;
    for (const ModelDiagnosticFeature &feature : features) {
        if (!out.empty()) {
            out += L", ";
        }
        out += FeatureBrief(feature);
        if (!feature.kind.empty()) {
            out += L"[" + feature.kind + L"]";
        }
        if (feature.status_flags != 0U) {
            out += L"{flags=" + std::to_wstring(feature.status_flags) + L"}";
        }
        if (!feature.type_name.empty() && !feature.tree_name.empty()) {
            out += L"(" + feature.type_name + L")";
        }
    }
    return out.empty() ? L"-" : out;
}

} // namespace

const wchar_t *ModelDiagnosticSeverityLabel(ModelDiagnosticSeverity severity)
{
    switch (severity) {
    case ModelDiagnosticSeverity::Error:
        return L"错误";
    case ModelDiagnosticSeverity::Warning:
        return L"警告";
    case ModelDiagnosticSeverity::Info:
    default:
        return L"正常";
    }
}

std::vector<ModelDiagnosticItem> CollectModelDiagnostics(bool deep_check,
                                                        const ModelDiagnosticsLogSink &log_sink)
{
    std::vector<ModelDiagnosticItem> items;
    const std::vector<DiagnosticTarget> targets = CollectTargets();
    LogLine(log_sink,
            "model-diagnostics collect targets=%d deep=%d",
            static_cast<int>(targets.size()),
            deep_check ? 1 : 0);

    for (const DiagnosticTarget &target : targets) {
        ModelDiagnosticItem item;
        item.mdl = target.mdl;
        item.component_path = target.component_path;
        item.has_component_path = target.has_component_path;
        item.model_name = autobbox::creo::ModelName(target.mdl, L"<unknown>");
        item.model_type_label = ModelTypeLabel(autobbox::creo::ModelType(target.mdl));

        ProSolidRegenerationStatus regen = PRO_SOLID_REGENERATED;
        item.regen_status_get =
            ProSolidRegenerationstatusGet(reinterpret_cast<ProSolid>(target.mdl), &regen);
        if (item.regen_status_get == PRO_TK_NO_ERROR) {
            item.regen_status = static_cast<int>(regen);
        }
        const bool needs_detail =
            deep_check ||
            item.regen_status_get != PRO_TK_NO_ERROR ||
            item.regen_status != PRO_SOLID_REGENERATED;
        if (needs_detail) {
            ReadFailedFeatureLists(item);
            ReadStatusFlagFeatures(item);
        }
        ClassifyItem(item);
        if (deep_check) {
            ApplyDeepCheck(item);
            item.failed_features.clear();
            item.child_failed_features.clear();
            item.external_child_failed_features.clear();
            item.flagged_features.clear();
            item.failed_features_status = PRO_TK_GENERAL_ERROR;
            item.status_flags_status = PRO_TK_GENERAL_ERROR;
            ProSolidRegenerationStatus after_regen = PRO_SOLID_REGENERATED;
            item.regen_status_get =
                ProSolidRegenerationstatusGet(reinterpret_cast<ProSolid>(target.mdl), &after_regen);
            if (item.regen_status_get == PRO_TK_NO_ERROR) {
                item.regen_status = static_cast<int>(after_regen);
            }
            ReadFailedFeatureLists(item);
            ReadStatusFlagFeatures(item);
            ClassifyItem(item);
            if (item.unstable_after_regen) {
                item.severity = std::max(item.severity, ModelDiagnosticSeverity::Warning);
                if (!item.reason.empty() && item.reason != L"未发现异常") {
                    item.reason += L"；";
                } else {
                    item.reason.clear();
                }
                item.reason += L"重新生成前后或多次重新生成结果发生变化";
                item.suggestion = L"检查关系式、族表/Pro Program、循环引用、外部参照或装配分析特征。";
            }
        }

        LogLine(log_sink,
                "model-diagnostics item name=%s type=%s severity=%d regen_get=%d regen=%d failed=%d child=%d ext_child=%d flagged=%d flag_status=%d",
                autobbox::common::WToA(item.model_name.c_str()).c_str(),
                autobbox::common::WToA(item.model_type_label.c_str()).c_str(),
                static_cast<int>(item.severity),
                static_cast<int>(item.regen_status_get),
                item.regen_status,
                static_cast<int>(item.failed_features.size()),
                static_cast<int>(item.child_failed_features.size()),
                static_cast<int>(item.external_child_failed_features.size()),
                static_cast<int>(item.flagged_features.size()),
                static_cast<int>(item.status_flags_status));
        items.push_back(item);
    }

    std::sort(items.begin(), items.end(), SeverityLessForSort);
    return items;
}

ProError LocateModelDiagnosticItem(const ModelDiagnosticItem &item,
                                   const ModelDiagnosticsLogSink &log_sink)
{
    if (item.mdl == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    ProSelection selection = nullptr;
    ProError alloc_st = PRO_TK_GENERAL_ERROR;
    if (item.has_component_path) {
        ProAsmcomppath path = item.component_path;
        ProModelitem model_item = {};
        const ProError model_item_st = ProMdlToModelitem(item.mdl, &model_item);
        LogLine(log_sink,
                "model-diagnostics locate component-mdl-to-modelitem model=%s status=%d path_len=%d",
                autobbox::common::WToA(item.model_name.c_str()).c_str(),
                static_cast<int>(model_item_st),
                path.table_num);
        if (model_item_st == PRO_TK_NO_ERROR) {
            alloc_st = ProSelectionAlloc(&path, &model_item, &selection);
            LogLine(log_sink,
                    "model-diagnostics locate component-selection-alloc model=%s status=%d",
                    autobbox::common::WToA(item.model_name.c_str()).c_str(),
                    static_cast<int>(alloc_st));
        } else {
            alloc_st = model_item_st;
        }
    } else {
        ProModelitem model_item = {};
        const ProError model_item_st = ProMdlToModelitem(item.mdl, &model_item);
        LogLine(log_sink,
                "model-diagnostics locate mdl-to-modelitem model=%s status=%d",
                autobbox::common::WToA(item.model_name.c_str()).c_str(),
                static_cast<int>(model_item_st));
        if (model_item_st == PRO_TK_NO_ERROR) {
            ProAsmcomppath root_path = {};
            ProAsmcomppath *path_ptr = nullptr;
            if (autobbox::creo::ModelType(item.mdl) == PRO_MDL_ASSEMBLY) {
                ProIdTable empty_table = {0};
                const ProError path_st = ProAsmcomppathInit(
                    reinterpret_cast<ProSolid>(item.mdl),
                    empty_table,
                    0,
                    &root_path);
                LogLine(log_sink,
                        "model-diagnostics locate root-asm-path-init model=%s status=%d",
                        autobbox::common::WToA(item.model_name.c_str()).c_str(),
                        static_cast<int>(path_st));
                if (path_st == PRO_TK_NO_ERROR) {
                    path_ptr = &root_path;
                }
            }
            alloc_st = ProSelectionAlloc(path_ptr, &model_item, &selection);
            LogLine(log_sink,
                    "model-diagnostics locate modelitem-selection-alloc model=%s status=%d",
                    autobbox::common::WToA(item.model_name.c_str()).c_str(),
                    static_cast<int>(alloc_st));
        } else {
            alloc_st = model_item_st;
        }
    }

    if (alloc_st != PRO_TK_NO_ERROR || selection == nullptr) {
        if (selection != nullptr) {
            ProSelectionFree(&selection);
        }
        return alloc_st;
    }

    const ProError clear_st = ProSelbufferClear();
    const ProError add_st = ProSelbufferSelectionAdd(selection);
    const ProError display_st = ProSelectionDisplay(selection);
    const ProError highlight_st = ProSelectionHighlight(selection, PRO_COLOR_SELECTED);
    ProSelectionFree(&selection);

    LogLine(log_sink,
            "model-diagnostics locate selbuffer-clear=%d add=%d display=%d highlight=%d",
            static_cast<int>(clear_st),
            static_cast<int>(add_st),
            static_cast<int>(display_st),
            static_cast<int>(highlight_st));

    if (add_st == PRO_TK_NO_ERROR || highlight_st == PRO_TK_NO_ERROR || display_st == PRO_TK_NO_ERROR) {
        return PRO_TK_NO_ERROR;
    }
    if (add_st != PRO_TK_NO_ERROR) {
        return add_st;
    }
    if (highlight_st != PRO_TK_NO_ERROR) {
        return highlight_st;
    }
    return display_st;
}

std::wstring BuildModelDiagnosticsSummary(const std::vector<ModelDiagnosticItem> &items)
{
    int errors = 0;
    int warnings = 0;
    for (const ModelDiagnosticItem &item : items) {
        if (item.severity == ModelDiagnosticSeverity::Error) {
            ++errors;
        } else if (item.severity == ModelDiagnosticSeverity::Warning) {
            ++warnings;
        }
    }
    return L"模型检测：共 " + std::to_wstring(items.size()) +
           L" 个模型，错误 " + std::to_wstring(errors) +
           L" 个，警告 " + std::to_wstring(warnings) + L" 个。";
}

void LogModelDiagnosticsReport(const std::vector<ModelDiagnosticItem> &items,
                               bool deep_check,
                               const ModelDiagnosticsLogSink &log_sink)
{
    LogLine(log_sink, "===== Model diagnostics report deep=%d =====", deep_check ? 1 : 0);
    LogLine(log_sink, "%s", autobbox::common::WToA(BuildModelDiagnosticsSummary(items).c_str()).c_str());
    for (const ModelDiagnosticItem &item : items) {
        LogLine(log_sink,
                "[%s] %s type=%s regen=%s status_get=%d failed_status=%d flag_status=%d",
                autobbox::common::WToA(ModelDiagnosticSeverityLabel(item.severity)).c_str(),
                autobbox::common::WToA(item.model_name.c_str()).c_str(),
                autobbox::common::WToA(item.model_type_label.c_str()).c_str(),
                autobbox::common::WToA(RegenStatusText(item.regen_status).c_str()).c_str(),
                static_cast<int>(item.regen_status_get),
                static_cast<int>(item.failed_features_status),
                static_cast<int>(item.status_flags_status));
        LogLine(log_sink,
                "  reason=%s",
                autobbox::common::WToA(item.reason.c_str()).c_str());
        LogLine(log_sink,
                "  first_feature=%s",
                autobbox::common::WToA(FirstFeatureBrief(item).c_str()).c_str());
        LogLine(log_sink,
                "  failed=%s",
                autobbox::common::WToA(FeaturesForReport(item.failed_features).c_str()).c_str());
        LogLine(log_sink,
                "  child_failed=%s",
                autobbox::common::WToA(FeaturesForReport(item.child_failed_features).c_str()).c_str());
        LogLine(log_sink,
                "  external_child_failed=%s",
                autobbox::common::WToA(FeaturesForReport(item.external_child_failed_features).c_str()).c_str());
        LogLine(log_sink,
                "  flagged=%s",
                autobbox::common::WToA(FeaturesForReport(item.flagged_features).c_str()).c_str());
        if (item.deep_checked) {
            LogLine(log_sink,
                    "  deep=%s unstable=%d",
                    autobbox::common::WToA(item.deep_summary.c_str()).c_str(),
                    item.unstable_after_regen ? 1 : 0);
        }
        LogLine(log_sink,
                "  suggestion=%s",
                autobbox::common::WToA(item.suggestion.c_str()).c_str());
    }
    LogLine(log_sink, "===== Model diagnostics report end =====");
}

} // namespace autobbox::application
