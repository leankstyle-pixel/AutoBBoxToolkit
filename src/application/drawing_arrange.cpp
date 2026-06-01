#include "autobbox/application/drawing_arrange.h"

#include "autobbox/application/drawing_view_layout.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/creo/parameter_api.h"

#include <ProArray.h>
#include <ProCurvedata.h>
#include <ProDtlattach.h>
#include <ProDtlentity.h>
#include <ProDtlnote.h>
#include <ProDrawingView.h>
#include <ProMdl.h>
#include <ProSolid.h>
#include <ProSelbuffer.h>
#include <ProSelection.h>
#include <ProToolkit.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cwctype>

namespace autobbox::application {

namespace {

struct SelectedDrawingView {
    ProView view = nullptr;
    std::wstring name;
    ProPoint3d origin = {0.0, 0.0, 0.0};
    core::Dwg3GroupOutline outline = {};
    int selection_order = 0;
};

struct ArrangeGroupCandidate {
    size_t main_index = 0;
    std::array<int, core::kDwg3ViewCount> slot_to_view = {};
    int matched_count = 0;
    double score = -1.0e18;
    double total_cost = 0.0;
    bool valid = false;
};

struct PreparedArrangeGroup {
    ArrangeGroupCandidate group = {};
    core::Dwg3CreatedViews arranged = {};
    ProView anchor_view = nullptr;
    core::Dwg3ViewMask full_mask = 0;
    core::Dwg3GroupOutline anchor_outline = {};
    core::Dwg3GroupOutline packed_outline = {};
    int first_selection_order = 0;
};

struct ParsedArrangeViewName {
    std::string group_key;
    core::Dwg3ViewType type = core::Dwg3ViewType::Front;
    bool valid = false;
};

struct DraftLineEntity {
    ProDtlentity entity = {};
    ProView view = nullptr;
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
};

struct DraftTitleNote {
    ProDtlnote note = {};
    ProView view = nullptr;
    ProVector location = {0.0, 0.0, 0.0};
    std::wstring text;
};

template <typename T, size_t N>
void CopyWStr(T (&dest)[N], const wchar_t *src)
{
    if (N == 0) {
        return;
    }
    dest[0] = L'\0';
    if (src == nullptr) {
        return;
    }
    wcsncpy_s(dest, N, src, _TRUNCATE);
    dest[N - 1] = L'\0';
}

void LogLine(const Drawing3LogSink &log_sink, const char *fmt, ...)
{
    if (!log_sink || fmt == nullptr) {
        return;
    }

    char buffer[4096] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_sink(buffer);
}

std::string ViewNameForLog(const SelectedDrawingView &view)
{
    if (!view.name.empty()) {
        return autobbox::common::WToA(view.name.c_str());
    }
    return std::string("<unnamed>");
}

bool SameView(ProView lhs, ProView rhs)
{
    return reinterpret_cast<std::uintptr_t>(lhs) == reinterpret_cast<std::uintptr_t>(rhs);
}

bool Near(double lhs, double rhs, double tol)
{
    return std::abs(lhs - rhs) <= tol;
}

bool SpanMatches(double a1, double a2, double b1, double b2, double tol)
{
    const double amin = std::min(a1, a2);
    const double amax = std::max(a1, a2);
    const double bmin = std::min(b1, b2);
    const double bmax = std::max(b1, b2);
    return Near(amin, bmin, tol) && Near(amax, bmax, tol);
}

std::string NormalizeUpperAscii(const std::wstring &value)
{
    std::string raw = autobbox::common::WToA(value.c_str());
    std::string out;
    out.reserve(raw.size());
    for (unsigned char ch : raw) {
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::toupper(ch)));
        } else {
            out.push_back('_');
        }
    }
    return out;
}

const char *ArrangeSlotName(core::Dwg3ViewType type)
{
    switch (type) {
    case core::Dwg3ViewType::Front:
        return "front";
    case core::Dwg3ViewType::Right:
        return "right";
    case core::Dwg3ViewType::Left:
        return "left";
    case core::Dwg3ViewType::Top:
        return "top";
    case core::Dwg3ViewType::Bottom:
        return "bottom";
    case core::Dwg3ViewType::Back:
        return "back";
    case core::Dwg3ViewType::Iso:
        return "iso";
    default:
        return "view";
    }
}

bool TryParseArrangeViewType(const std::string &token, core::Dwg3ViewType &type_out)
{
    if (token == "FRONT") {
        type_out = core::Dwg3ViewType::Front;
        return true;
    }
    if (token == "RIGHT") {
        type_out = core::Dwg3ViewType::Right;
        return true;
    }
    if (token == "LEFT") {
        type_out = core::Dwg3ViewType::Left;
        return true;
    }
    if (token == "TOP") {
        type_out = core::Dwg3ViewType::Top;
        return true;
    }
    if (token == "BOTTOM") {
        type_out = core::Dwg3ViewType::Bottom;
        return true;
    }
    if (token == "BACK") {
        type_out = core::Dwg3ViewType::Back;
        return true;
    }
    if (token == "ISO") {
        type_out = core::Dwg3ViewType::Iso;
        return true;
    }
    return false;
}

ParsedArrangeViewName ParseArrangeViewName(const std::wstring &name)
{
    ParsedArrangeViewName parsed = {};
    const std::string upper = NormalizeUpperAscii(name);
    if (upper.rfind("AB_", 0) != 0) {
        return parsed;
    }

    const size_t last_sep = upper.find_last_of('_');
    if (last_sep == std::string::npos || last_sep <= 3 || last_sep + 1 >= upper.size()) {
        return parsed;
    }

    core::Dwg3ViewType type = core::Dwg3ViewType::Front;
    if (!TryParseArrangeViewType(upper.substr(last_sep + 1), type)) {
        return parsed;
    }

    parsed.group_key = upper.substr(0, last_sep);
    parsed.type = type;
    parsed.valid = !parsed.group_key.empty();
    return parsed;
}

bool NameHintsSlot(const std::wstring &name, core::Dwg3ViewType type)
{
    const std::string upper = NormalizeUpperAscii(name);
    if (upper.empty()) {
        return false;
    }

    const auto contains = [&](const char *token) {
        return token != nullptr && upper.find(token) != std::string::npos;
    };

    switch (type) {
    case core::Dwg3ViewType::Front:
        return contains("FRONT") || contains("MAIN");
    case core::Dwg3ViewType::Right:
        return contains("RIGHT");
    case core::Dwg3ViewType::Left:
        return contains("LEFT");
    case core::Dwg3ViewType::Top:
        return contains("TOP") || contains("UP");
    case core::Dwg3ViewType::Bottom:
        return contains("BOTTOM") || contains("DOWN");
    case core::Dwg3ViewType::Back:
        return contains("BACK");
    case core::Dwg3ViewType::Iso:
        return contains("ISO");
    default:
        return false;
    }
}

