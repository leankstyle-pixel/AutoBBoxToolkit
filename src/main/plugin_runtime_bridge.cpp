#include "autobbox/main/plugin_runtime_bridge.h"

#ifndef PRO_USE_VAR_ARGS
#define PRO_USE_VAR_ARGS
#endif
#include <ProMessage.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>

namespace autobbox::main {

namespace {

PluginRuntimeBridgeState g_state = {};

void AppendReportLine(const char *fmt, ...)
{
    if (g_state.entry_state == nullptr || fmt == nullptr) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    autobbox::common::AppendFormattedLine(
        g_state.entry_state->report_log,
        g_state.report_session != nullptr ? g_state.report_session->file : nullptr,
        nullptr,
        fmt,
        args);
    va_end(args);
}

bool IsTaskRunning()
{
    return g_state.task_running != nullptr && *g_state.task_running;
}

void SetTaskRunning(bool running)
{
    if (g_state.task_running != nullptr) {
        *g_state.task_running = running;
    }
}

std::string FormatModelTag(ProMdl mdl)
{
    return g_state.format_model_tag != nullptr
               ? g_state.format_model_tag(mdl)
               : std::string();
}

void LogPerfLine(const std::string &line)
{
    if (g_state.log_perf_line != nullptr) {
        g_state.log_perf_line(line);
    }
}

OpenPerfTraceCallbacks BuildOpenPerfTraceCallbacks()
{
    OpenPerfTraceCallbacks callbacks = {};
    callbacks.is_task_running = IsTaskRunning;
    callbacks.log_sink = LogPerfLine;
    callbacks.format_model_tag = FormatModelTag;
    return callbacks;
}

void ResetOpenPerfTraceState()
{
    if (g_state.open_perf_trace_state != nullptr) {
        ResetOpenPerfTrace(*g_state.open_perf_trace_state);
    }
}

void RegisterPerfNotifications()
{
    if (g_state.open_perf_trace_state != nullptr) {
        RegisterOpenPerfNotifications(
            *g_state.open_perf_trace_state,
            BuildOpenPerfTraceCallbacks(),
            g_state.enable_open_perf_notifications);
    }
}

void UnregisterPerfNotifications()
{
    if (g_state.open_perf_trace_state != nullptr) {
        UnregisterOpenPerfNotifications(
            *g_state.open_perf_trace_state,
            BuildOpenPerfTraceCallbacks(),
            g_state.enable_open_perf_notifications);
    }
}

void LogOpenPerfShutdownSummary()
{
    if (g_state.open_perf_trace_state != nullptr) {
        autobbox::main::LogOpenPerfShutdownSummary(
            *g_state.open_perf_trace_state,
            BuildOpenPerfTraceCallbacks(),
            g_state.enable_open_perf_notifications);
    }
}

} // namespace

void ConfigurePluginRuntimeBridge(const PluginRuntimeBridgeState &state)
{
    g_state = state;
}

PluginMainRunRuntime BuildPluginMainRunRuntime()
{
    PluginMainRunRuntime runtime = {};
    runtime.is_task_running = IsTaskRunning;
    runtime.set_task_running = SetTaskRunning;
    runtime.report_session_begin = BeginPluginReportSession;
    runtime.report_session_end = EndPluginReportSession;
    runtime.open_report_log = OpenPluginReportLog;
    runtime.log_line = LogPluginReportLine;
    runtime.format_model_tag = FormatModelTag;
    return runtime;
}

PluginPerfCallbacks BuildPluginPerfCallbacks()
{
    PluginPerfCallbacks callbacks = {};
    callbacks.reset_open_perf_trace = ResetOpenPerfTraceState;
    callbacks.register_perf_notifications = RegisterPerfNotifications;
    callbacks.unregister_perf_notifications = UnregisterPerfNotifications;
    callbacks.log_shutdown_perf_summary = LogOpenPerfShutdownSummary;
    return callbacks;
}

void BeginPluginReportSession()
{
    if (g_state.entry_state != nullptr && g_state.report_session != nullptr) {
        autobbox::common::BeginBufferedLogSession(
            *g_state.report_session,
            g_state.entry_state->report_log);
    }
}

void EndPluginReportSession()
{
    if (g_state.report_session != nullptr) {
        autobbox::common::EndBufferedLogSession(*g_state.report_session);
    }
}

void OpenPluginReportLog()
{
    // Keep the historical call sites as "report finalized" markers, but do
    // not pop Notepad after command execution. The report file is still
    // written under the current Creo working directory (autobbox_report.txt).
    //
    // End the buffered session here so early-return paths also leave the
    // report complete on disk, then display a non-modal Creo message-area
    // notice. This preserves the "no report popup" behavior while avoiding a
    // silent click when the command only updates the report.
    EndPluginReportSession();

    ProFileName message_file = {0};
    wcsncpy_s(message_file,
              sizeof(message_file) / sizeof(message_file[0]),
              L"autobbox_msg.txt",
              _TRUNCATE);
    ProMessageDisplay(message_file, const_cast<char *>("ABReportReady"));
}

void LogPluginReportLine(const std::string &line)
{
    AppendReportLine("%s", line.c_str());
}

} // namespace autobbox::main
