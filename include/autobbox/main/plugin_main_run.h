#pragma once

#include <ProMdl.h>
#include <ProToolkit.h>

#include <string>

namespace autobbox::main {

struct PluginMainRunOptionState {
    ProBoolean *parts = nullptr;
    ProBoolean *assemblies = nullptr;
    ProBoolean *surface = nullptr;
    ProBoolean *curve = nullptr;
    ProBoolean *recompute = nullptr;
    ProBoolean *top_level_only = nullptr;
    ProBoolean *preheat_generics = nullptr;
};

struct PluginMainRunRuntime {
    bool (*is_task_running)() = nullptr;
    void (*set_task_running)(bool running) = nullptr;
    void (*report_session_begin)() = nullptr;
    void (*report_session_end)() = nullptr;
    void (*open_report_log)() = nullptr;
    void (*log_line)(const std::string &line) = nullptr;
    std::string (*format_model_tag)(ProMdl mdl) = nullptr;
};

struct PluginMainRunCommands {
    int (*run_size)() = nullptr;
    int (*run_volume)() = nullptr;
    int (*create_iso)() = nullptr;
    int (*delete_params)() = nullptr;
};

PluginMainRunCommands BuildPluginMainRunCommands(const PluginMainRunOptionState &option_state,
                                                 const PluginMainRunRuntime &runtime);

} // namespace autobbox::main
