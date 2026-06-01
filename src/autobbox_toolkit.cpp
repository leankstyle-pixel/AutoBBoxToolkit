#include <ProToolkit.h>
#include <ProObjects.h>
#include <ProMdl.h>
#include <ProSolid.h>
#include <ProSolidBody.h>
#include <ProMdlUnits.h>
#include <ProAsmcomppath.h>
#include <ProArray.h>
#include <ProParameter.h>
#include <ProParamDriver.h>
#include <ProParamval.h>
#include <ProFamtable.h>
#include <ProFaminstance.h>
#include <ProAsmcomp.h>
#include <ProFeature.h>
#include <ProUtil.h>
#include <ProModelitem.h>
#include <ProUICmd.h>
#include <ProRibbon.h>
#include <ProNotify.h>
#include <ProUIMessage.h>
#include <ProUIDialog.h>
#include <ProUICheckbutton.h>
#include <ProUIPushbutton.h>
#include <ProUIInputpanel.h>
#include <ProUIList.h>
#include <ProUILabel.h>
#include <ProUIOptionmenu.h>
#include <ProUITable.h>
#include <ProUITextarea.h>
#include <ProRelSet.h>
#include <ProDtlentity.h>
#include <ProDtlnote.h>
#include <ProDtlattach.h>
#include <ProDrawing.h>
#include <ProDrawingView.h>
#include <ProGraphic.h>
#include <ProView.h>
#include <ProWindows.h>

#include "autobbox/application/model_run_tasks.h"
#include "autobbox/application/split_instances.h"
#include "autobbox/common/files.h"
#include "autobbox/common/log.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/core/bom_types.h"
#include "autobbox/core/dwg3_types.h"
#include "autobbox/core/split_types.h"
#include "autobbox/common/strings.h"
#include "autobbox/main/command_registry.h"
#include "autobbox/main/open_perf_trace.h"
#include "autobbox/main/plugin_balloon_arrange.h"
#include "autobbox/main/plugin_command_callbacks.h"
#include "autobbox/main/plugin_batch_rename.h"
#include "autobbox/main/plugin_drawing_arrange.h"
#include "autobbox/main/plugin_drawing_export.h"
#include "autobbox/main/plugin_drawing3.h"
#include "autobbox/main/plugin_drawing_scale_sync.h"
#include "autobbox/main/plugin_drawing_view_brush.h"
#include "autobbox/main/plugin_main_run.h"
#include "autobbox/main/plugin_model_diagnostics.h"
#include "autobbox/main/plugin_model_structure_analyzer.h"
#include "autobbox/main/plugin_param_tool.h"
#include "autobbox/main/plugin_quick_simprep.h"
#include "autobbox/main/plugin_sheetmetal_flat_batch.h"
#include "autobbox/main/plugin_quick_rename.h"
#include "autobbox/main/plugin_save_copy_to_workdir.h"
#include "autobbox/main/plugin_random_color.h"
#include "autobbox/main/plugin_entry.h"
#include "autobbox/main/plugin_family_table_manager.h"
#include "autobbox/main/plugin_feature_visibility.h"
#include "autobbox/main/plugin_force_open_drawing.h"
#include "autobbox/main/plugin_relations.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/main/plugin_smart_dimension.h"
#include "autobbox/main/plugin_split.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace {

static const wchar_t *kMsgFileName = L"autobbox_msg.txt";
static const wchar_t *kRibbonFileName = L"toolkitribbonui.rbn";
static const char *kBuildStamp = "AutoBBox build 2026-04-02 20:25 bom-tool-v1";

struct Options {
    ProBoolean parts = PRO_B_TRUE;
    ProBoolean assemblies = PRO_B_TRUE;
    ProBoolean surface = PRO_B_FALSE;
    ProBoolean curve = PRO_B_FALSE;
    ProBoolean recompute = PRO_B_TRUE;
    ProBoolean top_level_only = PRO_B_FALSE;
    ProBoolean preheat_generics = PRO_B_TRUE;
};

