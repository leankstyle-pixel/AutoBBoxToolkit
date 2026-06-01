#include "autobbox/application/ft_logger.h"

#include "autobbox/common/strings.h"
#include "autobbox/main/plugin_runtime_bridge.h"

#include <sstream>

namespace autobbox::application {

void FtLog(core::FtWorkspace &workspace,
           const std::wstring &level_path,
           const std::wstring &severity,
           const std::wstring &operation,
           const std::wstring &message,
           ProError status)
{
    core::FtLogEntry entry;
    entry.level_path = level_path;
    entry.severity = severity;
    entry.operation = operation;
    entry.message = message;
    entry.status = status;
    workspace.logs.push_back(entry);

    std::wstringstream ss;
    ss << L"FT_LOG severity=" << severity
       << L" level=" << level_path
       << L" op=" << operation
       << L" status=" << static_cast<int>(status)
       << L" msg=" << message;
    autobbox::main::LogPluginReportLine(autobbox::common::WToA(ss.str().c_str()));
}

void FtLog(core::FtWorkspace *workspace,
           const std::wstring &level_path,
           const std::wstring &severity,
           const std::wstring &operation,
           const std::wstring &message,
           ProError status)
{
    if (workspace != nullptr) {
        FtLog(*workspace, level_path, severity, operation, message, status);
    }
}

std::wstring BuildFtLogText(const core::FtWorkspace &workspace)
{
    std::wstringstream ss;
    for (const core::FtLogEntry &entry : workspace.logs) {
        ss << L"[" << entry.severity << L"] ";
        if (!entry.level_path.empty()) {
            ss << entry.level_path << L" ";
        }
        ss << entry.operation << L" status=" << static_cast<int>(entry.status)
           << L" " << entry.message << L"\n";
    }
    return ss.str();
}

} // namespace autobbox::application
