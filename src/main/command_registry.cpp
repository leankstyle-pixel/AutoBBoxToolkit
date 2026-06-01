#include "autobbox/main/command_registry.h"

#include "autobbox/common/files.h"
#include "autobbox/common/log.h"
#include "autobbox/common/strings.h"

#include <ProMenuBar.h>
#include <ProRibbon.h>

#include <cstdarg>
#include <string>
#include <vector>

namespace autobbox::main {

namespace {

template <size_t N>
void CopyCStr(char (&dest)[N], const char *src)
{
    if (N == 0) {
        return;
    }
    size_t i = 0;
    if (src != nullptr) {
        while (i + 1 < N && src[i] != '\0') {
            dest[i] = src[i];
            ++i;
        }
    }
    dest[i] = '\0';
}

template <size_t N>
void CopyWStr(wchar_t (&dest)[N], const wchar_t *src)
{
    if (N == 0) {
        return;
    }
    size_t i = 0;
    if (src != nullptr) {
        while (i + 1 < N && src[i] != L'\0') {
            dest[i] = src[i];
            ++i;
        }
    }
    dest[i] = L'\0';
}

void LogStartupLine(const std::string &startup_log_path, const char *fmt, ...)
{
    if (startup_log_path.empty() || fmt == nullptr) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    autobbox::common::AppendFormattedLine(startup_log_path, nullptr, fmt, args);
    va_end(args);
}

ProError RegisterActionWithPriority(const char *name,
                                    uiCmdCmdActFn fn,
                                    uiCmdPriority priority,
                                    uiCmdAccessFn access_fn,
                                    uiCmdCmdId *id)
{
    return ProCmdActionAdd(
        const_cast<char *>(name),
        fn,
        priority,
        access_fn,
        PRO_B_FALSE,
        PRO_B_FALSE,
        id);
}

ProError RegisterAction(const char *name, uiCmdCmdActFn fn, uiCmdAccessFn access_fn, uiCmdCmdId *id)
{
    return RegisterActionWithPriority(name, fn, uiProeAsynch, access_fn, id);
}

void RegisterActionAlias(const char *name,
                         uiCmdCmdActFn fn,
                         uiCmdPriority priority,
                         uiCmdAccessFn access_fn,
                         const std::string &startup_log_path)
{
    uiCmdCmdId alias_id = nullptr;
    const ProError st = RegisterActionWithPriority(name, fn, priority, access_fn, &alias_id);
    LogStartupLine(startup_log_path,
                   "RegisterActionAlias(%s) -> status=%d id=%p",
                   name != nullptr ? name : "<null>",
                   static_cast<int>(st),
                   static_cast<void *>(alias_id));
}

ProError RegisterOption(const char *name,
                        uiCmdCmdActFn on_cb,
                        uiCmdCmdValFn set_cb,
                        uiCmdAccessFn access_fn,
                        uiCmdCmdId *id)
{
    return ProCmdOptionAdd(
        const_cast<char *>(name),
        on_cb,
        PRO_B_TRUE,
        set_cb,
        access_fn,
        PRO_B_FALSE,
        PRO_B_FALSE,
        id);
}

void TryDesignate(uiCmdCmdId id,
                  const char *lbl,
                  const char *help,
                  const char *desc,
                  const std::wstring &message_file_name,
                  const std::string &message_file_path,
                  const std::string &startup_log_path)
{
    if (id == nullptr || lbl == nullptr || help == nullptr || desc == nullptr || message_file_path.empty()) {
        return;
    }
    std::FILE *fp = autobbox::common::OpenFile(message_file_path, "r");
    if (fp == nullptr) {
        LogStartupLine(startup_log_path, "Designate(%s) skipped, msg file missing: %s", lbl, message_file_path.c_str());
        return;
    }
    std::fclose(fp);

    ProFileName msg_token = {0};
    CopyWStr(msg_token, message_file_name.c_str());
    ProCmdItemLabel lbl_key = {0};
    ProCmdLineHelp help_key = {0};
    ProCmdDescription desc_key = {0};
    CopyCStr(lbl_key, lbl);
    CopyCStr(help_key, help);
    CopyCStr(desc_key, desc);
    LogStartupLine(startup_log_path,
                   "Designate(%s) begin file=%s token=%s",
                   lbl,
                   message_file_path.c_str(),
                   autobbox::common::WToA(msg_token).c_str());
    const ProError st = ProCmdDesignate(id, lbl_key, help_key, desc_key, msg_token);
    LogStartupLine(startup_log_path, "Designate(%s) -> %d", lbl, static_cast<int>(st));
}

void TryIcon(uiCmdCmdId id,
             const char *icon,
             const std::wstring &text_root,
             const std::string &startup_log_path)
{
    if (id == nullptr || icon == nullptr || icon[0] == '\0') {
        return;
    }

    std::vector<std::string> candidates;
    candidates.emplace_back(icon);
    candidates.emplace_back(std::string("resource\\") + icon);
    if (!text_root.empty()) {
        const std::wstring w_icon = autobbox::common::AToW(icon);
        const std::wstring c1 = autobbox::common::JoinPath(
            autobbox::common::JoinPath(text_root, L"resource"),
            w_icon.c_str());
        const std::wstring c2 = autobbox::common::JoinPath(
            autobbox::common::JoinPath(autobbox::common::JoinPath(text_root, L"text"), L"resource"),
            w_icon.c_str());
        candidates.emplace_back(autobbox::common::WToA(c1.c_str()));
        candidates.emplace_back(autobbox::common::WToA(c2.c_str()));
    }

    ProError best = PRO_TK_E_NOT_FOUND;
    for (const std::string &candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }
        ProCmdItemIcon item_icon = {0};
        CopyCStr(item_icon, candidate.c_str());
        const ProError st = ProCmdIconSet(id, item_icon);
        LogStartupLine(startup_log_path, "IconTry(%s) -> %d", candidate.c_str(), static_cast<int>(st));
        if (st == PRO_TK_NO_ERROR) {
            LogStartupLine(startup_log_path, "Icon(%s) -> success using %s", icon, candidate.c_str());
            return;
        }
        best = st;
    }
    LogStartupLine(startup_log_path, "Icon(%s) -> failed last_status=%d", icon, static_cast<int>(best));
}

uiCmdAccessState AccessInvisible(uiCmdAccessMode)
{
    return ACCESS_INVISIBLE;
}

} // namespace

