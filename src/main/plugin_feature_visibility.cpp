#include "autobbox/main/plugin_feature_visibility.h"

#include "autobbox/application/feature_visibility.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/message_dialog.h"

#include <ProMdl.h>
#include <ProToolkit.h>
#include <ProUIMessage.h>

#include <cstdarg>
#include <cstdio>

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

} // namespace

int RunPluginFeatureVisibilityTask(const PluginFeatureVisibilityRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=feature-visibility reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("===== Run begin mode=feature-visibility =====");

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        autobbox::creo::ModelType(current) != PRO_MDL_ASSEMBLY) {
        LogLine("FAIL feature-visibility reason=current-not-assembly");
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u9690\u85cf/\u663e\u793a\u7279\u5f81",
            L"\u8bf7\u5148\u6253\u5f00\u88c5\u914d\uff0c\u518d\u6267\u884c\u9690\u85cf/\u663e\u793a\u66f2\u7ebf\u66f2\u9762\u8349\u7ed8\u7279\u5f81\u3002");
        OpenPluginReportLog();
        return 0;
    }

    autobbox::application::FeatureVisibilitySummary summary;
    const bool ok = autobbox::application::ToggleAssemblyDatumFeatureVisibility(
        summary,
        [](const std::string &line) { LogPluginReportLine(line); });

    std::wstring message;
    ProUIMessageType message_type = PROUIMESSAGE_INFO;
    if (summary.target_features <= 0 &&
        summary.status_layers_created <= 0 &&
        summary.status_layers_reused <= 0 &&
        summary.changed <= 0 &&
        summary.unchanged <= 0) {
        message = L"\u5f53\u524d\u88c5\u914d\u4e2d\u672a\u627e\u5230\u53ef\u9690\u85cf/\u663e\u793a\u7684\u66f2\u7ebf\u3001\u66f2\u9762\u6216\u8349\u7ed8\u7279\u5f81\u3002";
        message_type = PROUIMESSAGE_WARNING;
    } else {
        message = autobbox::application::BuildFeatureVisibilitySummaryText(summary);
        if (!ok) {
            message_type = PROUIMESSAGE_WARNING;
        }
    }

    autobbox::ui::ShowSimpleMessageDialog(
        message_type,
        L"\u9690\u85cf/\u663e\u793a\u7279\u5f81",
        message.c_str());
    OpenPluginReportLog();
    return 0;
}

} // namespace autobbox::main
