#pragma once

#include <ProMdl.h>

#include "autobbox/common/log.h"
#include "autobbox/main/open_perf_trace.h"
#include "autobbox/main/plugin_entry.h"
#include "autobbox/main/plugin_main_run.h"

namespace autobbox::main {

struct PluginRuntimeBridgeState {
    PluginEntryState *entry_state = nullptr;
    autobbox::common::BufferedLogSession *report_session = nullptr;
    OpenPerfTraceState *open_perf_trace_state = nullptr;
    bool *task_running = nullptr;
    bool enable_open_perf_notifications = false;
    std::string (*format_model_tag)(ProMdl mdl) = nullptr;
    void (*log_perf_line)(const std::string &line) = nullptr;
};

void ConfigurePluginRuntimeBridge(const PluginRuntimeBridgeState &state);

PluginMainRunRuntime BuildPluginMainRunRuntime();
PluginPerfCallbacks BuildPluginPerfCallbacks();

void BeginPluginReportSession();
void EndPluginReportSession();
void OpenPluginReportLog();
void LogPluginReportLine(const std::string &line);

} // namespace autobbox::main
