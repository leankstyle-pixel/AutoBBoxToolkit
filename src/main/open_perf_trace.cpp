#include "autobbox/main/open_perf_trace.h"

#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"

#include <ProNotify.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace autobbox::main {

namespace {

OpenPerfTraceState *g_active_state = nullptr;
OpenPerfTraceCallbacks g_callbacks = {};

void LogLine(const OpenPerfTraceCallbacks &callbacks, const char *fmt, ...)
{
    if (!callbacks.log_sink || fmt == nullptr) {
        return;
    }

    char buffer[2048] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    callbacks.log_sink(buffer);
}

std::string ModelTag(ProMdl mdl, const OpenPerfTraceCallbacks &callbacks)
{
    if (callbacks.format_model_tag) {
        return callbacks.format_model_tag(mdl);
    }
    return autobbox::creo::DefaultModelTag(mdl);
}

bool IsTaskRunning(const OpenPerfTraceCallbacks &callbacks)
{
    return callbacks.is_task_running && callbacks.is_task_running();
}

OpenPerfTraceState *State()
{
    return g_active_state;
}

ProError OnModelFileOpenOK(ProMdlType mdl_type, ProMdlsubtype sub_type, ProMdlName model_name)
{
    OpenPerfTraceState *state = State();
    if (state == nullptr || IsTaskRunning(g_callbacks)) {
        return PRO_TK_NO_ERROR;
    }

    ResetOpenPerfTrace(*state);
    state->open_ok_ms = GetTickCount64();
    state->open_mdl_type = mdl_type;
    state->open_mdl_subtype = static_cast<int>(sub_type);
    state->open_is_drawing = (mdl_type == PRO_MDL_DRAWING);
    if (model_name != nullptr) {
        state->open_mdl_name = std::wstring(model_name);
    }

    LogLine(g_callbacks,
            "PERF open_ok name=%s type=%d subtype=%d is_drawing=%d t=%llu",
            autobbox::common::WToA(state->open_mdl_name.c_str()).c_str(),
            static_cast<int>(state->open_mdl_type),
            state->open_mdl_subtype,
            state->open_is_drawing ? 1 : 0,
            static_cast<unsigned long long>(state->open_ok_ms));
    return PRO_TK_NO_ERROR;
}

ProError OnModelRetrievePost(ProMdl mdl)
{
    OpenPerfTraceState *state = State();
    if (state == nullptr || IsTaskRunning(g_callbacks) || state->open_ok_ms == 0 || !state->open_is_drawing) {
        return PRO_TK_NO_ERROR;
    }

    state->retrieve_post_ms = GetTickCount64();
    const ULONGLONG from_open_ms =
        (state->retrieve_post_ms >= state->open_ok_ms)
            ? (state->retrieve_post_ms - state->open_ok_ms)
            : 0;
    const ULONGLONG chain_span =
        (state->retrieve_all_last_ms >= state->retrieve_all_first_ms && state->retrieve_all_count > 0)
            ? (state->retrieve_all_last_ms - state->retrieve_all_first_ms)
            : 0;

    LogLine(g_callbacks,
            "PERF retrieve_post mdl=%s from_open_ok_ms=%llu retrieve_all_count=%d chain_span_ms=%llu",
            ModelTag(mdl, g_callbacks).c_str(),
            static_cast<unsigned long long>(from_open_ms),
            state->retrieve_all_count,
            static_cast<unsigned long long>(chain_span));
    state->chain_summarized = true;
    return PRO_TK_NO_ERROR;
}

ProError OnModelRetrievePostAll(ProMdl mdl)
{
    OpenPerfTraceState *state = State();
    if (state == nullptr || IsTaskRunning(g_callbacks) || state->open_ok_ms == 0 || !state->open_is_drawing) {
        return PRO_TK_NO_ERROR;
    }

    const ULONGLONG now = GetTickCount64();
    if (state->retrieve_all_count == 0) {
        state->retrieve_all_first_ms = now;
    }
    ++state->retrieve_all_count;
    state->retrieve_all_last_ms = now;

    if (state->retrieve_all_log_count < 8 || (state->retrieve_all_count % 25) == 0) {
        const ULONGLONG from_open_ms =
            (now >= state->open_ok_ms)
                ? (now - state->open_ok_ms)
                : 0;
        LogLine(g_callbacks,
                "PERF retrieve_post_all count=%d from_open_ok_ms=%llu mdl=%s",
                state->retrieve_all_count,
                static_cast<unsigned long long>(from_open_ms),
                ModelTag(mdl, g_callbacks).c_str());
        ++state->retrieve_all_log_count;
    }
    return PRO_TK_NO_ERROR;
}

} // namespace

