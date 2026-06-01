#include "autobbox/application/drawing_view_layout.h"

#include <ProDrawingView.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace autobbox::application {

namespace {

ProError MoveDrawingViewByVector(ProDrawing drawing, ProView view, double dx, double dy)
{
    if (drawing == nullptr || view == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }
    ProVector move_vector = {dx, dy, 0.0};
    return ProDrawingViewMove(drawing, view, move_vector);
}

ProError MoveDrawingViewCenterTo(ProDrawing drawing,
                                 ProView view,
                                 double target_center_x,
                                 double target_center_y,
                                 core::Dwg3GroupOutline &updated_outline)
{
    core::Dwg3GroupOutline current = {};
    ProError st = GetDrawingViewOutlineBox(drawing, view, current);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }

    const double dx = target_center_x - OutlineCenterX(current);
    const double dy = target_center_y - OutlineCenterY(current);
    st = MoveDrawingViewByVector(drawing, view, dx, dy);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }

    return GetDrawingViewOutlineBox(drawing, view, updated_outline);
}

} // namespace

std::array<core::Dwg3ViewType, core::kDwg3ViewCount> AllDwg3ViewTypes()
{
    return {
        core::Dwg3ViewType::Front,
        core::Dwg3ViewType::Right,
        core::Dwg3ViewType::Left,
        core::Dwg3ViewType::Top,
        core::Dwg3ViewType::Bottom,
        core::Dwg3ViewType::Back,
        core::Dwg3ViewType::Iso
    };
}

double ComputeDrawingGroupScale(ProDrawing drawing, int sheet)
{
    double scale = 1.0;
    if (drawing != nullptr) {
        double page_scale = 1.0;
        if (ProDrawingScaleGet(drawing, nullptr, sheet, &page_scale) == PRO_TK_NO_ERROR &&
            page_scale > 0.0 &&
            std::isfinite(page_scale)) {
            scale = page_scale;
        }
    }
    if (!(scale > 0.0) || !std::isfinite(scale)) {
        scale = 1.0;
    }
    return scale;
}

core::Dwg3Spacing ComputeDrawingSpacing(double page_scale)
{
    core::Dwg3Spacing spacing;
    const double safe_scale = (page_scale > 0.0 && std::isfinite(page_scale)) ? page_scale : 0.015;
    const double factor = std::max(1.0, safe_scale / 0.015);

    spacing.side_dx = 85.0 * factor;
    spacing.iso_dx = 190.0 * factor;
    spacing.vertical_dy = 85.0 * factor;
    spacing.gap_x = 22.0 * factor;
    return spacing;
}

ProView &CreatedViewSlot(core::Dwg3CreatedViews &views, core::Dwg3ViewType type)
{
    return views.items[core::Dwg3ViewIndex(type)];
}

ProView CreatedViewSlot(const core::Dwg3CreatedViews &views, core::Dwg3ViewType type)
{
    return views.items[core::Dwg3ViewIndex(type)];
}

void GetViewOriginOffset(const core::Dwg3Spacing &spacing, core::Dwg3ViewType type, ProPoint3d offset)
{
    GetViewOriginOffset(spacing, type, core::Dwg3ProjectionType::FirstAngle, offset);
}

void GetViewOriginOffset(const core::Dwg3Spacing &spacing,
                         core::Dwg3ViewType type,
                         core::Dwg3ProjectionType projection_type,
                         ProPoint3d offset)
{
    offset[0] = 0.0;
    offset[1] = 0.0;
    offset[2] = 0.0;

    const bool third_angle = (projection_type == core::Dwg3ProjectionType::ThirdAngle);

    switch (type) {
    case core::Dwg3ViewType::Front:
        break;
    case core::Dwg3ViewType::Right:
        offset[0] = third_angle ? spacing.side_dx : -spacing.side_dx;
        break;
    case core::Dwg3ViewType::Left:
        offset[0] = third_angle ? -spacing.side_dx : spacing.side_dx;
        break;
    case core::Dwg3ViewType::Top:
        offset[1] = third_angle ? spacing.vertical_dy : -spacing.vertical_dy;
        break;
    case core::Dwg3ViewType::Bottom:
        offset[1] = third_angle ? -spacing.vertical_dy : spacing.vertical_dy;
        break;
    case core::Dwg3ViewType::Back:
        offset[1] = third_angle ? spacing.vertical_dy * 2.0 : -spacing.vertical_dy * 2.0;
        break;
    case core::Dwg3ViewType::Iso:
        offset[0] = spacing.iso_dx;
        break;
    default:
        break;
    }
}

