#include "autobbox/main/plugin_save_copy_to_workdir.h"

#include "autobbox/application/save_copy_to_workdir.h"
#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/message_dialog.h"
#include "autobbox/ui/save_copy_to_workdir_dialog.h"

#include <ProAsmcomp.h>
#include <ProAssembly.h>
#include <ProMdl.h>
#include <ProObjects.h>
#include <ProSizeConst.h>
#include <ProToolkit.h>
#include <ProUIMessage.h>
#include <ProUtil.h>
#include <ProWindows.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>
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

std::string RuntimeTag(ProMdl mdl, const PluginSaveCopyToWorkdirRuntime &runtime)
{
    if (runtime.format_model_tag != nullptr) {
        return runtime.format_model_tag(mdl);
    }
    return autobbox::creo::DefaultModelTag(mdl);
}

void CopyToProPath(const std::wstring &path, ProPath pro_path)
{
    if (pro_path != nullptr) {
        pro_path[0] = L'\0';
        wcsncpy_s(pro_path, PRO_PATH_SIZE, path.c_str(), _TRUNCATE);
    }
}

} // namespace

int RunPluginSaveCopyToWorkdirTask(const PluginSaveCopyToWorkdirRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=save-copy-to-workdir reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("===== Run begin mode=save-copy-to-workdir =====");

    ProMdl current_mdl = nullptr;
    const ProError current_status = ProMdlCurrentGet(&current_mdl);
    if (current_status != PRO_TK_NO_ERROR ||
        current_mdl == nullptr ||
        autobbox::creo::ModelType(current_mdl) != PRO_MDL_ASSEMBLY) {
        LogLine("save-copy failed reason=no-current-assembly current_status=%d current=%s",
                static_cast<int>(current_status),
                current_mdl != nullptr ? RuntimeTag(current_mdl, runtime).c_str() : "(null)");
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u7ec4\u88c5\u526f\u672c",
            L"\u8bf7\u5148\u6fc0\u6d3b\u8981\u88c5\u5165\u526f\u672c\u7684\u5f53\u524d\u88c5\u914d\u3002\n\u6e90\u6a21\u578b\u4e0d\u9700\u8981\u9884\u5148\u6253\u5f00\uff0c\u70b9\u51fb\u547d\u4ee4\u540e\u518d\u4ece Creo \u6587\u4ef6\u7ba1\u7406\u5668\u9009\u62e9\u3002");
        OpenPluginReportLog();
        return 0;
    }
    ProAssembly target_assembly = ProMdlToAssembly(current_mdl);

    const std::wstring workdir = autobbox::common::CurrentWorkingDirectoryW();
    if (workdir.empty()) {
        LogLine("save-copy failed reason=no-current-working-directory");
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_ERROR,
            L"\u7ec4\u88c5\u526f\u672c",
            L"\u672a\u83b7\u53d6\u5230 Creo \u5f53\u524d\u5de5\u4f5c\u76ee\u5f55\u3002");
        OpenPluginReportLog();
        return 0;
    }

    autobbox::application::SaveCopyToWorkdirSource source = {};
    bool cancelled = false;
    std::wstring error_text;
    if (!autobbox::application::ResolveSaveCopyToWorkdirSource(
            source,
            cancelled,
            error_text,
            [](const std::string &line) { LogPluginReportLine(line); })) {
        LogLine("save-copy source status=%s error=%s",
                cancelled ? "cancelled" : "failed",
                autobbox::common::WToA(error_text.c_str()).c_str());
        if (!cancelled) {
            autobbox::ui::ShowSimpleMessageDialog(
                PROUIMESSAGE_WARNING,
                L"\u7ec4\u88c5\u526f\u672c",
                error_text.empty()
                    ? L"\u672a\u83b7\u53d6\u5230\u53ef\u7ec4\u88c5\u526f\u672c\u7684\u96f6\u4ef6\u6216\u88c5\u914d\u6a21\u578b\u3002"
                    : error_text.c_str());
            OpenPluginReportLog();
        }
        return 0;
    }

    LogLine("save-copy source=%s name=%s target_assembly=%s workdir=%s from_file=%d",
            RuntimeTag(source.mdl, runtime).c_str(),
            autobbox::common::WToA(source.name.c_str()).c_str(),
            RuntimeTag(current_mdl, runtime).c_str(),
            autobbox::common::WToA(workdir.c_str()).c_str(),
            source.from_file_picker ? 1 : 0);

    autobbox::ui::SaveCopyToWorkdirDialogResult dialog_result = {};
    if (!autobbox::ui::PromptSaveCopyToWorkdirDialog(
            source,
            dialog_result,
            cancelled,
            error_text,
            [](const std::string &line) { LogPluginReportLine(line); })) {
        LogLine("save-copy dialog status=%s error=%s",
                cancelled ? "cancelled" : "failed",
                autobbox::common::WToA(error_text.c_str()).c_str());
        if (!cancelled && !error_text.empty()) {
            autobbox::ui::ShowSimpleMessageDialog(
                PROUIMESSAGE_ERROR,
                L"\u7ec4\u88c5\u526f\u672c",
                error_text.c_str());
            OpenPluginReportLog();
        }
        return 0;
    }

    const autobbox::application::SaveCopyToWorkdirValidationResult validation =
        autobbox::application::ValidateSaveCopyToWorkdirName(
            source,
            dialog_result.new_name,
            workdir);
    if (!validation.ok) {
        LogLine("save-copy validate failed source=%s input=%s error=%s target=%s",
                autobbox::common::WToA(source.name.c_str()).c_str(),
                autobbox::common::WToA(dialog_result.new_name.c_str()).c_str(),
                autobbox::common::WToA(validation.error_text.c_str()).c_str(),
                autobbox::common::WToA(validation.target_path.c_str()).c_str());
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u7ec4\u88c5\u526f\u672c",
            validation.error_text.empty()
                ? L"\u65b0\u540d\u79f0\u6821\u9a8c\u5931\u8d25\u3002"
                : validation.error_text.c_str());
        OpenPluginReportLog();
        return 0;
    }

    ProPath workdir_path = {0};
    CopyToProPath(workdir, workdir_path);
    const ProError directory_status = ProDirectoryChange(workdir_path);
    LogLine("save-copy restore-workdir status=%d path=%s",
            static_cast<int>(directory_status),
            autobbox::common::WToA(workdir.c_str()).c_str());
    if (directory_status != PRO_TK_NO_ERROR) {
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_ERROR,
            L"\u7ec4\u88c5\u526f\u672c",
            (L"\u65e0\u6cd5\u5207\u56de\u547d\u4ee4\u5f00\u59cb\u65f6\u7684 Creo \u5de5\u4f5c\u76ee\u5f55\uff0c\u4e3a\u907f\u514d\u4fdd\u5b58\u5230\u9519\u8bef\u4f4d\u7f6e\u5df2\u53d6\u6d88\u3002\n\u76ee\u6807\u76ee\u5f55\uff1a" + workdir +
             L"\nCreo \u8fd4\u56de\u72b6\u6001\uff1a" + std::to_wstring(static_cast<int>(directory_status))).c_str());
        OpenPluginReportLog();
        return 0;
    }

    ProMdl copied = nullptr;
    const ProError save_status =
        autobbox::application::SaveModelCopyToWorkdir(
            source,
            validation.normalized_name,
            &copied);
    LogLine("save-copy copy-save source=%s new=%s status=%d copied=%s target=%s",
            autobbox::common::WToA(source.name.c_str()).c_str(),
            autobbox::common::WToA(validation.normalized_name.c_str()).c_str(),
            static_cast<int>(save_status),
            copied != nullptr ? RuntimeTag(copied, runtime).c_str() : "(null)",
            autobbox::common::WToA(validation.target_path.c_str()).c_str());

    if (save_status != PRO_TK_NO_ERROR || copied == nullptr) {
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_ERROR,
            L"\u7ec4\u88c5\u526f\u672c",
            autobbox::application::SaveCopyToWorkdirStatusMessage(save_status).c_str());
        OpenPluginReportLog();
        return 0;
    }

    ProAsmcomp assembled_component = {};
    ProError constraint_ui_status = PRO_TK_GENERAL_ERROR;
    const ProError assemble_status =
        autobbox::application::AssembleSavedCopyToAssembly(
            target_assembly,
            copied,
            &assembled_component,
            &constraint_ui_status);
    LogLine("save-copy assemble status=%d constraint_ui_status=%d copied=%s target_assembly=%s",
            static_cast<int>(assemble_status),
            static_cast<int>(constraint_ui_status),
            RuntimeTag(copied, runtime).c_str(),
            RuntimeTag(current_mdl, runtime).c_str());
    if (assemble_status != PRO_TK_NO_ERROR) {
        const std::wstring message =
            L"\u5df2\u4fdd\u5b58\u526f\u672c\uff1a\n" + validation.target_path +
            L"\n\u4f46\u88c5\u914d\u5230\u5f53\u524d\u88c5\u914d\u5931\u8d25\uff1a" +
            autobbox::application::AssembleSavedCopyToAssemblyStatusMessage(assemble_status);
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_ERROR,
            L"\u7ec4\u88c5\u526f\u672c",
            message.c_str());
        OpenPluginReportLog();
        return 0;
    }

    RefreshCurrentWindow();

    if (constraint_ui_status != PRO_TK_NO_ERROR) {
        const std::wstring message =
            L"\u5df2\u4fdd\u5b58\u5e76\u88c5\u5165\u526f\u672c\uff1a\n" + validation.target_path +
            L"\n\u4f46 Creo \u7ec4\u4ef6\u7ea6\u675f\u5b9a\u4e49\u672a\u5b8c\u6210\uff1a" +
            autobbox::application::AssembleSavedCopyToAssemblyStatusMessage(constraint_ui_status) +
            L"\n\u53ef\u5728 Creo \u4e2d\u7ee7\u7eed\u91cd\u5b9a\u4e49\u8be5\u7ec4\u4ef6\u7ea6\u675f\u3002\n\u6ce8\u610f\uff1a\u672a\u81ea\u52a8\u4fdd\u5b58\u5f53\u524d\u88c5\u914d\u3002";
        autobbox::ui::ShowSimpleMessageDialog(
            constraint_ui_status == PRO_TK_USER_ABORT ? PROUIMESSAGE_WARNING : PROUIMESSAGE_ERROR,
            L"\u7ec4\u88c5\u526f\u672c",
            message.c_str());
        OpenPluginReportLog();
        return 0;
    }

    const std::wstring summary =
        L"\u5df2\u4fdd\u5b58\u5e76\u7ec4\u88c5\u526f\u672c\uff1a" + source.name + L" -> " + validation.normalized_name +
        L"\n\u76ee\u6807\u6587\u4ef6\uff1a" + validation.target_path +
        L"\n\u5df2\u5b8c\u6210 Creo \u5b98\u65b9\u7ec4\u4ef6\u7ea6\u675f\u5b9a\u4e49\u3002\n\u6ce8\u610f\uff1a\u672a\u81ea\u52a8\u4fdd\u5b58\u5f53\u524d\u88c5\u914d\u3002";
    autobbox::ui::ShowSimpleMessageDialog(
        PROUIMESSAGE_INFO,
        L"\u7ec4\u88c5\u526f\u672c",
        summary.c_str());
    OpenPluginReportLog();
    return 0;
}

} // namespace autobbox::main
