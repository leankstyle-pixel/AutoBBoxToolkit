#include "autobbox/main/plugin_quick_rename.h"

#include "autobbox/application/quick_rename.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/message_dialog.h"
#include "autobbox/ui/quick_rename_dialog.h"

#include <ProMdl.h>
#include <ProToolkit.h>
#include <ProUIMessage.h>
#include <ProWindows.h>

#include <cstdarg>
#include <cstdio>
#include <string>

namespace autobbox::main {

namespace {

void LogLine(const char *fmt, ...)
{
    if (fmt == nullptr) {
        return;
    }

    char buffer[4096] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    LogPluginReportLine(buffer);
}

struct TaskGuard {
    explicit TaskGuard(bool *task_running)
        : task_running(task_running)
    {
    }

    ~TaskGuard()
    {
        EndPluginReportSession();
        if (task_running != nullptr) {
            *task_running = false;
        }
    }

    bool *task_running = nullptr;
};

void RefreshCurrentWindow()
{
    int window_id = -1;
    if (ProWindowCurrentGet(&window_id) == PRO_TK_NO_ERROR && window_id >= 0) {
        ProWindowRefresh(window_id);
        ProWindowRepaint(window_id);
    }
}

std::string RuntimeTag(ProMdl mdl, const PluginQuickRenameRuntime &runtime)
{
    if (runtime.format_model_tag != nullptr) {
        return runtime.format_model_tag(mdl);
    }
    return autobbox::creo::DefaultModelTag(mdl);
}

} // namespace

int RunPluginQuickRenameTask(const PluginQuickRenameRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=quick-rename reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("===== Run begin mode=quick-rename =====");

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        autobbox::creo::ModelType(current) != PRO_MDL_ASSEMBLY) {
        LogLine("FAIL quick-rename reason=current-not-assembly");
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u5feb\u901f\u91cd\u547d\u540d",
            L"\u8bf7\u5148\u6253\u5f00\u88c5\u914d\uff0c\u518d\u6267\u884c\u5feb\u901f\u91cd\u547d\u540d\u3002");
        OpenPluginReportLog();
        return 0;
    }

    autobbox::core::QuickRenameTarget target = {};
    bool cancelled = false;
    std::wstring error_text;
    if (!autobbox::application::ResolveQuickRenameTarget(
            target,
            cancelled,
            error_text,
            [](const std::string &line) { LogPluginReportLine(line); })) {
        LogLine("quick-rename target status=%s error=%s",
                cancelled ? "cancelled" : "failed",
                autobbox::common::WToA(error_text.c_str()).c_str());
        if (!cancelled) {
            autobbox::ui::ShowSimpleMessageDialog(
                PROUIMESSAGE_WARNING,
                L"\u5feb\u901f\u91cd\u547d\u540d",
                error_text.empty()
                    ? L"\u672a\u83b7\u53d6\u5230\u53ef\u91cd\u547d\u540d\u7684\u6a21\u578b\u3002"
                    : error_text.c_str());
            OpenPluginReportLog();
        }
        return 0;
    }

    LogLine("quick-rename target=%s",
            RuntimeTag(target.mdl, runtime).c_str());

    std::wstring input_name;
    autobbox::ui::QuickRenameDialogAction action = autobbox::ui::QuickRenameDialogAction::rename;
    if (!autobbox::ui::PromptQuickRenameDialog(
            target,
            input_name,
            action,
            cancelled,
            error_text,
            [](const std::string &line) { LogPluginReportLine(line); })) {
        LogLine("quick-rename dialog status=%s error=%s",
                cancelled ? "cancelled" : "failed",
                autobbox::common::WToA(error_text.c_str()).c_str());
        if (!cancelled && !error_text.empty()) {
            autobbox::ui::ShowSimpleMessageDialog(
                PROUIMESSAGE_ERROR,
                L"\u5feb\u901f\u91cd\u547d\u540d",
                error_text.c_str());
            OpenPluginReportLog();
        }
        return 0;
    }

    const bool clone_requested = action == autobbox::ui::QuickRenameDialogAction::clone;
    autobbox::core::QuickRenameValidationResult validation =
        autobbox::application::ValidateQuickRenameName(target, input_name, !clone_requested);
    if (!validation.ok) {
        LogLine("quick-rename validate failed old=%s input=%s error=%s",
                autobbox::common::WToA(target.old_name.c_str()).c_str(),
                autobbox::common::WToA(input_name.c_str()).c_str(),
                autobbox::common::WToA(validation.error_text.c_str()).c_str());
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u5feb\u901f\u91cd\u547d\u540d",
            validation.error_text.c_str());
        OpenPluginReportLog();
        return 0;
    }
    if (validation.unchanged) {
        LogLine("%s unchanged name=%s",
                clone_requested ? "quick-rename clone" : "quick-rename",
                autobbox::common::WToA(target.old_name.c_str()).c_str());
        const wchar_t *message = clone_requested
                                     ? L"\u514b\u9686\u540d\u79f0\u4e0d\u80fd\u4e0e\u5f53\u524d\u540d\u79f0\u76f8\u540c\u3002"
                                     : L"\u65b0\u540d\u79f0\u4e0e\u5f53\u524d\u540d\u79f0\u76f8\u540c\uff0c\u65e0\u9700\u4fee\u6539\u3002";
        autobbox::ui::ShowSimpleMessageDialog(
            clone_requested ? PROUIMESSAGE_WARNING : PROUIMESSAGE_INFO,
            L"\u5feb\u901f\u91cd\u547d\u540d",
            message);
        return 0;
    }

    if (validation.existing_name_conflict) {
        if (!target.has_component_path || target.parent_assembly == nullptr || target.component_id <= 0) {
            LogLine("quick-rename rename-conflict failed reason=no-component-path old=%s new=%s",
                    autobbox::common::WToA(target.old_name.c_str()).c_str(),
                    autobbox::common::WToA(validation.normalized_name.c_str()).c_str());
            autobbox::ui::ShowSimpleMessageDialog(
                PROUIMESSAGE_WARNING,
                L"\u5feb\u901f\u91cd\u547d\u540d",
                L"\u540c\u540d\u6a21\u578b\u5df2\u5728\u4f1a\u8bdd\u4e2d\u5b58\u5728\u3002\n"
                L"\u8bf7\u5728\u88c5\u914d\u6811\u6216\u56fe\u5f62\u533a\u9009\u4e2d\u5177\u4f53\u7ec4\u4ef6\u5b9e\u4f8b\uff0c\u624d\u80fd\u5c06\u8be5\u5b9e\u4f8b\u66ff\u6362\u4e3a\u5df2\u6709\u540c\u540d\u6a21\u578b\u3002");
            OpenPluginReportLog();
            return 0;
        }

        const std::wstring confirm_message =
            L"\u540c\u540d\u6a21\u578b\u5df2\u5728\u4f1a\u8bdd\u4e2d\u5b58\u5728\uff1a" +
            validation.normalized_name +
            L"\n\u662f\u5426\u6539\u4e3a\u7528\u8be5\u5df2\u6709\u6a21\u578b\u539f\u5730\u66ff\u6362\u5f53\u524d\u88c5\u914d\u5b9e\u4f8b\uff1f";
        const bool replace_confirmed = autobbox::ui::ShowYesNoMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u5feb\u901f\u91cd\u547d\u540d",
            confirm_message.c_str(),
            false);
        if (!replace_confirmed) {
            LogLine("quick-rename rename-conflict replace-declined old=%s new=%s",
                    autobbox::common::WToA(target.old_name.c_str()).c_str(),
                    autobbox::common::WToA(validation.normalized_name.c_str()).c_str());
            return 0;
        }

        const ProError replace_status =
            autobbox::application::ReplaceLoadedModelInAssembly(target, validation.existing_mdl);
        LogLine("quick-rename rename-conflict replace old=%s new=%s status=%d replacement=%s",
                autobbox::common::WToA(target.old_name.c_str()).c_str(),
                autobbox::common::WToA(validation.normalized_name.c_str()).c_str(),
                static_cast<int>(replace_status),
                validation.existing_mdl != nullptr ? RuntimeTag(validation.existing_mdl, runtime).c_str() : "(null)");

        if (replace_status != PRO_TK_NO_ERROR) {
            autobbox::ui::ShowSimpleMessageDialog(
                replace_status == PRO_TK_NO_CHANGE ? PROUIMESSAGE_WARNING : PROUIMESSAGE_ERROR,
                L"\u5feb\u901f\u91cd\u547d\u540d",
                autobbox::application::QuickReplaceStatusMessage(replace_status).c_str());
            OpenPluginReportLog();
            return 0;
        }

        RefreshCurrentWindow();
        return 0;
    }

    if (clone_requested) {
        if (!target.has_component_path || target.parent_assembly == nullptr || target.component_id <= 0) {
            LogLine("quick-rename clone failed reason=no-component-path old=%s new=%s",
                    autobbox::common::WToA(target.old_name.c_str()).c_str(),
                    autobbox::common::WToA(validation.normalized_name.c_str()).c_str());
            autobbox::ui::ShowSimpleMessageDialog(
                PROUIMESSAGE_WARNING,
                L"\u5feb\u901f\u91cd\u547d\u540d",
                L"\u8bf7\u5728\u88c5\u914d\u6811\u6216\u56fe\u5f62\u533a\u9009\u4e2d\u5177\u4f53\u7ec4\u4ef6\u5b9e\u4f8b\uff0c\u514b\u9686\u624d\u80fd\u590d\u5236\u6a21\u578b\u5e76\u539f\u5730\u66ff\u6362\u8be5\u5b9e\u4f8b\u3002");
            OpenPluginReportLog();
            return 0;
        }

        ProMdl cloned = nullptr;
        const ProError clone_status =
            autobbox::application::CloneModelInSession(target, validation.normalized_name, &cloned);
        LogLine("quick-rename clone old=%s new=%s status=%d cloned=%s",
                autobbox::common::WToA(target.old_name.c_str()).c_str(),
                autobbox::common::WToA(validation.normalized_name.c_str()).c_str(),
                static_cast<int>(clone_status),
                cloned != nullptr ? RuntimeTag(cloned, runtime).c_str() : "(null)");

        if (clone_status != PRO_TK_NO_ERROR) {
            autobbox::ui::ShowSimpleMessageDialog(
                PROUIMESSAGE_ERROR,
                L"\u5feb\u901f\u91cd\u547d\u540d",
                autobbox::application::QuickCloneStatusMessage(clone_status).c_str());
            OpenPluginReportLog();
            return 0;
        }

        const ProError replace_status =
            autobbox::application::ReplaceLoadedModelInAssembly(target, cloned);
        LogLine("quick-rename clone-replace old=%s new=%s status=%d cloned=%s",
                autobbox::common::WToA(target.old_name.c_str()).c_str(),
                autobbox::common::WToA(validation.normalized_name.c_str()).c_str(),
                static_cast<int>(replace_status),
                cloned != nullptr ? RuntimeTag(cloned, runtime).c_str() : "(null)");

        if (replace_status != PRO_TK_NO_ERROR) {
            const std::wstring message =
                L"\u5df2\u751f\u6210\u514b\u9686\u6a21\u578b\uff1a" + validation.normalized_name +
                L"\n\u4f46\u539f\u5730\u66ff\u6362\u5931\u8d25\uff1a" +
                autobbox::application::QuickReplaceStatusMessage(replace_status);
            autobbox::ui::ShowSimpleMessageDialog(
                replace_status == PRO_TK_NO_CHANGE ? PROUIMESSAGE_WARNING : PROUIMESSAGE_ERROR,
                L"\u5feb\u901f\u91cd\u547d\u540d",
                message.c_str());
            OpenPluginReportLog();
            return 0;
        }

        RefreshCurrentWindow();
        const std::wstring summary =
            L"\u5df2\u514b\u9686\uff1a" + target.old_name + L" -> " + validation.normalized_name +
            L"\n\u5df2\u539f\u5730\u66ff\u6362\u5f53\u524d\u88c5\u914d\u5b9e\u4f8b\u3002"
            L"\n\u6ce8\u610f\uff1a\u672a\u81ea\u52a8\u4fdd\u5b58\u6a21\u578b\u6587\u4ef6\u6216\u88c5\u914d\u3002";
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_INFO,
            L"\u5feb\u901f\u91cd\u547d\u540d",
            summary.c_str());
        OpenPluginReportLog();
        return 0;
    }

    const ProError rename_status =
        autobbox::application::RenameModelInSession(target, validation.normalized_name);
    LogLine("quick-rename rename old=%s new=%s status=%d",
            autobbox::common::WToA(target.old_name.c_str()).c_str(),
            autobbox::common::WToA(validation.normalized_name.c_str()).c_str(),
            static_cast<int>(rename_status));

    if (rename_status != PRO_TK_NO_ERROR) {
        if (target.has_component_path && target.parent_assembly != nullptr && target.component_id > 0) {
            const std::wstring confirm_message =
                L"\u76f4\u63a5\u6539\u540d\u5931\u8d25\uff0cCreo \u8fd4\u56de\u72b6\u6001\uff1a" +
                std::to_wstring(static_cast<int>(rename_status)) +
                L"\n\u5982\u679c\u76ee\u6807\u540d\u79f0\u5bf9\u5e94\u5df2\u6709\u6a21\u578b\uff0c\u53ef\u6539\u4e3a\u5c06\u5f53\u524d\u88c5\u914d\u5b9e\u4f8b\u66ff\u6362\u4e3a\u8be5\u6a21\u578b\u3002"
                L"\n\u662f\u5426\u5c1d\u8bd5\u539f\u5730\u66ff\u6362\uff1f";
            const bool replace_confirmed = autobbox::ui::ShowYesNoMessageDialog(
                PROUIMESSAGE_WARNING,
                L"\u5feb\u901f\u91cd\u547d\u540d",
                confirm_message.c_str(),
                false);
            if (replace_confirmed) {
                ProMdl replacement = nullptr;
                const ProError replace_status =
                    autobbox::application::ReplaceModelInAssembly(target, validation.normalized_name, &replacement);
                LogLine("quick-rename rename-fallback-replace old=%s new=%s rename_status=%d replace_status=%d replacement=%s",
                        autobbox::common::WToA(target.old_name.c_str()).c_str(),
                        autobbox::common::WToA(validation.normalized_name.c_str()).c_str(),
                        static_cast<int>(rename_status),
                        static_cast<int>(replace_status),
                        replacement != nullptr ? RuntimeTag(replacement, runtime).c_str() : "(null)");

                if (replace_status == PRO_TK_NO_ERROR) {
                    RefreshCurrentWindow();
                    return 0;
                }

                const std::wstring message =
                    L"\u76f4\u63a5\u6539\u540d\u5931\u8d25\uff1a" +
                    autobbox::application::QuickRenameStatusMessage(rename_status) +
                    L"\n\u5c1d\u8bd5\u539f\u5730\u66ff\u6362\u4e5f\u5931\u8d25\uff1a" +
                    autobbox::application::QuickReplaceStatusMessage(replace_status);
                autobbox::ui::ShowSimpleMessageDialog(
                    replace_status == PRO_TK_NO_CHANGE ? PROUIMESSAGE_WARNING : PROUIMESSAGE_ERROR,
                    L"\u5feb\u901f\u91cd\u547d\u540d",
                    message.c_str());
                OpenPluginReportLog();
                return 0;
            }

            LogLine("quick-rename rename-fallback-replace-declined old=%s new=%s rename_status=%d",
                    autobbox::common::WToA(target.old_name.c_str()).c_str(),
                    autobbox::common::WToA(validation.normalized_name.c_str()).c_str(),
                    static_cast<int>(rename_status));
        }

        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_ERROR,
            L"\u5feb\u901f\u91cd\u547d\u540d",
            autobbox::application::QuickRenameStatusMessage(rename_status).c_str());
        OpenPluginReportLog();
        return 0;
    }

    RefreshCurrentWindow();
    return 0;
}

} // namespace autobbox::main
