#pragma once

#include <ProToolkit.h>

#include "autobbox/main/command_registry.h"

namespace autobbox::main {

struct PluginCommandActions {
    int (*run)() = nullptr;
    int (*run_volume)() = nullptr;
    int (*create_iso)() = nullptr;
    int (*delete_params)() = nullptr;
    int (*model_diagnostics)() = nullptr;
    int (*model_structure_analyzer)() = nullptr;
    int (*create_dwg3_views)() = nullptr;
    int (*arrange_dwg_views)() = nullptr;
    int (*arrange_balloons)() = nullptr;
    int (*rebuild_balloons)() = nullptr;
    int (*sync_page_scale)() = nullptr;
    int (*drawing_view_brush)() = nullptr;
    int (*smart_dimension)() = nullptr;
    int (*drawing_export)() = nullptr;
    int (*force_open_drawing)() = nullptr;
    int (*split_instances)() = nullptr;
    int (*clean_relations)() = nullptr;
    int (*add_relations)() = nullptr;
    int (*param_tool)() = nullptr;
    int (*random_color)() = nullptr;
    int (*quick_rename)() = nullptr;
    int (*save_copy_to_workdir)() = nullptr;
    int (*batch_rename)() = nullptr;
    int (*quick_simprep)() = nullptr;
    int (*sheetmetal_flat_batch)() = nullptr;
    int (*family_table_manager)() = nullptr;
    int (*feature_visibility)() = nullptr;
};

struct PluginOptionState {
    ProBoolean *parts = nullptr;
    ProBoolean *assemblies = nullptr;
    ProBoolean *surface = nullptr;
    ProBoolean *curve = nullptr;
    ProBoolean *recompute = nullptr;
    ProBoolean *top_level_only = nullptr;
};

CommandCallbacks BuildPluginCommandCallbacks(const PluginCommandActions &actions,
                                             const PluginOptionState &option_state);

} // namespace autobbox::main
