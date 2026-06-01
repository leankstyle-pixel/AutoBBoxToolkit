#include "autobbox/main/plugin_entry.h"

#include "autobbox/common/files.h"
#include "autobbox/common/log.h"
#include "autobbox/common/strings.h"

#include <ProNotify.h>
#include <ProUtil.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <Windows.h>

namespace autobbox::main {

namespace {

PluginEntryState *g_active_entry_state = nullptr;

void LogLine(const std::string &startup_log, const char *prefix, const char *fmt, ...)
{
    if (startup_log.empty() || fmt == nullptr) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    autobbox::common::AppendFormattedLine(startup_log, prefix, fmt, args);
    va_end(args);
}

void LogDirectoryLine(const char *fmt, ...)
{
    if (g_active_entry_state == nullptr || g_active_entry_state->startup_log.empty() || fmt == nullptr) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    autobbox::common::AppendFormattedLine(g_active_entry_state->startup_log, "DIR ", fmt, args);
    va_end(args);
}

ProError OnDirectoryChangePost(ProPath new_path)
{
    const std::wstring new_dir(new_path);
    const bool saved = !new_dir.empty() && autobbox::common::SaveLastWorkingDirectoryW(new_dir);
    LogDirectoryLine("directory_change_post path=%s saved=%d",
                     autobbox::common::WToA(new_dir.c_str()).c_str(),
                     saved ? 1 : 0);
    return PRO_TK_NO_ERROR;
}

} // namespace

PluginEntryCallbacks BuildPluginEntryCallbacks(const CommandCallbacks &command_callbacks,
                                               const PluginPerfCallbacks &perf_callbacks)
{
    PluginEntryCallbacks callbacks = {};
    callbacks.command_callbacks = command_callbacks;
    callbacks.reset_open_perf_trace = perf_callbacks.reset_open_perf_trace;
    callbacks.register_perf_notifications = perf_callbacks.register_perf_notifications;
    callbacks.unregister_perf_notifications = perf_callbacks.unregister_perf_notifications;
    callbacks.log_shutdown_perf_summary = perf_callbacks.log_shutdown_perf_summary;
    return callbacks;
}

ProError InitializePlugin(PluginEntryState &state,
                          const PluginEntryCallbacks &callbacks,
                          const wchar_t *build_stamp,
                          const wchar_t *message_file_name,
                          const wchar_t *ribbon_file_name,
                          bool enable_cmd_designate,
                          bool enable_cmd_icons,
                          bool enable_perf_notifications,
                          wchar_t err_buff[])
{
    g_active_entry_state = &state;

    const std::wstring initial_working_dir = autobbox::common::CurrentWorkingDirectoryW();
    std::wstring saved_working_dir;
    const bool has_saved_working_dir = autobbox::common::LoadLastWorkingDirectoryW(saved_working_dir);
    ProError restore_working_dir_status = PRO_TK_GENERAL_ERROR;
    if (has_saved_working_dir && autobbox::common::DirectoryExistsW(saved_working_dir)) {
        ProPath restore_dir = {0};
        wcsncpy_s(restore_dir, saved_working_dir.c_str(), _TRUNCATE);
        restore_working_dir_status = ProDirectoryChange(restore_dir);
    }

    autobbox::common::BuildLogPaths(state.startup_log, state.report_log);
    if (!state.startup_log.empty()) {
        std::remove(state.startup_log.c_str());
    }
    if (!state.report_log.empty()) {
        std::remove(state.report_log.c_str());
    }

    LogLine(state.startup_log, nullptr, "===== user_initialize begin =====");
    LogLine(state.startup_log, nullptr, "%s", build_stamp == nullptr ? "" : autobbox::common::WToA(build_stamp).c_str());
    LogLine(state.startup_log,
            "DIR ",
            "initial=%s",
            autobbox::common::WToA(initial_working_dir.c_str()).c_str());
    LogLine(state.startup_log,
            "DIR ",
            "saved=%s has_saved=%d exists=%d restore_status=%d current=%s",
            has_saved_working_dir ? autobbox::common::WToA(saved_working_dir.c_str()).c_str() : "<none>",
            has_saved_working_dir ? 1 : 0,
            has_saved_working_dir && autobbox::common::DirectoryExistsW(saved_working_dir) ? 1 : 0,
            static_cast<int>(restore_working_dir_status),
            autobbox::common::WToA(autobbox::common::CurrentWorkingDirectoryW().c_str()).c_str());
    const ULONGLONG t_init_begin = GetTickCount64();
    if (callbacks.reset_open_perf_trace != nullptr) {
        callbacks.reset_open_perf_trace();
    }
    LogLine(state.startup_log, "PERF ", "init begin");

    const std::wstring raw_text_root = autobbox::common::ResolveToolkitTextRoot();
    LogLine(state.startup_log,
            nullptr,
            "Toolkit text path raw: %s",
            autobbox::common::WToA(raw_text_root.c_str()).c_str());

    state.message_file_w = autobbox::common::ResolveMsgFile(message_file_name);
    state.message_file = autobbox::common::WToA(state.message_file_w.c_str());
    state.ribbon_file_w = autobbox::common::ResolveRibbonFile(ribbon_file_name);
    state.ribbon_file = autobbox::common::WToA(state.ribbon_file_w.c_str());
    LogLine(state.startup_log, nullptr, "Resolved message file path: %s", state.message_file.c_str());
    LogLine(state.startup_log, nullptr, "Resolved ribbon file path: %s", state.ribbon_file.c_str());

    const ProError directory_notify_status =
        ProNotificationSet(PRO_DIRECTORY_CHANGE_POST, reinterpret_cast<ProFunction>(OnDirectoryChangePost));
    LogLine(state.startup_log,
            "DIR ",
            "notification_set status=%d",
            static_cast<int>(directory_notify_status));

    CommandIds command_ids = {};
    const ULONGLONG t_register_begin = GetTickCount64();
    RegisterPluginCommands(callbacks.command_callbacks, command_ids, state.startup_log);
    const ULONGLONG t_register_ms = GetTickCount64() - t_register_begin;
    LogLine(state.startup_log, "PERF ", "init register_ms=%llu", static_cast<unsigned long long>(t_register_ms));

    const ULONGLONG t_designate_begin = GetTickCount64();
    if (enable_cmd_designate) {
        ApplyCommandDesignations(command_ids, message_file_name == nullptr ? L"" : message_file_name, state.message_file, state.startup_log);
    } else {
        LogLine(state.startup_log, nullptr, "Skip ProCmdDesignate path (rbn text used)");
    }
    const ULONGLONG t_designate_ms = GetTickCount64() - t_designate_begin;
    LogLine(state.startup_log,
            "PERF ",
            "init designate_ms=%llu enabled=%d",
            static_cast<unsigned long long>(t_designate_ms),
            enable_cmd_designate ? 1 : 0);

    const ULONGLONG t_icon_begin = GetTickCount64();
    if (enable_cmd_icons) {
        ApplyCommandIcons(command_ids, raw_text_root, state.startup_log);
        LogLine(state.startup_log, nullptr, "Skip option ProCmdIconSet for text-first checkbox labels");
    } else {
        LogLine(state.startup_log, nullptr, "Skip ProCmdIconSet path (rbn icon used)");
    }
    const ULONGLONG t_icon_ms = GetTickCount64() - t_icon_begin;
    LogLine(state.startup_log,
            "PERF ",
            "init icon_ms=%llu enabled=%d",
            static_cast<unsigned long long>(t_icon_ms),
            enable_cmd_icons ? 1 : 0);

    const ULONGLONG t_ribbon_begin = GetTickCount64();
    LoadRibbonDefinition(state.ribbon_file_w,
                         ribbon_file_name == nullptr ? L"" : ribbon_file_name,
                         state.ribbon_file,
                         state.startup_log);
    const ULONGLONG t_ribbon_ms = GetTickCount64() - t_ribbon_begin;
    LogLine(state.startup_log, "PERF ", "init ribbon_ms=%llu", static_cast<unsigned long long>(t_ribbon_ms));

    AddLegacyMenuFallback(
        command_ids,
        message_file_name == nullptr ? L"" : message_file_name,
        state.startup_log);

    const ULONGLONG t_notify_begin = GetTickCount64();
    if (enable_perf_notifications && callbacks.register_perf_notifications != nullptr) {
        callbacks.register_perf_notifications();
    }
    const ULONGLONG t_notify_ms = GetTickCount64() - t_notify_begin;
    LogLine(state.startup_log,
            "PERF ",
            "init notify_ms=%llu enabled=%d",
            static_cast<unsigned long long>(t_notify_ms),
            enable_perf_notifications ? 1 : 0);

    const ULONGLONG t_init_ms = GetTickCount64() - t_init_begin;
    LogLine(state.startup_log, "PERF ", "init total_ms=%llu", static_cast<unsigned long long>(t_init_ms));
    LogLine(state.startup_log, nullptr, "===== user_initialize end =====");

    if (err_buff != nullptr) {
        err_buff[0] = L'\0';
    }
    return PRO_TK_NO_ERROR;
}

void TerminatePlugin(const PluginEntryState &state,
                     const PluginEntryCallbacks &callbacks,
                     bool enable_perf_notifications)
{
    const bool saved_current_working_dir = autobbox::common::SaveCurrentWorkingDirectoryW();
    LogLine(state.startup_log,
            "DIR ",
            "shutdown_save_current=%d current=%s",
            saved_current_working_dir ? 1 : 0,
            autobbox::common::WToA(autobbox::common::CurrentWorkingDirectoryW().c_str()).c_str());
    const ProError unset_directory_notify_status = ProNotificationUnset(PRO_DIRECTORY_CHANGE_POST);
    LogLine(state.startup_log,
            "DIR ",
            "notification_unset status=%d",
            static_cast<int>(unset_directory_notify_status));

    if (enable_perf_notifications && callbacks.log_shutdown_perf_summary != nullptr) {
        callbacks.log_shutdown_perf_summary();
    }
    if (callbacks.unregister_perf_notifications != nullptr) {
        callbacks.unregister_perf_notifications();
    }
    LogLine(state.startup_log, nullptr, "===== user_terminate =====");
    g_active_entry_state = nullptr;
}

} // namespace autobbox::main
