#include "autobbox/main/plugin_batch_rename.h"

#include "autobbox/application/batch_rename.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/batch_rename_dialog.h"
#include "autobbox/ui/message_dialog.h"

#include <ProMdl.h>
#include <ProToolkit.h>
#include <ProUIMessage.h>
#include <ProWindows.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace autobbox::main {

namespace {

void LogLine(const char *fmt, ...)
{
    if (fmt == nullptr) {
        return;
    }

    char buffer[4096] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    LogPluginReportLine(buffer);
}

struct TaskGuard {
    explicit TaskGuard(bool *task_running)
        : task_running(task_running)
    {
    }

    ~TaskGuard()
    {
        EndPluginReportSession();
        if (task_running != nullptr) {
            *task_running = false;
        }
    }

    bool *task_running = nullptr;
};

ProBoolean ValueOrDefault(const ProBoolean *value, ProBoolean fallback)
{
    return value != nullptr ? *value : fallback;
}

void RefreshCurrentWindow()
{
    int window_id = -1;
    if (ProWindowCurrentGet(&window_id) == PRO_TK_NO_ERROR && window_id >= 0) {
        ProWindowRefresh(window_id);
        ProWindowRepaint(window_id);
    }
}

std::string RuntimeTag(ProMdl mdl, const PluginBatchRenameRuntime &runtime)
{
    if (runtime.format_model_tag != nullptr) {
        return runtime.format_model_tag(mdl);
    }
    return autobbox::creo::DefaultModelTag(mdl);
}

autobbox::core::BatchRenameOptions BuildOptions(const PluginBatchRenameRuntime &runtime)
{
    autobbox::core::BatchRenameOptions options;
    options.parts = ValueOrDefault(runtime.parts, PRO_B_TRUE);
    options.assemblies = ValueOrDefault(runtime.assemblies, PRO_B_TRUE);
    options.top_level_only = ValueOrDefault(runtime.top_level_only, PRO_B_FALSE);
    return options;
}

std::wstring FirstValidationError(const std::vector<autobbox::core::BatchRenameValidationIssue> &issues)
{
    if (issues.empty()) {
        return L"";
    }
    return L"第 " + std::to_wstring(static_cast<int>(issues.front().row_index) + 1) +
           L" 行：" + issues.front().message;
}

} // namespace

int RunPluginBatchRenameTask(const PluginBatchRenameRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=batch-rename reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("===== Run begin mode=batch-rename =====");

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        autobbox::creo::ModelType(current) != PRO_MDL_ASSEMBLY) {
        LogLine("FAIL batch-rename reason=current-not-assembly");
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"批量重命名",
            L"当前不是装配，请先打开一个装配模型。");
        OpenPluginReportLog();
        return 0;
    }

    const autobbox::core::BatchRenameOptions options = BuildOptions(runtime);
    LogLine("batch-rename options parts=%d assemblies=%d top2=%d current=%s",
            static_cast<int>(options.parts),
            static_cast<int>(options.assemblies),
            static_cast<int>(options.top_level_only),
            RuntimeTag(current, runtime).c_str());

    if (options.parts != PRO_B_TRUE && options.assemblies != PRO_B_TRUE) {
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_INFO,
            L"批量重命名",
            L"请至少勾选“零件”或“组件”，然后重新打开批量重命名。");
        OpenPluginReportLog();
        return 0;
    }

    std::vector<autobbox::core::BatchRenameCandidate> candidates =
        autobbox::application::CollectBatchRenameCandidates(options);
    LogLine("batch-rename initial candidates=%d", static_cast<int>(candidates.size()));
    if (candidates.empty()) {
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_INFO,
            L"批量重命名",
            L"按当前零件/组件/仅二层过滤条件未找到候选模型。");
        OpenPluginReportLog();
        return 0;
    }

    autobbox::ui::BatchRenameDialogCallbacks callbacks;
    callbacks.collect_candidates = [runtime]() {
        return autobbox::application::CollectBatchRenameCandidates(BuildOptions(runtime));
    };
    callbacks.validate_candidates = [](std::vector<autobbox::core::BatchRenameCandidate> &rows,
                                       std::wstring &error_out) {
        std::vector<autobbox::core::BatchRenameValidationIssue> issues;
        const bool ok = autobbox::application::ValidateBatchRenameCandidates(rows, issues);
        error_out = ok ? L"" : FirstValidationError(issues);
        LogLine("batch-rename validate rows=%d ok=%d issues=%d",
                static_cast<int>(rows.size()),
                ok ? 1 : 0,
                static_cast<int>(issues.size()));
        return ok;
    };
    callbacks.apply_candidates = [](std::vector<autobbox::core::BatchRenameCandidate> &rows,
                                    autobbox::core::BatchRenameApplySummary &summary,
                                    std::wstring &error_out) {
        const ProError st = autobbox::application::ApplyBatchRenameCandidates(
            rows,
            summary,
            [](const std::string &line) { LogPluginReportLine(line); });
        error_out = summary.summary_text;
        LogLine("batch-rename apply status=%d changed=%d renamed=%d common=%d failed=%d skipped=%d",
                static_cast<int>(st),
                summary.changed_rows,
                summary.renamed,
                summary.common_updated,
                summary.failed,
                summary.skipped);
        if (st == PRO_TK_NO_ERROR || summary.renamed > 0 || summary.common_updated > 0) {
            RefreshCurrentWindow();
        }
        return st == PRO_TK_NO_ERROR;
    };
    callbacks.log_sink = [](const std::string &line) { LogPluginReportLine(line); };

    autobbox::ui::PromptBatchRenameDialog(candidates, callbacks);
    OpenPluginReportLog();
    return 0;
}

} // namespace autobbox::main
