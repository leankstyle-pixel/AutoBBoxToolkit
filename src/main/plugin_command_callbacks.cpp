#include "autobbox/main/plugin_command_callbacks.h"

#include "autobbox/creo/model_info.h"

#include <ProMdl.h>
#include <ProToolkit.h>
#include <ProUICmd.h>

namespace autobbox::main {

namespace {

PluginCommandActions g_actions = {};
PluginOptionState g_option_state = {};

int InvokeAction(int (*fn)())
{
    return fn != nullptr ? fn() : 0;
}

int OnRun(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.run);
}

int OnRunVolume(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.run_volume);
}

int OnCreateIso(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.create_iso);
}

int OnDeleteParams(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.delete_params);
}

int OnModelDiagnostics(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.model_diagnostics);
}

int OnModelStructureAnalyzer(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.model_structure_analyzer);
}

int OnCreateDwg3Views(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.create_dwg3_views);
}

int OnArrangeDwgViews(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.arrange_dwg_views);
}

int OnArrangeBalloons(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.arrange_balloons);
}

int OnRebuildBalloons(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.rebuild_balloons);
}

int OnSyncPageScale(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.sync_page_scale);
}

int OnDrawingViewBrush(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.drawing_view_brush);
}

int OnSmartDimension(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.smart_dimension);
}

int OnDrawingExport(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.drawing_export);
}

int OnForceOpenDrawing(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.force_open_drawing);
}

int OnSplitInstances(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.split_instances);
}

int OnCleanRelations(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.clean_relations);
}

int OnAddRelations(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.add_relations);
}

int OnParamTool(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.param_tool);
}

int OnRandomColor(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.random_color);
}

int OnQuickRename(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.quick_rename);
}

int OnSaveCopyToWorkdir(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.save_copy_to_workdir);
}

int OnBatchRename(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.batch_rename);
}

int OnQuickSimprep(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.quick_simprep);
}

int OnSheetmetalFlatBatch(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.sheetmetal_flat_batch);
}

int OnFamilyTableManager(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.family_table_manager);
}

int OnFeatureVisibility(uiCmdCmdId, uiCmdValue *, void *)
{
    return InvokeAction(g_actions.feature_visibility);
}

int SyncOptionFromUi(uiCmdValue *value, ProBoolean *target)
{
    if (value != nullptr && target != nullptr) {
        ProCmdChkbuttonValueGet(value, target);
    }
    return 0;
}

int SyncOptionToUi(uiCmdValue *value, const ProBoolean *source)
{
    if (value != nullptr && source != nullptr) {
        ProCmdChkbuttonValueSet(value, *source);
    }
    return 0;
}

int OnOptionParts(uiCmdCmdId, uiCmdValue *value, void *)
{
    return SyncOptionFromUi(value, g_option_state.parts);
}

int OnOptionAssemblies(uiCmdCmdId, uiCmdValue *value, void *)
{
    return SyncOptionFromUi(value, g_option_state.assemblies);
}

int OnOptionSurface(uiCmdCmdId, uiCmdValue *value, void *)
{
    return SyncOptionFromUi(value, g_option_state.surface);
}

int OnOptionCurve(uiCmdCmdId, uiCmdValue *value, void *)
{
    return SyncOptionFromUi(value, g_option_state.curve);
}

int OnOptionRecompute(uiCmdCmdId, uiCmdValue *value, void *)
{
    return SyncOptionFromUi(value, g_option_state.recompute);
}

int OnOptionTop2(uiCmdCmdId, uiCmdValue *value, void *)
{
    return SyncOptionFromUi(value, g_option_state.top_level_only);
}

int SetOptionParts(uiCmdCmdId, uiCmdValue *value)
{
    return SyncOptionToUi(value, g_option_state.parts);
}

int SetOptionAssemblies(uiCmdCmdId, uiCmdValue *value)
{
    return SyncOptionToUi(value, g_option_state.assemblies);
}

int SetOptionSurface(uiCmdCmdId, uiCmdValue *value)
{
    return SyncOptionToUi(value, g_option_state.surface);
}

int SetOptionCurve(uiCmdCmdId, uiCmdValue *value)
{
    return SyncOptionToUi(value, g_option_state.curve);
}

int SetOptionRecompute(uiCmdCmdId, uiCmdValue *value)
{
    return SyncOptionToUi(value, g_option_state.recompute);
}

int SetOptionTop2(uiCmdCmdId, uiCmdValue *value)
{
    return SyncOptionToUi(value, g_option_state.top_level_only);
}

