#pragma once

#include "autobbox/core/batch_rename_types.h"

#include <ProToolkit.h>

#include <functional>
#include <string>
#include <vector>

namespace autobbox::application {

using BatchRenameLogSink = std::function<void(const std::string &line)>;

std::vector<core::BatchRenameCandidate> CollectBatchRenameCandidates(
    const core::BatchRenameOptions &options);

bool BatchRenameCandidateHasChanges(const core::BatchRenameCandidate &candidate);

bool ValidateBatchRenameCandidates(
    std::vector<core::BatchRenameCandidate> &candidates,
    std::vector<core::BatchRenameValidationIssue> &issues);

void ClearBatchRenameCandidates(
    std::vector<core::BatchRenameCandidate> &candidates,
    const core::BatchRenameClearSpec &spec,
    core::BatchRenameTransformSummary &summary);

bool ReplaceBatchRenameCandidates(
    std::vector<core::BatchRenameCandidate> &candidates,
    const core::BatchRenameReplaceSpec &spec,
    core::BatchRenameTransformSummary &summary,
    std::wstring &error_out);

bool SequenceBatchRenameCandidates(
    std::vector<core::BatchRenameCandidate> &candidates,
    const core::BatchRenameSequenceSpec &spec,
    core::BatchRenameTransformSummary &summary,
    std::wstring &error_out);

ProError ApplyBatchRenameCandidates(
    std::vector<core::BatchRenameCandidate> &candidates,
    core::BatchRenameApplySummary &summary,
    const BatchRenameLogSink &log_sink = BatchRenameLogSink());

} // namespace autobbox::application
