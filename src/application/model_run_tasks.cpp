#include "autobbox/application/model_run_tasks.h"

#include "autobbox/application/model_metrics.h"
#include "autobbox/application/target_collectors.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/family_table_api.h"
#include "autobbox/ui/delete_params_dialog.h"

#include <ProMdl.h>
#include <ProModelitem.h>
#include <ProParamval.h>
#include <ProParameter.h>
#include <ProUtil.h>
#include <ProView.h>
#include <ProWindows.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace autobbox::application {

namespace {

constexpr const wchar_t *kParamSize = L"BBOX_LXWXH";
constexpr const wchar_t *kParamVol = L"BBOX_VOL_M3";

template <size_t N>
void CopyWStr(wchar_t (&dest)[N], const wchar_t *src)
{
    if (N == 0) {
        return;
    }

    size_t i = 0;
    if (src != nullptr) {
        while (i + 1 < N && src[i] != L'\0') {
            dest[i] = src[i];
            ++i;
        }
    }
    dest[i] = L'\0';
}

void LogLine(const std::function<void(const std::string &line)> &log_sink,
             const char *fmt,
             ...)
{
    if (!log_sink || fmt == nullptr) {
        return;
    }

    char buffer[4096] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_sink(buffer);
}

ProModelitem MdlAsModelitem(ProMdl mdl)
{
    ProModelitem item;
    std::memset(&item, 0, sizeof(item));
    ProMdlToModelitem(mdl, &item);
    return item;
}

ProError RemoveParamIfExists(ProMdl mdl, const wchar_t *param_name)
{
    if (mdl == nullptr || param_name == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    ProModelitem owner = MdlAsModelitem(mdl);
    ProParameter param;
    ProName pname = {0};
    CopyWStr(pname, param_name);
    ProError st = ProParameterInit(&owner, pname, &param);
    if (st == PRO_TK_E_NOT_FOUND) {
        return PRO_TK_E_NOT_FOUND;
    }
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }
    return ProParameterDelete(&param);
}

bool IsWriteSuccess(ProError st)
{
    return st == PRO_TK_NO_ERROR || st == PRO_TK_NO_CHANGE || st == PRO_TK_E_FOUND;
}

bool IsDeleteSuccess(ProError st)
{
    return st == PRO_TK_NO_ERROR || st == PRO_TK_NO_CHANGE || st == PRO_TK_E_NOT_FOUND;
}

std::string FormatModelTag(ProMdl mdl, const RunContext &ctx)
{
    if (ctx.format_model_tag) {
        return ctx.format_model_tag(mdl);
    }

    ProName name = {0};
    ProMdlType type = PRO_MDL_UNUSED;
    ProMdlNameGet(mdl, name);
    ProMdlTypeGet(mdl, &type);
    return autobbox::common::WToA(std::wstring(name).c_str()) +
           "(type=" + std::to_string(static_cast<int>(type)) + ")";
}

void FillIsoMatrix(ProMatrix matrix)
{
    matrix[0][0] = 0.707107;
    matrix[0][1] = -0.408103;
    matrix[0][2] = 0.577453;
    matrix[0][3] = 0.0;
    matrix[1][0] = -6.52932e-8;
    matrix[1][1] = 0.816642;
    matrix[1][2] = 0.577145;
    matrix[1][3] = 0.0;
    matrix[2][0] = -0.707107;
    matrix[2][1] = -0.408103;
    matrix[2][2] = 0.577453;
    matrix[2][3] = 0.0;
    matrix[3][0] = 0.0;
    matrix[3][1] = 0.0;
    matrix[3][2] = 0.0;
    matrix[3][3] = 1.0;
}

class TaskGuard {
public:
    explicit TaskGuard(const RunContext &ctx) : ctx_(ctx) {}

    ~TaskGuard()
    {
        if (ctx_.report_session_end) {
            ctx_.report_session_end();
        }
        if (ctx_.set_task_running) {
            ctx_.set_task_running(false);
        }
    }

private:
    const RunContext &ctx_;
};

} // namespace