void ResetOpenPerfTrace(OpenPerfTraceState &state)
{
    state.open_ok_ms = 0;
    state.retrieve_post_ms = 0;
    state.retrieve_all_first_ms = 0;
    state.retrieve_all_last_ms = 0;
    state.retrieve_all_count = 0;
    state.open_mdl_type = PRO_MDL_UNUSED;
    state.open_mdl_subtype = 0;
    state.open_mdl_name.clear();
    state.open_is_drawing = false;
    state.chain_summarized = false;
    state.retrieve_all_log_count = 0;
}

void RegisterOpenPerfNotifications(OpenPerfTraceState &state,
                                   const OpenPerfTraceCallbacks &callbacks,
                                   bool enabled)
{
    if (!enabled) {
        LogLine(callbacks, "PERF notify_set skipped disabled=1");
        return;
    }

    g_active_state = &state;
    g_callbacks = callbacks;

    ProError st = ProNotificationSet(
        PRO_MODEL_FILE_OPEN_OK,
        reinterpret_cast<ProFunction>(OnModelFileOpenOK));
    state.notify_open_ok = (st == PRO_TK_NO_ERROR);
    LogLine(callbacks, "PERF notify_set PRO_MODEL_FILE_OPEN_OK status=%d", static_cast<int>(st));

    st = ProNotificationSet(
        PRO_MODEL_RETRIEVE_POST,
        reinterpret_cast<ProFunction>(OnModelRetrievePost));
    state.notify_retrieve_post = (st == PRO_TK_NO_ERROR);
    LogLine(callbacks, "PERF notify_set PRO_MODEL_RETRIEVE_POST status=%d", static_cast<int>(st));

    st = ProNotificationSet(
        PRO_MODEL_RETRIEVE_POST_ALL,
        reinterpret_cast<ProFunction>(OnModelRetrievePostAll));
    state.notify_retrieve_post_all = (st == PRO_TK_NO_ERROR);
    LogLine(callbacks, "PERF notify_set PRO_MODEL_RETRIEVE_POST_ALL status=%d", static_cast<int>(st));
}

void UnregisterOpenPerfNotifications(OpenPerfTraceState &state,
                                     const OpenPerfTraceCallbacks &callbacks,
                                     bool enabled)
{
    if (!enabled) {
        return;
    }

    if (state.notify_open_ok) {
        const ProError st = ProNotificationUnset(PRO_MODEL_FILE_OPEN_OK);
        LogLine(callbacks, "PERF notify_unset PRO_MODEL_FILE_OPEN_OK status=%d", static_cast<int>(st));
        state.notify_open_ok = false;
    }
    if (state.notify_retrieve_post) {
        const ProError st = ProNotificationUnset(PRO_MODEL_RETRIEVE_POST);
        LogLine(callbacks, "PERF notify_unset PRO_MODEL_RETRIEVE_POST status=%d", static_cast<int>(st));
        state.notify_retrieve_post = false;
    }
    if (state.notify_retrieve_post_all) {
        const ProError st = ProNotificationUnset(PRO_MODEL_RETRIEVE_POST_ALL);
        LogLine(callbacks, "PERF notify_unset PRO_MODEL_RETRIEVE_POST_ALL status=%d", static_cast<int>(st));
        state.notify_retrieve_post_all = false;
    }

    g_active_state = nullptr;
    g_callbacks = {};
}

void LogOpenPerfShutdownSummary(const OpenPerfTraceState &state,
                                const OpenPerfTraceCallbacks &callbacks,
                                bool enabled)
{
    if (!enabled ||
        !state.open_is_drawing ||
        state.open_ok_ms == 0 ||
        state.retrieve_all_count <= 0) {
        return;
    }

    const ULONGLONG from_open_to_last =
        (state.retrieve_all_last_ms >= state.open_ok_ms)
            ? (state.retrieve_all_last_ms - state.open_ok_ms)
            : 0;
    const ULONGLONG chain_span =
        (state.retrieve_all_last_ms >= state.retrieve_all_first_ms)
            ? (state.retrieve_all_last_ms - state.retrieve_all_first_ms)
            : 0;
    LogLine(callbacks,
            "PERF open_chain_summary name=%s type=%d subtype=%d retrieve_all_count=%d from_open_to_last_ms=%llu chain_span_ms=%llu",
            autobbox::common::WToA(state.open_mdl_name.c_str()).c_str(),
            static_cast<int>(state.open_mdl_type),
            state.open_mdl_subtype,
            state.retrieve_all_count,
            static_cast<unsigned long long>(from_open_to_last),
            static_cast<unsigned long long>(chain_span));
    if (!state.chain_summarized) {
        LogLine(callbacks,
                "PERF open_chain_incomplete name=%s retrieve_post_missing=1 retrieve_all_count=%d",
                autobbox::common::WToA(state.open_mdl_name.c_str()).c_str(),
                state.retrieve_all_count);
    }
}

} // namespace autobbox::main
