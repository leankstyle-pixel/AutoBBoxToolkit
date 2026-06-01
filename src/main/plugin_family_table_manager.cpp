#include "autobbox/main/plugin_family_table_manager.h"

#include "autobbox/application/ft_discovery.h"
#include "autobbox/application/ft_logger.h"
#include "autobbox/application/ft_reader.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/family_table_manager_dialog.h"
#include "autobbox/ui/message_dialog.h"

#include <ProMdl.h>
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
    explicit TaskGuard(bool *running) : running(running) {}
    ~TaskGuard() { EndPluginReportSession(); if (running != nullptr) *running = false; }
    bool *running = nullptr;
};

} // namespace

int RunPluginFamilyTableManagerTask(const PluginFamilyTableManagerRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=family-table-manager reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) *runtime.task_running = true;
    TaskGuard guard(runtime.task_running);
    BeginPluginReportSession();
    LogLine("===== Run begin mode=family-table-manager =====");

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR || current == nullptr) {
        autobbox::ui::ShowSimpleMessageDialog(PROUIMESSAGE_WARNING, L"No active model", L"Open a Creo part or assembly before launching Family Table Manager.");
        OpenPluginReportLog();
        return 0;
    }
    LogLine("family-table-manager current model acquired");

    autobbox::core::FtWorkspace workspace;
    LogLine("family-table-manager discover begin");
    ProError st = autobbox::application::DiscoverFamilyTableWorkspace(current, workspace);
    LogLine("family-table-manager discover end status=%d levels=%d", static_cast<int>(st), static_cast<int>(workspace.level_nodes.size()));
    if (st == PRO_TK_NO_ERROR) {
        LogLine("family-table-manager read begin");
        st = autobbox::application::ReadFamilyTableWorkspace(workspace);
        LogLine("family-table-manager read end status=%d levels=%d", static_cast<int>(st), static_cast<int>(workspace.level_nodes.size()));
    }
    LogLine("family-table-manager discover/read status=%d levels=%d", static_cast<int>(st), static_cast<int>(workspace.level_nodes.size()));
    LogLine("family-table-manager dialog begin");
    autobbox::ui::PromptFamilyTableManagerDialog(workspace);
    LogLine("family-table-manager dialog end");
    OpenPluginReportLog();
    return 0;
}

} // namespace autobbox::main