ProError CreateIsoView(ProMdl mdl)
{
    ProLine view_name = {0};
    ProStringToWstring(view_name, const_cast<char *>("auto_ISOMETRIC"));
    ProView view = nullptr;
    ProError st = ProViewCreate(mdl, view_name, &view);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }

    ProMatrix matrix = {{0}};
    FillIsoMatrix(matrix);
    return ProViewMatrixSet(mdl, view, matrix);
}

const wchar_t *ModeName(TaskMode mode)
{
    switch (mode) {
    case TaskMode::SizeOnly:
        return L"size";
    case TaskMode::VolumeOnly:
        return L"volume";
    case TaskMode::IsoOnly:
        return L"iso";
    case TaskMode::DeleteOnly:
        return L"delete";
    default:
        return L"task";
    }
}

void ShowRunSummary(TaskMode mode,
                    int targets,
                    int ok_count,
                    int fail_count,
                    const std::function<void(const std::string &line)> &log_sink)
{
    LogLine(log_sink,
            "Summary mode=%s targets=%d ok=%d fail=%d",
            autobbox::common::WToA(ModeName(mode)).c_str(),
            targets,
            ok_count,
            fail_count);
}

int ExecuteMainRunTask(TaskMode mode, const RunContext &ctx)
{
    if (ctx.is_task_running && ctx.is_task_running()) {
        LogLine(ctx.log_sink, "SKIP run mode=%d reason=already-running", static_cast<int>(mode));
        return 0;
    }
    if (ctx.set_task_running) {
        ctx.set_task_running(true);
    }
    if (ctx.report_session_begin) {
        ctx.report_session_begin();
    }
    TaskGuard guard(ctx);

    bool delete_size = true;
    bool delete_volume = true;
    if (mode == TaskMode::DeleteOnly) {
        bool cancelled = false;
        ui::PromptDeleteParamsDialog(
            delete_size,
            delete_volume,
            cancelled,
            [&](const std::string &line) { LogLine(ctx.log_sink, "%s", line.c_str()); });
        if (cancelled) {
            LogLine(ctx.log_sink, "Delete cancelled by user");
            if (ctx.open_report_log) {
                ctx.open_report_log();
            }
            return 0;
        }
        if (!delete_size && !delete_volume) {
            LogLine(ctx.log_sink, "Delete skipped: neither size nor volume selected");
            if (ctx.open_report_log) {
                ctx.open_report_log();
            }
            return 0;
        }
    }

    MainRunOptions opt = ctx.options;
    if (mode == TaskMode::SizeOnly) {
        opt.surface = PRO_B_FALSE;
        opt.curve = PRO_B_FALSE;
    }
    const ULONGLONG collect_begin = GetTickCount64();
    std::vector<ProMdl> models =
        CollectTargetsFromCurrentModel(opt.parts, opt.assemblies, opt.top_level_only);
    const ULONGLONG collect_ms = GetTickCount64() - collect_begin;
    const int target_count = static_cast<int>(models.size());

    LogLine(ctx.log_sink, "===== Run begin mode=%d =====", static_cast<int>(mode));
    LogLine(ctx.log_sink,
            "targets=%d parts=%d asms=%d surf=%d curve=%d recompute=%d top2=%d preheat=%d",
            target_count,
            static_cast<int>(opt.parts),
            static_cast<int>(opt.assemblies),
            static_cast<int>(opt.surface),
            static_cast<int>(opt.curve),
            static_cast<int>(opt.recompute),
            static_cast<int>(opt.top_level_only),
            static_cast<int>(opt.preheat_generics));
    LogLine(ctx.log_sink,
            "timing collect_targets_ms=%llu",
            static_cast<unsigned long long>(collect_ms));

    if (opt.preheat_generics == PRO_B_TRUE &&
        (mode == TaskMode::SizeOnly || mode == TaskMode::VolumeOnly)) {
        std::vector<ProMdl> preheat_models;
        preheat_models.reserve(models.size());
        for (ProMdl mdl : models) {
            if (!ShouldSkipModel(mdl) && !HasFailedRegeneration(mdl)) {
                preheat_models.push_back(mdl);
            }
        }
        const ULONGLONG preheat_ms = creo::PreheatImmediateGenericCache(preheat_models);
        LogLine(ctx.log_sink,
                "timing preheat_generics_ms=%llu",
                static_cast<unsigned long long>(preheat_ms));
    }

    int ok_count = 0;
    int fail_count = 0;
    int skip_generic_count = 0;
    int skip_regen_count = 0;
    int progress_count = 0;
    const bool log_each_success = (target_count <= 120);

    const ULONGLONG run_begin = GetTickCount64();
    for (ProMdl mdl : models) {
        const ULONGLONG model_begin = GetTickCount64();
        ++progress_count;

        if (ShouldSkipModel(mdl)) {
            ++skip_generic_count;
            if (log_each_success) {
                LogLine(ctx.log_sink,
                        "SKIP %s reason=generic-skip",
                        FormatModelTag(mdl, ctx).c_str());
            }
            continue;
        }
        if (HasFailedRegeneration(mdl)) {
            ++skip_regen_count;
            if (log_each_success) {
                LogLine(ctx.log_sink,
                        "SKIP %s reason=regen-failed",
                        FormatModelTag(mdl, ctx).c_str());
            }
            continue;
        }

        RemoveParamIfExists(mdl, L"BBOX_L");
        RemoveParamIfExists(mdl, L"BBOX_W");
        RemoveParamIfExists(mdl, L"BBOX_H");
        RemoveParamIfExists(mdl, L"BBOX_MAX");

        ProError st = PRO_TK_GENERAL_ERROR;
        if (mode == TaskMode::SizeOnly) {
            double length = 0.0;
            double width = 0.0;
            double height = 0.0;
            const ULONGLONG bbox_begin = GetTickCount64();
            if (!ComputeBBoxLwh(mdl,
                                opt.surface == PRO_B_TRUE,
                                opt.curve == PRO_B_TRUE,
                                length,
                                width,
                                height)) {
                LogLine(ctx.log_sink,
                        "FAIL %s reason=bbox-calc",
                        FormatModelTag(mdl, ctx).c_str());
                ++fail_count;
                continue;
            }
            const ULONGLONG bbox_ms = GetTickCount64() - bbox_begin;
            const std::wstring value = IntLwhString(length, width, height);
            const ULONGLONG param_begin = GetTickCount64();
            st = creo::SetStringParamWithFamtableSupport(
                mdl, kParamSize, value, opt.recompute == PRO_B_TRUE);
            const ULONGLONG param_ms = GetTickCount64() - param_begin;
            if (IsWriteSuccess(st)) {
                if (log_each_success) {
                    LogLine(ctx.log_sink,
                            "OK   %s size=%s status=%d bbox_ms=%llu param_ms=%llu",
                            FormatModelTag(mdl, ctx).c_str(),
                            autobbox::common::WToA(value.c_str()).c_str(),
                            static_cast<int>(st),
                            static_cast<unsigned long long>(bbox_ms),
                            static_cast<unsigned long long>(param_ms));
                }
                ++ok_count;
            } else {
                LogLine(ctx.log_sink,
                        "FAIL %s reason=param-set status=%d",
                        FormatModelTag(mdl, ctx).c_str(),
                        static_cast<int>(st));
                ++fail_count;
            }
        } else if (mode == TaskMode::VolumeOnly) {
            double volume_m3 = 0.0;
            if (!ComputeVolumeM3(mdl, volume_m3)) {
                LogLine(ctx.log_sink,
                        "FAIL %s reason=bbox-vol-calc",
                        FormatModelTag(mdl, ctx).c_str());
                ++fail_count;
                continue;
            }
            const ULONGLONG param_begin = GetTickCount64();
            st = creo::SetDoubleParamWithFamtableSupport(
                mdl, kParamVol, volume_m3, opt.recompute == PRO_B_TRUE);
            const ULONGLONG param_ms = GetTickCount64() - param_begin;
            if (IsWriteSuccess(st)) {
                if (log_each_success) {
                    const std::wstring formatted = FormatVol(volume_m3);
                    LogLine(ctx.log_sink,
                            "OK   %s bbox_volume_m3=%s status=%d param_ms=%llu",
                            FormatModelTag(mdl, ctx).c_str(),
                            autobbox::common::WToA(formatted.c_str()).c_str(),
                            static_cast<int>(st),
                            static_cast<unsigned long long>(param_ms));
                }
                ++ok_count;
            } else {
                LogLine(ctx.log_sink,
                        "FAIL %s reason=vol-param status=%d",
                        FormatModelTag(mdl, ctx).c_str(),
                        static_cast<int>(st));
                ++fail_count;
            }
        } else if (mode == TaskMode::IsoOnly) {
            st = CreateIsoView(mdl);
            if (IsWriteSuccess(st)) {
                if (log_each_success) {
                    LogLine(ctx.log_sink,
                            "OK   %s iso-view status=%d",
                            FormatModelTag(mdl, ctx).c_str(),
                            static_cast<int>(st));
                }
                ++ok_count;
            } else {
                LogLine(ctx.log_sink,
                        "FAIL %s reason=iso-view status=%d",
                        FormatModelTag(mdl, ctx).c_str(),
                        static_cast<int>(st));
                ++fail_count;
            }
        } else {
            ProError size_status = PRO_TK_NO_CHANGE;
            ProError vol_status = PRO_TK_NO_CHANGE;
            if (delete_size) {
                size_status = creo::DeleteParamWithFamtableSupport(mdl, kParamSize);
                RemoveParamIfExists(mdl, L"BBOX_L");
                RemoveParamIfExists(mdl, L"BBOX_W");
                RemoveParamIfExists(mdl, L"BBOX_H");
                RemoveParamIfExists(mdl, L"BBOX_MAX");
            }
            if (delete_volume) {
                vol_status = creo::DeleteParamWithFamtableSupport(mdl, kParamVol);
            }
            if (IsDeleteSuccess(size_status) && IsDeleteSuccess(vol_status)) {
                if (log_each_success) {
                    LogLine(ctx.log_sink,
                            "OK   %s delete size=%d vol=%d",
                            FormatModelTag(mdl, ctx).c_str(),
                            static_cast<int>(size_status),
                            static_cast<int>(vol_status));
                }
                ++ok_count;
            } else {
                LogLine(ctx.log_sink,
                        "FAIL %s reason=delete-param size=%d vol=%d",
                        FormatModelTag(mdl, ctx).c_str(),
                        static_cast<int>(size_status),
                        static_cast<int>(vol_status));
                ++fail_count;
            }
        }

        if (!log_each_success && (progress_count % 200 == 0)) {
            LogLine(ctx.log_sink,
                    "Progress processed=%d/%d ok=%d fail=%d skip_generic=%d skip_regen=%d",
                    progress_count,
                    target_count,
                    ok_count,
                    fail_count,
                    skip_generic_count,
                    skip_regen_count);
        }

        const ULONGLONG model_ms = GetTickCount64() - model_begin;
        if (model_ms >= 300) {
            LogLine(ctx.log_sink,
                    "SLOW %s model_ms=%llu mode=%d",
                    FormatModelTag(mdl, ctx).c_str(),
                    static_cast<unsigned long long>(model_ms),
                    static_cast<int>(mode));
        }
    }

    const ULONGLONG run_ms = GetTickCount64() - run_begin;
    LogLine(ctx.log_sink,
            "Skip summary: generic=%d regen=%d",
            skip_generic_count,
            skip_regen_count);
    LogLine(ctx.log_sink,
            "Run done: ok=%d fail=%d elapsed_ms=%llu",
            ok_count,
            fail_count,
            static_cast<unsigned long long>(run_ms));
    ShowRunSummary(mode, target_count, ok_count, fail_count, ctx.log_sink);
    if (ctx.open_report_log) {
        ctx.open_report_log();
    }
    return 0;
}

} // namespace autobbox::application
