#include "autobbox/main/plugin_model_diagnostics.h"

#include "autobbox/application/model_diagnostics.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/message_dialog.h"
#include "autobbox/ui/model_diagnostics_dialog.h"

#include <ProMdl.h>
#include <ProToolkit.h>
#include <ProUIMessage.h>

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
    explicit TaskGuard(bool *task_running) : task_running_(task_running) {}
    ~TaskGuard()
    {
        EndPluginReportSession();
        if (task_running_ != nullptr) {
            *task_running_ = false;
        }
    }

private:
    bool *task_running_ = nullptr;
};

bool CurrentModelIsPartOrAsm()
{
    ProMdl current = nullptr;
    return ProMdlCurrentGet(&current) == PRO_TK_NO_ERROR &&
           current != nullptr &&
           autobbox::creo::IsPartOrAsm(current);
}

} // namespace

int RunPluginModelDiagnosticsTask(const PluginModelDiagnosticsRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=model-diagnostics reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("===== Run begin mode=model-diagnostics =====");

    if (!CurrentModelIsPartOrAsm()) {
        LogLine("FAIL model-diagnostics reason=current-not-part-or-assembly");
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"模型检测",
            L"请先打开零件或装配，再执行模型检测。");
        return 0;
    }

    bool deep_run = false;
    std::vector<autobbox::application::ModelDiagnosticItem> items =
        autobbox::application::CollectModelDiagnostics(
            false,
            [](const std::string &line) { LogPluginReportLine(line); });
    autobbox::application::LogModelDiagnosticsReport(
        items,
        false,
        [](const std::string &line) { LogPluginReportLine(line); });

    while (true) {
        const autobbox::ui::ModelDiagnosticsDialogResult dialog_result =
            autobbox::ui::PromptModelDiagnosticsDialog(
                items,
                deep_run,
                [](const autobbox::application::ModelDiagnosticItem &item) {
                    return autobbox::application::LocateModelDiagnosticItem(
                        item,
                        [](const std::string &line) { LogPluginReportLine(line); });
                },
                [&runtime]() {
                    EndPluginReportSession();
                    if (runtime.open_report_file != nullptr) {
                        runtime.open_report_file();
                    } else {
                        OpenPluginReportLog();
                    }
                    BeginPluginReportSession();
                },
                [](const std::string &line) { LogPluginReportLine(line); });

        if (dialog_result != autobbox::ui::ModelDiagnosticsDialogResult::RequestDeepCheck ||
            deep_run) {
            break;
        }

        LogLine("model-diagnostics deep-check requested");
        deep_run = true;
        items = autobbox::application::CollectModelDiagnostics(
            true,
            [](const std::string &line) { LogPluginReportLine(line); });
        autobbox::application::LogModelDiagnosticsReport(
            items,
            true,
            [](const std::string &line) { LogPluginReportLine(line); });
    }

    LogLine("===== Run end mode=model-diagnostics =====");
    return 0;
}

} // namespace autobbox::main
