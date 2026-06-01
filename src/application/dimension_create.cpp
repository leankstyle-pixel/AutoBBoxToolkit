#include "autobbox/application/dimension_create.h"

#include "autobbox/application/ordinate_mode.h"

#include <ProArray.h>
#include <ProAnnotation.h>
#include <ProDimension.h>
#include <ProDrawing.h>
#include <ProSelection.h>

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

ProError CreateDrawingDimensionWithOfficialApi(ProDrawing drawing,
                                               ProDimAttachment *attachments_arr,
                                               ProDimSense *dsense_arr,
                                               ProDimOrient orient_hint,
                                               ProVector location,
                                               ProBoolean ref_dim,
                                               ProDimension *dimension)
{
    return ProDrawingDimensionCreate(
        drawing,
        attachments_arr,
        dsense_arr,
        orient_hint,
        location,
        ref_dim,
        dimension);
}

ProError ShowDimensionAnnotationWithOfficialApi(ProDimension dimension,
                                                ProAsmcomppath *comp_path,
                                                ProView view)
{
    return ProAnnotationShow(reinterpret_cast<ProAnnotation *>(&dimension), comp_path, view);
}

ProDimOrient OrientForCandidate(core::SmartDimensionKind kind)
{
    switch (kind) {
    case core::SmartDimensionKind::Aligned:
        return PRO_DIM_ORNT_SLANTED;
    case core::SmartDimensionKind::Angle:
        return PRO_DIM_ORNT_NONE;
    case core::SmartDimensionKind::Linear:
    default:
        return PRO_DIM_ORNT_NONE;
    }
}

} // namespace

