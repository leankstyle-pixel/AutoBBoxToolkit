#include "autobbox/ui/quick_rename_dialog.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"

#include <ProToolkit.h>
#include <ProUI.h>
#include <ProUIDialog.h>
#include <ProUIInputpanel.h>
#include <ProUIPushbutton.h>
#include <ProUtil.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#pragma comment(lib, "User32.lib")

namespace autobbox::ui {

namespace {

struct QuickRenameDialogConfig {
    const char *dialog_name = nullptr;
    const char *resource_base_name = nullptr;
    const char *new_name_comp = nullptr;
    const char *rename_comp = nullptr;
    const char *clone_comp = nullptr;
    const char *cancel_comp = nullptr;
    int status_cancel = 0;
    int status_rename = 1;
    int status_clone = 2;
};

QuickRenameDialogConfig DefaultQuickRenameDialogConfig()
{
    QuickRenameDialogConfig config = {};
    config.dialog_name = "autobbox_quick_rename_inst";
    config.resource_base_name = "autobbox_quick_rename";
    config.new_name_comp = "NewNameInput";
    config.rename_comp = "RenameBtn";
    config.clone_comp = "CloneBtn";
    config.cancel_comp = "CancelBtn";
    return config;
}

void LogLine(const QuickRenameDialogLogSink &log_sink, const std::string &line)
{
    if (log_sink) {
        log_sink(line);
    }
}

ProError TryCreateDialog(const QuickRenameDialogConfig &config,
                         const QuickRenameDialogLogSink &log_sink,
                         std::string &used_resource)
{
    used_resource.clear();
    if (config.dialog_name == nullptr || config.resource_base_name == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    ProError last = PRO_TK_GENERAL_ERROR;
    const std::string base_name = config.resource_base_name;
    const std::vector<std::string> candidates = {
        base_name,
        base_name + ".res",
        std::string("resource\\") + base_name,
        std::string("resource\\") + base_name + ".res",
    };

    for (const std::string &resource : candidates) {
        last = ProUIDialogCreate(
            const_cast<char *>(config.dialog_name),
            const_cast<char *>(resource.c_str()));
        LogLine(log_sink,
                "quick-rename-dialog create try resource=" + resource +
                    " status=" + std::to_string(static_cast<int>(last)));
        if (last == PRO_TK_NO_ERROR) {
            used_resource = resource;
            return last;
        }
    }

    ProPath text_root_w = {0};
    if (ProToolkitApplTextPathGet(text_root_w) == PRO_TK_NO_ERROR) {
        const std::string text_root = autobbox::common::WToA(text_root_w);
        if (!text_root.empty()) {
            const std::vector<std::string> abs_candidates = {
                text_root + "\\resource\\" + base_name,
                text_root + "\\resource\\" + base_name + ".res",
                text_root + "\\text\\resource\\" + base_name,
                text_root + "\\text\\resource\\" + base_name + ".res",
            };
            for (const std::string &path : abs_candidates) {
                if (!autobbox::common::FileExistsA(path)) {
                    continue;
                }
                last = ProUIDialogCreate(
                    const_cast<char *>(config.dialog_name),
                    const_cast<char *>(path.c_str()));
                LogLine(log_sink,
                        "quick-rename-dialog create try resource=" + path +
                            " status=" + std::to_string(static_cast<int>(last)));
                if (last == PRO_TK_NO_ERROR) {
                    used_resource = path;
                    return last;
                }
            }
        }
    }

    return last;
}

void ExitWithStatus(char *dialog, char *, ProAppData app_data)
{
    if (dialog != nullptr) {
        ProUIDialogExit(dialog, static_cast<int>(reinterpret_cast<std::intptr_t>(app_data)));
    }
}

struct QuickRenameDialogPlacement {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    POINT cursor = {};
    bool has_position = false;
    const QuickRenameDialogLogSink *log_sink = nullptr;
};

QuickRenameDialogPlacement BuildDialogPlacementNearCursor(char *dialog,
                                                          const QuickRenameDialogLogSink &log_sink)
{
    QuickRenameDialogPlacement placement = {};
    placement.log_sink = &log_sink;
    if (dialog == nullptr) {
        return placement;
    }

    POINT cursor = {};
    if (GetCursorPos(&cursor) == 0) {
        LogLine(log_sink, "quick-rename-dialog position skipped reason=get-cursor-failed");
        return placement;
    }
    placement.cursor = cursor;

    int width = 0;
    int height = 0;
    const ProError size_status = ProUIDialogSizeGet(dialog, &width, &height);
    if (size_status != PRO_TK_NO_ERROR || width <= 0 || height <= 0) {
        width = 360;
        height = 48;
    }

    const int screen_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int screen_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int screen_width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int screen_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    const int screen_right = screen_left + std::max(1, screen_width);
    const int screen_bottom = screen_top + std::max(1, screen_height);

    int x = static_cast<int>(cursor.x) + 12;
    int y = static_cast<int>(cursor.y) + 12;
    x = std::clamp(x, screen_left + 8, std::max(screen_left + 8, screen_right - width - 8));
    y = std::clamp(y, screen_top + 8, std::max(screen_top + 8, screen_bottom - height - 8));

    placement.x = x;
    placement.y = y;
    placement.width = width;
    placement.height = height;
    placement.has_position = true;
    return placement;
}

void ReconfigureDialogToPlacement(char *dialog,
                                  const QuickRenameDialogPlacement &placement,
                                  const char *phase)
{
    if (dialog == nullptr || !placement.has_position || placement.log_sink == nullptr) {
        return;
    }

    int before_x = 0;
    int before_y = 0;
    const ProError before_status = ProUIDialogScreenpositionGet(dialog, &before_x, &before_y);
    const ProError forget_status = ProUIDialogForgetPosition(dialog);
    const ProError reposition_status = ProUIDialogReconfigure(
        dialog,
        placement.x,
        placement.y,
        static_cast<int>(PROUIPOSITIONING_UNUSED),
        static_cast<int>(PROUIPOSITIONING_UNUSED));

    int after_x = 0;
    int after_y = 0;
    const ProError after_status = ProUIDialogScreenpositionGet(dialog, &after_x, &after_y);
    LogLine(*placement.log_sink,
            std::string("quick-rename-dialog ") + phase +
                " cursor=(" + std::to_string(placement.cursor.x) + "," +
                std::to_string(placement.cursor.y) + ") xy=(" + std::to_string(placement.x) + "," +
                std::to_string(placement.y) + ") size=(" + std::to_string(placement.width) + "," +
                std::to_string(placement.height) + ") before_status=" +
                std::to_string(static_cast<int>(before_status)) + " before=(" +
                std::to_string(before_x) + "," + std::to_string(before_y) +
                ") forget_status=" +
                std::to_string(static_cast<int>(forget_status)) + " status=" +
                std::to_string(static_cast<int>(reposition_status)) + " after_status=" +
                std::to_string(static_cast<int>(after_status)) + " after=(" +
                std::to_string(after_x) + "," + std::to_string(after_y) + ")");
}

void PositionDialogInEventLoop(char *dialog, char *, ProAppData app_data)
{
    auto *placement = reinterpret_cast<QuickRenameDialogPlacement *>(app_data);
    if (placement == nullptr) {
        return;
    }
    ReconfigureDialogToPlacement(dialog, *placement, "eventloop-position");
}

} // namespace

bool PromptQuickRenameDialog(const core::QuickRenameTarget &target,
                             std::wstring &new_name,
                             QuickRenameDialogAction &action,
                             bool &cancelled,
                             std::wstring &error_out,
                             const QuickRenameDialogLogSink &log_sink)
{
    const QuickRenameDialogConfig config = DefaultQuickRenameDialogConfig();
    new_name.clear();
    action = QuickRenameDialogAction::rename;
    cancelled = false;
    error_out.clear();

    std::string used_resource;
    const ProError create_status = TryCreateDialog(config, log_sink, used_resource);
    if (create_status != PRO_TK_NO_ERROR) {
        error_out =
            L"\u6253\u5f00\u5feb\u901f\u91cd\u547d\u540d\u5bf9\u8bdd\u6846\u5931\u8d25\uff0cCreo \u8fd4\u56de\u72b6\u6001\uff1a" +
            std::to_wstring(static_cast<int>(create_status));
        return false;
    }
    LogLine(log_sink, "quick-rename-dialog create ok resource=" + used_resource);

    char *dialog = const_cast<char *>(config.dialog_name);

    ProUIDialogTitleSet(dialog, const_cast<wchar_t *>(L"\u5feb\u901f\u91cd\u547d\u540d"));
    ProUIInputpanelColumnsSet(dialog, const_cast<char *>(config.new_name_comp), 24);
    ProUIInputpanelValueSet(
        dialog,
        const_cast<char *>(config.new_name_comp),
        const_cast<wchar_t *>(target.old_name.c_str()));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.rename_comp), const_cast<wchar_t *>(L"\u6539\u540d"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.clone_comp), const_cast<wchar_t *>(L"\u514b\u9686"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.cancel_comp), const_cast<wchar_t *>(L"\u53d6\u6d88"));
    ProUIPushbuttonActivateActionSet(
        dialog,
        const_cast<char *>(config.rename_comp),
        ExitWithStatus,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_rename)));
    ProUIPushbuttonActivateActionSet(
        dialog,
        const_cast<char *>(config.clone_comp),
        ExitWithStatus,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_clone)));
    ProUIPushbuttonActivateActionSet(
        dialog,
        const_cast<char *>(config.cancel_comp),
        ExitWithStatus,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_cancel)));
    ProUIDialogCloseActionSet(
        dialog,
        ExitWithStatus,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_cancel)));
    ProUIDialogDefaultbuttonSet(dialog, const_cast<char *>(config.rename_comp));
    const ProError input_activate_status = ProUIInputpanelActivateActionSet(
        dialog,
        const_cast<char *>(config.new_name_comp),
        ExitWithStatus,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_rename)));
    LogLine(log_sink,
            "quick-rename-dialog input-activate status=" +
                std::to_string(static_cast<int>(input_activate_status)));

    QuickRenameDialogPlacement placement = BuildDialogPlacementNearCursor(dialog, log_sink);
    ReconfigureDialogToPlacement(dialog, placement, "preposition");
    const ProError app_action_status = ProUIDialogAppActionSet(dialog, PositionDialogInEventLoop, &placement);
    LogLine(log_sink,
            "quick-rename-dialog app-action status=" +
                std::to_string(static_cast<int>(app_action_status)));

    int dialog_status = config.status_cancel;
    const ProError activate_status = ProUIDialogActivate(dialog, &dialog_status);
    LogLine(log_sink,
            "quick-rename-dialog activate status=" +
                std::to_string(static_cast<int>(activate_status)) +
                " dialog_status=" + std::to_string(dialog_status));
    if (activate_status != PRO_TK_NO_ERROR) {
        error_out =
            L"\u6fc0\u6d3b\u5feb\u901f\u91cd\u547d\u540d\u5bf9\u8bdd\u6846\u5931\u8d25\uff0cCreo \u8fd4\u56de\u72b6\u6001\uff1a" +
            std::to_wstring(static_cast<int>(activate_status));
        ProUIDialogDestroy(dialog);
        return false;
    }
    if (dialog_status != config.status_rename &&
        dialog_status != config.status_clone) {
        cancelled = true;
        ProUIDialogDestroy(dialog);
        return false;
    }

    wchar_t *input_value = nullptr;
    if (ProUIInputpanelValueGet(dialog, const_cast<char *>(config.new_name_comp), &input_value) != PRO_TK_NO_ERROR ||
        input_value == nullptr) {
        if (input_value != nullptr) {
            ProWstringFree(input_value);
        }
        error_out = L"\u8bfb\u53d6\u65b0\u540d\u79f0\u5931\u8d25\u3002";
        ProUIDialogDestroy(dialog);
        return false;
    }

    if (dialog_status == config.status_clone) {
        action = QuickRenameDialogAction::clone;
    } else {
        action = QuickRenameDialogAction::rename;
    }
    new_name = input_value;
    ProWstringFree(input_value);
    ProUIDialogDestroy(dialog);
    return true;
}

} // namespace autobbox::ui