static Options g_opts;
static bool g_task_running = false;
static const bool kEnableCmdDesignate = true;
static const bool kEnableCmdIcons = true;
static const bool kEnableOpenPerfNotifications = true;
static autobbox::main::PluginEntryState g_plugin_entry_state;
static autobbox::common::BufferedLogSession g_report_session;
static autobbox::main::OpenPerfTraceState g_open_perf_trace_state;
static autobbox::core::BomToolState g_bom_tool_state;

static std::wstring AToW(const char *s)
{
    return autobbox::common::AToW(s);
}

static void LogPerf(const char *fmt, ...)
{
    if (g_plugin_entry_state.startup_log.empty() || fmt == nullptr) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    autobbox::common::AppendFormattedLine(g_plugin_entry_state.startup_log, "PERF ", fmt, args);
    va_end(args);
}

static void LogReport(const char *fmt, ...)
{
    if (fmt == nullptr) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    autobbox::common::AppendFormattedLine(g_plugin_entry_state.report_log, g_report_session.file, nullptr, fmt, args);
    va_end(args);
}

static std::string MdlTag(ProMdl mdl)
{
    return autobbox::creo::DefaultModelTag(mdl);
}

static autobbox::main::PluginRelationsRuntime BuildRelationsRuntime()
{
    autobbox::main::PluginRelationsRuntime runtime = {};
    runtime.task_running = &g_task_running;
    runtime.parts = &g_opts.parts;
    runtime.assemblies = &g_opts.assemblies;
    runtime.surface = &g_opts.surface;
    runtime.curve = &g_opts.curve;
    runtime.recompute = &g_opts.recompute;
    runtime.top_level_only = &g_opts.top_level_only;
    runtime.format_model_tag = MdlTag;
    return runtime;
}

static autobbox::main::PluginPerfCallbacks BuildPerfCallbacks()
{
    return autobbox::main::BuildPluginPerfCallbacks();
}