bool CurrentModelIsPartOrAsm()
{
    ProMdl current = nullptr;
    return ProMdlCurrentGet(&current) == PRO_TK_NO_ERROR &&
           current != nullptr &&
           autobbox::creo::IsPartOrAsm(current);
}

uiCmdAccessState AccessPartOrAsm(uiCmdAccessMode)
{
    if (CurrentModelIsPartOrAsm()) {
        return ACCESS_AVAILABLE;
    }
    return ACCESS_UNAVAILABLE;
}

uiCmdAccessState AccessPartAsmOnly(uiCmdAccessMode)
{
    if (CurrentModelIsPartOrAsm()) {
        return ACCESS_AVAILABLE;
    }
    return ACCESS_UNAVAILABLE;
}

uiCmdAccessState AccessDrawingOnly(uiCmdAccessMode)
{
    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) == PRO_TK_NO_ERROR &&
        current != nullptr &&
        autobbox::creo::ModelType(current) == PRO_MDL_DRAWING) {
        return ACCESS_AVAILABLE;
    }
    return ACCESS_UNAVAILABLE;
}

uiCmdAccessState AccessAssemblyOnly(uiCmdAccessMode)
{
    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) == PRO_TK_NO_ERROR &&
        current != nullptr &&
        autobbox::creo::ModelType(current) == PRO_MDL_ASSEMBLY) {
        return ACCESS_AVAILABLE;
    }
    return ACCESS_UNAVAILABLE;
}

uiCmdAccessState AccessAlways(uiCmdAccessMode)
{
    return ACCESS_AVAILABLE;
}

} // namespace

CommandCallbacks BuildPluginCommandCallbacks(const PluginCommandActions &actions,
                                             const PluginOptionState &option_state)
{
    g_actions = actions;
    g_option_state = option_state;

    CommandCallbacks callbacks = {};
    callbacks.on_run = OnRun;
    callbacks.on_run_volume = OnRunVolume;
    callbacks.on_create_iso = OnCreateIso;
    callbacks.on_delete_params = OnDeleteParams;
    callbacks.on_model_diagnostics = OnModelDiagnostics;
    callbacks.on_model_structure_analyzer = OnModelStructureAnalyzer;
    callbacks.on_create_dwg3_views = OnCreateDwg3Views;
    callbacks.on_arrange_dwg_views = OnArrangeDwgViews;
    callbacks.on_arrange_balloons = OnArrangeBalloons;
    callbacks.on_rebuild_balloons = OnRebuildBalloons;
    callbacks.on_sync_page_scale = OnSyncPageScale;
    callbacks.on_drawing_view_brush = OnDrawingViewBrush;
    callbacks.on_smart_dimension = OnSmartDimension;
    callbacks.on_drawing_export = OnDrawingExport;
    callbacks.on_force_open_drawing = OnForceOpenDrawing;
    callbacks.on_split_instances = OnSplitInstances;
    callbacks.on_clean_relations = OnCleanRelations;
    callbacks.on_add_relations = OnAddRelations;
    callbacks.on_param_tool = OnParamTool;
    callbacks.on_random_color = OnRandomColor;
    callbacks.on_quick_rename = OnQuickRename;
    callbacks.on_save_copy_to_workdir = OnSaveCopyToWorkdir;
    callbacks.on_batch_rename = OnBatchRename;
    callbacks.on_quick_simprep = OnQuickSimprep;
    callbacks.on_sheetmetal_flat_batch = OnSheetmetalFlatBatch;
    callbacks.on_family_table_manager = OnFamilyTableManager;
    callbacks.on_feature_visibility = OnFeatureVisibility;
    callbacks.on_option_parts = OnOptionParts;
    callbacks.on_option_assemblies = OnOptionAssemblies;
    callbacks.on_option_surface = OnOptionSurface;
    callbacks.on_option_curve = OnOptionCurve;
    callbacks.on_option_recompute = OnOptionRecompute;
    callbacks.on_option_top2 = OnOptionTop2;
    callbacks.set_option_parts = SetOptionParts;
    callbacks.set_option_assemblies = SetOptionAssemblies;
    callbacks.set_option_surface = SetOptionSurface;
    callbacks.set_option_curve = SetOptionCurve;
    callbacks.set_option_recompute = SetOptionRecompute;
    callbacks.set_option_top2 = SetOptionTop2;
    callbacks.access_part_or_asm = AccessPartOrAsm;
    callbacks.access_part_asm_only = AccessPartAsmOnly;
    callbacks.access_assembly_only = AccessAssemblyOnly;
    callbacks.access_drawing_only = AccessDrawingOnly;
    callbacks.access_always = AccessAlways;
    return callbacks;
}

} // namespace autobbox::main