double SlotToleranceX(core::Dwg3ViewType type, const core::Dwg3Spacing &spacing)
{
    switch (type) {
    case core::Dwg3ViewType::Iso:
        return std::max(24.0, spacing.iso_dx * 0.45);
    default:
        return std::max(18.0, spacing.side_dx * 0.55);
    }
}

double SlotToleranceY(core::Dwg3ViewType type, const core::Dwg3Spacing &spacing)
{
    switch (type) {
    case core::Dwg3ViewType::Back:
        return std::max(18.0, spacing.vertical_dy * 0.65);
    default:
        return std::max(18.0, spacing.vertical_dy * 0.55);
    }
}

bool TryCollectSelectedDrawingViews(ProDrawing drawing,
                                    int sheet,
                                    std::vector<SelectedDrawingView> &views,
                                    int &selection_count,
                                    const Drawing3LogSink &log_sink)
{
    views.clear();
    selection_count = 0;

    ProSelection *buffer = nullptr;
    const ProError st = ProSelbufferSelectionsGet(&buffer);
    if (st == PRO_TK_E_NOT_FOUND) {
        LogLine(log_sink, "ArrangeDwgViews selection_buffer empty");
        return true;
    }
    if (st != PRO_TK_NO_ERROR) {
        LogLine(log_sink, "FAIL arrange-dwgviews reason=selection-buffer status=%d", static_cast<int>(st));
        return false;
    }

    std::unordered_set<std::uintptr_t> seen;
    int count = 0;
    ProArraySizeGet(reinterpret_cast<ProArray>(buffer), &count);
    selection_count = count;
    views.reserve(static_cast<size_t>(count));

    for (int i = 0; i < count; ++i) {
        ProView view = nullptr;
        const ProError st_view = ProSelectionViewGet(buffer[i], &view);
        if (st_view != PRO_TK_NO_ERROR || view == nullptr) {
            LogLine(log_sink,
                    "SKIP arrange-dwgviews selection=%d reason=no-view status=%d",
                    i,
                    static_cast<int>(st_view));
            continue;
        }

        const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(view);
        if (!seen.insert(key).second) {
            LogLine(log_sink, "SKIP arrange-dwgviews selection=%d reason=duplicate-view", i);
            continue;
        }

        int view_sheet = 0;
        const ProError st_sheet = ProDrawingViewSheetGet(drawing, view, &view_sheet);
        if (st_sheet != PRO_TK_NO_ERROR || view_sheet != sheet) {
            LogLine(log_sink,
                    "SKIP arrange-dwgviews selection=%d reason=sheet-mismatch status=%d view_sheet=%d current_sheet=%d",
                    i,
                    static_cast<int>(st_sheet),
                    view_sheet,
                    sheet);
            continue;
        }

        SelectedDrawingView item = {};
        item.view = view;
        item.selection_order = i;

        ProName name = {0};
        if (ProDrawingViewNameGet(drawing, view, name) == PRO_TK_NO_ERROR) {
            item.name = name;
        }

        if (GetDrawingViewOutlineBox(drawing, view, item.outline) != PRO_TK_NO_ERROR) {
            LogLine(log_sink,
                    "SKIP arrange-dwgviews selection=%d view=%s reason=outline",
                    i,
                    ViewNameForLog(item).c_str());
            continue;
        }

        ProPoint3d origin = {0.0, 0.0, 0.0};
        if (ProDrawingViewOriginGet(drawing, view, origin, nullptr) == PRO_TK_NO_ERROR) {
            item.origin[0] = origin[0];
            item.origin[1] = origin[1];
            item.origin[2] = 0.0;
        } else {
            item.origin[0] = OutlineCenterX(item.outline);
            item.origin[1] = OutlineCenterY(item.outline);
            item.origin[2] = 0.0;
        }

        views.push_back(item);
    }

    ProSelectionarrayFree(buffer);
    return true;
}

struct SlotCandidate {
    size_t view_index = 0;
    double cost = 0.0;
};

ArrangeGroupCandidate EvaluateArrangeGroupCandidate(const std::vector<SelectedDrawingView> &views,
                                                    const std::vector<size_t> &available,
                                                    size_t main_index,
                                                    const core::Dwg3Spacing &spacing)
{
    ArrangeGroupCandidate best = {};
    best.slot_to_view.fill(-1);
    best.main_index = main_index;
    best.slot_to_view[core::Dwg3ViewIndex(core::Dwg3ViewType::Front)] = static_cast<int>(main_index);

    const SelectedDrawingView &main_view = views[main_index];
    const std::array<core::Dwg3ViewType, 6> slot_order = {
        core::Dwg3ViewType::Right,
        core::Dwg3ViewType::Left,
        core::Dwg3ViewType::Top,
        core::Dwg3ViewType::Bottom,
        core::Dwg3ViewType::Back,
        core::Dwg3ViewType::Iso
    };

    std::array<std::vector<SlotCandidate>, core::kDwg3ViewCount> slot_candidates = {};
    for (core::Dwg3ViewType slot : slot_order) {
        ProPoint3d offset = {0.0, 0.0, 0.0};
        GetViewOriginOffset(spacing, slot, offset);
        const double expect_x = main_view.origin[0] + offset[0];
        const double expect_y = main_view.origin[1] + offset[1];
        const double tol_x = SlotToleranceX(slot, spacing);
        const double tol_y = SlotToleranceY(slot, spacing);

        for (size_t view_index : available) {
            if (view_index == main_index) {
                continue;
            }

            const SelectedDrawingView &candidate = views[view_index];
            const double dx = std::abs(candidate.origin[0] - expect_x);
            const double dy = std::abs(candidate.origin[1] - expect_y);
            if (dx > tol_x || dy > tol_y) {
                continue;
            }

            double cost = (dx / tol_x) + (dy / tol_y);
            if (NameHintsSlot(candidate.name, slot)) {
                cost -= 0.35;
            }
            slot_candidates[core::Dwg3ViewIndex(slot)].push_back({view_index, cost});
        }
    }

    std::array<int, core::kDwg3ViewCount> current = {};
    current.fill(-1);
    current[core::Dwg3ViewIndex(core::Dwg3ViewType::Front)] = static_cast<int>(main_index);
    std::unordered_set<size_t> used;
    used.insert(main_index);

    auto better = [](int lhs_count, double lhs_cost, int rhs_count, double rhs_cost) {
        if (lhs_count != rhs_count) {
            return lhs_count > rhs_count;
        }
        if (std::abs(lhs_cost - rhs_cost) > 1.0e-9) {
            return lhs_cost < rhs_cost;
        }
        return false;
    };

    std::function<void(size_t, int, double)> assign_slot =
        [&](size_t order_index, int matched_count, double total_cost) {
            if (order_index >= slot_order.size()) {
                const double score = matched_count * 1000.0 - total_cost * 100.0;
                if (!best.valid || better(matched_count, total_cost, best.matched_count, best.total_cost)) {
                    best.valid = matched_count >= 1;
                    best.matched_count = matched_count;
                    best.total_cost = total_cost;
                    best.score = score;
                    best.slot_to_view = current;
                }
                return;
            }

            const core::Dwg3ViewType slot = slot_order[order_index];
            assign_slot(order_index + 1, matched_count, total_cost);

            for (const SlotCandidate &candidate : slot_candidates[core::Dwg3ViewIndex(slot)]) {
                if (used.find(candidate.view_index) != used.end()) {
                    continue;
                }

                used.insert(candidate.view_index);
                current[core::Dwg3ViewIndex(slot)] = static_cast<int>(candidate.view_index);
                assign_slot(order_index + 1, matched_count + 1, total_cost + candidate.cost);
                current[core::Dwg3ViewIndex(slot)] = -1;
                used.erase(candidate.view_index);
            }
        };

    assign_slot(0, 0, 0.0);
    return best;
}