core::SmartDimensionCreateResult CreateSmartDimension(const core::SmartDimensionCreateInput &input,
                                                      const Drawing3LogSink &log_sink)
{
    if (input.candidate.kind == core::SmartDimensionKind::AutoOrdinate) {
        return TryCreateAutoOrdinateDimension(input, log_sink);
    }

    core::SmartDimensionCreateResult result = {};
    result.note = L"Dimension creation was not completed.";

    if (!input.ready_for_official_create) {
        LogLine(log_sink, "smart-dim create skipped reason=not-ready");
        return result;
    }

    if (input.drawing == nullptr ||
        input.selections.first.selection == nullptr ||
        input.placement.view == nullptr) {
        result.note = L"Dimension creation failed: missing reference or view.";
        LogLine(log_sink, "smart-dim create skipped reason=bad-inputs");
        return result;
    }

    const bool has_second_reference = input.selections.second.selection != nullptr;
    const int reference_count = has_second_reference ? 2 : 1;

    ProDimAttachment *attachments_arr = nullptr;
    ProDimSense *dsense_arr = nullptr;
    ProDimension dimension = {};
    ProVector create_location = {0.0, 0.0, 0.0};

    create_location[0] = input.placement.location[0];
    create_location[1] = input.placement.location[1];
    create_location[2] = input.placement.location[2];

    ProError alloc_attach_status =
        ProArrayAlloc(reference_count, sizeof(ProDimAttachment), 1, reinterpret_cast<ProArray *>(&attachments_arr));
    ProError alloc_sense_status =
        ProArrayAlloc(reference_count, sizeof(ProDimSense), 1, reinterpret_cast<ProArray *>(&dsense_arr));
    if (alloc_attach_status != PRO_TK_NO_ERROR ||
        alloc_sense_status != PRO_TK_NO_ERROR ||
        attachments_arr == nullptr ||
        dsense_arr == nullptr) {
        result.note = L"Dimension creation failed: attachment array allocation failed.";
        LogLine(log_sink,
                "smart-dim create alloc failed attach=%d sense=%d refs=%d",
                static_cast<int>(alloc_attach_status),
                static_cast<int>(alloc_sense_status),
                reference_count);
        if (attachments_arr != nullptr) {
            ProDimattachmentarrayFree(attachments_arr);
        }
        if (dsense_arr != nullptr) {
            ProArrayFree(reinterpret_cast<ProArray *>(&dsense_arr));
        }
        return result;
    }

    for (int i = 0; i < reference_count; ++i) {
        dsense_arr[i] = {};
        dsense_arr[i].type = PRO_DIM_SNS_TYP_NONE;
        dsense_arr[i].orient_hint = i == 0 ? OrientForCandidate(input.candidate.kind) : PRO_DIM_ORNT_NONE;
    }

    ProError copy_first_status = ProSelectionCopy(input.selections.first.selection, &attachments_arr[0][0]);
    attachments_arr[0][1] = nullptr;

    ProError copy_second_status = PRO_TK_NO_ERROR;
    if (has_second_reference) {
        copy_second_status = ProSelectionCopy(input.selections.second.selection, &attachments_arr[1][0]);
        attachments_arr[1][1] = nullptr;
    }

    if (copy_first_status != PRO_TK_NO_ERROR || copy_second_status != PRO_TK_NO_ERROR) {
        result.note = L"Dimension creation failed: could not copy references.";
        LogLine(log_sink,
                "smart-dim create selection-copy failed first=%d second=%d refs=%d",
                static_cast<int>(copy_first_status),
                static_cast<int>(copy_second_status),
                reference_count);
        ProDimattachmentarrayFree(attachments_arr);
        ProArrayFree(reinterpret_cast<ProArray *>(&dsense_arr));
        return result;
    }

    // Official sample evidence:
    // D:\Program Files\PTC\Creo 10.0.8.0\Common Files\protoolkit\protk_appls\pt_examples\pt_dbase\TestDimension.c
    // ProTestDimStandardCreate() calls ProDrawingDimensionCreate(...) followed by ProAnnotationShow(...).
    result.create_status = CreateDrawingDimensionWithOfficialApi(
        input.drawing,
        attachments_arr,
        dsense_arr,
        PRO_DIM_ORNT_NONE,
        create_location,
        PRO_B_FALSE,
        &dimension);

    LogLine(log_sink,
            "smart-dim create status=%d refs=%d kind=%d orient0=%d loc=(%.3f,%.3f,%.3f)",
            static_cast<int>(result.create_status),
            reference_count,
            static_cast<int>(input.candidate.kind),
            static_cast<int>(dsense_arr[0].orient_hint),
            create_location[0],
            create_location[1],
            create_location[2]);

    if (result.create_status != PRO_TK_NO_ERROR) {
        result.note = L"Dimension creation failed: Creo rejected the current reference.";
        ProDimattachmentarrayFree(attachments_arr);
        ProArrayFree(reinterpret_cast<ProArray *>(&dsense_arr));
        return result;
    }

    result.dimension = dimension;
    result.created = true;
    result.show_status = ShowDimensionAnnotationWithOfficialApi(dimension, nullptr, input.placement.view);
    result.shown =
        result.show_status == PRO_TK_NO_ERROR || result.show_status == PRO_TK_NO_CHANGE;
    result.note = result.shown ? L"Dimension created and shown." : L"Dimension was created, but annotation show failed.";

    ProDimattachmentarrayFree(attachments_arr);
    ProArrayFree(reinterpret_cast<ProArray *>(&dsense_arr));
    return result;
}

ProError MoveSmartDimension(ProDrawing drawing,
                            ProDimension *dimension,
                            const core::SmartDimensionPlacement &placement,
                            const Drawing3LogSink &log_sink)
{
    if (drawing == nullptr || dimension == nullptr || !placement.confirmed) {
        return PRO_TK_BAD_INPUTS;
    }

    ProVector location = {placement.location[0], placement.location[1], placement.location[2]};
    const ProError status = ProDrawingDimensionMove(drawing, dimension, location);
    LogLine(log_sink,
            "smart-dim move status=%d loc=(%.3f,%.3f,%.3f)",
            static_cast<int>(status),
            location[0],
            location[1],
            location[2]);
    return status;
}

ProError DeleteSmartDimension(ProDimension *dimension,
                              const Drawing3LogSink &log_sink)
{
    if (dimension == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    const ProError status = ProDimensionDelete(dimension);
    LogLine(log_sink, "smart-dim delete-preview status=%d", static_cast<int>(status));
    return status;
}

} // namespace autobbox::application
