#pragma once

#include <ProToolkit.h>

#include "autobbox/main/command_registry.h"

#include <string>

namespace autobbox::main {

struct PluginEntryState {
    std::string startup_log;
    std::string report_log;
    std::string message_file;
    std::wstring message_file_w;
    std::string ribbon_file;
    std::wstring ribbon_file_w;
};

struct PluginEntryCallbacks {
    CommandCallbacks command_callbacks = {};
    void (*reset_open_perf_trace)() = nullptr;
    void (*register_perf_notifications)() = nullptr;
    void (*unregister_perf_notifications)() = nullptr;
    void (*log_shutdown_perf_summary)() = nullptr;
};

struct PluginPerfCallbacks {
    void (*reset_open_perf_trace)() = nullptr;
    void (*register_perf_notifications)() = nullptr;
    void (*unregister_perf_notifications)() = nullptr;
    void (*log_shutdown_perf_summary)() = nullptr;
};

PluginEntryCallbacks BuildPluginEntryCallbacks(const CommandCallbacks &command_callbacks,
                                               const PluginPerfCallbacks &perf_callbacks);

ProError InitializePlugin(PluginEntryState &state,
                          const PluginEntryCallbacks &callbacks,
                          const wchar_t *build_stamp,
                          const wchar_t *message_file_name,
                          const wchar_t *ribbon_file_name,
                          bool enable_cmd_designate,
                          bool enable_cmd_icons,
                          bool enable_perf_notifications,
                          wchar_t err_buff[]);

void TerminatePlugin(const PluginEntryState &state,
                     const PluginEntryCallbacks &callbacks,
                     bool enable_perf_notifications);

} // namespace autobbox::main