std::vector<ArrangeGroupCandidate> InferArrangeGroups(const std::vector<SelectedDrawingView> &views,
                                                      const core::Dwg3Spacing &spacing,
                                                      const Drawing3LogSink &log_sink)
{
    std::vector<ArrangeGroupCandidate> groups;
    std::vector<size_t> remaining;
    remaining.reserve(views.size());
    for (size_t i = 0; i < views.size(); ++i) {
        remaining.push_back(i);
    }

    while (remaining.size() >= 2) {
        ArrangeGroupCandidate best = {};
        best.slot_to_view.fill(-1);

        for (size_t candidate_index : remaining) {
            ArrangeGroupCandidate current = EvaluateArrangeGroupCandidate(views, remaining, candidate_index, spacing);
            if (!current.valid) {
                continue;
            }
            if (!best.valid ||
                current.matched_count > best.matched_count ||
                (current.matched_count == best.matched_count && current.total_cost < best.total_cost) ||
                (current.matched_count == best.matched_count &&
                 std::abs(current.total_cost - best.total_cost) < 1.0e-9 &&
                 views[current.main_index].selection_order < views[best.main_index].selection_order)) {
                best = current;
            }
        }

        if (!best.valid) {
            break;
        }

        std::unordered_set<size_t> consumed;
        for (int view_index : best.slot_to_view) {
            if (view_index >= 0) {
                consumed.insert(static_cast<size_t>(view_index));
            }
        }
        if (consumed.size() < 2) {
            break;
        }

        groups.push_back(best);
        std::vector<size_t> next_remaining;
        next_remaining.reserve(remaining.size());
        for (size_t view_index : remaining) {
            if (consumed.find(view_index) == consumed.end()) {
                next_remaining.push_back(view_index);
            }
        }
        remaining.swap(next_remaining);
    }

    for (size_t view_index : remaining) {
        LogLine(log_sink,
                "SKIP arrange-dwgviews view=%s reason=ambiguous-group",
                ViewNameForLog(views[view_index]).c_str());
    }

    return groups;
}

std::vector<ArrangeGroupCandidate> InferArrangeGroupsFromNames(const std::vector<SelectedDrawingView> &views,
                                                               std::unordered_set<size_t> &consumed,
                                                               const Drawing3LogSink &log_sink)
{
    struct NamedGroupBucket {
        std::array<int, core::kDwg3ViewCount> slot_to_view = {};
        int matched_count = 0;
        int first_selection_order = std::numeric_limits<int>::max();
    };

    std::unordered_map<std::string, NamedGroupBucket> buckets;
    for (size_t i = 0; i < views.size(); ++i) {
        const ParsedArrangeViewName parsed = ParseArrangeViewName(views[i].name);
        if (!parsed.valid) {
            continue;
        }

        NamedGroupBucket &bucket = buckets[parsed.group_key];
        if (bucket.first_selection_order == std::numeric_limits<int>::max()) {
            bucket.slot_to_view.fill(-1);
        }

        const size_t slot_index = core::Dwg3ViewIndex(parsed.type);
        const int current_view = bucket.slot_to_view[slot_index];
        if (current_view >= 0 &&
            views[static_cast<size_t>(current_view)].selection_order <= views[i].selection_order) {
            continue;
        }

        bucket.slot_to_view[slot_index] = static_cast<int>(i);
        bucket.first_selection_order = std::min(bucket.first_selection_order, views[i].selection_order);
    }

    std::vector<std::pair<std::string, NamedGroupBucket>> ordered;
    ordered.reserve(buckets.size());
    for (auto &entry : buckets) {
        int filled = 0;
        for (int view_index : entry.second.slot_to_view) {
            if (view_index >= 0) {
                ++filled;
            }
        }
        if (filled < 2) {
            continue;
        }
        entry.second.matched_count = filled - 1;
        ordered.push_back(entry);
    }

    std::sort(ordered.begin(), ordered.end(), [](const auto &lhs, const auto &rhs) {
        if (lhs.second.first_selection_order != rhs.second.first_selection_order) {
            return lhs.second.first_selection_order < rhs.second.first_selection_order;
        }
        return lhs.first < rhs.first;
    });

    std::vector<ArrangeGroupCandidate> groups;
    groups.reserve(ordered.size());
    for (const auto &entry : ordered) {
        ArrangeGroupCandidate group = {};
        group.slot_to_view = entry.second.slot_to_view;
        group.matched_count = entry.second.matched_count;
        group.valid = true;

        int front_index = group.slot_to_view[core::Dwg3ViewIndex(core::Dwg3ViewType::Front)];
        if (front_index >= 0) {
            group.main_index = static_cast<size_t>(front_index);
        } else {
            int earliest_view = -1;
            int earliest_order = std::numeric_limits<int>::max();
            for (int view_index : group.slot_to_view) {
                if (view_index < 0) {
                    continue;
                }
                const int order = views[static_cast<size_t>(view_index)].selection_order;
                if (order < earliest_order) {
                    earliest_order = order;
                    earliest_view = view_index;
                }
            }
            if (earliest_view < 0) {
                continue;
            }
            group.main_index = static_cast<size_t>(earliest_view);
        }

        for (int view_index : group.slot_to_view) {
            if (view_index >= 0) {
                consumed.insert(static_cast<size_t>(view_index));
            }
        }

        LogLine(log_sink,
                "INFO arrange-dwgviews named-group key=%s views=%d main=%s",
                entry.first.c_str(),
                group.matched_count + 1,
                ViewNameForLog(views[group.main_index]).c_str());
        groups.push_back(group);
    }

    return groups;
}

