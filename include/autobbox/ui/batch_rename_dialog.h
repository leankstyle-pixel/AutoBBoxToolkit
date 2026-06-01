#pragma once

#include "autobbox/core/batch_rename_types.h"

#include <functional>
#include <string>
#include <vector>

namespace autobbox::ui {

using BatchRenameDialogLogSink = std::function<void(const std::string &line)>;

struct BatchRenameDialogCallbacks {
    std::function<std::vector<core::BatchRenameCandidate>()> collect_candidates;
    std::function<bool(std::vector<core::BatchRenameCandidate> &candidates,
                       std::wstring &error_out)> validate_candidates;
    std::function<bool(std::vector<core::BatchRenameCandidate> &candidates,
                       core::BatchRenameApplySummary &summary,
                       std::wstring &error_out)> apply_candidates;
    BatchRenameDialogLogSink log_sink;
};

bool PromptBatchRenameDialog(std::vector<core::BatchRenameCandidate> &candidates,
                             const BatchRenameDialogCallbacks &callbacks);

} // namespace autobbox::ui