void MakeViewOrigin(const ProPoint3d base_origin,
                    const core::Dwg3Spacing &spacing,
                    core::Dwg3ViewType type,
                    ProPoint3d origin)
{
    MakeViewOrigin(base_origin, spacing, type, core::Dwg3ProjectionType::FirstAngle, origin);
}

void MakeViewOrigin(const ProPoint3d base_origin,
                    const core::Dwg3Spacing &spacing,
                    core::Dwg3ViewType type,
                    core::Dwg3ProjectionType projection_type,
                    ProPoint3d origin)
{
    ProPoint3d offset = {0.0, 0.0, 0.0};
    GetViewOriginOffset(spacing, type, projection_type, offset);
    origin[0] = base_origin[0] + offset[0];
    origin[1] = base_origin[1] + offset[1];
    origin[2] = 0.0;
}

ProError GetDrawingViewOutlineBox(ProDrawing drawing, ProView view, core::Dwg3GroupOutline &outline)
{
    if (drawing == nullptr || view == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    ProPoint3d corners[2] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
    const ProError st = ProDrawingViewOutlineGet(drawing, view, corners);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }

    outline.min_x = std::min(corners[0][0], corners[1][0]);
    outline.min_y = std::min(corners[0][1], corners[1][1]);
    outline.max_x = std::max(corners[0][0], corners[1][0]);
    outline.max_y = std::max(corners[0][1], corners[1][1]);
    return PRO_TK_NO_ERROR;
}

ProError GetDrawingViewGroupOutline(ProDrawing drawing,
                                    const core::Dwg3CreatedViews &views,
                                    core::Dwg3GroupOutline &outline)
{
    bool has_any = false;
    for (core::Dwg3ViewType type : AllDwg3ViewTypes()) {
        const ProView view = CreatedViewSlot(views, type);
        if (view == nullptr) {
            continue;
        }

        core::Dwg3GroupOutline current = {};
        const ProError st = GetDrawingViewOutlineBox(drawing, view, current);
        if (st != PRO_TK_NO_ERROR) {
            return st;
        }

        if (!has_any) {
            outline = current;
            has_any = true;
        } else {
            outline.min_x = std::min(outline.min_x, current.min_x);
            outline.min_y = std::min(outline.min_y, current.min_y);
            outline.max_x = std::max(outline.max_x, current.max_x);
            outline.max_y = std::max(outline.max_y, current.max_y);
        }
    }
    return has_any ? PRO_TK_NO_ERROR : PRO_TK_E_NOT_FOUND;
}

ProError MoveCreatedViewsByScreenDelta(ProDrawing drawing,
                                       core::Dwg3CreatedViews &views,
                                       core::Dwg3ViewMask mask,
                                       double dx,
                                       double dy)
{
    for (core::Dwg3ViewType type : AllDwg3ViewTypes()) {
        if ((mask & core::Dwg3ViewBit(type)) == 0) {
            continue;
        }
        ProView view = CreatedViewSlot(views, type);
        if (view == nullptr) {
            continue;
        }
        const ProError st = MoveDrawingViewByVector(drawing, view, dx, dy);
        if (st != PRO_TK_NO_ERROR) {
            return st;
        }
    }
    return PRO_TK_NO_ERROR;
}

double OutlineWidth(const core::Dwg3GroupOutline &outline)
{
    return outline.max_x - outline.min_x;
}

double OutlineHeight(const core::Dwg3GroupOutline &outline)
{
    return outline.max_y - outline.min_y;
}