core::Dwg3ViewMask BuildArrangeGroupMask(const ArrangeGroupCandidate &group)
{
    core::Dwg3ViewMask mask = 0;
    for (core::Dwg3ViewType type : AllDwg3ViewTypes()) {
        if (group.slot_to_view[core::Dwg3ViewIndex(type)] >= 0) {
            mask |= core::Dwg3ViewBit(type);
        }
    }
    return mask;
}

core::Dwg3CreatedViews BuildArrangeGroupViews(const std::vector<SelectedDrawingView> &views,
                                              const ArrangeGroupCandidate &group)
{
    core::Dwg3CreatedViews arranged = {};
    for (core::Dwg3ViewType type : AllDwg3ViewTypes()) {
        const int view_index = group.slot_to_view[core::Dwg3ViewIndex(type)];
        if (view_index >= 0) {
            CreatedViewSlot(arranged, type) = views[static_cast<size_t>(view_index)].view;
        }
    }
    return arranged;
}

std::string FormatArrangeGroup(const std::vector<SelectedDrawingView> &views,
                               const ArrangeGroupCandidate &group)
{
    std::string out = "main=" + ViewNameForLog(views[group.main_index]);
    for (core::Dwg3ViewType type : AllDwg3ViewTypes()) {
        const int view_index = group.slot_to_view[core::Dwg3ViewIndex(type)];
        if (view_index < 0 || type == core::Dwg3ViewType::Front) {
            continue;
        }
        out += " ";
        out += ArrangeSlotName(type);
        out += "=";
        out += ViewNameForLog(views[static_cast<size_t>(view_index)]);
    }
    return out;
}

int FirstArrangeGroupSelectionOrder(const std::vector<SelectedDrawingView> &views,
                                    const ArrangeGroupCandidate &group)
{
    int first = std::numeric_limits<int>::max();
    for (int view_index : group.slot_to_view) {
        if (view_index < 0) {
            continue;
        }
        first = std::min(first, views[static_cast<size_t>(view_index)].selection_order);
    }
    return first == std::numeric_limits<int>::max() ? 0 : first;
}

bool TryReadDraftLineEntity(ProDtlentity *entity, DraftLineEntity &line)
{
    line = {};
    if (entity == nullptr) {
        return false;
    }

    ProDtlentitydata entdata = nullptr;
    ProCurvedata curve = {};
    const ProError st_data = ProDtlentityDataGet(entity, nullptr, &entdata);
    if (st_data != PRO_TK_NO_ERROR || entdata == nullptr) {
        return false;
    }

    auto cleanup = [&]() {
        if (entdata != nullptr) {
            ProDtlentitydataFree(entdata);
        }
    };

    ProView attached_view = nullptr;
    const ProError st_view = ProDtlentitydataViewGet(entdata, &attached_view);
    if (st_view != PRO_TK_NO_ERROR || attached_view == nullptr) {
        cleanup();
        return false;
    }

    const ProError st_curve = ProDtlentitydataCurveGet(entdata, &curve);
    if (st_curve != PRO_TK_NO_ERROR || curve.line.type != PRO_ENT_LINE) {
        cleanup();
        return false;
    }

    line.entity = *entity;
    line.view = attached_view;
    line.x1 = curve.line.end1[0];
    line.y1 = curve.line.end1[1];
    line.x2 = curve.line.end2[0];
    line.y2 = curve.line.end2[1];
    cleanup();
    return true;
}

std::vector<DraftLineEntity> CollectSheetDraftLines(ProDrawing drawing, int sheet, const Drawing3LogSink &log_sink)
{
    std::vector<DraftLineEntity> lines;
    if (drawing == nullptr) {
        return lines;
    }

    ProDtlentity *entities = nullptr;
    const ProError st_collect = ProDrawingDtlentitiesCollect(drawing, nullptr, sheet, &entities);
    if (st_collect == PRO_TK_E_NOT_FOUND) {
        return lines;
    }
    if (st_collect != PRO_TK_NO_ERROR || entities == nullptr) {
        LogLine(log_sink,
                "WARN arrange-dwgviews collect-frame-entities status=%d",
                static_cast<int>(st_collect));
        return lines;
    }

    int count = 0;
    ProArraySizeGet(reinterpret_cast<ProArray>(entities), &count);
    lines.reserve(static_cast<size_t>(std::max(0, count)));
    for (int i = 0; i < count; ++i) {
        DraftLineEntity line;
        if (TryReadDraftLineEntity(&entities[i], line)) {
            lines.push_back(line);
        }
    }
    ProArrayFree(reinterpret_cast<ProArray *>(&entities));
    return lines;
}

bool IsFrameLineForOutline(const DraftLineEntity &line,
                           const core::Dwg3GroupOutline &frame_outline,
                           double page_scale)
{
    const double factor = std::max(1.0, page_scale / 0.015);
    const double tol = std::max(1.0, 2.0 * factor);
    const bool horizontal = Near(line.y1, line.y2, tol);
    const bool vertical = Near(line.x1, line.x2, tol);

    if (horizontal) {
        const bool spans_x = SpanMatches(line.x1, line.x2, frame_outline.min_x, frame_outline.max_x, tol);
        if (!spans_x) {
            return false;
        }
        return Near(line.y1, frame_outline.max_y, tol) || Near(line.y1, frame_outline.min_y, tol);
    }

    if (vertical) {
        const bool spans_y = SpanMatches(line.y1, line.y2, frame_outline.min_y, frame_outline.max_y, tol);
        if (!spans_y) {
            return false;
        }
        return Near(line.x1, frame_outline.min_x, tol) || Near(line.x1, frame_outline.max_x, tol);
    }

    return false;
}

int DeleteExistingGroupFrameLines(ProDrawing drawing,
                                  int sheet,
                                  ProView anchor_view,
                                  const core::Dwg3GroupOutline &view_outline,
                                  double page_scale,
                                  const Drawing3LogSink &log_sink)
{
    if (drawing == nullptr) {
        return 0;
    }

    if (anchor_view == nullptr) {
        return 0;
    }

    const core::Dwg3GroupOutline frame_outline = ExpandDecoratedOutline(view_outline, page_scale, false);
    std::vector<DraftLineEntity> lines = CollectSheetDraftLines(drawing, sheet, log_sink);
    int deleted = 0;
    int failed = 0;
    for (DraftLineEntity &line : lines) {
        if (!SameView(line.view, anchor_view) || !IsFrameLineForOutline(line, frame_outline, page_scale)) {
            continue;
        }

        ProError st = ProDtlentityDelete(&line.entity, nullptr);
        if (st != PRO_TK_NO_ERROR) {
            st = ProDtlentityErase(&line.entity);
        }
        if (st == PRO_TK_NO_ERROR) {
            ++deleted;
        } else {
            ++failed;
        }
    }

    if (deleted > 0 || failed > 0) {
        LogLine(log_sink,
                "INFO arrange-dwgviews frame-clear sheet=%d deleted=%d failed=%d",
                sheet,
                deleted,
                failed);
    }
    return deleted;
}

