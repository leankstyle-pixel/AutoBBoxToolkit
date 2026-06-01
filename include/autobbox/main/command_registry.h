#pragma once

#include <ProToolkit.h>
#include <ProUICmd.h>

#include <string>

namespace autobbox::main {

struct CommandIds {
    uiCmdCmdId run = nullptr;
    uiCmdCmdId run_volume = nullptr;
    uiCmdCmdId create_iso = nullptr;
    uiCmdCmdId delete_params = nullptr;
    uiCmdCmdId model_diagnostics = nullptr;
    uiCmdCmdId model_structure_analyzer = nullptr;
    uiCmdCmdId create_dwg3_views = nullptr;
    uiCmdCmdId arrange_dwg_views = nullptr;
    uiCmdCmdId arrange_balloons = nullptr;
    uiCmdCmdId rebuild_balloons = nullptr;
    uiCmdCmdId sync_page_scale = nullptr;
    uiCmdCmdId drawing_view_brush = nullptr;
    uiCmdCmdId smart_dimension = nullptr;
    uiCmdCmdId drawing_export = nullptr;
    uiCmdCmdId force_open_drawing = nullptr;
    uiCmdCmdId split_instances = nullptr;
    uiCmdCmdId clean_relations = nullptr;
    uiCmdCmdId add_relations = nullptr;
    uiCmdCmdId param_tool = nullptr;
    uiCmdCmdId random_color = nullptr;
    uiCmdCmdId quick_rename = nullptr;
    uiCmdCmdId save_copy_to_workdir = nullptr;
    uiCmdCmdId batch_rename = nullptr;
    uiCmdCmdId quick_simprep = nullptr;
    uiCmdCmdId sheetmetal_flat_batch = nullptr;
    uiCmdCmdId family_table_manager = nullptr;
    uiCmdCmdId feature_visibility = nullptr;

    uiCmdCmdId opt_parts = nullptr;
    uiCmdCmdId opt_assemblies = nullptr;
    uiCmdCmdId opt_surface = nullptr;
    uiCmdCmdId opt_curve = nullptr;
    uiCmdCmdId opt_recompute = nullptr;
    uiCmdCmdId opt_top_level_only = nullptr;
};

struct CommandCallbacks {
    uiCmdCmdActFn on_run = nullptr;
    uiCmdCmdActFn on_run_volume = nullptr;
    uiCmdCmdActFn on_create_iso = nullptr;
    uiCmdCmdActFn on_delete_params = nullptr;
    uiCmdCmdActFn on_model_diagnostics = nullptr;
    uiCmdCmdActFn on_model_structure_analyzer = nullptr;
    uiCmdCmdActFn on_create_dwg3_views = nullptr;
    uiCmdCmdActFn on_arrange_dwg_views = nullptr;
    uiCmdCmdActFn on_arrange_balloons = nullptr;
    uiCmdCmdActFn on_rebuild_balloons = nullptr;
    uiCmdCmdActFn on_sync_page_scale = nullptr;
    uiCmdCmdActFn on_drawing_view_brush = nullptr;
    uiCmdCmdActFn on_smart_dimension = nullptr;
    uiCmdCmdActFn on_drawing_export = nullptr;
    uiCmdCmdActFn on_force_open_drawing = nullptr;
    uiCmdCmdActFn on_split_instances = nullptr;
    uiCmdCmdActFn on_clean_relations = nullptr;
    uiCmdCmdActFn on_add_relations = nullptr;
    uiCmdCmdActFn on_param_tool = nullptr;
    uiCmdCmdActFn on_random_color = nullptr;
    uiCmdCmdActFn on_quick_rename = nullptr;
    uiCmdCmdActFn on_save_copy_to_workdir = nullptr;
    uiCmdCmdActFn on_batch_rename = nullptr;
    uiCmdCmdActFn on_quick_simprep = nullptr;
    uiCmdCmdActFn on_sheetmetal_flat_batch = nullptr;
    uiCmdCmdActFn on_family_table_manager = nullptr;
    uiCmdCmdActFn on_feature_visibility = nullptr;

    uiCmdCmdActFn on_option_parts = nullptr;
    uiCmdCmdActFn on_option_assemblies = nullptr;
    uiCmdCmdActFn on_option_surface = nullptr;
    uiCmdCmdActFn on_option_curve = nullptr;
    uiCmdCmdActFn on_option_recompute = nullptr;
    uiCmdCmdActFn on_option_top2 = nullptr;

    uiCmdCmdValFn set_option_parts = nullptr;
    uiCmdCmdValFn set_option_assemblies = nullptr;
    uiCmdCmdValFn set_option_surface = nullptr;
    uiCmdCmdValFn set_option_curve = nullptr;
    uiCmdCmdValFn set_option_recompute = nullptr;
    uiCmdCmdValFn set_option_top2 = nullptr;

    uiCmdAccessFn access_part_or_asm = nullptr;
    uiCmdAccessFn access_part_asm_only = nullptr;
    uiCmdAccessFn access_assembly_only = nullptr;
    uiCmdAccessFn access_drawing_only = nullptr;
    uiCmdAccessFn access_always = nullptr;
};

ProError RegisterPluginCommands(const CommandCallbacks &callbacks,
                                CommandIds &ids,
                                const std::string &startup_log_path);

void ApplyCommandDesignations(const CommandIds &ids,
                              const std::wstring &message_file_name,
                              const std::string &message_file_path,
                              const std::string &startup_log_path);

void ApplyCommandIcons(const CommandIds &ids,
                       const std::wstring &text_root,
                       const std::string &startup_log_path);

void LoadRibbonDefinition(const std::wstring &resolved_ribbon_file,
                          const std::wstring &default_ribbon_file_name,
                          const std::string &ribbon_file_path_for_log,
                          const std::string &startup_log_path);

void AddLegacyMenuFallback(const CommandIds &ids,
                           const std::wstring &message_file_name,
                           const std::string &startup_log_path);

} // namespace autobbox::main