ProError RegisterPluginCommands(const CommandCallbacks &callbacks,
                                CommandIds &ids,
                                const std::string &startup_log_path)
{
    ProError st = RegisterAction("AutoBBox.Run", callbacks.on_run, callbacks.access_part_or_asm, &ids.run);
    LogStartupLine(startup_log_path, "RegisterAction(AutoBBox.Run) -> status=%d", static_cast<int>(st));

    st = RegisterAction("AutoBBox.RunVolume", callbacks.on_run_volume, callbacks.access_part_or_asm, &ids.run_volume);
    LogStartupLine(startup_log_path, "RegisterAction(AutoBBox.RunVolume) -> status=%d", static_cast<int>(st));

    st = RegisterAction("AutoBBox.CreateIso", callbacks.on_create_iso, callbacks.access_part_or_asm, &ids.create_iso);
    LogStartupLine(startup_log_path, "RegisterAction(AutoBBox.CreateIso) -> status=%d", static_cast<int>(st));

    st = RegisterAction("AutoBBox.DeleteParams", callbacks.on_delete_params, callbacks.access_part_or_asm, &ids.delete_params);
    LogStartupLine(startup_log_path, "RegisterAction(AutoBBox.DeleteParams) -> status=%d", static_cast<int>(st));

    st = RegisterAction("AutoBBox.ModelDiagnostics", callbacks.on_model_diagnostics, callbacks.access_part_or_asm, &ids.model_diagnostics);
    LogStartupLine(startup_log_path, "RegisterAction(AutoBBox.ModelDiagnostics) -> status=%d", static_cast<int>(st));

    st = RegisterAction("AutoBBox.ModelStructureAnalyzer", callbacks.on_model_structure_analyzer, callbacks.access_part_or_asm, &ids.model_structure_analyzer);
    LogStartupLine(startup_log_path, "RegisterAction(AutoBBox.ModelStructureAnalyzer) -> status=%d", static_cast<int>(st));

    st = RegisterAction("AutoBBox.CreateDwg3Views", callbacks.on_create_dwg3_views, callbacks.access_drawing_only, &ids.create_dwg3_views);
    LogStartupLine(startup_log_path, "RegisterDrawingAction(AutoBBox.CreateDwg3Views) -> status=%d", static_cast<int>(st));

    st = RegisterAction("AutoBBox.ArrangeDwgViews", callbacks.on_arrange_dwg_views, callbacks.access_drawing_only, &ids.arrange_dwg_views);
    LogStartupLine(startup_log_path, "RegisterDrawingAction(AutoBBox.ArrangeDwgViews) -> status=%d", static_cast<int>(st));

    st = RegisterAction("AutoBBox.ArrangeBalloons", callbacks.on_arrange_balloons, callbacks.access_drawing_only, &ids.arrange_balloons);
    LogStartupLine(startup_log_path, "RegisterDrawingAction(AutoBBox.ArrangeBalloons) -> status=%d", static_cast<int>(st));

    st = RegisterAction("AutoBBox.RebuildBalloons", callbacks.on_rebuild_balloons, callbacks.access_drawing_only, &ids.rebuild_balloons);
    LogStartupLine(startup_log_path, "RegisterDrawingAction(AutoBBox.RebuildBalloons) -> status=%d", static_cast<int>(st));

    st = RegisterAction("AutoBBox.SyncPageScale", callbacks.on_sync_page_scale, callbacks.access_drawing_only, &ids.sync_page_scale);
    LogStartupLine(startup_log_path, "RegisterDrawingAction(AutoBBox.SyncPageScale) -> status=%d", static_cast<int>(st));

    st = RegisterActionWithPriority("AutoBBox.BrushMainView",
                                    callbacks.on_drawing_view_brush,
                                    uiProeAsynch,
                                    callbacks.access_drawing_only,
                                    &ids.drawing_view_brush);
    LogStartupLine(startup_log_path, "RegisterDrawingAction(AutoBBox.BrushMainView) -> status=%d", static_cast<int>(st));
    RegisterActionAlias("AutoBBoxBrushMainView",
                        callbacks.on_drawing_view_brush,
                        uiProeAsynch,
                        callbacks.access_drawing_only,
                        startup_log_path);
    RegisterActionAlias("ABBrushMainView",
                        callbacks.on_drawing_view_brush,
                        uiProeAsynch,
                        callbacks.access_drawing_only,
                        startup_log_path);

    st = RegisterActionWithPriority("AutoBBox.SmartDimension",
                                    callbacks.on_smart_dimension,
                                    uiProeAsynch,
                                    callbacks.access_drawing_only,
                                    &ids.smart_dimension);
    LogStartupLine(startup_log_path, "RegisterDrawingAction(AutoBBox.SmartDimension) -> status=%d", static_cast<int>(st));
    RegisterActionAlias("AutoBBoxSmartDimension",
                        callbacks.on_smart_dimension,
                        uiProeAsynch,
                        callbacks.access_drawing_only,
                        startup_log_path);
    RegisterActionAlias("ABSmartDimension",
                        callbacks.on_smart_dimension,
                        uiProeAsynch,
                        callbacks.access_drawing_only,
                        startup_log_path);

    st = RegisterAction("AutoBBox.ExportDrawing", callbacks.on_drawing_export, callbacks.access_drawing_only, &ids.drawing_export);
    LogStartupLine(startup_log_path, "RegisterDrawingAction(AutoBBox.ExportDrawing) -> status=%d", static_cast<int>(st));

    st = RegisterAction("AutoBBox.ForceOpenDrawing", callbacks.on_force_open_drawing, callbacks.access_always, &ids.force_open_drawing);
    LogStartupLine(startup_log_path, "RegisterAction(AutoBBox.ForceOpenDrawing) -> status=%d", static_cast<int>(st));

    st = RegisterAction("AutoBBox.SplitInstances", callbacks.on_split_instances, callbacks.access_part_or_asm, &ids.split_instances);
    LogStartupLine(startup_log_path, "RegisterAction(AutoBBox.SplitInstances) -> status=%d", static_cast<int>(st));

    st = RegisterAction("AutoBBox.CleanRelations", callbacks.on_clean_relations, callbacks.access_part_asm_only, &ids.clean_relations);
    LogStartupLine(startup_log_path, "RegisterPartAsmAction(AutoBBox.CleanRelations) -> status=%d", static_cast<int>(st));

    st = RegisterAction("AutoBBox.AddRelations", callbacks.on_add_relations, callbacks.access_part_asm_only, &ids.add_relations);
    LogStartupLine(startup_log_path, "RegisterPartAsmAction(AutoBBox.AddRelations) -> status=%d", static_cast<int>(st));

    st = RegisterAction("AutoBBox.ParamTool", callbacks.on_param_tool, callbacks.access_part_asm_only, &ids.param_tool);
    LogStartupLine(startup_log_path, "RegisterPartAsmAction(AutoBBox.ParamTool) -> status=%d", static_cast<int>(st));

    st = RegisterAction("AutoBBox.RandomColor", callbacks.on_random_color, callbacks.access_assembly_only, &ids.random_color);
    LogStartupLine(startup_log_path, "RegisterAssemblyAction(AutoBBox.RandomColor) -> status=%d", static_cast<int>(st));

    st = RegisterActionWithPriority("AutoBBox.QuickRename",
                                    callbacks.on_quick_rename,
                                    uiProeAsynch,
                                    callbacks.access_assembly_only,
                                    &ids.quick_rename);
    LogStartupLine(startup_log_path, "RegisterAssemblyAction(AutoBBox.QuickRename) -> status=%d", static_cast<int>(st));
    RegisterActionAlias("AutoBBoxQuickRename",
                        callbacks.on_quick_rename,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);
    RegisterActionAlias("AutoBBoxQuickRenameBtn",
                        callbacks.on_quick_rename,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);
    RegisterActionAlias("AutoBBoxQuickRenameMenuBtn",
                        callbacks.on_quick_rename,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);
    RegisterActionAlias("AutoBBox.QuickRenameBtn",
                        callbacks.on_quick_rename,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);
    RegisterActionAlias("AutoBBox.QuickRenameMenuBtn",
                        callbacks.on_quick_rename,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);
    RegisterActionAlias("ABQuickRename",
                        callbacks.on_quick_rename,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);

    st = RegisterActionWithPriority("AutoBBox.SaveCopyToWorkdir",
                                    callbacks.on_save_copy_to_workdir,
                                    uiProeAsynch,
                                    callbacks.access_assembly_only,
                                    &ids.save_copy_to_workdir);
    LogStartupLine(startup_log_path, "RegisterAssemblyAction(AutoBBox.SaveCopyToWorkdir) -> status=%d", static_cast<int>(st));
    RegisterActionAlias("AutoBBoxSaveCopyToWorkdir",
                        callbacks.on_save_copy_to_workdir,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);
    RegisterActionAlias("AutoBBoxSaveCopyToWorkdirBtn",
                        callbacks.on_save_copy_to_workdir,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);
    RegisterActionAlias("AutoBBoxSaveCopyToWorkdirMenuBtn",
                        callbacks.on_save_copy_to_workdir,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);
    RegisterActionAlias("AutoBBox.SaveCopyToWorkdirBtn",
                        callbacks.on_save_copy_to_workdir,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);
    RegisterActionAlias("AutoBBox.SaveCopyToWorkdirMenuBtn",
                        callbacks.on_save_copy_to_workdir,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);
    RegisterActionAlias("ABSaveCopyToWorkdir",
                        callbacks.on_save_copy_to_workdir,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);

    st = RegisterActionWithPriority("AutoBBox.BatchRename",
                                    callbacks.on_batch_rename,
                                    uiProeAsynch,
                                    callbacks.access_assembly_only,
                                    &ids.batch_rename);
    LogStartupLine(startup_log_path, "RegisterAssemblyAction(AutoBBox.BatchRename) -> status=%d", static_cast<int>(st));
    RegisterActionAlias("AutoBBoxBatchRename",
                        callbacks.on_batch_rename,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);
    RegisterActionAlias("AutoBBoxBatchRenameMenuBtn",
                        callbacks.on_batch_rename,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);
    RegisterActionAlias("ABBatchRename",
                        callbacks.on_batch_rename,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);

    st = RegisterActionWithPriority("AutoBBox.QuickSimprep",
                                    callbacks.on_quick_simprep,
                                    uiProeAsynch,
                                    callbacks.access_assembly_only,
                                    &ids.quick_simprep);
    LogStartupLine(startup_log_path, "RegisterAssemblyAction(AutoBBox.QuickSimprep) -> status=%d", static_cast<int>(st));
    RegisterActionAlias("AutoBBoxQuickSimprep",
                        callbacks.on_quick_simprep,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);
    RegisterActionAlias("AutoBBoxQuickSimprepMenuBtn",
                        callbacks.on_quick_simprep,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);
    RegisterActionAlias("ABQuickSimprep",
                        callbacks.on_quick_simprep,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);

    st = RegisterActionWithPriority("AutoBBox.SheetmetalFlatBatch",
                                    callbacks.on_sheetmetal_flat_batch,
                                    uiProeAsynch,
                                    callbacks.access_part_asm_only,
                                    &ids.sheetmetal_flat_batch);
    LogStartupLine(startup_log_path, "RegisterPartAsmAction(AutoBBox.SheetmetalFlatBatch) -> status=%d", static_cast<int>(st));
    RegisterActionAlias("AutoBBoxSheetmetalFlatBatch",
                        callbacks.on_sheetmetal_flat_batch,
                        uiProeAsynch,
                        callbacks.access_part_asm_only,
                        startup_log_path);
    RegisterActionAlias("ABSheetmetalFlatBatch",
                        callbacks.on_sheetmetal_flat_batch,
                        uiProeAsynch,
                        callbacks.access_part_asm_only,
                        startup_log_path);

    st = RegisterActionWithPriority("AutoBBox.FamilyTableManager",
                                    callbacks.on_family_table_manager,
                                    uiProeAsynch,
                                    callbacks.access_part_asm_only,
                                    &ids.family_table_manager);
    LogStartupLine(startup_log_path, "RegisterPartAsmAction(AutoBBox.FamilyTableManager) -> status=%d", static_cast<int>(st));
    RegisterActionAlias("AutoBBoxFamilyTableManager",
                        callbacks.on_family_table_manager,
                        uiProeAsynch,
                        callbacks.access_part_asm_only,
                        startup_log_path);
    RegisterActionAlias("ABFamilyTableManager",
                        callbacks.on_family_table_manager,
                        uiProeAsynch,
                        callbacks.access_part_asm_only,
                        startup_log_path);

    st = RegisterActionWithPriority("AutoBBox.FeatureVisibility",
                                    callbacks.on_feature_visibility,
                                    uiProeAsynch,
                                    callbacks.access_assembly_only,
                                    &ids.feature_visibility);
    LogStartupLine(startup_log_path, "RegisterAssemblyAction(AutoBBox.FeatureVisibility) -> status=%d", static_cast<int>(st));
    RegisterActionAlias("AutoBBoxFeatureVisibility",
                        callbacks.on_feature_visibility,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);
    RegisterActionAlias("AutoBBoxFeatureVisibilityMenuBtn",
                        callbacks.on_feature_visibility,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);
    RegisterActionAlias("ABFeatureVisibility",
                        callbacks.on_feature_visibility,
                        uiProeAsynch,
                        callbacks.access_assembly_only,
                        startup_log_path);

    st = RegisterOption("AutoBBox.Option.Parts", callbacks.on_option_parts, callbacks.set_option_parts, callbacks.access_part_or_asm, &ids.opt_parts);
    LogStartupLine(startup_log_path, "RegisterOption(AutoBBox.Option.Parts) -> status=%d", static_cast<int>(st));

    st = RegisterOption("AutoBBox.Option.Assemblies", callbacks.on_option_assemblies, callbacks.set_option_assemblies, callbacks.access_part_or_asm, &ids.opt_assemblies);
    LogStartupLine(startup_log_path, "RegisterOption(AutoBBox.Option.Assemblies) -> status=%d", static_cast<int>(st));

    st = RegisterOption("AutoBBox.Option.Surface", callbacks.on_option_surface, callbacks.set_option_surface, AccessInvisible, &ids.opt_surface);
    LogStartupLine(startup_log_path, "RegisterOption(AutoBBox.Option.Surface) -> status=%d", static_cast<int>(st));

    st = RegisterOption("AutoBBox.Option.Curve", callbacks.on_option_curve, callbacks.set_option_curve, AccessInvisible, &ids.opt_curve);
    LogStartupLine(startup_log_path, "RegisterOption(AutoBBox.Option.Curve) -> status=%d", static_cast<int>(st));

    st = RegisterOption("AutoBBox.Option.Recompute", callbacks.on_option_recompute, callbacks.set_option_recompute, callbacks.access_part_or_asm, &ids.opt_recompute);
    LogStartupLine(startup_log_path, "RegisterOption(AutoBBox.Option.Recompute) -> status=%d", static_cast<int>(st));

    st = RegisterOption("AutoBBox.Option.TopLevelOnly", callbacks.on_option_top2, callbacks.set_option_top2, callbacks.access_part_or_asm, &ids.opt_top_level_only);
    LogStartupLine(startup_log_path, "RegisterOption(AutoBBox.Option.TopLevelOnly) -> status=%d", static_cast<int>(st));

    return st;
}