bool CollectNoteText(ProDtlnotedata note_data, std::wstring &text_out)
{
    text_out.clear();
    if (note_data == nullptr) {
        return false;
    }

    ProDtlnoteline *lines = nullptr;
    const ProError st_lines = ProDtlnotedataLinesCollect(note_data, &lines);
    if (st_lines != PRO_TK_NO_ERROR || lines == nullptr) {
        return false;
    }

    int line_count = 0;
    ProArraySizeGet(reinterpret_cast<ProArray>(lines), &line_count);
    for (int i = 0; i < line_count; ++i) {
        ProDtlnotetext *texts = nullptr;
        const ProError st_texts = ProDtlnotelineTextsCollect(lines[i], &texts);
        if (st_texts != PRO_TK_NO_ERROR || texts == nullptr) {
            continue;
        }

        int text_count = 0;
        ProArraySizeGet(reinterpret_cast<ProArray>(texts), &text_count);
        for (int j = 0; j < text_count; ++j) {
            ProLine value = {0};
            if (ProDtlnotetextStringGet(texts[j], value) == PRO_TK_NO_ERROR && value[0] != L'\0') {
                text_out += value;
            }
        }
        ProArrayFree(reinterpret_cast<ProArray *>(&texts));

        if (i + 1 < line_count && !text_out.empty()) {
            text_out += L"\n";
        }
    }
    ProArrayFree(reinterpret_cast<ProArray *>(&lines));
    return !text_out.empty();
}

bool TryReadTitleNote(ProDtlnote *note, DraftTitleNote &title)
{
    title = {};
    if (note == nullptr) {
        return false;
    }

    ProDtlnotedata note_data = nullptr;
    ProDtlattach attachment = nullptr;
    ProSelection attach_selection = nullptr;
    const ProError st_data = ProDtlnoteDataGet(note, nullptr, PRODISPMODE_SYMBOLIC, &note_data);
    if (st_data != PRO_TK_NO_ERROR || note_data == nullptr) {
        return false;
    }

    auto cleanup = [&]() {
        if (attach_selection != nullptr) {
            ProSelectionFree(&attach_selection);
        }
        if (attachment != nullptr) {
            ProDtlattachFree(attachment);
        }
        if (note_data != nullptr) {
            ProDtlnotedataFree(note_data);
        }
    };

    if (!CollectNoteText(note_data, title.text)) {
        cleanup();
        return false;
    }

    const ProError st_attachment = ProDtlnotedataAttachmentGet(note_data, &attachment);
    if (st_attachment != PRO_TK_NO_ERROR || attachment == nullptr) {
        cleanup();
        return false;
    }

    ProDtlattachType attach_type = PRO_DTLATTACHTYPE_UNIMPLEMENTED;
    const ProError st_attach_get =
        ProDtlattachGet(attachment, &attach_type, &title.view, title.location, &attach_selection);
    if (st_attach_get != PRO_TK_NO_ERROR) {
        cleanup();
        return false;
    }

    title.note = *note;
    cleanup();
    return true;
}

bool LooksLikeModelTitleNote(const std::wstring &text)
{
    if (text.empty()) {
        return false;
    }

    std::wstring upper = text;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towupper(ch));
    });
    return upper.find(L"数量:") != std::wstring::npos ||
           upper.find(L"数量：") != std::wstring::npos ||
           upper.find(L"QTY:") != std::wstring::npos ||
           upper.find(L"QTY：") != std::wstring::npos;
}

bool IsTitleNoteNearFrame(const DraftTitleNote &note,
                          const core::Dwg3GroupOutline &view_outline,
                          double page_scale)
{
    const core::Dwg3GroupOutline frame_outline = ExpandDecoratedOutline(view_outline, page_scale, false);
    const double factor = std::max(1.0, page_scale / 0.015);
    const double tol_x = 24.0 * factor;
    const double top_band = 28.0 * factor;
    const double x = note.location[0];
    const double y = note.location[1];
    return x >= frame_outline.min_x - tol_x &&
           x <= frame_outline.max_x + tol_x &&
           y >= frame_outline.max_y - top_band &&
           y <= frame_outline.max_y + top_band;
}

int DeleteExistingModelTitleNotes(ProDrawing drawing,
                                  int sheet,
                                  ProView anchor_view,
                                  const core::Dwg3GroupOutline &view_outline,
                                  double page_scale,
                                  const Drawing3LogSink &log_sink)
{
    if (drawing == nullptr || anchor_view == nullptr) {
        return 0;
    }

    ProDtlnote *notes = nullptr;
    const ProError st_collect = ProDrawingDtlnotesCollect(drawing, nullptr, sheet, &notes);
    if (st_collect == PRO_TK_E_NOT_FOUND) {
        return 0;
    }
    if (st_collect != PRO_TK_NO_ERROR || notes == nullptr) {
        LogLine(log_sink,
                "WARN arrange-dwgviews collect-title-notes status=%d",
                static_cast<int>(st_collect));
        return 0;
    }

    int count = 0;
    ProArraySizeGet(reinterpret_cast<ProArray>(notes), &count);
    int deleted = 0;
    int failed = 0;
    for (int i = 0; i < count; ++i) {
        DraftTitleNote title;
        if (!TryReadTitleNote(&notes[i], title)) {
            continue;
        }
        if (!SameView(title.view, anchor_view) ||
            !LooksLikeModelTitleNote(title.text) ||
            !IsTitleNoteNearFrame(title, view_outline, page_scale)) {
            continue;
        }

        const ProError st_delete = ProDtlnoteDelete(&title.note, nullptr);
        if (st_delete == PRO_TK_NO_ERROR) {
            ++deleted;
        } else {
            ++failed;
        }
    }
    ProArrayFree(reinterpret_cast<ProArray *>(&notes));

    if (deleted > 0 || failed > 0) {
        LogLine(log_sink,
                "INFO arrange-dwgviews title-clear sheet=%d deleted=%d failed=%d",
                sheet,
                deleted,
                failed);
    }
    return deleted;
}

