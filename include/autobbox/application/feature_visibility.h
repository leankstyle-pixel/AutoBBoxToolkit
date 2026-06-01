#pragma once

#include <ProToolkit.h>

#include <functional>
#include <string>

namespace autobbox::application {

struct FeatureVisibilitySummary {
    bool hide_requested = true;
    int models_visited = 0;
    int target_features = 0;
    int changed = 0;
    int unchanged = 0;
    int failed = 0;
    int status_layers_created = 0;
    int status_layers_reused = 0;
    int status_layer_items_added = 0;
    int status_layer_items_existing = 0;
    int status_layer_failures = 0;
};

bool ToggleAssemblyDatumFeatureVisibility(
    FeatureVisibilitySummary &summary,
    const std::function<void(const std::string &line)> &log_sink);

std::wstring BuildFeatureVisibilitySummaryText(const FeatureVisibilitySummary &summary);

} // namespace autobbox::application
