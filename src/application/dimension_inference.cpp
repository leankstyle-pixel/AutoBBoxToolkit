#include "autobbox/application/dimension_inference.h"

#include <cstdarg>
#include <cstdio>

namespace autobbox::application {

namespace {

void LogLine(const Drawing3LogSink &log_sink, const char *fmt, ...)
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

core::SmartDimensionCandidate MakeCandidate(core::SmartDimensionKind kind,
                                            int priority,
                                            const wchar_t *label,
                                            const wchar_t *rationale)
{
    core::SmartDimensionCandidate candidate = {};
    candidate.kind = kind;
    candidate.priority = priority;
    candidate.label = label;
    candidate.rationale = rationale;
    return candidate;
}

} // namespace

core::SmartDimensionInferenceResult InferSmartDimensionCandidates(
    const core::SmartDimensionSelectionSet &selections,
    const Drawing3LogSink &log_sink)
{
    core::SmartDimensionInferenceResult result = {};
    if (!selections.complete || selections.first.selection == nullptr) {
        LogLine(log_sink, "smart-dim inference skipped reason=incomplete-single-reference");
        return result;
    }

    // V1 deliberately follows the official ProTestDimStandardCreate() style:
    // one selected reference + one picked location + PRO_DIM_ORNT_NONE.
    // Creo decides whether the created standard dimension is radius/diameter/etc.
    result.candidates.push_back(MakeCandidate(
        core::SmartDimensionKind::Linear,
        100,
        L"Official single-reference standard dimension",
        L"Use ProDrawingDimensionCreate with one reference and PRO_DIM_ORNT_NONE; let Creo apply its drawing-standard dimension style."));

    result.has_supported_candidate = selections.same_view;
    LogLine(log_sink,
            "smart-dim inference single-reference candidates=%d same_view=%d supported=%d",
            static_cast<int>(result.candidates.size()),
            selections.same_view ? 1 : 0,
            result.has_supported_candidate ? 1 : 0);
    return result;
}

} // namespace autobbox::application