ProError CreateDraftLineEntity(ProDrawing drawing,
                               ProView anchor_view,
                               double x1,
                               double y1,
                               double x2,
                               double y2)
{
    if (drawing == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    ProDtlentitydata entdata = nullptr;
    ProCurvedata *curve = nullptr;
    ProError st = ProDtlentitydataAlloc(reinterpret_cast<ProMdl>(drawing), &entdata);
    if (st != PRO_TK_NO_ERROR || entdata == nullptr) {
        return st;
    }

    auto cleanup = [&]() {
        if (curve != nullptr) {
            ProCurvedataFree(curve);
        }
        if (entdata != nullptr) {
            ProDtlentitydataFree(entdata);
        }
    };

    st = ProCurvedataAlloc(&curve);
    if (st != PRO_TK_NO_ERROR || curve == nullptr) {
        cleanup();
        return st;
    }

    curve->line.type = PRO_ENT_LINE;
    curve->line.end1[0] = x1;
    curve->line.end1[1] = y1;
    curve->line.end1[2] = 0.0;
    curve->line.end2[0] = x2;
    curve->line.end2[1] = y2;
    curve->line.end2[2] = 0.0;

    st = ProDtlentitydataCurveSet(entdata, curve);
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }
    if (anchor_view != nullptr) {
        st = ProDtlentitydataViewSet(entdata, anchor_view);
        if (st != PRO_TK_NO_ERROR) {
            cleanup();
            return st;
        }
    }

    ProDtlentity entity;
    std::memset(&entity, 0, sizeof(entity));
    st = ProDtlentityCreate(reinterpret_cast<ProMdl>(drawing), nullptr, entdata, &entity);
    if (st == PRO_TK_NO_ERROR) {
        ProDtlentityDraw(&entity);
    }
    cleanup();
    return st;
}

ProError CreateGroupFrameForViewOutline(ProDrawing drawing,
                                        ProView anchor_view,
                                        const core::Dwg3GroupOutline &view_outline,
                                        double page_scale)
{
    if (drawing == nullptr || anchor_view == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    const core::Dwg3GroupOutline frame_outline = ExpandDecoratedOutline(view_outline, page_scale, false);
    ProError st = CreateDraftLineEntity(
        drawing,
        anchor_view,
        frame_outline.min_x,
        frame_outline.max_y,
        frame_outline.max_x,
        frame_outline.max_y);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }
    st = CreateDraftLineEntity(
        drawing,
        anchor_view,
        frame_outline.max_x,
        frame_outline.max_y,
        frame_outline.max_x,
        frame_outline.min_y);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }
    st = CreateDraftLineEntity(
        drawing,
        anchor_view,
        frame_outline.max_x,
        frame_outline.min_y,
        frame_outline.min_x,
        frame_outline.min_y);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }
    return CreateDraftLineEntity(
        drawing,
        anchor_view,
        frame_outline.min_x,
        frame_outline.min_y,
        frame_outline.min_x,
        frame_outline.max_y);
}

ProError CreateFreeDrawingNote(ProDrawing drawing,
                               ProView anchor_view,
                               const std::wstring &text,
                               const ProPoint3d screen_point,
                               ProTextHrzJustification horz_justification,
                               ProVerticalJustification vert_justification)
{
    if (drawing == nullptr || text.empty()) {
        return PRO_TK_BAD_INPUTS;
    }

    ProDtlnotedata note_data = nullptr;
    ProDtlattach attachment = nullptr;
    ProTextStyle text_style = nullptr;
    ProError st = ProDtlnotedataAlloc(reinterpret_cast<ProMdl>(drawing), &note_data);
    if (st != PRO_TK_NO_ERROR || note_data == nullptr) {
        return st;
    }

    auto cleanup = [&]() {
        if (note_data != nullptr) {
            ProDtlnotedataFree(note_data);
        }
        if (attachment != nullptr) {
            ProDtlattachFree(attachment);
        }
        if (text_style != nullptr) {
            ProTextStyleFree(&text_style);
        }
    };

    ProDtlnoteline line = nullptr;
    st = ProDtlnotelineAlloc(&line);
    if (st != PRO_TK_NO_ERROR || line == nullptr) {
        cleanup();
        return st;
    }

    ProDtlnotetext note_text = nullptr;
    st = ProDtlnotetextAlloc(&note_text);
    if (st != PRO_TK_NO_ERROR || note_text == nullptr) {
        cleanup();
        return st;
    }
    ProLine pro_text = {0};
    CopyWStr(pro_text, text.c_str());
    st = ProDtlnotetextStringSet(note_text, pro_text);
    if (st == PRO_TK_NO_ERROR) {
        st = ProDtlnotelineTextAdd(line, note_text);
    }
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }

    st = ProDtlnotedataLineAdd(note_data, line);
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }

    st = ProTextStyleAlloc(&text_style);
    if (st != PRO_TK_NO_ERROR || text_style == nullptr) {
        cleanup();
        return st;
    }
    st = ProTextStyleJustificationSet(text_style, horz_justification);
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }
    st = ProTextStyleVertJustificationSet(text_style, vert_justification);
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }
    st = ProDtlnotedataTextStyleSet(note_data, text_style);
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }

    ProVector location = {screen_point[0], screen_point[1], 0.0};
    st = ProDtlattachAlloc(PRO_DTLATTACHTYPE_FREE, anchor_view, location, nullptr, &attachment);
    if (st != PRO_TK_NO_ERROR || attachment == nullptr) {
        cleanup();
        return st;
    }
    st = ProDtlnotedataAttachmentSet(note_data, attachment);
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }
    st = ProDtlnotedataDisplayedSet(note_data, PRO_B_TRUE);
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }

    ProDtlnote note;
    std::memset(&note, 0, sizeof(note));
    st = ProDtlnoteCreate(reinterpret_cast<ProMdl>(drawing), nullptr, note_data, &note);
    if (st == PRO_TK_NO_ERROR) {
        const ProError show_st = ProDtlnoteShow(&note);
        if (show_st != PRO_TK_NO_ERROR) {
            const ProError draw_st = ProDtlnoteDraw(&note);
            st = (draw_st == PRO_TK_NO_ERROR) ? PRO_TK_NO_ERROR : show_st;
        }
    }
    cleanup();
    return st;
}

std::wstring BuildModelTitleForView(ProDrawing drawing, ProView view)
{
    ProSolid solid = nullptr;
    ProMdl mdl = nullptr;
    if (drawing != nullptr &&
        view != nullptr &&
        ProDrawingViewSolidGet(drawing, view, &solid) == PRO_TK_NO_ERROR &&
        solid != nullptr) {
        mdl = reinterpret_cast<ProMdl>(solid);
    }

    std::wstring model_name = autobbox::creo::ModelName(mdl, L"<unknown>");
    std::wstring qty_value;
    if (mdl == nullptr ||
        !autobbox::creo::ReadParamDisplayValueOnModel(mdl, L"QTY", qty_value) ||
        qty_value.empty()) {
        qty_value = L"1";
    }

    return model_name + L" 数量: " + qty_value;
}

