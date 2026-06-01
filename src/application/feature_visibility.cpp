#include "autobbox/application/feature_visibility.h"

#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"

#include <ProAsmcomppath.h>
#include <ProArray.h>
#include <ProFeatType.h>
#include <ProFeature.h>
#include <ProLayer.h>
#include <ProMdl.h>
#include <ProModelitem.h>
#include <ProSolid.h>
#include <ProToolkit.h>
#include <ProWindows.h>

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace autobbox::application {

namespace {

enum class StatusLayerKind : int {
    Curve = 0,
    Surface = 1,
    Sketch = 2,
    Count = 3,
};

struct StatusLayerSpec {
    StatusLayerKind kind;
    const wchar_t *name;
    const wchar_t *label;
};

constexpr std::array<StatusLayerSpec, static_cast<std::size_t>(StatusLayerKind::Count)> kStatusLayers = {{
    {StatusLayerKind::Curve, L"AB_MODEL_STATUS_CURVE", L"\u66f2\u7ebf"},
    {StatusLayerKind::Surface, L"AB_MODEL_STATUS_SURFACE", L"\u66f2\u9762"},
    {StatusLayerKind::Sketch, L"AB_MODEL_STATUS_SKETCH", L"\u8349\u7ed8"},
}};

void LogLine(const std::function<void(const std::string &line)> &log_sink, const char *fmt, ...)
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

void CopyToProName(ProName dest, const wchar_t *src)
{
    if (dest == nullptr || src == nullptr) {
        return;
    }

#if defined(_MSC_VER)
    wcsncpy_s(dest, PRO_NAME_SIZE, src, _TRUNCATE);
#else
    std::wcsncpy(dest, src, PRO_NAME_SIZE - 1);
    dest[PRO_NAME_SIZE - 1] = L'\0';
#endif
}

bool IsProNameEqual(const ProName name, const wchar_t *expected)
{
    return expected != nullptr && std::wcscmp(name, expected) == 0;
}

const StatusLayerSpec *SpecForKind(StatusLayerKind kind)
{
    const auto index = static_cast<std::size_t>(kind);
    return index < kStatusLayers.size() ? &kStatusLayers[index] : nullptr;
}

const StatusLayerSpec *SpecForFeatureType(ProFeattype type)
{
    switch (type) {
    case PRO_FEAT_CURVE:
    case PRO_FEAT_DRV_TOOL_CURVE:
        return SpecForKind(StatusLayerKind::Curve);
    case PRO_FEAT_DATUM_SURF:
    case PRO_FEAT_DATUM_QUILT:
    case PRO_FEAT_SRF_MDL:
    case PRO_FEAT_DRV_TOOL_SURF:
    case PRO_FEAT_FLATQLT:
    case PRO_FEAT_SUPERQUILT:
    case PRO_FEAT_IPM_QUILT:
        return SpecForKind(StatusLayerKind::Surface);
    case PRO_FEAT_DRV_TOOL_SKETCH:
    case PRO_FEAT_CE_SKET:
        return SpecForKind(StatusLayerKind::Sketch);
    default:
        return nullptr;
    }
}

bool IsLayerBlanked(ProLayerDisplay display)
{
    return display == PRO_LAYER_TYPE_BLANK || display == PRO_LAYER_TYPE_HIDDEN;
}

bool IsUsableShownFeature(ProFeature *feature, const StatusLayerSpec **spec_out, ProFeattype *type_out)
{
    if (feature == nullptr) {
        return false;
    }

    ProBoolean is_hidden = PRO_B_FALSE;
    if (ProModelitemIsHidden(reinterpret_cast<ProModelitem *>(feature), &is_hidden) != PRO_TK_NO_ERROR ||
        is_hidden == PRO_B_TRUE) {
        return false;
    }

    ProBoolean visible_in_tree = PRO_B_FALSE;
    if (ProFeatureVisibilityGet(feature, &visible_in_tree) != PRO_TK_NO_ERROR ||
        visible_in_tree != PRO_B_TRUE) {
        return false;
    }

    ProFeatStatus status = PRO_FEAT_INVALID;
    if (ProFeatureStatusGet(feature, &status) == PRO_TK_NO_ERROR &&
        status != PRO_FEAT_ACTIVE &&
        status != PRO_FEAT_UNREGENERATED) {
        return false;
    }

    ProFeattype type = 0;
    if (ProFeatureTypeGet(feature, &type) != PRO_TK_NO_ERROR) {
        return false;
    }

    const StatusLayerSpec *spec = SpecForFeatureType(type);
    if (spec == nullptr) {
        return false;
    }

    if (spec_out != nullptr) {
        *spec_out = spec;
    }
    if (type_out != nullptr) {
        *type_out = type;
    }
    return true;
}

struct DecisionContext {
    int managed_layers_found = 0;
    bool found_visible_status_layer = false;
};

struct ApplyContext {
    bool hide_requested = true;
    FeatureVisibilitySummary *summary = nullptr;
    const std::function<void(const std::string &line)> *log_sink = nullptr;
    std::unordered_map<std::uintptr_t, std::array<ProLayer, static_cast<std::size_t>(StatusLayerKind::Count)>> status_layers_by_model;
    std::unordered_map<std::uintptr_t, std::array<bool, static_cast<std::size_t>(StatusLayerKind::Count)>> status_layer_cached_by_model;
    std::unordered_set<std::uintptr_t> models_needing_layer_update;
};

bool FindStatusLayerByName(ProMdl owner, const wchar_t *status_layer_name, ProLayer *layer_out)
{
    if (owner == nullptr || status_layer_name == nullptr || layer_out == nullptr) {
        return false;
    }

    ProLayer *layers = nullptr;
    const ProError collect_status = ProMdlLayersCollect(owner, &layers);
    if (collect_status != PRO_TK_NO_ERROR || layers == nullptr) {
        if (layers != nullptr) {
            ProArrayFree(reinterpret_cast<ProArray *>(&layers));
        }
        return false;
    }

    int layer_count = 0;
    if (ProArraySizeGet(reinterpret_cast<ProArray>(layers), &layer_count) != PRO_TK_NO_ERROR) {
        ProArrayFree(reinterpret_cast<ProArray *>(&layers));
        return false;
    }

    bool found = false;
    for (int i = 0; i < layer_count; ++i) {
        ProName layer_name = {0};
        if (ProModelitemNameGet(reinterpret_cast<ProModelitem *>(&layers[i]), layer_name) == PRO_TK_NO_ERROR &&
            IsProNameEqual(layer_name, status_layer_name)) {
            *layer_out = layers[i];
            found = true;
            break;
        }
    }

    ProArrayFree(reinterpret_cast<ProArray *>(&layers));
    return found;
}

bool GetExistingStatusLayer(ProMdl owner, const StatusLayerSpec &spec, ProLayer *layer_out)
{
    return FindStatusLayerByName(owner, spec.name, layer_out);
}

bool EnsureStatusLayer(ProMdl owner, const StatusLayerSpec &spec, ApplyContext &ctx, ProLayer *layer_out)
{
    if (owner == nullptr || ctx.summary == nullptr || layer_out == nullptr) {
        return false;
    }

    const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(owner);
    const auto layer_index = static_cast<std::size_t>(spec.kind);
    auto &cached_flags = ctx.status_layer_cached_by_model[key];
    auto &cached_layers = ctx.status_layers_by_model[key];
    if (cached_flags[layer_index]) {
        *layer_out = cached_layers[layer_index];
        return true;
    }

    ProName layer_name = {0};
    CopyToProName(layer_name, spec.name);

    ProLayer layer = {};
    ProError status = ProLayerCreate(owner, layer_name, &layer);
    if (status == PRO_TK_NO_ERROR) {
        ++ctx.summary->status_layers_created;
        *layer_out = layer;
        if (ctx.log_sink != nullptr) {
            LogLine(*ctx.log_sink,
                    "feature-visibility status-layer created model=%s layer=%s",
                    autobbox::common::WToA(autobbox::creo::ModelName(owner, L"<unknown>").c_str()).c_str(),
                    autobbox::common::WToA(spec.name).c_str());
        }
    } else if (status == PRO_TK_E_FOUND || GetExistingStatusLayer(owner, spec, &layer)) {
        if (status == PRO_TK_E_FOUND && !GetExistingStatusLayer(owner, spec, &layer)) {
            ++ctx.summary->status_layer_failures;
            return false;
        }
        ++ctx.summary->status_layers_reused;
    } else {
        ++ctx.summary->status_layer_failures;
        if (ctx.log_sink != nullptr) {
            LogLine(*ctx.log_sink,
                    "feature-visibility status-layer create-failed model=%s layer=%s status=%d",
                    autobbox::common::WToA(autobbox::creo::ModelName(owner, L"<unknown>").c_str()).c_str(),
                    autobbox::common::WToA(spec.name).c_str(),
                    static_cast<int>(status));
        }
        return false;
    }

    cached_layers[layer_index] = layer;
    cached_flags[layer_index] = true;
    *layer_out = layer;
    return true;
}

void AddFeatureToStatusLayer(ProFeature *feature, const StatusLayerSpec &spec, ApplyContext &ctx)
{
    if (feature == nullptr || feature->owner == nullptr || ctx.summary == nullptr) {
        return;
    }

    ProLayer layer = {};
    if (!EnsureStatusLayer(feature->owner, spec, ctx, &layer)) {
        return;
    }

    ProLayerItem layer_item = {};
    ProError status = ProLayerItemInit(PRO_LAYER_FEAT, feature->id, feature->owner, &layer_item);
    if (status != PRO_TK_NO_ERROR) {
        ++ctx.summary->status_layer_failures;
        return;
    }

    ProLayerItemStatus item_status = PRO_LAY_ITEM_STATUS_NONE;
    status = ProLayeritemLayerStatusGet(nullptr, &layer_item, &layer, &item_status);
    if (status == PRO_TK_NO_ERROR) {
        ++ctx.summary->status_layer_items_existing;
        return;
    }
    if (status != PRO_TK_E_NOT_FOUND) {
        ++ctx.summary->status_layer_failures;
        return;
    }

    status = ProLayerItemAddNoUpdate(&layer, &layer_item);
    if (status == PRO_TK_NO_ERROR) {
        ++ctx.summary->status_layer_items_added;
        ctx.models_needing_layer_update.insert(reinterpret_cast<std::uintptr_t>(feature->owner));
    } else {
        ++ctx.summary->status_layer_failures;
    }
}

ProError ApplyFeatureVisitAction(ProFeature *feature, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || app_data == nullptr || feature == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    auto *ctx = reinterpret_cast<ApplyContext *>(app_data);
    if (ctx->summary == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    ProFeattype type = 0;
    const StatusLayerSpec *spec = nullptr;
    if (!IsUsableShownFeature(feature, &spec, &type) || spec == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    ++ctx->summary->target_features;
    AddFeatureToStatusLayer(feature, *spec, *ctx);
    return PRO_TK_NO_ERROR;
}

void RecordExistingLayerDisplay(ProMdl owner, DecisionContext &decision)
{
    if (owner == nullptr) {
        return;
    }

    for (const auto &spec : kStatusLayers) {
        ProLayer layer = {};
        if (!GetExistingStatusLayer(owner, spec, &layer)) {
            continue;
        }

        ++decision.managed_layers_found;
        ProLayerDisplay display_status = PRO_LAYER_TYPE_NONE;
        if (ProLayerDisplaystatusGet(&layer, &display_status) != PRO_TK_NO_ERROR ||
            !IsLayerBlanked(display_status)) {
            decision.found_visible_status_layer = true;
        }
    }
}

void ApplyExistingLayerDisplay(ProMdl owner, ApplyContext &ctx)
{
    if (owner == nullptr || ctx.summary == nullptr) {
        return;
    }

    bool update_needed_for_model = false;
    const ProLayerDisplay desired_status = ctx.hide_requested ? PRO_LAYER_TYPE_BLANK : PRO_LAYER_TYPE_NORMAL;

    for (const auto &spec : kStatusLayers) {
        ProLayer layer = {};
        if (!GetExistingStatusLayer(owner, spec, &layer)) {
            continue;
        }

        ++ctx.summary->status_layers_reused;
        ProLayerDisplay current_status = PRO_LAYER_TYPE_NONE;
        if (ProLayerDisplaystatusGet(&layer, &current_status) == PRO_TK_NO_ERROR &&
            ((ctx.hide_requested && IsLayerBlanked(current_status)) ||
             (!ctx.hide_requested && !IsLayerBlanked(current_status)))) {
            ++ctx.summary->unchanged;
            continue;
        }

        ProBoolean tree_update_needed = PRO_B_FALSE;
        const ProError status = ProLayerDisplaystatusNoUpdateSet(&layer, desired_status, &tree_update_needed);
        if (status == PRO_TK_NO_ERROR) {
            ++ctx.summary->changed;
            update_needed_for_model = true;
        } else {
            ++ctx.summary->failed;
            if (ctx.log_sink != nullptr) {
                LogLine(*ctx.log_sink,
                        "feature-visibility status-layer display-failed model=%s layer=%s desired=%d status=%d",
                        autobbox::common::WToA(autobbox::creo::ModelName(owner, L"<unknown>").c_str()).c_str(),
                        autobbox::common::WToA(spec.name).c_str(),
                        static_cast<int>(desired_status),
                        static_cast<int>(status));
            }
        }
    }

    if (update_needed_for_model) {
        ctx.models_needing_layer_update.insert(reinterpret_cast<std::uintptr_t>(owner));
    }
}

struct ModelVisitContext {
    std::unordered_set<std::uintptr_t> visited_models;
    DecisionContext *decision = nullptr;
    ApplyContext *apply = nullptr;
    bool apply_layer_display_only = false;
};

bool VisitModelFeatures(ProMdl mdl, ModelVisitContext &ctx)
{
    if (mdl == nullptr || !autobbox::creo::IsPartOrAsm(mdl)) {
        return false;
    }

    const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(mdl);
    if (!ctx.visited_models.insert(key).second) {
        return false;
    }

    if (ctx.decision != nullptr) {
        RecordExistingLayerDisplay(mdl, *ctx.decision);
    }
    if (ctx.apply != nullptr) {
        ++ctx.apply->summary->models_visited;
        if (ctx.apply_layer_display_only) {
            ApplyExistingLayerDisplay(mdl, *ctx.apply);
        } else {
            ProSolidFeatVisit(reinterpret_cast<ProSolid>(mdl), ApplyFeatureVisitAction, nullptr, ctx.apply);
            ApplyExistingLayerDisplay(mdl, *ctx.apply);
        }
    }
    return true;
}

ProError AssemblyComponentVisitAction(ProAsmcomppath *,
                                      ProSolid handle,
                                      ProBoolean down,
                                      ProAppData app_data)
{
    if (down != PRO_B_TRUE || handle == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    auto *ctx = reinterpret_cast<ModelVisitContext *>(app_data);
    VisitModelFeatures(reinterpret_cast<ProMdl>(handle), *ctx);
    return PRO_TK_NO_ERROR;
}

void VisitAssemblyAndDisplayedComponents(ProMdl root, ModelVisitContext &ctx)
{
    VisitModelFeatures(root, ctx);
    ProSolidDispCompVisit(
        reinterpret_cast<ProSolid>(root),
        AssemblyComponentVisitAction,
        nullptr,
        &ctx);
}

void UpdateLayerDisplayForTouchedModels(const std::unordered_set<std::uintptr_t> &model_keys)
{
    for (const std::uintptr_t key : model_keys) {
        ProMdl mdl = reinterpret_cast<ProMdl>(key);
        if (mdl != nullptr) {
            ProLayerDisplaystatusUpdate(mdl);
        }
    }
}

void RefreshCurrentWindow()
{
    int window_id = -1;
    if (ProWindowCurrentGet(&window_id) == PRO_TK_NO_ERROR && window_id >= 0) {
        ProWindowRefresh(window_id);
        ProWindowRepaint(window_id);
    }
}

} // namespace

bool ToggleAssemblyDatumFeatureVisibility(
    FeatureVisibilitySummary &summary,
    const std::function<void(const std::string &line)> &log_sink)
{
    summary = {};

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        autobbox::creo::ModelType(current) != PRO_MDL_ASSEMBLY) {
        return false;
    }

    DecisionContext decision;
    ModelVisitContext decision_visit;
    decision_visit.decision = &decision;
    VisitAssemblyAndDisplayedComponents(current, decision_visit);

    summary.hide_requested = decision.managed_layers_found == 0 || decision.found_visible_status_layer;

    ApplyContext apply;
    apply.hide_requested = summary.hide_requested;
    apply.summary = &summary;
    apply.log_sink = &log_sink;
    ModelVisitContext apply_visit;
    apply_visit.apply = &apply;
    apply_visit.apply_layer_display_only = !summary.hide_requested;
    VisitAssemblyAndDisplayedComponents(current, apply_visit);
    UpdateLayerDisplayForTouchedModels(apply.models_needing_layer_update);

    LogLine(log_sink,
            "feature-visibility summary action=%s models=%d visible_targets_scanned=%d layer_display_changed=%d layer_display_unchanged=%d layer_display_failed=%d status_layers_created=%d status_layers_seen=%d status_items_added=%d status_items_existing=%d status_layer_failures=%d",
            summary.hide_requested ? "hide" : "show",
            summary.models_visited,
            summary.target_features,
            summary.changed,
            summary.unchanged,
            summary.failed,
            summary.status_layers_created,
            summary.status_layers_reused,
            summary.status_layer_items_added,
            summary.status_layer_items_existing,
            summary.status_layer_failures);

    RefreshCurrentWindow();
    return summary.failed == 0 &&
           (summary.target_features > 0 ||
            summary.status_layers_created > 0 ||
            summary.status_layers_reused > 0 ||
            summary.changed > 0 ||
            summary.unchanged > 0);
}

std::wstring BuildFeatureVisibilitySummaryText(const FeatureVisibilitySummary &summary)
{
    std::wstring text = summary.hide_requested
                            ? L"\u5df2\u901a\u8fc7\u5206\u7c7b\u72b6\u6001\u5c42\u9690\u85cf\u66f2\u7ebf/\u66f2\u9762/\u8349\u7ed8\u7279\u5f81"
                            : L"\u5df2\u901a\u8fc7\u5206\u7c7b\u72b6\u6001\u5c42\u663e\u793a\u66f2\u7ebf/\u66f2\u9762/\u8349\u7ed8\u7279\u5f81";
    text += L"\n\u8bbf\u95ee\u6a21\u578b\uff1a" + std::to_wstring(summary.models_visited);
    if (summary.hide_requested) {
        text += L"\n\u8bfb\u53d6\u672a\u9690\u85cf\u7279\u5f81\uff1a" + std::to_wstring(summary.target_features);
    }
    text += L"\n\u6a21\u578b\u72b6\u6001\u5c42\uff1aAB_MODEL_STATUS_CURVE / AB_MODEL_STATUS_SURFACE / AB_MODEL_STATUS_SKETCH";
    text += L"\n\u65b0\u5efa\u56fe\u5c42\uff1a" + std::to_wstring(summary.status_layers_created);
    if (summary.status_layers_reused > 0) {
        text += L"\n\u5904\u7406\u56fe\u5c42\uff1a" + std::to_wstring(summary.status_layers_reused);
    }
    if (summary.hide_requested) {
        text += L"\n\u52a0\u5165\u56fe\u5c42\uff1a" + std::to_wstring(summary.status_layer_items_added);
    }
    if (summary.status_layer_items_existing > 0) {
        text += L"\n\u56fe\u5c42\u5df2\u5305\u542b\uff1a" + std::to_wstring(summary.status_layer_items_existing);
    }
    text += L"\n\u56fe\u5c42\u663e\u793a\u72b6\u6001\u5df2\u53d8\u66f4\uff1a" + std::to_wstring(summary.changed);
    if (summary.unchanged > 0) {
        text += L"\n\u672a\u53d8\u66f4\uff1a" + std::to_wstring(summary.unchanged);
    }
    if (summary.failed > 0) {
        text += L"\n\u5931\u8d25\uff1a" + std::to_wstring(summary.failed);
    }
    if (summary.status_layer_failures > 0) {
        text += L"\n\u56fe\u5c42\u5904\u7406\u5931\u8d25\uff1a" + std::to_wstring(summary.status_layer_failures);
    }
    return text;
}

} // namespace autobbox::application
