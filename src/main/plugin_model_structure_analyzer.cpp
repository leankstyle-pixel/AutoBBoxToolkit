#include "autobbox/main/plugin_model_structure_analyzer.h"

#include "autobbox/application/model_structure_analyzer.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/message_dialog.h"
#include "autobbox/ui/model_structure_analyzer_dialog.h"

#include <ProMdl.h>
#include <ProAsmcomppath.h>
#include <ProModelitem.h>
#include <ProSelection.h>
#include <ProToolkit.h>
#include <ProUIMessage.h>

#include <cstdarg>
#include <cstdio>

namespace autobbox::main {

namespace {

void LogLine(const char *fmt, ...)
{
    if (fmt == nullptr) return;
    char buffer[4096] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    LogPluginReportLine(buffer);
}

struct TaskGuard {
    explicit TaskGuard(bool *task_running) : task_running_(task_running) {}
    ~TaskGuard()
    {
        EndPluginReportSession();
        if (task_running_ != nullptr) *task_running_ = false;
    }
    bool *task_running_ = nullptr;
};

bool CurrentModelIsPartOrAsm(ProMdl &current)
{
    current = nullptr;
    return ProMdlCurrentGet(&current) == PRO_TK_NO_ERROR &&
           current != nullptr &&
           autobbox::creo::IsPartOrAsm(current);
}

struct SelectedAnalysisTarget {
    ProMdl model = nullptr;
    bool has_component_path = false;
    ProAsmcomppath component_path = {};
};

bool ResolveSelectionModel(ProSelection selection, SelectedAnalysisTarget &target_out)
{
    target_out = {};
    if (selection == nullptr) {
        return false;
    }
    ProAsmcomppath path = {};
    if (ProSelectionAsmcomppathGet(selection, &path) == PRO_TK_NO_ERROR) {
        ProMdl mdl = nullptr;
        if (ProAsmcomppathMdlGet(&path, &mdl) == PRO_TK_NO_ERROR &&
            mdl != nullptr &&
            autobbox::creo::IsPartOrAsm(mdl)) {
            target_out.model = mdl;
            if (path.table_num > 0) {
                target_out.has_component_path = true;
                target_out.component_path = path;
            }
            return true;
        }
    }
    ProModelitem item = {};
    if (ProSelectionModelitemGet(selection, &item) == PRO_TK_NO_ERROR) {
        ProMdl owner = nullptr;
        if (ProModelitemMdlGet(&item, &owner) == PRO_TK_NO_ERROR &&
            owner != nullptr &&
            autobbox::creo::IsPartOrAsm(owner)) {
            target_out.model = owner;
            return true;
        }
    }
    return false;
}

ProError SelectModelForAnalysis(SelectedAnalysisTarget &target_out)
{
    target_out = {};
    char filter[] = "prt_or_asm";
    ProSelection *selections = nullptr;
    int selection_count = 0;
    const ProError st = ProSelect(filter, 1, nullptr, nullptr, nullptr, nullptr, &selections, &selection_count);
    LogLine("model-structure-analyzer select status=%d count=%d", static_cast<int>(st), selection_count);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }
    if (selections == nullptr || selection_count <= 0) {
        return PRO_TK_USER_ABORT;
    }
    if (!ResolveSelectionModel(selections[0], target_out)) {
        return PRO_TK_INVALID_TYPE;
    }
    return PRO_TK_NO_ERROR;
}

} // namespace

int RunPluginModelStructureAnalyzerTask(const PluginModelStructureAnalyzerRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=model-structure-analyzer reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("===== Run begin mode=model-structure-analyzer =====");

    SelectedAnalysisTarget target;
    ProError select_status = SelectModelForAnalysis(target);
    ProMdl current = target.model;
    if (select_status == PRO_TK_USER_ABORT) {
        LogLine("model-structure-analyzer user-abort-select");
        return 0;
    }
    if (select_status != PRO_TK_NO_ERROR || current == nullptr) {
        LogLine("WARN model-structure-analyzer select-failed status=%d fallback-current", static_cast<int>(select_status));
    }
    if (current == nullptr && !CurrentModelIsPartOrAsm(current)) {
        LogLine("FAIL model-structure-analyzer reason=no-selected-or-current-part-or-assembly");
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"模型结构分析器",
            L"请选择零件或装配模型，或先打开零件/装配后再执行模型结构分析。");
        return 0;
    }

    while (true) {
        autobbox::application::ModelStructureReport report =
            autobbox::application::CollectModelStructureAnalysis(
                current,
                current,
                target.has_component_path ? &target.component_path : nullptr,
                [](const std::string &line) { LogPluginReportLine(line); });
        LogLine("%s", autobbox::common::WToA(
                           autobbox::application::BuildModelStructureSummary(report).c_str())
                           .c_str());

        const autobbox::ui::ModelStructureAnalyzerDialogResult result =
            autobbox::ui::PromptModelStructureAnalyzerDialog(
                report,
                [](const std::string &line) { LogPluginReportLine(line); });
        if (result != autobbox::ui::ModelStructureAnalyzerDialogResult::RequestRefresh) {
            break;
        }
        LogLine("model-structure-analyzer refresh requested");
        if (current == nullptr || !autobbox::creo::IsPartOrAsm(current)) {
            break;
        }
    }

    LogLine("===== Run end mode=model-structure-analyzer =====");
    return 0;
}

} // namespace autobbox::main