static int RunCreateDwg3ViewsCommand()
{
    const autobbox::main::PluginDrawing3Runtime runtime = {
        &g_task_running,
        MdlTag};
    return autobbox::main::RunPluginDrawing3Task(runtime);
}
static int RunArrangeDrawingViewsCommand()
{
    const autobbox::main::PluginDrawingArrangeRuntime runtime = {
        &g_task_running};
    return autobbox::main::RunPluginDrawingArrangeTask(runtime);
}
static int RunArrangeBalloonsCommand()
{
    const autobbox::main::PluginBalloonArrangeRuntime runtime = {
        &g_task_running};
    return autobbox::main::RunPluginBalloonArrangeTask(runtime);
}
static int RunRebuildBalloonsCommand()
{
    const autobbox::main::PluginBalloonArrangeRuntime runtime = {
        &g_task_running};
    return autobbox::main::RunPluginRebuildBalloonsTask(runtime);
}
static int RunSyncDrawingPageScaleCommand()
{
    const autobbox::main::PluginDrawingScaleSyncRuntime runtime = {
        &g_task_running};
    return autobbox::main::RunPluginDrawingScaleSyncTask(runtime);
}
static int RunDrawingViewBrushCommand()
{
    const autobbox::main::PluginDrawingViewBrushRuntime runtime = {
        &g_task_running};
    return autobbox::main::RunPluginDrawingViewBrushTask(runtime);
}
static int RunSmartDimensionCommand()
{
    const autobbox::main::PluginSmartDimensionRuntime runtime = {
        &g_task_running};
    return autobbox::main::RunPluginSmartDimensionTask(runtime);
}
static int RunDrawingExportCommand()
{
    const autobbox::main::PluginDrawingExportRuntime runtime = {
        &g_task_running};
    return autobbox::main::RunPluginDrawingExportTask(runtime);
}
static int RunForceOpenDrawingCommand()
{
    const autobbox::main::PluginForceOpenDrawingRuntime runtime = {
        &g_task_running};
    return autobbox::main::RunPluginForceOpenDrawingTask(runtime);
}
static int RunSplitInstancesCommand()
{
    const autobbox::main::PluginSplitRuntime runtime = {
        &g_task_running,
        MdlTag};
    return autobbox::main::RunPluginSplitTask(runtime);
}
static int RunCleanRelationsCommand()
{
    return autobbox::main::RunPluginRelationsTask(
        autobbox::main::PluginRelationsMode::Clean,
        BuildRelationsRuntime());
}
static int RunAddRelationsCommand()
{
    return autobbox::main::RunPluginRelationsTask(
        autobbox::main::PluginRelationsMode::Add,
        BuildRelationsRuntime());
}
static int RunParamToolCommand()
{
    const autobbox::main::PluginParamToolRuntime runtime = {
        &g_task_running,
        &g_opts.parts,
        &g_opts.assemblies,
        &g_opts.top_level_only,
        &g_bom_tool_state,
        MdlTag};
    return autobbox::main::RunPluginParamToolTask(runtime);
}
static int RunRandomColorCommand()
{
    const autobbox::main::PluginRandomColorRuntime runtime = {
        &g_task_running,
        &g_opts.parts,
        &g_opts.assemblies,
        &g_opts.top_level_only,
        MdlTag};
    return autobbox::main::RunPluginRandomColorTask(runtime);
}
static int RunQuickRenameCommand()
{
    const autobbox::main::PluginQuickRenameRuntime runtime = {
        &g_task_running,
        MdlTag};
    return autobbox::main::RunPluginQuickRenameTask(runtime);
}
static int RunSaveCopyToWorkdirCommand()
{
    const autobbox::main::PluginSaveCopyToWorkdirRuntime runtime = {
        &g_task_running,
        MdlTag};
    return autobbox::main::RunPluginSaveCopyToWorkdirTask(runtime);
}
static int RunBatchRenameCommand()
{
    const autobbox::main::PluginBatchRenameRuntime runtime = {
        &g_task_running,
        &g_opts.parts,
        &g_opts.assemblies,
        &g_opts.top_level_only,
        MdlTag};
    return autobbox::main::RunPluginBatchRenameTask(runtime);
}

static int RunQuickSimprepCommand()
{
    const autobbox::main::PluginQuickSimprepRuntime runtime = {
        &g_task_running,
        MdlTag};
    return autobbox::main::RunPluginQuickSimprepTask(runtime);
}

static int RunSheetmetalFlatBatchCommand()
{
    const autobbox::main::PluginSheetmetalFlatBatchRuntime runtime = {
        &g_task_running,
        MdlTag};
    return autobbox::main::RunPluginSheetmetalFlatBatchTask(runtime);
}

static int RunFamilyTableManagerCommand()
{
    const autobbox::main::PluginFamilyTableManagerRuntime runtime = {
        &g_task_running};
    return autobbox::main::RunPluginFamilyTableManagerTask(runtime);
}

static int RunFeatureVisibilityCommand()
{
    const autobbox::main::PluginFeatureVisibilityRuntime runtime = {
        &g_task_running};
    return autobbox::main::RunPluginFeatureVisibilityTask(runtime);
}

static void OpenReportFileInNotepad()
{
    autobbox::main::EndPluginReportSession();
    if (!g_plugin_entry_state.report_log.empty()) {
        unsigned long error_code = 0;
        if (!autobbox::common::OpenFileInNotepad(g_plugin_entry_state.report_log, &error_code)) {
            LogReport("Open report failed path=%s error=%lu",
                      g_plugin_entry_state.report_log.c_str(),
                      error_code);
        }
    }
}

static int RunModelDiagnosticsCommand()
{
    const autobbox::main::PluginModelDiagnosticsRuntime runtime = {
        &g_task_running,
        MdlTag,
        OpenReportFileInNotepad};
    return autobbox::main::RunPluginModelDiagnosticsTask(runtime);
}

