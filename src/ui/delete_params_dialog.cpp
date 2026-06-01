#include "autobbox/ui/delete_params_dialog.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"

#include <ProArray.h>
#include <ProUICheckbutton.h>
#include <ProUIDialog.h>
#include <ProUIMessage.h>
#include <ProUIPushbutton.h>
#include <ProToolkit.h>
#include <ProUtil.h>

#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace autobbox::ui {

namespace {

struct DeleteParamsDialogConfig {
    const char *dialog_inst_name = nullptr;
    const char *resource_base_name = nullptr;
    const char *size_comp = nullptr;
    const char *volume_comp = nullptr;
    const char *ok_comp = nullptr;
    const char *cancel_comp = nullptr;
    int status_ok = 0;
    int status_cancel = 0;
};

DeleteParamsDialogConfig DefaultDeleteParamsDialogConfig()
{
    DeleteParamsDialogConfig config = {};
    config.dialog_inst_name = "autobbox_delete_opts_inst";
    config.resource_base_name = "autobbox_delete_opts";
    config.size_comp = "DelSizeCheck";
    config.volume_comp = "DelVolCheck";
    config.ok_comp = "OKBtn";
    config.cancel_comp = "CancelBtn";
    config.status_ok = 1;
    config.status_cancel = 0;
    return config;
}

void LogLine(const DeleteParamsDialogLogSink &log_sink, const char *fmt, ...)
{
    if (!log_sink || fmt == nullptr) {
        return;
    }

    char buffer[2048] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_sink(buffer);
}

void OnDialogExit(char *dialog, char *, ProAppData app_data)
{
    if (dialog != nullptr) {
        ProUIDialogExit(dialog, static_cast<int>(reinterpret_cast<std::intptr_t>(app_data)));
    }
}

ProError TryCreateDialog(const DeleteParamsDialogConfig &config,
                         const DeleteParamsDialogLogSink &log_sink,
                         std::string &used_resource)
{
    used_resource.clear();
    if (config.dialog_inst_name == nullptr || config.resource_base_name == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    const std::string base_name = config.resource_base_name;
    const std::vector<std::string> rel_candidates = {
        base_name,
        std::string("resource\\") + base_name,
        std::string("text\\resource\\") + base_name,
        std::string("text\\usascii\\resource\\") + base_name,
        std::string("usascii\\resource\\") + base_name
    };

    ProError last = PRO_TK_GENERAL_ERROR;
    for (const std::string &res : rel_candidates) {
        last = ProUIDialogCreate(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(res.c_str()));
        LogLine(log_sink, "delete-dialog-create try resource=%s status=%d", res.c_str(), static_cast<int>(last));
        if (last == PRO_TK_NO_ERROR) {
            used_resource = res;
            return last;
        }
    }

    ProPath wtext = {0};
    if (ProToolkitApplTextPathGet(wtext) == PRO_TK_NO_ERROR) {
        const std::string text_root = autobbox::common::WToA(wtext);
        if (!text_root.empty()) {
            const std::vector<std::string> abs_candidates = {
                text_root + "\\text\\resource\\" + base_name,
                text_root + "\\text\\resource\\" + base_name + ".res",
                text_root + "\\resource\\" + base_name,
                text_root + "\\resource\\" + base_name + ".res",
                text_root + "\\text\\usascii\\resource\\" + base_name,
                text_root + "\\text\\usascii\\resource\\" + base_name + ".res"
            };
            for (const std::string &path : abs_candidates) {
                LogLine(log_sink,
                        "delete-dialog-path probe exists=%d path=%s",
                        autobbox::common::FileExistsA(path) ? 1 : 0,
                        path.c_str());
                last = ProUIDialogCreate(
                    const_cast<char *>(config.dialog_inst_name),
                    const_cast<char *>(path.c_str()));
                LogLine(log_sink, "delete-dialog-create try resource=%s status=%d", path.c_str(), static_cast<int>(last));
                if (last == PRO_TK_NO_ERROR) {
                    used_resource = path;
                    return last;
                }
            }
        }
    }

    return last;
}

void ShowCreateDialogError()
{
    ProUIMessageButton choice = PRO_UI_MESSAGE_OK;
    ProUIMessageButton *buttons = nullptr;
    if (ProArrayAlloc(0, sizeof(ProUIMessageButton), 1, (ProArray *)&buttons) == PRO_TK_NO_ERROR &&
        buttons != nullptr) {
        ProUIMessageButton button = PRO_UI_MESSAGE_OK;
        ProArrayObjectAdd((ProArray *)&buttons, PRO_VALUE_UNUSED, 1, &button);
        ProUIMessageDialogDisplay(
            PROUIMESSAGE_ERROR,
            const_cast<wchar_t *>(L"删除参数"),
            const_cast<wchar_t *>(L"删除复选框加载失败，请联系管理员更新插件资源文件。"),
            buttons,
            PRO_UI_MESSAGE_OK,
            &choice);
        ProArrayFree((ProArray *)&buttons);
    }
}

} // namespace

bool PromptDeleteParamsDialog(bool &delete_size,
                              bool &delete_volume,
                              bool &cancelled,
                              const DeleteParamsDialogLogSink &log_sink)
{
    const DeleteParamsDialogConfig config = DefaultDeleteParamsDialogConfig();
    delete_size = true;
    delete_volume = true;
    cancelled = false;

    std::string used_resource;
    const ProError st = TryCreateDialog(config, log_sink, used_resource);
    if (st != PRO_TK_NO_ERROR) {
        LogLine(log_sink, "ERROR delete-dialog-create failed status=%d", static_cast<int>(st));
        ShowCreateDialogError();
        cancelled = true;
        return false;
    }

    LogLine(log_sink, "delete-dialog-create success resource=%s", used_resource.c_str());

    ProUIDialogTitleSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<wchar_t *>(L"删除参数"));
    ProUICheckbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.size_comp),
        const_cast<wchar_t *>(L"删除外形尺寸参数 (BBOX_LXWXH)"));
    ProUICheckbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.volume_comp),
        const_cast<wchar_t *>(L"删除外形体积参数 (BBOX_VOL_M3)"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.ok_comp),
        const_cast<wchar_t *>(L"确定"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.cancel_comp),
        const_cast<wchar_t *>(L"取消"));

    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.ok_comp),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_ok)));
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.cancel_comp),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_cancel)));
    ProUIDialogCloseActionSet(
        const_cast<char *>(config.dialog_inst_name),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_cancel)));
    ProUIDialogDefaultbuttonSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.ok_comp));

    ProUICheckbuttonSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.size_comp));
    ProUICheckbuttonSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.volume_comp));

    int dialog_status = config.status_cancel;
    const ProError st_act = ProUIDialogActivate(
        const_cast<char *>(config.dialog_inst_name),
        &dialog_status);
    if (st_act != PRO_TK_NO_ERROR || dialog_status != config.status_ok) {
        cancelled = true;
        ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
        return false;
    }

    ProBoolean set = PRO_B_FALSE;
    if (ProUICheckbuttonGetState(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.size_comp),
            &set) == PRO_TK_NO_ERROR) {
        delete_size = (set == PRO_B_TRUE);
    } else {
        delete_size = false;
    }

    set = PRO_B_FALSE;
    if (ProUICheckbuttonGetState(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.volume_comp),
            &set) == PRO_TK_NO_ERROR) {
        delete_volume = (set == PRO_B_TRUE);
    } else {
        delete_volume = false;
    }

    ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
    return true;
}

} // namespace autobbox::ui
