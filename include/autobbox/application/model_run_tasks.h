#pragma once

#include <ProMdl.h>
#include <ProToolkit.h>

#include <functional>
#include <string>

namespace autobbox::application {

enum class TaskMode {
    SizeOnly,
    VolumeOnly,
    IsoOnly,
    DeleteOnly
};

struct MainRunOptions {
    ProBoolean parts = PRO_B_TRUE;
    ProBoolean assemblies = PRO_B_TRUE;
    ProBoolean surface = PRO_B_FALSE;
    ProBoolean curve = PRO_B_FALSE;
    ProBoolean recompute = PRO_B_TRUE;
    ProBoolean top_level_only = PRO_B_FALSE;
    ProBoolean preheat_generics = PRO_B_TRUE;
};

struct RunContext {
    MainRunOptions options = {};
    std::function<bool()> is_task_running;
    std::function<void(bool running)> set_task_running;
    std::function<void()> report_session_begin;
    std::function<void()> report_session_end;
    std::function<void()> open_report_log;
    std::function<void(const std::string &line)> log_sink;
    std::function<std::string(ProMdl mdl)> format_model_tag;
};

int ExecuteMainRunTask(TaskMode mode, const RunContext &ctx);
ProError CreateIsoView(ProMdl mdl);
const wchar_t *ModeName(TaskMode mode);
void ShowRunSummary(TaskMode mode,
                    int targets,
                    int ok_count,
                    int fail_count,
                    const std::function<void(const std::string &line)> &log_sink);

} // namespace autobbox::application