double OutlineCenterX(const core::Dwg3GroupOutline &outline)
{
    return (outline.min_x + outline.max_x) * 0.5;
}

double OutlineCenterY(const core::Dwg3GroupOutline &outline)
{
    return (outline.min_y + outline.max_y) * 0.5;
}

ProError PackCreatedViewsByOutline(ProDrawing drawing,
                                   const core::Dwg3CreatedViews &all_views,
                                   core::Dwg3ViewMask move_mask,
                                   double page_scale)
{
    return PackCreatedViewsByOutline(
        drawing,
        all_views,
        move_mask,
        page_scale,
        core::Dwg3ProjectionType::FirstAngle);
}

ProError PackCreatedViewsByOutline(ProDrawing drawing,
                                   const core::Dwg3CreatedViews &all_views,
                                   core::Dwg3ViewMask move_mask,
                                   double page_scale,
                                   core::Dwg3ProjectionType projection_type)
{
    const ProView front_view = CreatedViewSlot(all_views, core::Dwg3ViewType::Front);
    if (drawing == nullptr || front_view == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    std::array<core::Dwg3GroupOutline, core::kDwg3ViewCount> outlines = {};
    std::array<bool, core::kDwg3ViewCount> has_outline = {};
    for (core::Dwg3ViewType type : AllDwg3ViewTypes()) {
        const ProView view = CreatedViewSlot(all_views, type);
        if (view == nullptr) {
            continue;
        }
        if (GetDrawingViewOutlineBox(drawing, view, outlines[core::Dwg3ViewIndex(type)]) == PRO_TK_NO_ERROR) {
            has_outline[core::Dwg3ViewIndex(type)] = true;
        }
    }

    if (!has_outline[core::Dwg3ViewIndex(core::Dwg3ViewType::Front)]) {
        return PRO_TK_NO_ERROR;
    }

    const double factor = std::max(1.0, page_scale / 0.015);
    const double gap_x = 22.0 * factor;
    const double gap_y = 20.0 * factor;
    const double iso_gap_x = 28.0 * factor;

    auto try_move = [&](core::Dwg3ViewType type, double target_center_x, double target_center_y) -> ProError {
        if ((move_mask & core::Dwg3ViewBit(type)) == 0) {
            return PRO_TK_NO_ERROR;
        }
        ProView view = CreatedViewSlot(all_views, type);
        if (view == nullptr) {
            return PRO_TK_NO_ERROR;
        }
        core::Dwg3GroupOutline updated = {};
        const ProError st = MoveDrawingViewCenterTo(drawing, view, target_center_x, target_center_y, updated);
        if (st == PRO_TK_NO_ERROR) {
            outlines[core::Dwg3ViewIndex(type)] = updated;
            has_outline[core::Dwg3ViewIndex(type)] = true;
        }
        return st;
    };

    const core::Dwg3GroupOutline &front = outlines[core::Dwg3ViewIndex(core::Dwg3ViewType::Front)];
    const bool third_angle = (projection_type == core::Dwg3ProjectionType::ThirdAngle);

    if (has_outline[core::Dwg3ViewIndex(core::Dwg3ViewType::Right)]) {
        const core::Dwg3GroupOutline &right = outlines[core::Dwg3ViewIndex(core::Dwg3ViewType::Right)];
        const ProError st = try_move(
            core::Dwg3ViewType::Right,
            third_angle
                ? front.max_x + gap_x + OutlineWidth(right) * 0.5
                : front.min_x - gap_x - OutlineWidth(right) * 0.5,
            OutlineCenterY(front));
        if (st != PRO_TK_NO_ERROR) {
            return st;
        }
    }

    if (has_outline[core::Dwg3ViewIndex(core::Dwg3ViewType::Left)]) {
        const core::Dwg3GroupOutline &left = outlines[core::Dwg3ViewIndex(core::Dwg3ViewType::Left)];
        const ProError st = try_move(
            core::Dwg3ViewType::Left,
            third_angle
                ? front.min_x - gap_x - OutlineWidth(left) * 0.5
                : front.max_x + gap_x + OutlineWidth(left) * 0.5,
            OutlineCenterY(front));
        if (st != PRO_TK_NO_ERROR) {
            return st;
        }
    }

    if (has_outline[core::Dwg3ViewIndex(core::Dwg3ViewType::Top)]) {
        const core::Dwg3GroupOutline &top = outlines[core::Dwg3ViewIndex(core::Dwg3ViewType::Top)];
        const ProError st = try_move(
            core::Dwg3ViewType::Top,
            OutlineCenterX(front),
            third_angle
                ? front.max_y + gap_y + OutlineHeight(top) * 0.5
                : front.min_y - gap_y - OutlineHeight(top) * 0.5);
        if (st != PRO_TK_NO_ERROR) {
            return st;
        }
    }

    if (has_outline[core::Dwg3ViewIndex(core::Dwg3ViewType::Bottom)]) {
        const core::Dwg3GroupOutline &bottom = outlines[core::Dwg3ViewIndex(core::Dwg3ViewType::Bottom)];
        const ProError st = try_move(
            core::Dwg3ViewType::Bottom,
            OutlineCenterX(front),
            third_angle
                ? front.min_y - gap_y - OutlineHeight(bottom) * 0.5
                : front.max_y + gap_y + OutlineHeight(bottom) * 0.5);
        if (st != PRO_TK_NO_ERROR) {
            return st;
        }
    }

    if (has_outline[core::Dwg3ViewIndex(core::Dwg3ViewType::Back)]) {
        core::Dwg3GroupOutline back_base = front;
        if (has_outline[core::Dwg3ViewIndex(core::Dwg3ViewType::Top)]) {
            back_base = outlines[core::Dwg3ViewIndex(core::Dwg3ViewType::Top)];
        }
        const core::Dwg3GroupOutline &back = outlines[core::Dwg3ViewIndex(core::Dwg3ViewType::Back)];
        const ProError st = try_move(
            core::Dwg3ViewType::Back,
            OutlineCenterX(back_base),
            third_angle
                ? back_base.max_y + gap_y + OutlineHeight(back) * 0.5
                : back_base.min_y - gap_y - OutlineHeight(back) * 0.5);
        if (st != PRO_TK_NO_ERROR) {
            return st;
        }
    }

    if (has_outline[core::Dwg3ViewIndex(core::Dwg3ViewType::Iso)]) {
        double right_edge = front.max_x;
        if (has_outline[core::Dwg3ViewIndex(core::Dwg3ViewType::Left)]) {
            right_edge = std::max(right_edge, outlines[core::Dwg3ViewIndex(core::Dwg3ViewType::Left)].max_x);
        }
        if (has_outline[core::Dwg3ViewIndex(core::Dwg3ViewType::Right)]) {
            right_edge = std::max(right_edge, outlines[core::Dwg3ViewIndex(core::Dwg3ViewType::Right)].max_x);
        }
        const core::Dwg3GroupOutline &iso = outlines[core::Dwg3ViewIndex(core::Dwg3ViewType::Iso)];
        const ProError st = try_move(
            core::Dwg3ViewType::Iso,
            right_edge + iso_gap_x + OutlineWidth(iso) * 0.5,
            OutlineCenterY(front));
        if (st != PRO_TK_NO_ERROR) {
            return st;
        }
    }

    return PRO_TK_NO_ERROR;
}

core::Dwg3GroupOutline ExpandDecoratedOutline(const core::Dwg3GroupOutline &screen_outline,
                                              double page_scale,
                                              bool include_title)
{
    core::Dwg3GroupOutline out = screen_outline;
    const double factor = std::max(1.0, page_scale / 0.015);
    const double frame_pad_x = 18.0 * factor;
    const double frame_pad_y = 16.0 * factor;
    const double title_gap_y = include_title ? (28.0 * factor) : 0.0;

    out.min_x -= frame_pad_x;
    out.max_x += frame_pad_x;
    out.min_y -= frame_pad_y;
    out.max_y += frame_pad_y + title_gap_y;
    return out;
}

} // namespace autobbox::application