void ApplyCommandDesignations(const CommandIds &ids,
                              const std::wstring &message_file_name,
                              const std::string &message_file_path,
                              const std::string &startup_log_path)
{
    TryDesignate(ids.run, "ABRunLbl", "ABRunTip", "ABRun", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.run_volume, "ABVolLbl", "ABVolTip", "ABRunVol", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.create_iso, "ABIsoLbl", "ABIsoTip", "ABIso", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.delete_params, "ABDelLbl", "ABDelTip", "ABDel", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.model_diagnostics, "ABModelDiagLbl", "ABModelDiagTip", "ABModelDiag", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.model_structure_analyzer, "ABModelStructLbl", "ABModelStructTip", "ABModelStruct", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.create_dwg3_views, "ABDwg3Lbl", "ABDwg3Tip", "ABDwg3", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.arrange_dwg_views, "ABArrangeLbl", "ABArrangeTip", "ABArrange", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.arrange_balloons, "ABArrangeBalloonsLbl", "ABArrangeBalloonsTip", "ABArrangeBalloons", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.rebuild_balloons, "ABRebuildBalloonsLbl", "ABRebuildBalloonsTip", "ABRebuildBalloons", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.sync_page_scale, "ABSyncScaleLbl", "ABSyncScaleTip", "ABSyncScale", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.drawing_view_brush, "ABViewBrushLbl", "ABViewBrushTip", "ABViewBrush", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.smart_dimension, "ABSmartDimLbl", "ABSmartDimTip", "ABSmartDim", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.drawing_export, "ABDrawingExportLbl", "ABDrawingExportTip", "ABDrawingExport", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.force_open_drawing, "ABForceOpenDrawingLbl", "ABForceOpenDrawingTip", "ABForceOpenDrawing", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.split_instances, "ABSplitLbl", "ABSplitTip", "ABSplit", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.clean_relations, "ABRelCleanLbl", "ABRelCleanTip", "ABRelClean", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.add_relations, "ABRelAddLbl", "ABRelAddTip", "ABRelAdd", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.param_tool, "ABParamToolLbl", "ABParamToolTip", "ABParamTool", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.random_color, "ABRandomColorLbl", "ABRandomColorTip", "ABRandomColor", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.quick_rename, "ABQuickRenameLbl", "ABQuickRenameTip", "ABQuickRename", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.save_copy_to_workdir, "ABSaveCopyToWorkdirLbl", "ABSaveCopyToWorkdirTip", "ABSaveCopyToWorkdir", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.batch_rename, "ABBatchRenameLbl", "ABBatchRenameTip", "ABBatchRename", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.quick_simprep, "ABQuickSimprepLbl", "ABQuickSimprepTip", "ABQuickSimprep", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.sheetmetal_flat_batch, "ABSheetmetalFlatBatchLbl", "ABSheetmetalFlatBatchTip", "ABSheetmetalFlatBatch", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.family_table_manager, "ABFamilyTableMgrLbl", "ABFamilyTableMgrTip", "ABFamilyTableMgr", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.feature_visibility, "ABFeatureVisibilityLbl", "ABFeatureVisibilityTip", "ABFeatureVisibility", message_file_name, message_file_path, startup_log_path);

    TryDesignate(ids.opt_parts, "ABParts", "ABOptTip", "ABParts", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.opt_assemblies, "ABAsms", "ABOptTip", "ABAsms", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.opt_surface, "ABSurf", "ABOptTip", "ABSurf", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.opt_curve, "ABCurve", "ABOptTip", "ABCurve", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.opt_recompute, "ABRecalc", "ABOptTip", "ABRecalc", message_file_name, message_file_path, startup_log_path);
    TryDesignate(ids.opt_top_level_only, "ABTop2", "ABOptTip", "ABTop2", message_file_name, message_file_path, startup_log_path);
}

