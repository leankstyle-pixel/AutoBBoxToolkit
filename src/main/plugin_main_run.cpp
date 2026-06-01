#include "autobbox/main/plugin_main_run.h"

#include "autobbox/application/model_run_tasks.h"

namespace autobbox::main {

namespace {

PluginMainRunOptionState g_option_state = {};
PluginMainRunRuntime g_runtime = {};

autobbox::application::RunContext BuildMainRunContext()
{
    autobbox::application::RunContext ctx = {};
    if (g_option_state.parts != nullptr) {
        ctx.options.parts = *g_option_state.parts;
    }
    if (g_option_state.assemblies != nullptr) {
        ctx.options.assemblies = *g_option_state.assemblies;
    }
    if (g_option_state.surface != nullptr) {
        ctx.options.surface = *g_option_state.surface;
    }
    if (g_option_state.curve != nullptr) {
        ctx.options.curve = *g_option_state.curve;
    }
    if (g_option_state.recompute != nullptr) {
        ctx.options.recompute = *g_option_state.recompute;
    }
    if (g_option_state.top_level_only != nullptr) {
        ctx.options.top_level_only = *g_option_state.top_level_only;
    }
    if (g_option_state.preheat_generics != nullptr) {
        ctx.options.preheat_generics = *g_option_state.preheat_generics;
    }

    ctx.is_task_running = []() {
        return g_runtime.is_task_running != nullptr && g_runtime.is_task_running();
    };
    ctx.set_task_running = [](bool running) {
        if (g_runtime.set_task_running != nullptr) {
            g_runtime.set_task_running(running);
        }
    };
    ctx.report_session_begin = []() {
        if (g_runtime.report_session_begin != nullptr) {
            g_runtime.report_session_begin();
        }
    };
    ctx.report_session_end = []() {
        if (g_runtime.report_session_end != nullptr) {
            g_runtime.report_session_end();
        }
    };
    ctx.open_report_log = []() {
        if (g_runtime.open_report_log != nullptr) {
            g_runtime.open_report_log();
        }
    };
    ctx.log_sink = [](const std::string &line) {
        if (g_runtime.log_line != nullptr) {
            g_runtime.log_line(line);
        }
    };
    ctx.format_model_tag = [](ProMdl mdl) {
        return g_runtime.format_model_tag != nullptr
                   ? g_runtime.format_model_tag(mdl)
                   : std::string();
    };
    return ctx;
}

int RunTask(autobbox::application::TaskMode mode)
{
    return autobbox::application::ExecuteMainRunTask(mode, BuildMainRunContext());
}

int RunSizeCommand()
{
    return RunTask(autobbox::application::TaskMode::SizeOnly);
}

int RunVolumeCommand()
{
    return RunTask(autobbox::application::TaskMode::VolumeOnly);
}

int RunCreateIsoCommand()
{
    return RunTask(autobbox::application::TaskMode::IsoOnly);
}

int RunDeleteParamsCommand()
{
    return RunTask(autobbox::application::TaskMode::DeleteOnly);
}

} // namespace

PluginMainRunCommands BuildPluginMainRunCommands(const PluginMainRunOptionState &option_state,
                                                 const PluginMainRunRuntime &runtime)
{
    g_option_state = option_state;
    g_runtime = runtime;

    PluginMainRunCommands commands = {};
    commands.run_size = RunSizeCommand;
    commands.run_volume = RunVolumeCommand;
    commands.create_iso = RunCreateIsoCommand;
    commands.delete_params = RunDeleteParamsCommand;
    return commands;
}

} // namespace autobbox::main
