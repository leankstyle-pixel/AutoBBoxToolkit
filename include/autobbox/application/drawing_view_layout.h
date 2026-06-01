#pragma once

#include "autobbox/core/dwg3_types.h"

#include <ProDrawing.h>
#include <ProToolkit.h>
#include <ProView.h>

#include <array>

namespace autobbox::application {

std::array<core::Dwg3ViewType, core::kDwg3ViewCount> AllDwg3ViewTypes();

double ComputeDrawingGroupScale(ProDrawing drawing, int sheet);
core::Dwg3Spacing ComputeDrawingSpacing(double page_scale);

ProView &CreatedViewSlot(core::Dwg3CreatedViews &views, core::Dwg3ViewType type);
ProView CreatedViewSlot(const core::Dwg3CreatedViews &views, core::Dwg3ViewType type);

void GetViewOriginOffset(const core::Dwg3Spacing &spacing, core::Dwg3ViewType type, ProPoint3d offset);
void GetViewOriginOffset(const core::Dwg3Spacing &spacing,
                         core::Dwg3ViewType type,
                         core::Dwg3ProjectionType projection_type,
                         ProPoint3d offset);
void MakeViewOrigin(const ProPoint3d base_origin,
                    const core::Dwg3Spacing &spacing,
                    core::Dwg3ViewType type,
                    ProPoint3d origin);
void MakeViewOrigin(const ProPoint3d base_origin,
                    const core::Dwg3Spacing &spacing,
                    core::Dwg3ViewType type,
                    core::Dwg3ProjectionType projection_type,
                    ProPoint3d origin);

ProError GetDrawingViewOutlineBox(ProDrawing drawing, ProView view, core::Dwg3GroupOutline &outline);
ProError GetDrawingViewGroupOutline(ProDrawing drawing,
                                    const core::Dwg3CreatedViews &views,
                                    core::Dwg3GroupOutline &outline);
ProError MoveCreatedViewsByScreenDelta(ProDrawing drawing,
                                       core::Dwg3CreatedViews &views,
                                       core::Dwg3ViewMask mask,
                                       double dx,
                                       double dy);

double OutlineWidth(const core::Dwg3GroupOutline &outline);
double OutlineHeight(const core::Dwg3GroupOutline &outline);
double OutlineCenterX(const core::Dwg3GroupOutline &outline);
double OutlineCenterY(const core::Dwg3GroupOutline &outline);

ProError PackCreatedViewsByOutline(ProDrawing drawing,
                                   const core::Dwg3CreatedViews &all_views,
                                   core::Dwg3ViewMask move_mask,
                                   double page_scale);
ProError PackCreatedViewsByOutline(ProDrawing drawing,
                                   const core::Dwg3CreatedViews &all_views,
                                   core::Dwg3ViewMask move_mask,
                                   double page_scale,
                                   core::Dwg3ProjectionType projection_type);

core::Dwg3GroupOutline ExpandDecoratedOutline(const core::Dwg3GroupOutline &screen_outline,
                                              double page_scale,
                                              bool include_title);

} // namespace autobbox::application