void ApplyCommandIcons(const CommandIds &ids,
                       const std::wstring &text_root,
                       const std::string &startup_log_path)
{
    TryIcon(ids.run, "autobbox_size.png", text_root, startup_log_path);
    TryIcon(ids.run_volume, "autobbox_volume.png", text_root, startup_log_path);
    TryIcon(ids.create_iso, "autobbox_iso.png", text_root, startup_log_path);
    TryIcon(ids.delete_params, "autobbox_delete.png", text_root, startup_log_path);
    TryIcon(ids.model_diagnostics, "autobbox_recalc.png", text_root, startup_log_path);
    TryIcon(ids.model_structure_analyzer, "autobbox_asm.png", text_root, startup_log_path);
    TryIcon(ids.create_dwg3_views, "autobbox_dwg3.png", text_root, startup_log_path);
    TryIcon(ids.arrange_dwg_views, "autobbox_arrange.png", text_root, startup_log_path);
    TryIcon(ids.arrange_balloons, "autobbox_arrange.png", text_root, startup_log_path);
    TryIcon(ids.rebuild_balloons, "autobbox_arrange.png", text_root, startup_log_path);
    TryIcon(ids.sync_page_scale, "autobbox_scale_sync.png", text_root, startup_log_path);
    TryIcon(ids.drawing_view_brush, "autobbox_arrange.png", text_root, startup_log_path);
    TryIcon(ids.smart_dimension, "autobbox_size.png", text_root, startup_log_path);
    TryIcon(ids.drawing_export, "autobbox_dwg3.png", text_root, startup_log_path);
    TryIcon(ids.force_open_drawing, "autobbox_dwg3.png", text_root, startup_log_path);
    TryIcon(ids.split_instances, "autobbox_split.png", text_root, startup_log_path);
    TryIcon(ids.clean_relations, "autobbox_rel_clean.png", text_root, startup_log_path);
    TryIcon(ids.add_relations, "autobbox_rel_add.png", text_root, startup_log_path);
    TryIcon(ids.param_tool, "autobbox_param_tool.png", text_root, startup_log_path);
    TryIcon(ids.random_color, "autobbox_param_tool.png", text_root, startup_log_path);
    TryIcon(ids.quick_rename, "autobbox_param_tool.png", text_root, startup_log_path);
    TryIcon(ids.save_copy_to_workdir, "autobbox_asm.png", text_root, startup_log_path);
    TryIcon(ids.batch_rename, "autobbox_param_tool.png", text_root, startup_log_path);
    TryIcon(ids.quick_simprep, "autobbox_param_tool.png", text_root, startup_log_path);
    TryIcon(ids.sheetmetal_flat_batch, "autobbox_param_tool.png", text_root, startup_log_path);
    TryIcon(ids.family_table_manager, "autobbox_param_tool.png", text_root, startup_log_path);
    TryIcon(ids.feature_visibility, "autobbox_surface.png", text_root, startup_log_path);
}