static int RunModelStructureAnalyzerCommand()
{
    const autobbox::main::PluginModelStructureAnalyzerRuntime runtime = {
        &g_task_running,
        MdlTag};
    return autobbox::main::RunPluginModelStructureAnalyzerTask(runtime);
}

static void LogPerfLine(const std::string &line)
{
    LogPerf("%s", line.c_str());
}

} // namespace

extern "C" int __declspec(dllexport) user_initialize(
    int,
    char *[],
    char *,
    char *,
    wchar_t err_buff[])
{
    autobbox::main::ConfigurePluginRuntimeBridge({
        &g_plugin_entry_state,
        &g_report_session,
        &g_open_perf_trace_state,
        &g_task_running,
        kEnableOpenPerfNotifications,
        MdlTag,
        LogPerfLine});

    const autobbox::main::PluginMainRunOptionState main_run_option_state = {
        &g_opts.parts,
        &g_opts.assemblies,
        &g_opts.surface,
        &g_opts.curve,
        &g_opts.recompute,
        &g_opts.top_level_only,
        &g_opts.preheat_generics};
    const autobbox::main::PluginMainRunCommands run_commands =
        autobbox::main::BuildPluginMainRunCommands(
            main_run_option_state,
            autobbox::main::BuildPluginMainRunRuntime());
    const autobbox::main::PluginCommandActions command_actions = {
        run_commands.run_size,
        run_commands.run_volume,
        run_commands.create_iso,
        run_commands.delete_params,
        RunModelDiagnosticsCommand,
        RunModelStructureAnalyzerCommand,
        RunCreateDwg3ViewsCommand,
        RunArrangeDrawingViewsCommand,
        RunArrangeBalloonsCommand,
        RunRebuildBalloonsCommand,
        RunSyncDrawingPageScaleCommand,
        RunDrawingViewBrushCommand,
        RunSmartDimensionCommand,
        RunDrawingExportCommand,
        RunForceOpenDrawingCommand,
        RunSplitInstancesCommand,
        RunCleanRelationsCommand,
        RunAddRelationsCommand,
        RunParamToolCommand,
        RunRandomColorCommand,
        RunQuickRenameCommand,
        RunSaveCopyToWorkdirCommand,
        RunBatchRenameCommand,
        RunQuickSimprepCommand,
        RunSheetmetalFlatBatchCommand,
        RunFamilyTableManagerCommand,
        RunFeatureVisibilityCommand};
    const autobbox::main::PluginOptionState option_state = {
        &g_opts.parts,
        &g_opts.assemblies,
        &g_opts.surface,
        &g_opts.curve,
        &g_opts.recompute,
        &g_opts.top_level_only};
    const autobbox::main::PluginEntryCallbacks callbacks =
        autobbox::main::BuildPluginEntryCallbacks(
            autobbox::main::BuildPluginCommandCallbacks(
                command_actions,
                option_state),
            BuildPerfCallbacks());

    const ProError st = autobbox::main::InitializePlugin(
        g_plugin_entry_state,
        callbacks,
        AToW(kBuildStamp).c_str(),
        kMsgFileName,
        kRibbonFileName,
        kEnableCmdDesignate,
        kEnableCmdIcons,
        kEnableOpenPerfNotifications,
        err_buff);
    return st;
}

extern "C" void __declspec(dllexport) user_terminate(void)
{
    autobbox::main::PluginEntryCallbacks callbacks = {};
    const autobbox::main::PluginPerfCallbacks perf_callbacks = BuildPerfCallbacks();
    callbacks.unregister_perf_notifications = perf_callbacks.unregister_perf_notifications;
    callbacks.log_shutdown_perf_summary = perf_callbacks.log_shutdown_perf_summary;
    autobbox::main::TerminatePlugin(g_plugin_entry_state, callbacks, kEnableOpenPerfNotifications);
}
