#include "autobbox/ui/split_dialog.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"

#include <ProArray.h>
#include <ProUICheckbutton.h>
#include <ProUIDialog.h>
#include <ProUILabel.h>
#include <ProUIList.h>
#include <ProUIMessage.h>
#include <ProUIPushbutton.h>
#include <ProToolkit.h>
#include <ProUtil.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace autobbox::ui {

namespace {

struct SplitDialogConfig {
    const char *dialog_inst_name = nullptr;
    const char *resource_base_name = nullptr;
    const char *prompt_comp = nullptr;
    const char *list_comp = nullptr;
    const char *select_all_comp = nullptr;
    const char *clear_comp = nullptr;
    const char *replace_comp = nullptr;
    const char *out_dir_comp = nullptr;
    const char *reuse_comp = nullptr;
    const char *ok_comp = nullptr;
    const char *cancel_comp = nullptr;
    int status_ok = 0;
    int status_cancel = 0;
};

SplitDialogConfig DefaultSplitDialogConfig()
{
    SplitDialogConfig config = {};
    config.dialog_inst_name = "autobbox_split_pick_inst";
    config.resource_base_name = "autobbox_split_pick";
    config.prompt_comp = "PromptLabel";
    config.list_comp = "ModelList";
    config.select_all_comp = "SelectAllBtn";
    config.clear_comp = "ClearBtn";
    config.replace_comp = "ReplaceCheck";
    config.out_dir_comp = "OutDirCheck";
    config.reuse_comp = "ReuseCheck";
    config.ok_comp = "OKBtn";
    config.cancel_comp = "CancelBtn";
    config.status_ok = 1;
    config.status_cancel = 0;
    return config;
}

struct SplitDialogState {
    const std::vector<core::SplitCandidate> *candidates = nullptr;
};

void LogLine(const SplitDialogLogSink &log_sink, const char *fmt, ...)
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

void SetSplitDialogListState(char *dialog, const SplitDialogState *state, ProUIMixedState item_state, const SplitDialogConfig &config)
{
    if (dialog == nullptr || state == nullptr || state->candidates == nullptr || config.list_comp == nullptr) {
        return;
    }
    for (const core::SplitCandidate &cand : *state->candidates) {
        ProUIListStateSet(
            dialog,
            const_cast<char *>(config.list_comp),
            const_cast<char *>(cand.item_name.c_str()),
            item_state);
    }
}

void OnSplitSelectAll(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<std::pair<SplitDialogState *, const SplitDialogConfig *> *>(app_data);
    if (runtime == nullptr || runtime->first == nullptr || runtime->second == nullptr) {
        return;
    }
    SetSplitDialogListState(dialog, runtime->first, PROUI_SET, *runtime->second);
}

void OnSplitClearAll(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<std::pair<SplitDialogState *, const SplitDialogConfig *> *>(app_data);
    if (runtime == nullptr || runtime->first == nullptr || runtime->second == nullptr) {
        return;
    }
    SetSplitDialogListState(dialog, runtime->first, PROUI_UNSET, *runtime->second);
}

void SetSplitDialogCheckState(char *dialog, const char *comp, bool checked)
{
    if (dialog == nullptr || comp == nullptr) {
        return;
    }
    if (checked) {
        ProUICheckbuttonSet(dialog, const_cast<char *>(comp));
    } else {
        ProUICheckbuttonUnset(dialog, const_cast<char *>(comp));
    }
}

bool GetSplitDialogCheckState(char *dialog, const char *comp)
{
    if (dialog == nullptr || comp == nullptr) {
        return false;
    }
    ProBoolean checked = PRO_B_FALSE;
    const ProError st = ProUICheckbuttonGetState(dialog, const_cast<char *>(comp), &checked);
    return st == PRO_TK_NO_ERROR && checked == PRO_B_TRUE;
}

ProError TryCreateDialog(const SplitDialogConfig &config, const SplitDialogLogSink &log_sink, std::string &used_resource)
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
        LogLine(log_sink, "split-dialog-create try resource=%s status=%d", res.c_str(), static_cast<int>(last));
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
                text_root + "\\resource\\" + base_name,
                text_root + "\\resource\\" + base_name + ".res",
                text_root + "\\text\\resource\\" + base_name,
                text_root + "\\text\\resource\\" + base_name + ".res",
                text_root + "\\text\\usascii\\resource\\" + base_name,
                text_root + "\\text\\usascii\\resource\\" + base_name + ".res"
            };
            for (const std::string &path : abs_candidates) {
                LogLine(log_sink,
                        "split-dialog-path probe exists=%d path=%s",
                        autobbox::common::FileExistsA(path) ? 1 : 0,
                        path.c_str());
                last = ProUIDialogCreate(
                    const_cast<char *>(config.dialog_inst_name),
                    const_cast<char *>(path.c_str()));
                LogLine(log_sink, "split-dialog-create try resource=%s status=%d", path.c_str(), static_cast<int>(last));
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
            const_cast<wchar_t *>(L"拆实例"),
            const_cast<wchar_t *>(L"拆实例复选框加载失败，请联系管理员更新插件资源文件。"),
            buttons,
            PRO_UI_MESSAGE_OK,
            &choice);
        ProArrayFree((ProArray *)&buttons);
    }
}

} // namespace

