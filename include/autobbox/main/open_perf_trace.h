#pragma once

#include <ProMdl.h>
#include <ProNotify.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>
#include <string>

namespace autobbox::main {

struct OpenPerfTraceState {
    ULONGLONG open_ok_ms = 0;
    ULONGLONG retrieve_post_ms = 0;
    ULONGLONG retrieve_all_first_ms = 0;
    ULONGLONG retrieve_all_last_ms = 0;
    int retrieve_all_count = 0;
    ProMdlType open_mdl_type = PRO_MDL_UNUSED;
    int open_mdl_subtype = 0;
    std::wstring open_mdl_name;
    bool open_is_drawing = false;
    bool chain_summarized = false;
    int retrieve_all_log_count = 0;
    bool notify_open_ok = false;
    bool notify_retrieve_post = false;
    bool notify_retrieve_post_all = false;
};

struct OpenPerfTraceCallbacks {
    std::function<bool()> is_task_running;
    std::function<void(const std::string &line)> log_sink;
    std::function<std::string(ProMdl mdl)> format_model_tag;
};

void ResetOpenPerfTrace(OpenPerfTraceState &state);
void RegisterOpenPerfNotifications(OpenPerfTraceState &state,
                                   const OpenPerfTraceCallbacks &callbacks,
                                   bool enabled);
void UnregisterOpenPerfNotifications(OpenPerfTraceState &state,
                                     const OpenPerfTraceCallbacks &callbacks,
                                     bool enabled);
void LogOpenPerfShutdownSummary(const OpenPerfTraceState &state,
                                const OpenPerfTraceCallbacks &callbacks,
                                bool enabled);

} // namespace autobbox::main