void LoadRibbonDefinition(const std::wstring &resolved_ribbon_file,
                          const std::wstring &default_ribbon_file_name,
                          const std::string &ribbon_file_path_for_log,
                          const std::string &startup_log_path)
{
    ProError st = PRO_TK_GENERAL_ERROR;
    if (!resolved_ribbon_file.empty()) {
        wchar_t resolved_file[PRO_PATH_SIZE] = {0};
        CopyWStr(resolved_file, resolved_ribbon_file.c_str());
        st = ProRibbonDefinitionfileLoad(resolved_file);
        LogStartupLine(startup_log_path,
                       "ProRibbonDefinitionfileLoad(resolved-first) -> %d path=%s",
                       static_cast<int>(st),
                       ribbon_file_path_for_log.c_str());
        if (st == PRO_TK_NO_ERROR) {
            return;
        }
    }

    wchar_t default_file[PRO_PATH_SIZE] = {0};
    CopyWStr(default_file, default_ribbon_file_name.c_str());
    st = ProRibbonDefinitionfileLoad(default_file);
    LogStartupLine(startup_log_path, "ProRibbonDefinitionfileLoad(default-fallback) -> %d", static_cast<int>(st));
}

void AddLegacyMenuFallback(const CommandIds &ids,
                           const std::wstring &message_file_name,
                           const std::string &startup_log_path)
{
    if (message_file_name.empty()) {
        return;
    }

    ProFileName msg_token = {0};
    CopyWStr(msg_token, message_file_name.c_str());

    ProMenuItemName menu_name = {0};
    CopyCStr(menu_name, "AutoBBoxMenu");
    ProMenuItemLabel menu_label = {0};
    CopyCStr(menu_label, "ABMenu");

    const ProError st_menu = ProMenubarMenuAdd(
        menu_name,
        menu_label,
        nullptr,
        PRO_B_TRUE,
        msg_token);
    LogStartupLine(startup_log_path, "LegacyMenuAdd(AutoBBoxMenu) -> %d", static_cast<int>(st_menu));

    auto add_button = [&](const char *item_name,
                          const char *label_key,
                          const char *help_key,
                          uiCmdCmdId cmd_id) {
        if (cmd_id == nullptr) {
            return;
        }
        ProMenuItemName push_name = {0};
        CopyCStr(push_name, item_name);
        ProMenuItemLabel push_label = {0};
        CopyCStr(push_label, label_key);
        ProMenuLineHelp push_help = {0};
        CopyCStr(push_help, help_key);

        const ProError st = ProMenubarmenuPushbuttonAdd(
            menu_name,
            push_name,
            push_label,
            push_help,
            nullptr,
            PRO_B_TRUE,
            cmd_id,
            msg_token);
        LogStartupLine(startup_log_path, "LegacyMenuButton(%s) -> %d", item_name, static_cast<int>(st));
    };

    add_button("AutoBBoxCreateDwg3MenuBtn", "ABDwg3Lbl", "ABDwg3Tip", ids.create_dwg3_views);
    add_button("AutoBBoxArrangeDwgMenuBtn", "ABArrangeLbl", "ABArrangeTip", ids.arrange_dwg_views);
    add_button("AutoBBoxArrangeBalloonsMenuBtn", "ABArrangeBalloonsLbl", "ABArrangeBalloonsTip", ids.arrange_balloons);
    add_button("AutoBBoxRebuildBalloonsMenuBtn", "ABRebuildBalloonsLbl", "ABRebuildBalloonsTip", ids.rebuild_balloons);
    add_button("AutoBBoxSyncScaleMenuBtn", "ABSyncScaleLbl", "ABSyncScaleTip", ids.sync_page_scale);
    add_button("AutoBBoxViewBrushMenuBtn", "ABViewBrushLbl", "ABViewBrushTip", ids.drawing_view_brush);
    add_button("AutoBBoxSmartDimensionMenuBtn", "ABSmartDimLbl", "ABSmartDimTip", ids.smart_dimension);
    add_button("AutoBBoxDrawingExportMenuBtn", "ABDrawingExportLbl", "ABDrawingExportTip", ids.drawing_export);
    add_button("AutoBBoxForceOpenDrawingMenuBtn", "ABForceOpenDrawingLbl", "ABForceOpenDrawingTip", ids.force_open_drawing);
    add_button("AutoBBoxModelDiagnosticsMenuBtn", "ABModelDiagLbl", "ABModelDiagTip", ids.model_diagnostics);
    add_button("AutoBBoxModelStructureAnalyzerMenuBtn", "ABModelStructLbl", "ABModelStructTip", ids.model_structure_analyzer);
    add_button("AutoBBoxRandomColorMenuBtn", "ABRandomColorLbl", "ABRandomColorTip", ids.random_color);
    add_button("AutoBBoxQuickRenameMenuBtn", "ABQuickRenameLbl", "ABQuickRenameTip", ids.quick_rename);
    add_button("AutoBBoxSaveCopyToWorkdirMenuBtn", "ABSaveCopyToWorkdirLbl", "ABSaveCopyToWorkdirTip", ids.save_copy_to_workdir);
    add_button("AutoBBoxBatchRenameMenuBtn", "ABBatchRenameLbl", "ABBatchRenameTip", ids.batch_rename);
    add_button("AutoBBoxQuickSimprepMenuBtn", "ABQuickSimprepLbl", "ABQuickSimprepTip", ids.quick_simprep);
    add_button("AutoBBoxSheetmetalFlatBatchMenuBtn", "ABSheetmetalFlatBatchLbl", "ABSheetmetalFlatBatchTip", ids.sheetmetal_flat_batch);
    add_button("AutoBBoxFamilyTableMenuBtn", "ABFamilyTableMgrLbl", "ABFamilyTableMgrTip", ids.family_table_manager);
    add_button("AutoBBoxFeatureVisibilityMenuBtn", "ABFeatureVisibilityLbl", "ABFeatureVisibilityTip", ids.feature_visibility);
}

} // namespace autobbox::main