ProError CreateModelTitleNoteForViewOutline(ProDrawing drawing,
                                            ProView anchor_view,
                                            const core::Dwg3GroupOutline &view_outline,
                                            double page_scale)
{
    if (drawing == nullptr || anchor_view == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    const double factor = std::max(1.0, page_scale / 0.015);
    const core::Dwg3GroupOutline frame_outline = ExpandDecoratedOutline(view_outline, page_scale, false);
    const double title_inset_x = 6.0 * factor;
    const double title_inset_y = 5.0 * factor;
    const ProPoint3d title_pos = {
        frame_outline.max_x - title_inset_x,
        frame_outline.max_y - title_inset_y,
        0.0};
    return CreateFreeDrawingNote(
        drawing,
        anchor_view,
        BuildModelTitleForView(drawing, anchor_view),
        title_pos,
        PRO_TEXT_HRZJUST_RIGHT,
        PRO_VERTJUST_TOP);
}

bool PrepareArrangeGroup(ProDrawing drawing,
                         int sheet,
                         const std::vector<SelectedDrawingView> &views,
                         const ArrangeGroupCandidate &group,
                         double page_scale,
                         const DrawingArrangeOptions &options,
                         PreparedArrangeGroup &prepared,
                         DrawingArrangeSummary &summary,
                         const Drawing3LogSink &log_sink)
{
    prepared = {};
    prepared.group = group;
    prepared.arranged = BuildArrangeGroupViews(views, group);
    prepared.anchor_view = views[group.main_index].view;
    prepared.full_mask = BuildArrangeGroupMask(group);
    prepared.first_selection_order = FirstArrangeGroupSelectionOrder(views, group);
    const core::Dwg3ViewMask move_mask = prepared.full_mask & ~core::Dwg3ViewBit(core::Dwg3ViewType::Front);

    core::Dwg3GroupOutline before = {};
    ProError st = GetDrawingViewGroupOutline(drawing, prepared.arranged, before);
    if (st != PRO_TK_NO_ERROR) {
        LogLine(log_sink,
                "FAIL arrange-dwgviews group=%s reason=outline-before status=%d",
                FormatArrangeGroup(views, group).c_str(),
                static_cast<int>(st));
        return false;
    }
    if (options.add_frame) {
        summary.frames_deleted +=
            DeleteExistingGroupFrameLines(drawing, sheet, prepared.anchor_view, before, page_scale, log_sink);
    }
    if (options.update_model_title) {
        summary.title_notes_deleted +=
            DeleteExistingModelTitleNotes(drawing, sheet, prepared.anchor_view, before, page_scale, log_sink);
    }
    prepared.anchor_outline = ExpandDecoratedOutline(before, page_scale, false);

    st = PackCreatedViewsByOutline(drawing, prepared.arranged, move_mask, page_scale);
    if (st != PRO_TK_NO_ERROR) {
        LogLine(log_sink,
                "FAIL arrange-dwgviews group=%s reason=pack status=%d",
                FormatArrangeGroup(views, group).c_str(),
                static_cast<int>(st));
        return false;
    }

    core::Dwg3GroupOutline packed = {};
    st = GetDrawingViewGroupOutline(drawing, prepared.arranged, packed);
    if (st != PRO_TK_NO_ERROR) {
        LogLine(log_sink,
                "FAIL arrange-dwgviews group=%s reason=outline-after-pack status=%d",
                FormatArrangeGroup(views, group).c_str(),
                static_cast<int>(st));
        return false;
    }
    prepared.packed_outline = ExpandDecoratedOutline(packed, page_scale, false);
    return true;
}

} // namespace

