#include "autobbox/main/plugin_random_color.h"

#include "autobbox/application/random_color.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/message_dialog.h"
#include "autobbox/ui/random_color_dialog.h"

#include <ProMdl.h>
#include <ProToolkit.h>
#include <ProUIMessage.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

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

ProBoolean ValueOrDefault(const ProBoolean *value, ProBoolean fallback)
{
    return value != nullptr ? *value : fallback;
}

} // namespace

int RunPluginRandomColorTask(const PluginRandomColorRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=random-color reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("===== Run begin mode=random-color =====");

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        autobbox::creo::ModelType(current) != PRO_MDL_ASSEMBLY) {
        LogLine("FAIL random-color reason=current-not-assembly");
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u968f\u673a\u4e0a\u8272",
            L"\u8bf7\u5148\u6253\u5f00\u88c5\u914d\uff0c\u518d\u6267\u884c\u968f\u673a\u4e0a\u8272\u3002");
        OpenPluginReportLog();
        return 0;
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"随机上色",
            L"请先打开装配，再执行随机上色。");
        OpenPluginReportLog();
        return 0;
    }

    const std::vector<autobbox::core::RandomColorCandidate> candidates =
        autobbox::application::CollectRandomColorCandidates(
            ValueOrDefault(runtime.parts, PRO_B_TRUE),
            ValueOrDefault(runtime.assemblies, PRO_B_TRUE),
            ValueOrDefault(runtime.top_level_only, PRO_B_FALSE));
    LogLine("random-color candidates=%d top2=%d parts=%d assemblies=%d",
            static_cast<int>(candidates.size()),
            static_cast<int>(ValueOrDefault(runtime.top_level_only, PRO_B_FALSE)),
            static_cast<int>(ValueOrDefault(runtime.parts, PRO_B_TRUE)),
            static_cast<int>(ValueOrDefault(runtime.assemblies, PRO_B_TRUE)));

    if (candidates.empty()) {
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_INFO,
            L"\u968f\u673a\u4e0a\u8272",
            L"\u6309\u5f53\u524d\u8fc7\u6ee4\u6761\u4ef6\u672a\u627e\u5230\u53ef\u4e0a\u8272\u7684\u6a21\u578b\u3002");
        OpenPluginReportLog();
        return 0;
        autobbox::ui::ShowSimpleMessageDialog(
            PROUIMESSAGE_INFO,
            L"随机上色",
            L"按当前过滤条件未找到可上色的模型。");
        OpenPluginReportLog();
        return 0;
    }

    const std::wstring default_library_path =
        autobbox::application::ResolveDefaultRandomColorLibraryPath();
    LogLine("random-color default-library=%s",
            autobbox::common::WToA(default_library_path.c_str()).c_str());

    std::vector<autobbox::core::RandomColorAssignment> selected;
    std::vector<autobbox::core::RandomColorParameterPreview> parameter_selected;
    std::vector<autobbox::core::RandomColorCandidate> clear_targets;
    bool use_parameter_colors = false;
    std::wstring parameter_name;
    bool clear_all_colors = false;
    bool cancelled = false;
    if (!autobbox::ui::PromptRandomColorDialog(
            candidates,
            default_library_path,
            selected,
            parameter_selected,
            clear_targets,
            use_parameter_colors,
            parameter_name,
            clear_all_colors,
            cancelled,
            [](const std::string &line) { LogPluginReportLine(line); })) {
        LogLine("random-color dialog status=%s", cancelled ? "cancelled" : "failed");
        if (!cancelled) {
            OpenPluginReportLog();
        }
        return 0;
    }
    if (cancelled) {
        LogLine("random-color cancelled by user");
        return 0;
    }

    std::wstring summary_text;
    bool ok = false;
    if (clear_all_colors) {
        LogLine("random-color clear requested targets=%d", static_cast<int>(clear_targets.size()));
        ok = autobbox::application::ClearRandomColors(
            clear_targets,
            summary_text,
            [](const std::string &line) { LogPluginReportLine(line); });
    } else if (use_parameter_colors) {
        LogLine("parameter-color selected=%d param=%s",
                static_cast<int>(parameter_selected.size()),
                autobbox::common::WToA(parameter_name.c_str()).c_str());
        ok = autobbox::application::ApplyParameterColors(
            parameter_selected,
            summary_text,
            [](const std::string &line) { LogPluginReportLine(line); });
    } else {
        LogLine("random-color selected=%d", static_cast<int>(selected.size()));
        ok = autobbox::application::ApplyRandomColors(
            selected,
            summary_text,
            [](const std::string &line) { LogPluginReportLine(line); });
    }

    autobbox::ui::ShowSimpleMessageDialog(
        ok ? PROUIMESSAGE_INFO : PROUIMESSAGE_ERROR,
        clear_all_colors ? L"\u6e05\u9664\u989c\u8272" : (use_parameter_colors ? L"\u6309\u53c2\u6570\u4e0a\u8272" : L"\u968f\u673a\u4e0a\u8272"),
        summary_text.empty()
            ? (ok ? L"\u64cd\u4f5c\u5b8c\u6210\u3002" : L"\u64cd\u4f5c\u5931\u8d25\u3002")
            : summary_text.c_str());
    OpenPluginReportLog();
    return 0;

    autobbox::ui::ShowSimpleMessageDialog(
        ok ? PROUIMESSAGE_INFO : PROUIMESSAGE_ERROR,
        clear_all_colors ? L"清除颜色" : L"随机上色",
        summary_text.empty()
            ? (ok ? L"操作完成。" : L"操作失败。")
            : summary_text.c_str());
    OpenPluginReportLog();
    return 0;
}

} // namespace autobbox::main