bool PromptSplitDialog(const std::vector<core::SplitCandidate> &candidates,
                       std::vector<core::SplitCandidate> &selected,
                       core::SplitRunOptions &options,
                       bool &cancelled,
                       const SplitDialogLogSink &log_sink)
{
    const SplitDialogConfig config = DefaultSplitDialogConfig();
    selected.clear();
    options.replace_in_assembly = true;
    options.output_to_split_dir = true;
    options.reuse_existing_split = true;
    cancelled = false;
    if (candidates.empty()) {
        return true;
    }

    std::string used_resource;
    const ProError st = TryCreateDialog(config, log_sink, used_resource);
    if (st != PRO_TK_NO_ERROR) {
        LogLine(log_sink, "ERROR split-dialog-create failed status=%d", static_cast<int>(st));
        ShowCreateDialogError();
        cancelled = true;
        return false;
    }

    LogLine(log_sink, "split-dialog-create success resource=%s", used_resource.c_str());
    ProUIDialogTitleSet(const_cast<char *>(config.dialog_inst_name), const_cast<wchar_t *>(L"拆实例"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.prompt_comp),
        const_cast<wchar_t *>(L"勾选需要拆分的族表实例模型："));
    ProUICheckbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.replace_comp),
        const_cast<wchar_t *>(L"替换当前装配中的原实例引用"));
    ProUICheckbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.out_dir_comp),
        const_cast<wchar_t *>(L"输出到当前目录下 AB_SPLIT 文件夹"));
    ProUICheckbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.reuse_comp),
        const_cast<wchar_t *>(L"优先复用已存在的 _SPLIT 模型"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.select_all_comp),
        const_cast<wchar_t *>(L"全选"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.clear_comp),
        const_cast<wchar_t *>(L"清空"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.ok_comp),
        const_cast<wchar_t *>(L"确定"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.cancel_comp),
        const_cast<wchar_t *>(L"取消"));

    std::vector<char *> name_ptrs;
    std::vector<wchar_t *> label_ptrs;
    name_ptrs.reserve(candidates.size());
    label_ptrs.reserve(candidates.size());
    for (const core::SplitCandidate &cand : candidates) {
        name_ptrs.push_back(const_cast<char *>(cand.item_name.c_str()));
        label_ptrs.push_back(const_cast<wchar_t *>(cand.label.c_str()));
    }

    ProUIListColumnsSet(const_cast<char *>(config.dialog_inst_name), const_cast<char *>(config.list_comp), 1);
    ProUIListVisiblerowsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.list_comp),
        std::min(12, std::max(4, static_cast<int>(candidates.size()))));
    ProUIListMinrowsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.list_comp),
        std::min(12, std::max(4, static_cast<int>(candidates.size()))));
    ProUIListListtypeSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.list_comp),
        PROUILISTTYPE_CHECk);
    ProUIListSelectionpolicySet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.list_comp),
        PROUISELPOLICY_NONE);
    ProUIListNamesSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.list_comp),
        static_cast<int>(name_ptrs.size()),
        name_ptrs.data());
    ProUIListLabelsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.list_comp),
        static_cast<int>(label_ptrs.size()),
        label_ptrs.data());

    SplitDialogState state;
    state.candidates = &candidates;
    auto runtime = std::make_pair(&state, &config);

    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.select_all_comp),
        OnSplitSelectAll,
        &runtime);
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.clear_comp),
        OnSplitClearAll,
        &runtime);
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

    SetSplitDialogListState(const_cast<char *>(config.dialog_inst_name), &state, PROUI_SET, config);
    SetSplitDialogCheckState(const_cast<char *>(config.dialog_inst_name), config.replace_comp, true);
    SetSplitDialogCheckState(const_cast<char *>(config.dialog_inst_name), config.out_dir_comp, true);
    SetSplitDialogCheckState(const_cast<char *>(config.dialog_inst_name), config.reuse_comp, true);

    int dlg_status = config.status_cancel;
    const ProError st_act = ProUIDialogActivate(
        const_cast<char *>(config.dialog_inst_name),
        &dlg_status);
    if (st_act != PRO_TK_NO_ERROR || dlg_status != config.status_ok) {
        cancelled = true;
        ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
        return false;
    }

    for (const core::SplitCandidate &cand : candidates) {
        ProUIMixedState item_state = PROUI_UNSET;
        if (ProUIListStateGet(
                const_cast<char *>(config.dialog_inst_name),
                const_cast<char *>(config.list_comp),
                const_cast<char *>(cand.item_name.c_str()),
                &item_state) == PRO_TK_NO_ERROR &&
            item_state == PROUI_SET) {
            selected.push_back(cand);
        }
    }

    options.replace_in_assembly = GetSplitDialogCheckState(const_cast<char *>(config.dialog_inst_name), config.replace_comp);
    options.output_to_split_dir = GetSplitDialogCheckState(const_cast<char *>(config.dialog_inst_name), config.out_dir_comp);
    options.reuse_existing_split = GetSplitDialogCheckState(const_cast<char *>(config.dialog_inst_name), config.reuse_comp);

    ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
    return true;
}

} // namespace autobbox::ui