DrawingArrangeSummary ExecuteArrangeSelectedDrawingViewsTask(ProDrawing drawing,
                                                            int sheet,
                                                            const DrawingArrangeOptions &options,
                                                            const Drawing3LogSink &log_sink)
{
    DrawingArrangeSummary summary = {};
    summary.sheet = sheet;

    if (drawing == nullptr) {
        LogLine(log_sink, "FAIL arrange-dwgviews reason=null-drawing");
        return summary;
    }

    std::vector<SelectedDrawingView> selected_views;
    int selection_count = 0;
    if (!TryCollectSelectedDrawingViews(drawing, sheet, selected_views, selection_count, log_sink)) {
        return summary;
    }

    summary.selected_total = selection_count;
    summary.valid_views = static_cast<int>(selected_views.size());
    if (selected_views.size() < 2) {
        LogLine(log_sink,
                "FAIL arrange-dwgviews reason=insufficient-selected-views selected=%d valid=%d",
                selection_count,
                static_cast<int>(selected_views.size()));
        return summary;
    }

    summary.page_scale = ComputeDrawingGroupScale(drawing, sheet);
    summary.page_scale_valid = summary.page_scale > 0.0 && std::isfinite(summary.page_scale);
    const core::Dwg3Spacing spacing = ComputeDrawingSpacing(summary.page_scale);

    LogLine(log_sink,
            "ArrangeDwgViews start sheet=%d selected=%d valid=%d page_scale=%.6f side_dx=%.3f iso_dx=%.3f vertical_dy=%.3f",
            sheet,
            selection_count,
            static_cast<int>(selected_views.size()),
            summary.page_scale,
            spacing.side_dx,
            spacing.iso_dx,
            spacing.vertical_dy);

    std::unordered_set<size_t> consumed_named_views;
    std::vector<ArrangeGroupCandidate> groups =
        InferArrangeGroupsFromNames(selected_views, consumed_named_views, log_sink);

    if (consumed_named_views.size() < selected_views.size()) {
        std::vector<SelectedDrawingView> remaining_views;
        std::vector<size_t> remaining_to_original;
        remaining_views.reserve(selected_views.size() - consumed_named_views.size());
        remaining_to_original.reserve(selected_views.size() - consumed_named_views.size());
        for (size_t i = 0; i < selected_views.size(); ++i) {
            if (consumed_named_views.find(i) != consumed_named_views.end()) {
                continue;
            }
            remaining_to_original.push_back(i);
            remaining_views.push_back(selected_views[i]);
        }

        const std::vector<ArrangeGroupCandidate> inferred_remaining =
            InferArrangeGroups(remaining_views, spacing, log_sink);
        for (const ArrangeGroupCandidate &remaining_group : inferred_remaining) {
            ArrangeGroupCandidate mapped = remaining_group;
            mapped.main_index = remaining_to_original[remaining_group.main_index];
            for (size_t slot = 0; slot < mapped.slot_to_view.size(); ++slot) {
                if (mapped.slot_to_view[slot] >= 0) {
                    mapped.slot_to_view[slot] =
                        static_cast<int>(remaining_to_original[static_cast<size_t>(mapped.slot_to_view[slot])]);
                }
            }
            groups.push_back(mapped);
        }
    }

    std::sort(groups.begin(), groups.end(), [&](const ArrangeGroupCandidate &lhs, const ArrangeGroupCandidate &rhs) {
        return FirstArrangeGroupSelectionOrder(selected_views, lhs) <
               FirstArrangeGroupSelectionOrder(selected_views, rhs);
    });

    summary.groups_total = static_cast<int>(groups.size());

    std::unordered_set<size_t> grouped_indices;
    for (const ArrangeGroupCandidate &group : groups) {
        for (int view_index : group.slot_to_view) {
            if (view_index >= 0) {
                grouped_indices.insert(static_cast<size_t>(view_index));
            }
        }
    }
    summary.grouped_views = static_cast<int>(grouped_indices.size());
    summary.groups_skipped = summary.valid_views - summary.grouped_views;

    std::vector<PreparedArrangeGroup> prepared_groups;
    prepared_groups.reserve(groups.size());
    for (const ArrangeGroupCandidate &group : groups) {
        PreparedArrangeGroup prepared;
        if (PrepareArrangeGroup(
                drawing,
                sheet,
                selected_views,
                group,
                summary.page_scale,
                options,
                prepared,
                summary,
                log_sink)) {
            prepared_groups.push_back(prepared);
        } else {
            ++summary.groups_skipped;
        }
    }

    std::sort(prepared_groups.begin(), prepared_groups.end(), [](const PreparedArrangeGroup &lhs, const PreparedArrangeGroup &rhs) {
        if (std::abs(lhs.anchor_outline.min_x - rhs.anchor_outline.min_x) > 1.0e-6) {
            return lhs.anchor_outline.min_x < rhs.anchor_outline.min_x;
        }
        if (std::abs(lhs.anchor_outline.max_y - rhs.anchor_outline.max_y) > 1.0e-6) {
            return lhs.anchor_outline.max_y > rhs.anchor_outline.max_y;
        }
        return lhs.first_selection_order < rhs.first_selection_order;
    });

    if (!prepared_groups.empty()) {
        double next_left_screen = prepared_groups.front().anchor_outline.min_x;
        double anchor_top_screen = prepared_groups.front().anchor_outline.max_y;
        for (const PreparedArrangeGroup &prepared : prepared_groups) {
            next_left_screen = std::min(next_left_screen, prepared.anchor_outline.min_x);
            anchor_top_screen = std::max(anchor_top_screen, prepared.anchor_outline.max_y);
        }

        LogLine(log_sink,
                "ArrangeDwgViews layout groups=%d anchor_left=%.3f anchor_top=%.3f gap_x=%.3f",
                static_cast<int>(prepared_groups.size()),
                next_left_screen,
                anchor_top_screen,
                spacing.gap_x);

        for (PreparedArrangeGroup &prepared : prepared_groups) {
            const double dx = next_left_screen - prepared.packed_outline.min_x;
            const double dy = anchor_top_screen - prepared.packed_outline.max_y;
            const ProError st = MoveCreatedViewsByScreenDelta(
                drawing,
                prepared.arranged,
                prepared.full_mask,
                dx,
                dy);
            if (st != PRO_TK_NO_ERROR) {
                ++summary.groups_skipped;
                LogLine(log_sink,
                        "FAIL arrange-dwgviews group=%s reason=layout status=%d dx=%.3f dy=%.3f",
                        FormatArrangeGroup(selected_views, prepared.group).c_str(),
                        static_cast<int>(st),
                        dx,
                        dy);
                continue;
            }

            core::Dwg3GroupOutline after = {};
            if (GetDrawingViewGroupOutline(drawing, prepared.arranged, after) != PRO_TK_NO_ERROR) {
                ++summary.groups_skipped;
                LogLine(log_sink,
                        "FAIL arrange-dwgviews group=%s reason=outline-after-layout",
                        FormatArrangeGroup(selected_views, prepared.group).c_str());
                continue;
            }

            const core::Dwg3GroupOutline decorated_after = ExpandDecoratedOutline(after, summary.page_scale, false);
            if (options.add_frame) {
                const ProError st_frame = CreateGroupFrameForViewOutline(
                    drawing,
                    prepared.anchor_view,
                    after,
                    summary.page_scale);
                if (st_frame == PRO_TK_NO_ERROR) {
                    ++summary.frames_created;
                } else {
                    LogLine(log_sink,
                            "WARN arrange-dwgviews group=%s frame-create status=%d",
                            FormatArrangeGroup(selected_views, prepared.group).c_str(),
                            static_cast<int>(st_frame));
                }
            }
            if (options.update_model_title) {
                const ProError st_title = CreateModelTitleNoteForViewOutline(
                    drawing,
                    prepared.anchor_view,
                    after,
                    summary.page_scale);
                if (st_title == PRO_TK_NO_ERROR) {
                    ++summary.title_notes_updated;
                } else {
                    LogLine(log_sink,
                            "WARN arrange-dwgviews group=%s title-create status=%d",
                            FormatArrangeGroup(selected_views, prepared.group).c_str(),
                            static_cast<int>(st_title));
                }
            }
            next_left_screen = decorated_after.max_x + spacing.gap_x;

            ++summary.groups_arranged;
            summary.moved_views += prepared.group.matched_count + 1;
            LogLine(log_sink,
                    "OK   arrange-dwgviews group=%s matched=%d left=%.3f top=%.3f w=%.3f h=%.3f dx=%.3f dy=%.3f",
                    FormatArrangeGroup(selected_views, prepared.group).c_str(),
                    prepared.group.matched_count + 1,
                    after.min_x,
                    after.max_y,
                    OutlineWidth(after),
                    OutlineHeight(after),
                    dx,
                    dy);
        }
    }

    LogLine(log_sink,
            "Summary mode=arrange-dwgviews sheet=%d selected=%d valid=%d grouped=%d groups=%d arranged=%d skipped=%d moved=%d frames_deleted=%d frames_created=%d title_deleted=%d title_updated=%d",
            summary.sheet,
            summary.selected_total,
            summary.valid_views,
            summary.grouped_views,
            summary.groups_total,
            summary.groups_arranged,
            summary.groups_skipped,
            summary.moved_views,
            summary.frames_deleted,
            summary.frames_created,
            summary.title_notes_deleted,
            summary.title_notes_updated);

    return summary;
}

DrawingArrangeSummary ExecuteArrangeSelectedDrawingViewsTask(ProDrawing drawing,
                                                            int sheet,
                                                            const Drawing3LogSink &log_sink)
{
    DrawingArrangeOptions options;
    return ExecuteArrangeSelectedDrawingViewsTask(drawing, sheet, options, log_sink);
}

} // namespace autobbox::application
