#include "autobbox/application/selection_capture.h"

#include <ProSelection.h>

#include <cstdarg>
#include <cstdio>
#include <string>

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

void PopulateReference(ProSelection selection, core::SmartDimensionReference &reference)
{
    reference.selection = selection;

    // VERIFY_WITH_OFFICIAL_DOC: helper signatures already appear in project usage,
    // but the smart-dimension flow still needs an explicit final verification pass.
    if (selection != nullptr) {
        if (ProSelectionViewGet(selection, &reference.view) == PRO_TK_NO_ERROR) {
            reference.view = reference.view;
        }
        if (ProSelectionModelitemGet(selection, &reference.model_item) == PRO_TK_NO_ERROR) {
            reference.has_model_item = true;
        }
    }
}

bool IsSupportedSingleReference(const core::SmartDimensionReference &reference)
{
    if (!reference.has_model_item) {
        return false;
    }

    const ProType type = reference.model_item.type;
    const bool is_silhouette_edge =
        type >= PRO_SILH_EDGE && type <= PRO_SILH_EDGE_MAX;

    return type == PRO_EDGE ||
           type == PRO_CURVE ||
           type == PRO_SURFACE ||
           is_silhouette_edge;
}

} // namespace

core::SmartDimensionSelectionSet CaptureSmartDimensionReferences(const Drawing3LogSink &log_sink)
{
    core::SmartDimensionSelectionSet result = {};

    // Official sample evidence:
    // D:\Program Files\PTC\Creo 10.0.8.0\Common Files\protoolkit\protk_appls\pt_examples\pt_dbase\TestDimension.c
    // ProTestDimStandardCreate() uses this filter before ProDrawingDimensionCreate().
    // Keep V1 restricted to geometry that Creo's own drawing-dimension sample
    // accepts, but include solid/surface face aliases so rounded/tangent drawing
    // view edges are not rejected when Creo reports them as surface selections.
    // Official sample evidence for these filter names:
    // - TestDimension.c uses point,dtl_axis,datum,csys,edge,curve,surface for drawing dimensions.
    // - TestSelect_c.html maps comp_crv to PRO_CURVE, and sldedge/qltedge to PRO_EDGE.
    // - TestSelect_c.html maps surface/sldface/qltface to PRO_SURFACE.
    // - ProObjects.h defines PRO_SILH_EDGE..PRO_SILH_EDGE_MAX for silhouette
    //   edges, and Drawings.html documents drawing model edges as regular,
    //   silhouette, or non-analytical silhouette edges.
    // Do not include points, edge_end, datum, axis, or csys in this single-reference
    // command because the user rejected extra/helper object creation.
    char selection_filter[] = "edge,curve,comp_crv,sldedge,qltedge,surface,sldface,qltface";
    ProSelection *selection_array = nullptr;
    int selection_count = 0;

    result.status =
        ProSelect(selection_filter, 1, nullptr, nullptr, nullptr, nullptr, &selection_array, &selection_count);

    LogLine(log_sink,
            "smart-dim select status=%d count=%d filter='%s'",
            static_cast<int>(result.status),
            selection_count,
            selection_filter);

    if (result.status == PRO_TK_USER_ABORT ||
        selection_count == 0 ||
        selection_array == nullptr) {
        result.cancelled = true;
        return result;
    }

    if (result.status != PRO_TK_NO_ERROR || selection_count < 1) {
        return result;
    }

    PopulateReference(selection_array[0], result.first);
    LogLine(log_sink,
            "smart-dim selected modelitem has=%d type=%d id=%d owner=%p view=%p",
            result.first.has_model_item ? 1 : 0,
            result.first.has_model_item ? static_cast<int>(result.first.model_item.type) : -1,
            result.first.has_model_item ? result.first.model_item.id : -1,
            result.first.has_model_item ? result.first.model_item.owner : nullptr,
            result.first.view);

    if (!IsSupportedSingleReference(result.first)) {
        LogLine(log_sink,
                "smart-dim select rejected reason=unsupported-single-reference type=%d",
                result.first.has_model_item ? static_cast<int>(result.first.model_item.type) : -1);
        result.status = PRO_TK_BAD_DIM_ATTACH;
        result.complete = false;
        return result;
    }

    result.same_view = result.first.view != nullptr;
    result.complete = true;
    return result;
}

} // namespace autobbox::application
