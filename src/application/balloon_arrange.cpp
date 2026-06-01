#include "autobbox/application/balloon_arrange.h"

#include "autobbox/application/drawing_view_layout.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/creo/parameter_api.h"

#include <ProAsmcomppath.h>
#include <ProArray.h>
#include <ProBomballoon.h>
#include <ProDrawing.h>
#include <ProDrawingView.h>
#include <ProDtlattach.h>
#include <ProDtlsyminst.h>
#include <ProDtlnote.h>
#include <ProDwgtable.h>
#include <ProGeomitem.h>
#include <ProModelitem.h>
#include <ProSelection.h>
#include <ProSelbuffer.h>
#include <ProSolid.h>
#include <ProSurface.h>
#include <ProToolkit.h>
#include <ProWindows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <string>
#include <unordered_set>
#include <vector>

namespace autobbox::application {

namespace {

constexpr double kSecondaryMinVerticalSpacing = 12.0;
constexpr double kCustomBalloonOutsideOffset = 36.0;
constexpr double kCustomBalloonVerticalSpacing = 14.0;
constexpr double kCustomBalloonTextHeight = 3.5;
constexpr double kCustomBalloonElbowLength = 6.0;
constexpr double kTraditionalLongLeaderThreshold = 180.0;
constexpr double kTraditionalSecondSnaplineIncrement = 28.0;

struct SelectedBalloonNote {
    ProDtlnote note = {};
    ProView view = nullptr;
    std::wstring view_key;
    int selection_order = 0;
};

struct BalloonLayoutItem {
    ProDtlnote note = {};
    ProView view = nullptr;
    std::wstring view_key;
    ProVector note_location = {0.0, 0.0, 0.0};
    ProVector leader_location = {0.0, 0.0, 0.0};
    int selection_order = 0;
    bool valid = false;
};

struct CustomBalloonCandidate {
    ProView view = nullptr;
    std::wstring view_key;
    ProMdl model = nullptr;
    ProMdl table_assembly = nullptr;
    ProAsmcomppath component_path = {};
    bool has_component_path = false;
    bool allow_null_component_path = false;
    ProModelitem leader_model_item = {};
    bool has_leader_model_item = false;
    bool has_entity_attach_point = false;
    bool from_bom_table = false;
    std::wstring label;
    ProPoint3d attach_point = {0.0, 0.0, 0.0};
    ProPoint3d leader_point = {0.0, 0.0, 0.0};
    ProPoint3d note_point = {0.0, 0.0, 0.0};
    int selection_order = 0;
    bool right_side = true;
};

struct CustomBalloonCreateDiagnostics {
    bool attempted_parametric = false;
    bool used_parametric = false;
    bool used_free = false;
    ProError selection_alloc_status = PRO_TK_NO_ERROR;
    ProError selection_view_status = PRO_TK_NO_ERROR;
    ProError selection_point_status = PRO_TK_NO_ERROR;
    ProError parametric_attach_status = PRO_TK_NO_ERROR;
    ProError parametric_leader_status = PRO_TK_NO_ERROR;
    ProError free_attach_status = PRO_TK_NO_ERROR;
    ProError free_leader_status = PRO_TK_NO_ERROR;
};

struct SelectedBomTableSegment {
    ProDwgtable table = {};
    int segment = PRO_VALUE_UNUSED;
};

struct CreatedCustomBalloonNote {
    ProDtlnote note = {};
    ProView view = nullptr;
    std::wstring view_key;
    int selection_order = 0;
};

enum class TraditionalBalloonSide {
    Left,
    Right,
    Top,
    Bottom
};

struct TraditionalBomBalloonSymbol {
    ProDtlsyminst symbol = {};
    ProView view = nullptr;
    std::wstring view_key;
    ProMdl model = nullptr;
    ProAsmcomppath component_path = {};
    bool has_component_path = false;
    bool allow_null_component_path = false;
    ProModelitem leader_model_item = {};
    bool has_leader_model_item = false;
    ProPoint3d component_attach_point = {0.0, 0.0, 0.0};
    bool has_component_attach_point = false;
    ProPoint3d leader_point = {0.0, 0.0, 0.0};
    ProPoint3d note_point = {0.0, 0.0, 0.0};
    ProPoint3d target_point = {0.0, 0.0, 0.0};
    core::Dwg3GroupOutline reference_outline = {};
    bool has_reference_outline = false;
    TraditionalBalloonSide side = TraditionalBalloonSide::Right;
    int selection_order = 0;
};

struct RebuildBomRecordCandidate {
    CustomBalloonCandidate bom = {};
    std::vector<int> record_indices;
    int region_id = PRO_VALUE_UNUSED;
    bool used = false;
};

struct TraditionalBomBalloonMoveDiagnostics {
    ProError data_status = PRO_TK_GENERAL_ERROR;
    ProError attachment_get_status = PRO_TK_GENERAL_ERROR;
    ProError attachment_read_status = PRO_TK_GENERAL_ERROR;
    ProError attachment_set_status = PRO_TK_GENERAL_ERROR;
    ProError data_attachment_status = PRO_TK_GENERAL_ERROR;
    ProError modify_status = PRO_TK_GENERAL_ERROR;
    ProError alloc_status = PRO_TK_GENERAL_ERROR;
    ProError attachtype_status = PRO_TK_GENERAL_ERROR;
    ProError fallback_attachment_status = PRO_TK_GENERAL_ERROR;
    ProError fallback_modify_status = PRO_TK_GENERAL_ERROR;
    ProError show_status = PRO_TK_GENERAL_ERROR;
    ProError draw_status = PRO_TK_GENERAL_ERROR;
    ProDtlattachType original_attach_type = PRO_DTLATTACHTYPE_UNIMPLEMENTED;
    bool used_existing_attachment = false;
    bool used_fallback_attachment = false;
};

struct TraditionalStaggerDecision {
    bool enable = false;
    int long_leader_risk = 0;
    int overlap_risk_sides = 0;
    double worst_expected_leader_len = 0.0;
    int worst_symbol = -1;
};


struct TraditionalPostCleanAudit {
    int symbols = 0;
    int actual = 0;
    int matched = 0;
    int side_mismatch = 0;
    int long_leader = 0;
    int leader_crossings = 0;
    double worst_leader_len = 0.0;
    int worst_symbol = -1;
};

int TraditionalAuditScore(const TraditionalPostCleanAudit &audit)
{
    return audit.leader_crossings * 10000 + audit.side_mismatch * 1000 + audit.long_leader * 100 +
           static_cast<int>(std::ceil(audit.worst_leader_len));
}

struct OfficialBomLeaderSample {
    ProDtlsyminst symbol = {};
    ProView view = nullptr;
    std::wstring view_key;
    ProMdl model = nullptr;
    ProAsmcomppath component_path = {};
    bool has_component_path = false;
    bool allow_null_component_path = false;
    ProModelitem leader_model_item = {};
    bool has_leader_model_item = false;
    ProPoint3d leader_point = {0.0, 0.0, 0.0};
    ProPoint3d drawing_point = {0.0, 0.0, 0.0};
    bool has_drawing_point = false;
    bool matched = false;
};

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

template <size_t N>
void CopyWStr(wchar_t (&dest)[N], const wchar_t *src)
{
    if (N == 0) {
        return;
    }
    size_t i = 0;
    if (src != nullptr) {
        while (i + 1 < N && src[i] != L'\0') {
            dest[i] = src[i];
            ++i;
        }
    }
    dest[i] = L'\0';
}

std::wstring ViewIdentityKey(ProDrawing drawing, ProView view)
{
    if (drawing != nullptr && view != nullptr) {
        ProName name = {0};
        if (ProDrawingViewNameGet(drawing, view, name) == PRO_TK_NO_ERROR && name[0] != L'\0') {
            return std::wstring(L"name:") + name;
        }
    }
    return std::wstring(L"ptr:") + std::to_wstring(reinterpret_cast<std::uintptr_t>(view));
}

ProView ViewFromAttachment(ProDtlattach attachment)
{
    if (attachment == nullptr) {
        return nullptr;
    }

    ProDtlattachType attach_type = PRO_DTLATTACHTYPE_UNIMPLEMENTED;
    ProView view = nullptr;
    ProVector location = {0.0, 0.0, 0.0};
    ProSelection attach_selection = nullptr;
    const ProError st_get = ProDtlattachGet(attachment, &attach_type, &view, location, &attach_selection);
    if (view == nullptr && attach_selection != nullptr) {
        ProView selection_view = nullptr;
        if (ProSelectionViewGet(attach_selection, &selection_view) == PRO_TK_NO_ERROR) {
            view = selection_view;
        }
    }
    if (attach_selection != nullptr) {
        ProSelectionFree(&attach_selection);
    }
    return (st_get == PRO_TK_NO_ERROR) ? view : nullptr;
}

ProView ResolveViewFromNote(ProDtlnote *note, const Drawing3LogSink &log_sink, int selection_index)
{
    if (note == nullptr) {
        return nullptr;
    }

    ProDtlnotedata note_data = nullptr;
    ProDtlattach attachment = nullptr;
    ProDtlattach *leaders = nullptr;
    ProView resolved_view = nullptr;

    const ProError st_data = ProDtlnoteDataGet(note, nullptr, PRODISPMODE_SYMBOLIC, &note_data);
    if (st_data == PRO_TK_NO_ERROR && note_data != nullptr) {
        const ProError st_attachment = ProDtlnotedataAttachmentGet(note_data, &attachment);
        if (st_attachment == PRO_TK_NO_ERROR && attachment != nullptr) {
            resolved_view = ViewFromAttachment(attachment);
        }

        int leader_count = 0;
        const ProError st_leaders = ProDtlnotedataLeadersCollect(note_data, &leaders);
        if (st_leaders == PRO_TK_NO_ERROR && leaders != nullptr) {
            ProArraySizeGet(reinterpret_cast<ProArray>(leaders), &leader_count);
            for (int i = 0; i < leader_count && resolved_view == nullptr; ++i) {
                resolved_view = ViewFromAttachment(leaders[i]);
            }
        }

        LogLine(log_sink,
                "ArrangeBalloons resolve-view-from-note selection=%d note_id=%d data=%d leaders=%d resolved=%d",
                selection_index,
                note->id,
                static_cast<int>(st_data),
                leader_count,
                resolved_view != nullptr ? 1 : 0);
    } else {
        LogLine(log_sink,
                "ArrangeBalloons resolve-view-from-note selection=%d note_id=%d data=%d resolved=0",
                selection_index,
                note->id,
                static_cast<int>(st_data));
    }

    if (leaders != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&leaders));
    }
    if (attachment != nullptr) {
        ProDtlattachFree(attachment);
    }
    if (note_data != nullptr) {
        ProDtlnotedataFree(note_data);
    }
    return resolved_view;
}

ProMdl ResolveBalloonModelFromSelection(ProDrawing drawing,
                                        ProView view,
                                        ProSelection selection,
                                        const ProModelitem &model_item,
                                        const Drawing3LogSink &log_sink,
                                        int selection_index)
{
    ProAsmcomppath component_path;
    std::memset(&component_path, 0, sizeof(component_path));
    ProMdl model = nullptr;
    const ProError st_path = ProSelectionAsmcomppathGet(selection, &component_path);
    if (st_path == PRO_TK_NO_ERROR &&
        ProAsmcomppathMdlGet(&component_path, &model) == PRO_TK_NO_ERROR &&
        model != nullptr &&
        autobbox::creo::IsPartOrAsm(model)) {
        LogLine(log_sink,
                "ArrangeBalloons custom-model selection=%d source=asmcomppath model=%s",
                selection_index,
                autobbox::creo::DefaultModelTag(model).c_str());
        return model;
    }

    if (model_item.owner != nullptr && autobbox::creo::IsPartOrAsm(model_item.owner)) {
        LogLine(log_sink,
                "ArrangeBalloons custom-model selection=%d source=modelitem-owner model=%s",
                selection_index,
                autobbox::creo::DefaultModelTag(model_item.owner).c_str());
        return model_item.owner;
    }

    ProSolid view_solid = nullptr;
    const ProError st_solid = (drawing != nullptr && view != nullptr)
                                  ? ProDrawingViewSolidGet(drawing, view, &view_solid)
                                  : PRO_TK_BAD_INPUTS;
    model = reinterpret_cast<ProMdl>(view_solid);
    if (st_solid == PRO_TK_NO_ERROR && model != nullptr && autobbox::creo::IsPartOrAsm(model)) {
        LogLine(log_sink,
                "ArrangeBalloons custom-model selection=%d source=view-solid model=%s",
                selection_index,
                autobbox::creo::DefaultModelTag(model).c_str());
        return model;
    }

    LogLine(log_sink,
            "SKIP arrange-balloons custom selection=%d reason=no-model asm_path_status=%d view_solid_status=%d owner=%p",
            selection_index,
            static_cast<int>(st_path),
            static_cast<int>(st_solid),
            static_cast<void *>(model_item.owner));
    return nullptr;
}

std::wstring CustomBalloonLabel(ProMdl model, const BalloonArrangeOptions &options)
{
    if (options.label_source == BalloonArrangeLabelSource::ModelName) {
        return autobbox::creo::ModelName(model);
    }

    std::wstring label;
    const std::wstring param_name = autobbox::creo::NormalizeParameterName(options.parameter_name);
    if (!param_name.empty() &&
        autobbox::creo::ReadParamDisplayValueOnModel(model, param_name.c_str(), label) &&
        !label.empty()) {
        return label;
    }
    return options.fallback_to_model_name ? autobbox::creo::ModelName(model) : std::wstring();
}

bool SameModel(ProMdl lhs, ProMdl rhs)
{
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }
    return lhs == rhs ||
           (autobbox::creo::ModelType(lhs) == autobbox::creo::ModelType(rhs) &&
            autobbox::creo::ModelName(lhs) == autobbox::creo::ModelName(rhs));
}

std::string DtlSymbolKey(const ProDtlsyminst &symbol)
{
    char buffer[96] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%p:%d:%d",
                  static_cast<void *>(symbol.owner),
                  static_cast<int>(symbol.type),
                  symbol.id);
    return std::string(buffer);
}

bool ComputeDrawingPointFromMemberPoint(ProDrawing drawing,
                                        ProView view,
                                        const ProPoint3d member_point,
                                        const ProAsmcomppath &component_path,
                                        bool has_component_path,
                                        ProPoint3d drawing_point_out)
{
    if (drawing == nullptr || view == nullptr || member_point == nullptr || drawing_point_out == nullptr) {
        return false;
    }

    ProVector local_point = {member_point[0], member_point[1], member_point[2]};
    ProVector view_point = {member_point[0], member_point[1], member_point[2]};
    if (has_component_path) {
        ProAsmcomppath path = component_path;
        ProMatrix component_trf = {{0.0}};
        if (ProAsmcomppathTrfGet(&path, PRO_B_TRUE, component_trf) != PRO_TK_NO_ERROR ||
            ProPntTrfEval(local_point, component_trf, view_point) != PRO_TK_NO_ERROR) {
            return false;
        }
    }

    ProMatrix view_to_drawing = {{0.0}};
    ProVector drawing_point = {0.0, 0.0, 0.0};
    if (ProDrawingViewTransformGet(drawing, view, PRO_B_TRUE, view_to_drawing) != PRO_TK_NO_ERROR ||
        ProPntTrfEval(view_point, view_to_drawing, drawing_point) != PRO_TK_NO_ERROR) {
        return false;
    }

    drawing_point_out[0] = drawing_point[0];
    drawing_point_out[1] = drawing_point[1];
    drawing_point_out[2] = drawing_point[2];
    return true;
}


bool ComputeComponentDrawingOutline(ProDrawing drawing,
                                    ProView view,
                                    ProMdl model,
                                    const ProAsmcomppath &component_path,
                                    bool has_component_path,
                                    core::Dwg3GroupOutline &outline_out)
{
    outline_out = {};
    if (drawing == nullptr || view == nullptr || model == nullptr) {
        return false;
    }

    Pro3dPnt model_outline[2] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
    if (ProSolidOutlineGet(reinterpret_cast<ProSolid>(model), model_outline) != PRO_TK_NO_ERROR) {
        return false;
    }

    ProMatrix component_trf = {{0.0}};
    bool use_component_trf = false;
    if (has_component_path) {
        ProAsmcomppath path = component_path;
        if (ProAsmcomppathTrfGet(&path, PRO_B_TRUE, component_trf) != PRO_TK_NO_ERROR) {
            return false;
        }
        use_component_trf = true;
    }

    ProMatrix view_to_drawing = {{0.0}};
    if (ProDrawingViewTransformGet(drawing, view, PRO_B_TRUE, view_to_drawing) != PRO_TK_NO_ERROR) {
        return false;
    }

    bool have = false;
    for (int ix = 0; ix < 2; ++ix) {
        for (int iy = 0; iy < 2; ++iy) {
            for (int iz = 0; iz < 2; ++iz) {
                ProVector local = {
                    model_outline[ix][0],
                    model_outline[iy][1],
                    model_outline[iz][2]};
                ProVector assembly = {local[0], local[1], local[2]};
                if (use_component_trf && ProPntTrfEval(local, component_trf, assembly) != PRO_TK_NO_ERROR) {
                    return false;
                }
                ProVector drawing_point = {0.0, 0.0, 0.0};
                if (ProPntTrfEval(assembly, view_to_drawing, drawing_point) != PRO_TK_NO_ERROR) {
                    return false;
                }
                if (!have) {
                    outline_out.min_x = outline_out.max_x = drawing_point[0];
                    outline_out.min_y = outline_out.max_y = drawing_point[1];
                    have = true;
                } else {
                    outline_out.min_x = std::min(outline_out.min_x, drawing_point[0]);
                    outline_out.max_x = std::max(outline_out.max_x, drawing_point[0]);
                    outline_out.min_y = std::min(outline_out.min_y, drawing_point[1]);
                    outline_out.max_y = std::max(outline_out.max_y, drawing_point[1]);
                }
            }
        }
    }
    return have;
}

struct ComponentPathFindContext {
    ProMdl target_model = nullptr;
    ProAsmcomppath path = {};
    bool found = false;
};

ProError FindDisplayedComponentPathVisitAction(ProAsmcomppath *path,
                                               ProSolid handle,
                                               ProBoolean down,
                                               ProAppData app_data)
{
    if (path == nullptr || handle == nullptr || down != PRO_B_TRUE || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    ComponentPathFindContext *ctx = reinterpret_cast<ComponentPathFindContext *>(app_data);
    if (ctx->target_model != nullptr && SameModel(reinterpret_cast<ProMdl>(handle), ctx->target_model)) {
        ctx->path = *path;
        ctx->found = true;
        return PRO_TK_E_FOUND;
    }

    return PRO_TK_NO_ERROR;
}

bool FindDisplayedComponentPath(ProMdl assembly_model, ProMdl target_model, ProAsmcomppath &path_out)
{
    std::memset(&path_out, 0, sizeof(path_out));
    if (assembly_model == nullptr ||
        target_model == nullptr ||
        SameModel(assembly_model, target_model) ||
        !autobbox::creo::IsPartOrAsm(assembly_model)) {
        return false;
    }

    ComponentPathFindContext ctx = {};
    ctx.target_model = target_model;
    const ProError st_visit = ProSolidDispCompVisit(
        reinterpret_cast<ProSolid>(assembly_model),
        FindDisplayedComponentPathVisitAction,
        nullptr,
        &ctx);
    if (!ctx.found) {
        return false;
    }

    path_out = ctx.path;
    return st_visit == PRO_TK_NO_ERROR || st_visit == PRO_TK_E_FOUND;
}

std::vector<ProView> CollectSheetViews(ProDrawing drawing, int sheet, const Drawing3LogSink &log_sink)
{
    std::vector<ProView> views;
    if (drawing == nullptr || sheet <= 0) {
        return views;
    }

    ProView *all_views = nullptr;
    const ProError st_collect = ProDrawingViewsCollect(drawing, &all_views);
    int count = 0;
    if (st_collect == PRO_TK_NO_ERROR && all_views != nullptr) {
        ProArraySizeGet(reinterpret_cast<ProArray>(all_views), &count);
    }
    for (int i = 0; i < count; ++i) {
        int view_sheet = 0;
        if (ProDrawingViewSheetGet(drawing, all_views[i], &view_sheet) == PRO_TK_NO_ERROR &&
            view_sheet == sheet) {
            views.push_back(all_views[i]);
        }
    }
    LogLine(log_sink,
            "ArrangeBalloons collect-sheet-views status=%d count=%d sheet_views=%zu",
            static_cast<int>(st_collect),
            count,
            views.size());
    if (all_views != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&all_views));
    }
    return views;
}

ProView FindAnchorViewForBomRow(ProDrawing drawing,
                                int sheet,
                                ProMdl table_assembly,
                                ProMdl row_model,
                                const std::vector<ProView> &sheet_views)
{
    for (ProView view : sheet_views) {
        ProSolid solid = nullptr;
        if (ProDrawingViewSolidGet(drawing, view, &solid) != PRO_TK_NO_ERROR || solid == nullptr) {
            continue;
        }
        ProMdl view_model = reinterpret_cast<ProMdl>(solid);
        if (SameModel(view_model, table_assembly) || SameModel(view_model, row_model)) {
            return view;
        }
    }
    return sheet_views.empty() ? nullptr : sheet_views.front();
}

bool SelectionIsBomTableCell(ProSelection selection,
                             ProDwgtable &table_out,
                             int &segment_out,
                             int &row_out,
                             int &column_out)
{
    table_out = {};
    segment_out = PRO_VALUE_UNUSED;
    row_out = 0;
    column_out = 0;
    if (selection == nullptr) {
        return false;
    }

    ProDwgtable table = {};
    int segment = PRO_VALUE_UNUSED;
    int row0 = 0;
    int col0 = 0;
    if (ProSelectionDwgtableGet(selection, &table) != PRO_TK_NO_ERROR ||
        ProSelectionDwgtblcellGet(selection, &segment, &row0, &col0) != PRO_TK_NO_ERROR) {
        return false;
    }

    table_out = table;
    segment_out = segment;
    row_out = row0 + 1;
    column_out = col0 + 1;
    return true;
}

std::uint64_t DwgtableKey(const ProDwgtable &table)
{
    return (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(table.owner)) << 32) ^
           static_cast<std::uint64_t>(table.id);
}

bool CollectSelectedBomTableSegments(std::vector<SelectedBomTableSegment> &segments,
                                     BalloonArrangeSummary &summary,
                                     const Drawing3LogSink &log_sink)
{
    segments.clear();

    ProSelection *buffer = nullptr;
    const ProError st = ProSelbufferSelectionsGet(&buffer);
    if (st == PRO_TK_E_NOT_FOUND) {
        return true;
    }
    if (st != PRO_TK_NO_ERROR) {
        summary.first_error = st;
        LogLine(log_sink, "FAIL arrange-balloons bom-table reason=selection-buffer status=%d", static_cast<int>(st));
        return false;
    }

    int count = 0;
    ProArraySizeGet(reinterpret_cast<ProArray>(buffer), &count);
    summary.selected_total = std::max(summary.selected_total, count);

    for (int i = 0; i < count; ++i) {
        ProDwgtable table = {};
        int segment = PRO_VALUE_UNUSED;
        int row = 0;
        int column = 0;
        if (!SelectionIsBomTableCell(buffer[i], table, segment, row, column)) {
            continue;
        }

        const bool exists = std::any_of(segments.begin(), segments.end(), [&](const SelectedBomTableSegment &seg) {
            return DwgtableKey(seg.table) == DwgtableKey(table) && seg.segment == segment;
        });
        if (!exists) {
            SelectedBomTableSegment seg = {};
            seg.table = table;
            seg.segment = segment;
            segments.push_back(seg);
        }
        LogLine(log_sink,
                "ArrangeBalloons selected-bom-table-cell selection=%d table=%p segment=%d row=%d column=%d",
                i,
                static_cast<void *>(table.owner),
                segment,
                row,
                column);
    }

    ProSelectionarrayFree(buffer);
    return true;
}

bool ResolveBomCellModel(ProDwgtable *table,
                         int column,
                         int row,
                         ProMdl &model_out,
                         ProMdl &assembly_out,
                         ProAsmcomppath &component_path_out,
                         bool &has_component_path_out)
{
    model_out = nullptr;
    assembly_out = nullptr;
    std::memset(&component_path_out, 0, sizeof(component_path_out));
    has_component_path_out = false;

    ProAsmcomppath component_path;
    std::memset(&component_path, 0, sizeof(component_path));
    if (ProDwgtableCellComponentGet(table, column, row, &component_path) == PRO_TK_NO_ERROR) {
        ProMdl component_model = nullptr;
        if (ProAsmcomppathMdlGet(&component_path, &component_model) == PRO_TK_NO_ERROR &&
            component_model != nullptr &&
            autobbox::creo::IsPartOrAsm(component_model)) {
            model_out = component_model;
            component_path_out = component_path;
            has_component_path_out = true;
        }
    }

    ProAssembly assembly = nullptr;
    ProMdl ref_model = nullptr;
    if (ProDwgtableCellRefmodelGet(table, column, row, &assembly, &ref_model) == PRO_TK_NO_ERROR) {
        if (ref_model != nullptr && autobbox::creo::IsPartOrAsm(ref_model)) {
            model_out = ref_model;
        }
        if (assembly != nullptr) {
            assembly_out = reinterpret_cast<ProMdl>(assembly);
        }
    }

    if (!has_component_path_out &&
        assembly_out != nullptr &&
        model_out != nullptr &&
        FindDisplayedComponentPath(assembly_out, model_out, component_path_out)) {
        has_component_path_out = true;
    }

    return model_out != nullptr;
}

struct LeaderSurfacePick {
    ProSolid solid = nullptr;
    ProSurface surface = nullptr;
    ProModelitem geom_item = {};
    double area = -1.0;
};

ProError LeaderSurfaceVisitAction(ProSurface surface, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || surface == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    LeaderSurfacePick *pick = reinterpret_cast<LeaderSurfacePick *>(app_data);
    double area = 0.0;
    if (ProSurfaceAreaEval(surface, &area) != PRO_TK_NO_ERROR) {
        area = 0.0;
    }

    if (pick->surface == nullptr || area > pick->area) {
        ProGeomitem geom_item = {};
        if (ProSurfaceToGeomitem(pick->solid, surface, &geom_item) == PRO_TK_NO_ERROR) {
            pick->surface = surface;
            pick->geom_item = geom_item;
            pick->area = area;
        }
    }
    return PRO_TK_NO_ERROR;
}

bool PickLeaderSurfaceAndPoint(ProMdl model,
                               const ProVector preferred_local_point,
                               ProModelitem &surface_item_out,
                               ProVector surface_point_out)
{
    surface_item_out = {};
    if (surface_point_out == nullptr || model == nullptr || !autobbox::creo::IsPartOrAsm(model)) {
        return false;
    }

    LeaderSurfacePick pick = {};
    pick.solid = reinterpret_cast<ProSolid>(model);
    const ProError st_visit = ProSolidSurfaceVisit(pick.solid, LeaderSurfaceVisitAction, nullptr, &pick);
    if (st_visit != PRO_TK_NO_ERROR || pick.surface == nullptr) {
        return false;
    }

    ProUvParam uv = {0.0, 0.0};
    ProVector local_point = {
        preferred_local_point[0],
        preferred_local_point[1],
        preferred_local_point[2]};
    if (ProSurfaceParamEval(pick.solid, pick.surface, local_point, uv) != PRO_TK_NO_ERROR) {
        return false;
    }

    ProVector deriv1[2] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
    ProVector deriv2[3] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
    ProVector normal = {0.0, 0.0, 0.0};
    if (ProSurfaceXyzdataEval(pick.surface, uv, surface_point_out, deriv1, deriv2, normal) != PRO_TK_NO_ERROR) {
        return false;
    }

    surface_item_out = pick.geom_item;
    return true;
}

bool ComputeComponentDrawingAttachPoint(ProDrawing drawing,
                                        ProView view,
                                        ProMdl model,
                                        const ProAsmcomppath &component_path,
                                        bool has_component_path,
                                        ProPoint3d attach_point_out,
                                        ProPoint3d leader_point_out,
                                        ProModelitem &leader_model_item_out,
                                        bool &has_leader_model_item_out)
{
    leader_model_item_out = {};
    has_leader_model_item_out = false;
    if (drawing == nullptr || view == nullptr || model == nullptr || attach_point_out == nullptr || leader_point_out == nullptr) {
        return false;
    }

    Pro3dPnt outline[2] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
    if (ProSolidOutlineGet(reinterpret_cast<ProSolid>(model), outline) != PRO_TK_NO_ERROR) {
        return false;
    }

    ProVector local_center = {
        (outline[0][0] + outline[1][0]) * 0.5,
        (outline[0][1] + outline[1][1]) * 0.5,
        (outline[0][2] + outline[1][2]) * 0.5};
    ProVector local_attach = {local_center[0], local_center[1], local_center[2]};
    ProModelitem surface_item = {};
    if (PickLeaderSurfaceAndPoint(model, local_center, surface_item, local_attach)) {
        leader_model_item_out = surface_item;
        has_leader_model_item_out = true;
    }

    ProVector assembly_center = {local_center[0], local_center[1], local_center[2]};
    ProVector assembly_attach = {local_attach[0], local_attach[1], local_attach[2]};

    if (has_component_path) {
        ProAsmcomppath path = component_path;
        ProMatrix component_trf = {{0.0}};
        if (ProAsmcomppathTrfGet(&path, PRO_B_TRUE, component_trf) != PRO_TK_NO_ERROR ||
            ProPntTrfEval(local_center, component_trf, assembly_center) != PRO_TK_NO_ERROR ||
            ProPntTrfEval(local_attach, component_trf, assembly_attach) != PRO_TK_NO_ERROR) {
            return false;
        }
    }

    ProMatrix view_to_drawing = {{0.0}};
    ProVector drawing_point = {0.0, 0.0, 0.0};
    if (ProDrawingViewTransformGet(drawing, view, PRO_B_TRUE, view_to_drawing) != PRO_TK_NO_ERROR ||
        ProPntTrfEval(assembly_attach, view_to_drawing, drawing_point) != PRO_TK_NO_ERROR) {
        return false;
    }

    attach_point_out[0] = drawing_point[0];
    attach_point_out[1] = drawing_point[1];
    attach_point_out[2] = drawing_point[2];
    leader_point_out[0] = local_attach[0];
    leader_point_out[1] = local_attach[1];
    leader_point_out[2] = local_attach[2];
    return true;
}

bool CollectDrawingSymbols(ProDrawing drawing, int sheet, std::vector<ProDtlsyminst> &symbols)
{
    symbols.clear();
    if (drawing == nullptr || sheet <= 0) {
        return false;
    }

    ProDtlsyminst *buffer = nullptr;
    const ProError st_collect = ProDrawingDtlsyminstsCollect(drawing, sheet, &buffer);
    if (st_collect == PRO_TK_E_NOT_FOUND) {
        return true;
    }
    if (st_collect != PRO_TK_NO_ERROR || buffer == nullptr) {
        return false;
    }

    int count = 0;
    ProArraySizeGet(reinterpret_cast<ProArray>(buffer), &count);
    symbols.reserve(symbols.size() + static_cast<size_t>(std::max(count, 0)));
    for (int i = 0; i < count; ++i) {
        symbols.push_back(buffer[i]);
    }
    ProArrayFree(reinterpret_cast<ProArray *>(&buffer));
    return true;
}

ProError AllocDoubleProArray(const double *values, int count, double **array_out)
{
    if (values == nullptr || count <= 0 || array_out == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }
    *array_out = nullptr;
    ProError st = ProArrayAlloc(0, sizeof(double), count, reinterpret_cast<ProArray *>(array_out));
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }
    st = ProArrayObjectAdd(reinterpret_cast<ProArray *>(array_out), PRO_VALUE_UNUSED, count, const_cast<double *>(values));
    if (st != PRO_TK_NO_ERROR) {
        ProArrayFree(reinterpret_cast<ProArray *>(array_out));
        *array_out = nullptr;
    }
    return st;
}

ProError AllocIntProArray(const int *values, int count, int **array_out)
{
    if (values == nullptr || count <= 0 || array_out == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }
    *array_out = nullptr;
    ProError st = ProArrayAlloc(0, sizeof(int), count, reinterpret_cast<ProArray *>(array_out));
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }
    st = ProArrayObjectAdd(reinterpret_cast<ProArray *>(array_out), PRO_VALUE_UNUSED, count, const_cast<int *>(values));
    if (st != PRO_TK_NO_ERROR) {
        ProArrayFree(reinterpret_cast<ProArray *>(array_out));
        *array_out = nullptr;
    }
    return st;
}

std::unordered_set<std::string> SymbolKeySet(const std::vector<ProDtlsyminst> &symbols)
{
    std::unordered_set<std::string> keys;
    keys.reserve(symbols.size());
    for (const ProDtlsyminst &symbol : symbols) {
        keys.insert(DtlSymbolKey(symbol));
    }
    return keys;
}

bool ExtractOfficialBomLeaderSample(ProDrawing drawing,
                                    const ProDtlsyminst &symbol,
                                    OfficialBomLeaderSample &sample)
{
    sample = {};
    sample.symbol = symbol;
    if (drawing == nullptr) {
        return false;
    }

    ProDtlsyminst symbol_copy = symbol;
    ProDtlsyminstdata data = nullptr;
    ProDtlattach *leaders = nullptr;
    bool ok = false;

    const ProError st_data = ProDtlsyminstDataGet(&symbol_copy, PRODISPMODE_SYMBOLIC, &data);
    if (st_data != PRO_TK_NO_ERROR || data == nullptr) {
        return false;
    }

    const ProError st_leaders = ProDtlsyminstdataLeadersCollect(data, &leaders);
    int leader_count = 0;
    if (st_leaders == PRO_TK_NO_ERROR && leaders != nullptr) {
        ProArraySizeGet(reinterpret_cast<ProArray>(leaders), &leader_count);
    }

    for (int i = 0; i < leader_count && !ok; ++i) {
        ProDtlattachType attach_type = PRO_DTLATTACHTYPE_UNIMPLEMENTED;
        ProView attach_view = nullptr;
        ProVector attach_location = {0.0, 0.0, 0.0};
        ProSelection attach_selection = nullptr;
        if (ProDtlattachGet(leaders[i], &attach_type, &attach_view, attach_location, &attach_selection) !=
                PRO_TK_NO_ERROR ||
            attach_selection == nullptr) {
            continue;
        }

        ProModelitem model_item = {};
        if (ProSelectionModelitemGet(attach_selection, &model_item) != PRO_TK_NO_ERROR ||
            model_item.owner == nullptr) {
            continue;
        }

        ProView selection_view = nullptr;
        if (ProSelectionViewGet(attach_selection, &selection_view) == PRO_TK_NO_ERROR && selection_view != nullptr) {
            sample.view = selection_view;
        } else {
            sample.view = attach_view;
        }
        if (sample.view == nullptr) {
            continue;
        }
        sample.view_key = ViewIdentityKey(drawing, sample.view);

        ProAsmcomppath selection_path;
        std::memset(&selection_path, 0, sizeof(selection_path));
        ProMdl path_model = nullptr;
        const ProError st_path = ProSelectionAsmcomppathGet(attach_selection, &selection_path);
        if (st_path == PRO_TK_NO_ERROR &&
            ProAsmcomppathMdlGet(&selection_path, &path_model) == PRO_TK_NO_ERROR &&
            path_model != nullptr &&
            autobbox::creo::IsPartOrAsm(path_model)) {
            sample.component_path = selection_path;
            sample.has_component_path = true;
            sample.model = path_model;
        } else if (autobbox::creo::IsPartOrAsm(model_item.owner)) {
            sample.model = model_item.owner;
            sample.allow_null_component_path = true;
        }

        if (sample.model == nullptr) {
            continue;
        }

        ProPoint3d selected_point = {0.0, 0.0, 0.0};
        if (ProSelectionPoint3dGet(attach_selection, selected_point) != PRO_TK_NO_ERROR) {
            selected_point[0] = attach_location[0];
            selected_point[1] = attach_location[1];
            selected_point[2] = attach_location[2];
        }
        sample.leader_point[0] = selected_point[0];
        sample.leader_point[1] = selected_point[1];
        sample.leader_point[2] = selected_point[2];
        sample.leader_model_item = model_item;
        sample.has_leader_model_item = true;
        sample.has_drawing_point = ComputeDrawingPointFromMemberPoint(
            drawing,
            sample.view,
            selected_point,
            sample.component_path,
            sample.has_component_path,
            sample.drawing_point);
        ok = sample.has_drawing_point;
    }

    if (leaders != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&leaders));
    }
    ProDtlsyminstdataFree(data);
    return ok;
}

const char *TraditionalBalloonSideName(TraditionalBalloonSide side)
{
    switch (side) {
    case TraditionalBalloonSide::Left:
        return "left";
    case TraditionalBalloonSide::Right:
        return "right";
    case TraditionalBalloonSide::Top:
        return "top";
    case TraditionalBalloonSide::Bottom:
        return "bottom";
    }
    return "unknown";
}

bool ExtractTraditionalBomBalloonSymbol(ProDrawing drawing,
                                        ProView target_view,
                                        const std::wstring &target_view_key,
                                        const ProDtlsyminst &symbol,
                                        int selection_order,
                                        TraditionalBomBalloonSymbol &balloon,
                                        const Drawing3LogSink &log_sink)
{
    balloon = {};
    balloon.symbol = symbol;
    balloon.view = target_view;
    balloon.view_key = target_view_key;
    balloon.selection_order = selection_order;

    if (drawing == nullptr || target_view == nullptr) {
        return false;
    }

    ProDtlsyminst symbol_copy = symbol;
    ProDtlsyminstdata data = nullptr;
    ProDtlattach placement = nullptr;
    ProSelection placement_selection = nullptr;
    ProDtlattach *leaders = nullptr;
    bool ok = false;

    ProError st_data = ProDtlsyminstDataGet(&symbol_copy, PRODISPMODE_SYMBOLIC, &data);
    if (st_data != PRO_TK_NO_ERROR || data == nullptr) {
        LogLine(log_sink,
                "ArrangeBalloons traditional-skip symbol=%d reason=data status=%d",
                symbol.id,
                static_cast<int>(st_data));
        return false;
    }

    ProDtlattachType placement_type = PRO_DTLATTACHTYPE_UNIMPLEMENTED;
    ProView placement_view = nullptr;
    ProVector placement_location = {0.0, 0.0, 0.0};
    const ProError st_placement = ProDtlsyminstdataAttachmentGet(data, &placement);
    const ProError st_placement_get =
        (st_placement == PRO_TK_NO_ERROR && placement != nullptr)
            ? ProDtlattachGet(placement, &placement_type, &placement_view, placement_location, &placement_selection)
            : PRO_TK_E_NOT_FOUND;
    if (st_placement_get == PRO_TK_NO_ERROR) {
        balloon.note_point[0] = placement_location[0];
        balloon.note_point[1] = placement_location[1];
        balloon.note_point[2] = placement_location[2];
        if (placement_view != nullptr && ViewIdentityKey(drawing, placement_view) != target_view_key) {
            LogLine(log_sink,
                    "ArrangeBalloons traditional-skip symbol=%d reason=placement-other-view view=%p",
                    symbol.id,
                    static_cast<void *>(placement_view));
            if (placement_selection != nullptr) {
                ProSelectionFree(&placement_selection);
            }
            if (placement != nullptr) {
                ProDtlattachFree(placement);
            }
            ProDtlsyminstdataFree(data);
            return false;
        }
    }

    const ProError st_leaders = ProDtlsyminstdataLeadersCollect(data, &leaders);
    int leader_count = 0;
    if (st_leaders == PRO_TK_NO_ERROR && leaders != nullptr) {
        ProArraySizeGet(reinterpret_cast<ProArray>(leaders), &leader_count);
    }

    for (int i = 0; i < leader_count && !ok; ++i) {
        ProDtlattachType leader_type = PRO_DTLATTACHTYPE_UNIMPLEMENTED;
        ProView leader_view = nullptr;
        ProVector leader_location = {0.0, 0.0, 0.0};
        ProSelection leader_selection = nullptr;
        const ProError st_leader_get =
            ProDtlattachGet(leaders[i], &leader_type, &leader_view, leader_location, &leader_selection);
        if (st_leader_get != PRO_TK_NO_ERROR || leader_selection == nullptr) {
            if (leader_selection != nullptr) {
                ProSelectionFree(&leader_selection);
            }
            continue;
        }

        ProView selection_view = nullptr;
        if (ProSelectionViewGet(leader_selection, &selection_view) != PRO_TK_NO_ERROR || selection_view == nullptr) {
            selection_view = leader_view;
        }
        if (selection_view == nullptr || ViewIdentityKey(drawing, selection_view) != target_view_key) {
            ProSelectionFree(&leader_selection);
            continue;
        }

        ProModelitem model_item = {};
        if (ProSelectionModelitemGet(leader_selection, &model_item) != PRO_TK_NO_ERROR ||
            model_item.owner == nullptr) {
            ProSelectionFree(&leader_selection);
            continue;
        }

        ProPoint3d selected_point = {0.0, 0.0, 0.0};
        if (ProSelectionPoint3dGet(leader_selection, selected_point) != PRO_TK_NO_ERROR) {
            ProSelectionFree(&leader_selection);
            continue;
        }

        ProAsmcomppath component_path;
        std::memset(&component_path, 0, sizeof(component_path));
        bool has_component_path = false;
        ProMdl path_model = nullptr;
        const ProError st_path = ProSelectionAsmcomppathGet(leader_selection, &component_path);
        if (st_path == PRO_TK_NO_ERROR &&
            ProAsmcomppathMdlGet(&component_path, &path_model) == PRO_TK_NO_ERROR &&
            path_model != nullptr) {
            has_component_path = true;
        }

        ok = ComputeDrawingPointFromMemberPoint(
            drawing,
            target_view,
            selected_point,
            component_path,
            has_component_path,
            balloon.leader_point);
        if (ok) {
            balloon.component_attach_point[0] = selected_point[0];
            balloon.component_attach_point[1] = selected_point[1];
            balloon.component_attach_point[2] = selected_point[2];
            balloon.has_component_attach_point = true;
            balloon.leader_model_item = model_item;
            balloon.has_leader_model_item = true;
            if (has_component_path && autobbox::creo::IsPartOrAsm(path_model)) {
                balloon.component_path = component_path;
                balloon.has_component_path = true;
                balloon.model = path_model;
            } else if (autobbox::creo::IsPartOrAsm(model_item.owner)) {
                balloon.model = model_item.owner;
                balloon.allow_null_component_path = true;
            }
            if (balloon.model != nullptr) {
                balloon.has_reference_outline = ComputeComponentDrawingOutline(
                    drawing,
                    target_view,
                    balloon.model,
                    balloon.component_path,
                    balloon.has_component_path,
                    balloon.reference_outline);
            }
        }

        ProSelectionFree(&leader_selection);
    }

    LogLine(log_sink,
            "ArrangeBalloons traditional-candidate symbol=%d data=%d placement=%d/%d leaders=%d ok=%d note=(%.3f,%.3f) leader=(%.3f,%.3f) ref_outline=%d ref_box=(%.3f,%.3f)-(%.3f,%.3f)",
            symbol.id,
            static_cast<int>(st_data),
            static_cast<int>(st_placement),
            static_cast<int>(st_placement_get),
            leader_count,
            ok ? 1 : 0,
            balloon.note_point[0],
            balloon.note_point[1],
            balloon.leader_point[0],
            balloon.leader_point[1],
            balloon.has_reference_outline ? 1 : 0,
            balloon.reference_outline.min_x,
            balloon.reference_outline.min_y,
            balloon.reference_outline.max_x,
            balloon.reference_outline.max_y);

    if (leaders != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&leaders));
    }
    if (placement_selection != nullptr) {
        ProSelectionFree(&placement_selection);
    }
    if (placement != nullptr) {
        ProDtlattachFree(placement);
    }
    ProDtlsyminstdataFree(data);
    return ok;
}

void SpreadVerticalTraditionalBalloons(std::vector<TraditionalBomBalloonSymbol *> items,
                                       double x,
                                       double min_y,
                                       double max_y)
{
    if (items.empty()) {
        return;
    }

    std::sort(items.begin(), items.end(), [](const TraditionalBomBalloonSymbol *lhs,
                                             const TraditionalBomBalloonSymbol *rhs) {
        if (std::abs(lhs->leader_point[1] - rhs->leader_point[1]) > 1.0e-9) {
            return lhs->leader_point[1] > rhs->leader_point[1];
        }
        return lhs->selection_order < rhs->selection_order;
    });

    double previous_y = max_y + kCustomBalloonVerticalSpacing;
    for (TraditionalBomBalloonSymbol *item : items) {
        double y = std::max(min_y, std::min(max_y, item->leader_point[1]));
        y = std::min(y, previous_y - kCustomBalloonVerticalSpacing);
        item->target_point[0] = x;
        item->target_point[1] = y;
        item->target_point[2] = 0.0;
        previous_y = y;
    }

    if (items.back()->target_point[1] < min_y) {
        const double shift_up = min_y - items.back()->target_point[1];
        for (TraditionalBomBalloonSymbol *item : items) {
            item->target_point[1] += shift_up;
        }
    }
}

void SpreadHorizontalTraditionalBalloons(std::vector<TraditionalBomBalloonSymbol *> items,
                                         double y,
                                         double min_x,
                                         double max_x)
{
    if (items.empty()) {
        return;
    }

    std::sort(items.begin(), items.end(), [](const TraditionalBomBalloonSymbol *lhs,
                                             const TraditionalBomBalloonSymbol *rhs) {
        if (std::abs(lhs->leader_point[0] - rhs->leader_point[0]) > 1.0e-9) {
            return lhs->leader_point[0] < rhs->leader_point[0];
        }
        return lhs->selection_order < rhs->selection_order;
    });

    double previous_x = min_x - kCustomBalloonVerticalSpacing;
    for (TraditionalBomBalloonSymbol *item : items) {
        double x = std::max(min_x, std::min(max_x, item->leader_point[0]));
        x = std::max(x, previous_x + kCustomBalloonVerticalSpacing);
        item->target_point[0] = x;
        item->target_point[1] = y;
        item->target_point[2] = 0.0;
        previous_x = x;
    }

    if (items.back()->target_point[0] > max_x) {
        const double shift_left = items.back()->target_point[0] - max_x;
        for (TraditionalBomBalloonSymbol *item : items) {
            item->target_point[0] -= shift_left;
        }
    }
}

void LayoutTraditionalBomBalloonsOnFourSides(ProDrawing drawing,
                                             ProView view,
                                             std::vector<TraditionalBomBalloonSymbol> &balloons,
                                             const Drawing3LogSink &log_sink)
{
    core::Dwg3GroupOutline outline = {};
    const ProError st_outline = GetDrawingViewOutlineBox(drawing, view, outline);
    if (st_outline != PRO_TK_NO_ERROR) {
        LogLine(log_sink,
                "ArrangeBalloons traditional-layout outline-status=%d",
                static_cast<int>(st_outline));
        return;
    }

    const double left_x = outline.min_x - kCustomBalloonOutsideOffset;
    const double right_x = outline.max_x + kCustomBalloonOutsideOffset;
    const double top_y = outline.max_y + kCustomBalloonOutsideOffset;
    const double bottom_y = outline.min_y - kCustomBalloonOutsideOffset;

    LogLine(log_sink,
            "ArrangeBalloons traditional-snaplines view=%p left_x=%.3f right_x=%.3f top_y=%.3f bottom_y=%.3f outline=(%.3f,%.3f,%.3f,%.3f)",
            static_cast<void *>(view),
            left_x,
            right_x,
            top_y,
            bottom_y,
            outline.min_x,
            outline.min_y,
            outline.max_x,
            outline.max_y);

    std::vector<TraditionalBomBalloonSymbol *> left;
    std::vector<TraditionalBomBalloonSymbol *> right;
    std::vector<TraditionalBomBalloonSymbol *> top;
    std::vector<TraditionalBomBalloonSymbol *> bottom;

    for (TraditionalBomBalloonSymbol &balloon : balloons) {
        const double dl = std::abs(balloon.leader_point[0] - left_x);
        const double dr = std::abs(balloon.leader_point[0] - right_x);
        const double dt = std::abs(balloon.leader_point[1] - top_y);
        const double db = std::abs(balloon.leader_point[1] - bottom_y);

        double best = dl;
        balloon.side = TraditionalBalloonSide::Left;
        if (dr < best) {
            best = dr;
            balloon.side = TraditionalBalloonSide::Right;
        }
        if (dt < best) {
            best = dt;
            balloon.side = TraditionalBalloonSide::Top;
        }
        if (db < best) {
            best = db;
            balloon.side = TraditionalBalloonSide::Bottom;
        }

        LogLine(log_sink,
                "ArrangeBalloons traditional-snap-distance symbol=%d leader=(%.3f,%.3f) d_left=%.3f d_right=%.3f d_top=%.3f d_bottom=%.3f chosen=%s chosen_dist=%.3f",
                balloon.symbol.id,
                balloon.leader_point[0],
                balloon.leader_point[1],
                dl,
                dr,
                dt,
                db,
                TraditionalBalloonSideName(balloon.side),
                best);

        switch (balloon.side) {
        case TraditionalBalloonSide::Left:
            left.push_back(&balloon);
            break;
        case TraditionalBalloonSide::Right:
            right.push_back(&balloon);
            break;
        case TraditionalBalloonSide::Top:
            top.push_back(&balloon);
            break;
        case TraditionalBalloonSide::Bottom:
            bottom.push_back(&balloon);
            break;
        }
    }

    SpreadVerticalTraditionalBalloons(left, left_x, outline.min_y, outline.max_y);
    SpreadVerticalTraditionalBalloons(right, right_x, outline.min_y, outline.max_y);
    SpreadHorizontalTraditionalBalloons(top, top_y, outline.min_x, outline.max_x);
    SpreadHorizontalTraditionalBalloons(bottom, bottom_y, outline.min_x, outline.max_x);

    LogLine(log_sink,
            "ArrangeBalloons traditional-layout view=%p count=%zu left=%zu right=%zu top=%zu bottom=%zu lines=(%.3f,%.3f,%.3f,%.3f)",
            static_cast<void *>(view),
            balloons.size(),
            left.size(),
            right.size(),
            top.size(),
            bottom.size(),
            left_x,
            right_x,
            top_y,
            bottom_y);
}



double TraditionalReferenceIdealY(const TraditionalBomBalloonSymbol *item)
{
    if (item != nullptr && item->has_reference_outline) {
        return (item->reference_outline.min_y + item->reference_outline.max_y) * 0.5;
    }
    return item != nullptr ? item->leader_point[1] : 0.0;
}

double TraditionalReferenceIdealX(const TraditionalBomBalloonSymbol *item)
{
    if (item != nullptr && item->has_reference_outline) {
        return (item->reference_outline.min_x + item->reference_outline.max_x) * 0.5;
    }
    return item != nullptr ? item->leader_point[0] : 0.0;
}

void AssignTwoLaneVerticalSnapline(std::vector<TraditionalBomBalloonSymbol *> items,
                                   TraditionalBalloonSide side,
                                   double base_x,
                                   double second_x,
                                   double min_y,
                                   double max_y,
                                   const Drawing3LogSink &log_sink)
{
    if (items.empty()) {
        return;
    }
    std::sort(items.begin(), items.end(), [](const TraditionalBomBalloonSymbol *lhs,
                                             const TraditionalBomBalloonSymbol *rhs) {
        const double ly = TraditionalReferenceIdealY(lhs);
        const double ry = TraditionalReferenceIdealY(rhs);
        if (std::abs(ly - ry) > 1.0e-9) {
            return ly < ry;
        }
        return lhs->selection_order < rhs->selection_order;
    });

    double last_y[2] = {-1.0e300, -1.0e300};
    int lane_count = 1;
    for (TraditionalBomBalloonSymbol *item : items) {
        double ideal = std::max(min_y, std::min(max_y, TraditionalReferenceIdealY(item)));
        int lane = 0;
        if (ideal - last_y[0] < kCustomBalloonVerticalSpacing * 0.98) {
            lane = 1;
            lane_count = 2;
        }
        double y = ideal;
        if (y - last_y[lane] < kCustomBalloonVerticalSpacing) {
            y = last_y[lane] + kCustomBalloonVerticalSpacing;
        }
        y = std::max(min_y, std::min(max_y, y));
        item->side = side;
        item->target_point[0] = lane == 0 ? base_x : second_x;
        item->target_point[1] = y;
        item->target_point[2] = 0.0;
        last_y[lane] = y;
        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-snap-place symbol=%d side=%s lane=%d leader=(%.3f,%.3f) ref_outline=%d ref_center=(%.3f,%.3f) target=(%.3f,%.3f) ideal=%.3f rule=shortest-snapline-to-reference-model",
                item->symbol.id,
                TraditionalBalloonSideName(side),
                lane + 1,
                item->leader_point[0],
                item->leader_point[1],
                item->has_reference_outline ? 1 : 0,
                TraditionalReferenceIdealX(item),
                TraditionalReferenceIdealY(item),
                item->target_point[0],
                item->target_point[1],
                ideal);
    }
    LogLine(log_sink,
            "ArrangeBalloons official-rebuild-snapline side=%s count=%d base=%.3f second=%.3f items=%zu max_lines=2",
            TraditionalBalloonSideName(side),
            lane_count,
            base_x,
            second_x,
            items.size());
}

void AssignTwoLaneHorizontalSnapline(std::vector<TraditionalBomBalloonSymbol *> items,
                                     TraditionalBalloonSide side,
                                     double base_y,
                                     double second_y,
                                     double min_x,
                                     double max_x,
                                     const Drawing3LogSink &log_sink)
{
    if (items.empty()) {
        return;
    }
    std::sort(items.begin(), items.end(), [](const TraditionalBomBalloonSymbol *lhs,
                                             const TraditionalBomBalloonSymbol *rhs) {
        const double lx = TraditionalReferenceIdealX(lhs);
        const double rx = TraditionalReferenceIdealX(rhs);
        if (std::abs(lx - rx) > 1.0e-9) {
            return lx < rx;
        }
        return lhs->selection_order < rhs->selection_order;
    });

    double last_x[2] = {-1.0e300, -1.0e300};
    int lane_count = 1;
    for (TraditionalBomBalloonSymbol *item : items) {
        double ideal = std::max(min_x, std::min(max_x, TraditionalReferenceIdealX(item)));
        int lane = 0;
        if (ideal - last_x[0] < kCustomBalloonVerticalSpacing * 0.98) {
            lane = 1;
            lane_count = 2;
        }
        double x = ideal;
        if (x - last_x[lane] < kCustomBalloonVerticalSpacing) {
            x = last_x[lane] + kCustomBalloonVerticalSpacing;
        }
        x = std::max(min_x, std::min(max_x, x));
        item->side = side;
        item->target_point[0] = x;
        item->target_point[1] = lane == 0 ? base_y : second_y;
        item->target_point[2] = 0.0;
        last_x[lane] = x;
        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-snap-place symbol=%d side=%s lane=%d leader=(%.3f,%.3f) ref_outline=%d ref_center=(%.3f,%.3f) target=(%.3f,%.3f) ideal=%.3f rule=shortest-snapline-to-reference-model",
                item->symbol.id,
                TraditionalBalloonSideName(side),
                lane + 1,
                item->leader_point[0],
                item->leader_point[1],
                item->has_reference_outline ? 1 : 0,
                TraditionalReferenceIdealX(item),
                TraditionalReferenceIdealY(item),
                item->target_point[0],
                item->target_point[1],
                ideal);
    }
    LogLine(log_sink,
            "ArrangeBalloons official-rebuild-snapline side=%s count=%d base=%.3f second=%.3f items=%zu max_lines=2",
            TraditionalBalloonSideName(side),
            lane_count,
            base_y,
            second_y,
            items.size());
}

void LayoutOfficialRebuildBalloonsTwoSnaplines(ProDrawing drawing,
                                               ProView view,
                                               std::vector<TraditionalBomBalloonSymbol> &balloons,
                                               const Drawing3LogSink &log_sink)
{
    core::Dwg3GroupOutline outline = {};
    const ProError st_outline = GetDrawingViewOutlineBox(drawing, view, outline);
    if (st_outline != PRO_TK_NO_ERROR) {
        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-layout outline-status=%d",
                static_cast<int>(st_outline));
        return;
    }

    const double left_x = outline.min_x - kCustomBalloonOutsideOffset;
    const double right_x = outline.max_x + kCustomBalloonOutsideOffset;
    const double top_y = outline.max_y + kCustomBalloonOutsideOffset;
    const double bottom_y = outline.min_y - kCustomBalloonOutsideOffset;
    const double second_left_x = left_x - kTraditionalSecondSnaplineIncrement;
    const double second_right_x = right_x + kTraditionalSecondSnaplineIncrement;
    const double second_top_y = top_y + kTraditionalSecondSnaplineIncrement;
    const double second_bottom_y = bottom_y - kTraditionalSecondSnaplineIncrement;

    std::vector<TraditionalBomBalloonSymbol *> left;
    std::vector<TraditionalBomBalloonSymbol *> right;
    std::vector<TraditionalBomBalloonSymbol *> top;
    std::vector<TraditionalBomBalloonSymbol *> bottom;

    for (TraditionalBomBalloonSymbol &balloon : balloons) {
        const core::Dwg3GroupOutline ref = balloon.has_reference_outline ? balloon.reference_outline : core::Dwg3GroupOutline{balloon.leader_point[0], balloon.leader_point[1], balloon.leader_point[0], balloon.leader_point[1]};
        const double ref_cx = (ref.min_x + ref.max_x) * 0.5;
        const double ref_cy = (ref.min_y + ref.max_y) * 0.5;
        const double dl = std::max(0.0, ref.min_x - left_x);
        const double dr = std::max(0.0, right_x - ref.max_x);
        const double dt = std::max(0.0, top_y - ref.max_y);
        const double db = std::max(0.0, ref.min_y - bottom_y);
        double best = dl;
        TraditionalBalloonSide side = TraditionalBalloonSide::Left;
        if (dr < best) { best = dr; side = TraditionalBalloonSide::Right; }
        if (dt < best) { best = dt; side = TraditionalBalloonSide::Top; }
        if (db < best) { best = db; side = TraditionalBalloonSide::Bottom; }
        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-snap-distance symbol=%d rule=shortest-snapline-to-reference-model ref_outline=%d ref_box=(%.3f,%.3f)-(%.3f,%.3f) ref_center=(%.3f,%.3f) leader=(%.3f,%.3f) d_left=%.3f d_right=%.3f d_top=%.3f d_bottom=%.3f chosen=%s chosen_dist=%.3f max_lines=2",
                balloon.symbol.id,
                balloon.has_reference_outline ? 1 : 0,
                ref.min_x,
                ref.min_y,
                ref.max_x,
                ref.max_y,
                ref_cx,
                ref_cy,
                balloon.leader_point[0],
                balloon.leader_point[1],
                dl,
                dr,
                dt,
                db,
                TraditionalBalloonSideName(side),
                best);
        switch (side) {
        case TraditionalBalloonSide::Left: left.push_back(&balloon); break;
        case TraditionalBalloonSide::Right: right.push_back(&balloon); break;
        case TraditionalBalloonSide::Top: top.push_back(&balloon); break;
        case TraditionalBalloonSide::Bottom: bottom.push_back(&balloon); break;
        }
    }

    AssignTwoLaneVerticalSnapline(left, TraditionalBalloonSide::Left, left_x, second_left_x, outline.min_y, outline.max_y, log_sink);
    AssignTwoLaneVerticalSnapline(right, TraditionalBalloonSide::Right, right_x, second_right_x, outline.min_y, outline.max_y, log_sink);
    AssignTwoLaneHorizontalSnapline(top, TraditionalBalloonSide::Top, top_y, second_top_y, outline.min_x, outline.max_x, log_sink);
    AssignTwoLaneHorizontalSnapline(bottom, TraditionalBalloonSide::Bottom, bottom_y, second_bottom_y, outline.min_x, outline.max_x, log_sink);

    LogLine(log_sink,
            "ArrangeBalloons official-rebuild-layout view=%p count=%zu left=%zu right=%zu top=%zu bottom=%zu base_lines=(%.3f,%.3f,%.3f,%.3f) second_offset=%.3f max_lines_per_side=2",
            static_cast<void *>(view),
            balloons.size(),
            left.size(),
            right.size(),
            top.size(),
            bottom.size(),
            left_x,
            right_x,
            top_y,
            bottom_y,
            kTraditionalSecondSnaplineIncrement);
}

TraditionalStaggerDecision AnalyzeTraditionalStaggerNeed(
    const std::vector<TraditionalBomBalloonSymbol> &balloons,
    const Drawing3LogSink &log_sink)
{
    TraditionalStaggerDecision decision = {};
    if (balloons.empty()) {
        return decision;
    }

    int left = 0;
    int right = 0;
    int top = 0;
    int bottom = 0;
    int overlap_left = 0;
    int overlap_right = 0;
    int overlap_top = 0;
    int overlap_bottom = 0;

    auto count_side = [&](TraditionalBalloonSide side) -> int & {
        switch (side) {
        case TraditionalBalloonSide::Left:
            return left;
        case TraditionalBalloonSide::Right:
            return right;
        case TraditionalBalloonSide::Top:
            return top;
        case TraditionalBalloonSide::Bottom:
            return bottom;
        }
        return right;
    };

    for (const TraditionalBomBalloonSymbol &balloon : balloons) {
        ++count_side(balloon.side);
        const double dx = balloon.target_point[0] - balloon.leader_point[0];
        const double dy = balloon.target_point[1] - balloon.leader_point[1];
        const double expected_len = std::sqrt(dx * dx + dy * dy);
        if (expected_len > decision.worst_expected_leader_len) {
            decision.worst_expected_leader_len = expected_len;
            decision.worst_symbol = balloon.symbol.id;
        }
        if (expected_len > kTraditionalLongLeaderThreshold) {
            ++decision.long_leader_risk;
        }
    }

    auto count_overlap_risk = [&](TraditionalBalloonSide side, bool vertical) {
        std::vector<const TraditionalBomBalloonSymbol *> side_items;
        for (const TraditionalBomBalloonSymbol &balloon : balloons) {
            if (balloon.side == side) {
                side_items.push_back(&balloon);
            }
        }
        if (side_items.size() < 2) {
            return 0;
        }
        std::sort(side_items.begin(), side_items.end(), [&](const TraditionalBomBalloonSymbol *lhs,
                                                            const TraditionalBomBalloonSymbol *rhs) {
            const double l = vertical ? lhs->target_point[1] : lhs->target_point[0];
            const double r = vertical ? rhs->target_point[1] : rhs->target_point[0];
            return l < r;
        });
        int risk = 0;
        for (size_t i = 1; i < side_items.size(); ++i) {
            const double previous = vertical ? side_items[i - 1]->target_point[1] : side_items[i - 1]->target_point[0];
            const double current = vertical ? side_items[i]->target_point[1] : side_items[i]->target_point[0];
            if (std::abs(current - previous) < kCustomBalloonVerticalSpacing * 0.95) {
                ++risk;
            }
        }
        return risk;
    };

    overlap_left = count_overlap_risk(TraditionalBalloonSide::Left, true);
    overlap_right = count_overlap_risk(TraditionalBalloonSide::Right, true);
    overlap_top = count_overlap_risk(TraditionalBalloonSide::Top, false);
    overlap_bottom = count_overlap_risk(TraditionalBalloonSide::Bottom, false);

    decision.overlap_risk_sides = (overlap_left > 0 ? 1 : 0) +
                                  (overlap_right > 0 ? 1 : 0) +
                                  (overlap_top > 0 ? 1 : 0) +
                                  (overlap_bottom > 0 ? 1 : 0);
    decision.enable = decision.long_leader_risk > 0 || decision.overlap_risk_sides > 0;

    LogLine(log_sink,
            "ArrangeBalloons traditional-stagger-decision enable=%d requested_max_lines_per_side=2 long_leader_risk=%d long_threshold=%.3f overlap_risk_sides=%d overlap=(left:%d,right:%d,top:%d,bottom:%d) counts=(left:%d,right:%d,top:%d,bottom:%d) worst_symbol=%d worst_expected_leader_len=%.3f stagger_val=%.3f note=toolkit-stagger-has-no-explicit-max-line-count",
            decision.enable ? 1 : 0,
            decision.long_leader_risk,
            kTraditionalLongLeaderThreshold,
            decision.overlap_risk_sides,
            overlap_left,
            overlap_right,
            overlap_top,
            overlap_bottom,
            left,
            right,
            top,
            bottom,
            decision.worst_symbol,
            decision.worst_expected_leader_len,
            kTraditionalSecondSnaplineIncrement);

    return decision;
}

TraditionalBalloonSide NearestOutlineSideForPoint(const core::Dwg3GroupOutline &outline,
                                                  const ProPoint3d point)
{
    const double dl = std::abs(point[0] - outline.min_x);
    const double dr = std::abs(point[0] - outline.max_x);
    const double dt = std::abs(point[1] - outline.max_y);
    const double db = std::abs(point[1] - outline.min_y);

    double best = dl;
    TraditionalBalloonSide side = TraditionalBalloonSide::Left;
    if (dr < best) {
        best = dr;
        side = TraditionalBalloonSide::Right;
    }
    if (dt < best) {
        best = dt;
        side = TraditionalBalloonSide::Top;
    }
    if (db < best) {
        side = TraditionalBalloonSide::Bottom;
    }
    return side;
}


double Orient2d(double ax, double ay, double bx, double by, double cx, double cy)
{
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

bool BetweenOpen(double value, double min_value, double max_value, double eps)
{
    return value > std::min(min_value, max_value) + eps &&
           value < std::max(min_value, max_value) - eps;
}

bool LeaderSegmentsCrossStrict(const TraditionalBomBalloonSymbol &a,
                               const TraditionalBomBalloonSymbol &b)
{
    const double ax = a.note_point[0];
    const double ay = a.note_point[1];
    const double bx = a.leader_point[0];
    const double by = a.leader_point[1];
    const double cx = b.note_point[0];
    const double cy = b.note_point[1];
    const double dx = b.leader_point[0];
    const double dy = b.leader_point[1];
    const double eps = 1.0e-6;

    auto same_point = [eps](double x1, double y1, double x2, double y2) {
        return std::abs(x1 - x2) <= eps && std::abs(y1 - y2) <= eps;
    };
    if (same_point(ax, ay, cx, cy) || same_point(ax, ay, dx, dy) ||
        same_point(bx, by, cx, cy) || same_point(bx, by, dx, dy)) {
        return false;
    }

    const double o1 = Orient2d(ax, ay, bx, by, cx, cy);
    const double o2 = Orient2d(ax, ay, bx, by, dx, dy);
    const double o3 = Orient2d(cx, cy, dx, dy, ax, ay);
    const double o4 = Orient2d(cx, cy, dx, dy, bx, by);

    if (std::abs(o1) <= eps && BetweenOpen(cx, ax, bx, eps) && BetweenOpen(cy, ay, by, eps)) return true;
    if (std::abs(o2) <= eps && BetweenOpen(dx, ax, bx, eps) && BetweenOpen(dy, ay, by, eps)) return true;
    if (std::abs(o3) <= eps && BetweenOpen(ax, cx, dx, eps) && BetweenOpen(ay, cy, dy, eps)) return true;
    if (std::abs(o4) <= eps && BetweenOpen(bx, cx, dx, eps) && BetweenOpen(by, cy, dy, eps)) return true;

    return (o1 > eps && o2 < -eps || o1 < -eps && o2 > eps) &&
           (o3 > eps && o4 < -eps || o3 < -eps && o4 > eps);
}

TraditionalPostCleanAudit AuditTraditionalBomBalloonPostClean(ProDrawing drawing,
                                                              int sheet,
                                                              ProView target_view,
                                                              const std::wstring &target_view_key,
                                                              const std::vector<TraditionalBomBalloonSymbol> &desired_balloons,
                                                              const Drawing3LogSink &log_sink)
{
    TraditionalPostCleanAudit audit = {};
    if (drawing == nullptr || sheet <= 0 || target_view == nullptr || desired_balloons.empty()) {
        return audit;
    }

    core::Dwg3GroupOutline outline = {};
    const ProError st_outline = GetDrawingViewOutlineBox(drawing, target_view, outline);
    if (st_outline != PRO_TK_NO_ERROR) {
        LogLine(log_sink,
                "ArrangeBalloons traditional-post-clean-audit outline_status=%d",
                static_cast<int>(st_outline));
        return audit;
    }

    std::vector<ProDtlsyminst> symbols;
    if (!CollectDrawingSymbols(drawing, sheet, symbols)) {
        LogLine(log_sink, "ArrangeBalloons traditional-post-clean-audit collect=0");
        return audit;
    }
    audit.symbols = static_cast<int>(symbols.size());

    std::vector<TraditionalBomBalloonSymbol> actual_balloons;
    actual_balloons.reserve(symbols.size());
    const Drawing3LogSink quiet_log;
    for (size_t i = 0; i < symbols.size(); ++i) {
        TraditionalBomBalloonSymbol balloon = {};
        if (ExtractTraditionalBomBalloonSymbol(
                drawing,
                target_view,
                target_view_key,
                symbols[i],
                static_cast<int>(i),
                balloon,
                quiet_log)) {
            actual_balloons.push_back(balloon);
        }
    }

    audit.actual = static_cast<int>(actual_balloons.size());
    int long_leader_logged = 0;
    int crossing_logged = 0;
    const double long_leader_threshold = std::max(kTraditionalLongLeaderThreshold, kCustomBalloonOutsideOffset * 5.0);

    for (const TraditionalBomBalloonSymbol &actual : actual_balloons) {
        const TraditionalBomBalloonSymbol *desired = nullptr;
        double best_leader_delta2 = 1.0e300;
        for (const TraditionalBomBalloonSymbol &candidate : desired_balloons) {
            const double dx = actual.leader_point[0] - candidate.leader_point[0];
            const double dy = actual.leader_point[1] - candidate.leader_point[1];
            const double delta2 = dx * dx + dy * dy;
            if (desired == nullptr || delta2 < best_leader_delta2) {
                desired = &candidate;
                best_leader_delta2 = delta2;
            }
        }
        if (desired == nullptr) {
            continue;
        }

        ++audit.matched;
        const TraditionalBalloonSide actual_side = NearestOutlineSideForPoint(outline, actual.note_point);
        const double dx = actual.note_point[0] - actual.leader_point[0];
        const double dy = actual.note_point[1] - actual.leader_point[1];
        const double leader_len = std::sqrt(dx * dx + dy * dy);
        if (leader_len > audit.worst_leader_len) {
            audit.worst_leader_len = leader_len;
            audit.worst_symbol = actual.symbol.id;
        }
        if (leader_len > long_leader_threshold) {
            ++audit.long_leader;
            if (long_leader_logged < 40) {
                LogLine(log_sink,
                        "ArrangeBalloons traditional-post-clean-long-leader symbol=%d matched_symbol=%d desired=%s actual=%s leader_delta=%.3f leader_len=%.3f threshold=%.3f note=(%.3f,%.3f) leader=(%.3f,%.3f)",
                        actual.symbol.id,
                        desired->symbol.id,
                        TraditionalBalloonSideName(desired->side),
                        TraditionalBalloonSideName(actual_side),
                        std::sqrt(best_leader_delta2),
                        leader_len,
                        long_leader_threshold,
                        actual.note_point[0],
                        actual.note_point[1],
                        actual.leader_point[0],
                        actual.leader_point[1]);
                ++long_leader_logged;
            }
        }
        if (actual_side != desired->side) {
            ++audit.side_mismatch;
            LogLine(log_sink,
                    "ArrangeBalloons traditional-post-clean-mismatch symbol=%d matched_symbol=%d desired=%s actual=%s leader_delta=%.3f leader_len=%.3f note=(%.3f,%.3f) leader=(%.3f,%.3f)",
                    actual.symbol.id,
                    desired->symbol.id,
                    TraditionalBalloonSideName(desired->side),
                    TraditionalBalloonSideName(actual_side),
                    std::sqrt(best_leader_delta2),
                    leader_len,
                    actual.note_point[0],
                    actual.note_point[1],
                    actual.leader_point[0],
                    actual.leader_point[1]);
        }
    }

    for (size_t i = 0; i < actual_balloons.size(); ++i) {
        for (size_t j = i + 1; j < actual_balloons.size(); ++j) {
            if (!LeaderSegmentsCrossStrict(actual_balloons[i], actual_balloons[j])) {
                continue;
            }
            ++audit.leader_crossings;
            if (crossing_logged < 60) {
                LogLine(log_sink,
                        "ArrangeBalloons traditional-post-clean-leader-cross a_symbol=%d b_symbol=%d a_note=(%.3f,%.3f) a_leader=(%.3f,%.3f) b_note=(%.3f,%.3f) b_leader=(%.3f,%.3f)",
                        actual_balloons[i].symbol.id,
                        actual_balloons[j].symbol.id,
                        actual_balloons[i].note_point[0],
                        actual_balloons[i].note_point[1],
                        actual_balloons[i].leader_point[0],
                        actual_balloons[i].leader_point[1],
                        actual_balloons[j].note_point[0],
                        actual_balloons[j].note_point[1],
                        actual_balloons[j].leader_point[0],
                        actual_balloons[j].leader_point[1]);
                ++crossing_logged;
            }
        }
    }

    LogLine(log_sink,
            "ArrangeBalloons traditional-post-clean-audit symbols=%d actual=%d matched=%d side_mismatch=%d long_leader=%d leader_crossings=%d score=%d long_threshold=%.3f worst_symbol=%d worst_leader_len=%.3f",
            audit.symbols,
            audit.actual,
            audit.matched,
            audit.side_mismatch,
            audit.long_leader,
            audit.leader_crossings,
            TraditionalAuditScore(audit),
            long_leader_threshold,
            audit.worst_symbol,
            audit.worst_leader_len);
    return audit;
}

ProError MoveTraditionalBomBalloonSymbol(const TraditionalBomBalloonSymbol &balloon,
                                         TraditionalBomBalloonMoveDiagnostics *diagnostics = nullptr)
{
    TraditionalBomBalloonMoveDiagnostics local_diag = {};
    TraditionalBomBalloonMoveDiagnostics &diag = diagnostics != nullptr ? *diagnostics : local_diag;
    diag = {};

    ProDtlsyminst symbol = balloon.symbol;
    ProDtlsyminstdata data = nullptr;
    ProDtlattach attachment = nullptr;
    ProSelection attachment_selection = nullptr;

    ProError st = ProDtlsyminstDataGet(&symbol, PRODISPMODE_SYMBOLIC, &data);
    diag.data_status = st;
    if (st != PRO_TK_NO_ERROR || data == nullptr) {
        return st;
    }

    ProVector target = {balloon.target_point[0], balloon.target_point[1], balloon.target_point[2]};

    st = ProDtlsyminstdataAttachmentGet(data, &attachment);
    diag.attachment_get_status = st;
    if (st == PRO_TK_NO_ERROR && attachment != nullptr) {
        ProView attachment_view = nullptr;
        ProVector current_location = {0.0, 0.0, 0.0};
        ProDtlattachType attachment_type = PRO_DTLATTACHTYPE_UNIMPLEMENTED;
        st = ProDtlattachGet(attachment, &attachment_type, &attachment_view, current_location, &attachment_selection);
        diag.attachment_read_status = st;
        diag.original_attach_type = attachment_type;
        if (st == PRO_TK_NO_ERROR) {
            ProView target_view = attachment_view != nullptr ? attachment_view : balloon.view;
            st = ProDtlattachSet(attachment, attachment_type, target_view, target, attachment_selection);
            diag.attachment_set_status = st;
            if (st == PRO_TK_NO_ERROR) {
                st = ProDtlsyminstdataAttachmentSet(data, attachment);
                diag.data_attachment_status = st;
            }
            if (st == PRO_TK_NO_ERROR) {
                st = ProDtlsyminstModify(&symbol, data);
                diag.modify_status = st;
                diag.used_existing_attachment = true;
            }
        }
    }

    if (st != PRO_TK_NO_ERROR) {
        if (attachment_selection != nullptr) {
            ProSelectionFree(&attachment_selection);
            attachment_selection = nullptr;
        }
        if (attachment != nullptr) {
            ProDtlattachFree(attachment);
            attachment = nullptr;
        }

        st = ProDtlattachAlloc(PRO_DTLATTACHTYPE_FREE, balloon.view, target, nullptr, &attachment);
        diag.alloc_status = st;
        if (st == PRO_TK_NO_ERROR && attachment != nullptr) {
            diag.attachtype_status = ProDtlsyminstdataAttachtypeSet(data, PROSYMDEFATTACHTYPE_FREE);
            st = ProDtlsyminstdataAttachmentSet(data, attachment);
            diag.fallback_attachment_status = st;
        }
        if (st == PRO_TK_NO_ERROR) {
            st = ProDtlsyminstModify(&symbol, data);
            diag.fallback_modify_status = st;
            diag.used_fallback_attachment = true;
        }
    }

    if (st == PRO_TK_NO_ERROR) {
        const ProError st_show = ProDtlsyminstShow(&symbol);
        diag.show_status = st_show;
        if (st_show != PRO_TK_NO_ERROR) {
            st = st_show;
        }
    }
    if (st == PRO_TK_NO_ERROR) {
        st = ProDtlsyminstDraw(&symbol);
        diag.draw_status = st;
    }

    if (attachment_selection != nullptr) {
        ProSelectionFree(&attachment_selection);
    }
    if (attachment != nullptr) {
        ProDtlattachFree(attachment);
    }
    if (data != nullptr) {
        ProDtlsyminstdataFree(data);
    }
    return st;
}

void DeleteOfficialBomSymbols(const std::vector<ProDtlsyminst> &symbols, const Drawing3LogSink &log_sink)
{
    for (ProDtlsyminst symbol : symbols) {
        ProDtlsyminst copy = symbol;
        const ProError st_erase = ProDtlsyminstErase(&copy);
        copy = symbol;
        ProError st_delete = ProDtlsyminstDelete(&copy);
        if (st_delete != PRO_TK_NO_ERROR) {
            copy = symbol;
            const ProError st_remove = ProDtlsyminstRemove(&copy);
            LogLine(log_sink,
                    "ArrangeBalloons official-bom-temp-delete symbol=%d erase=%d delete=%d remove=%d",
                    symbol.id,
                    static_cast<int>(st_erase),
                    static_cast<int>(st_delete),
                    static_cast<int>(st_remove));
        } else {
            LogLine(log_sink,
                    "ArrangeBalloons official-bom-temp-delete symbol=%d erase=%d delete=%d",
                    symbol.id,
                    static_cast<int>(st_erase),
                    static_cast<int>(st_delete));
        }
    }
}

void FlushDrawingViewDisplay(ProDrawing drawing,
                             int sheet,
                             ProView view,
                             const char *phase,
                             const Drawing3LogSink &log_sink)
{
    ProError st_view_regen = PRO_TK_BAD_INPUTS;
    ProError st_sheet_regen = PRO_TK_BAD_INPUTS;
    if (drawing != nullptr && view != nullptr) {
        st_view_regen = ProDrawingViewRegenerate(drawing, view);
    }
    if (drawing != nullptr && sheet > 0) {
        st_sheet_regen = ProDwgSheetRegenerate(drawing, sheet);
    }

    int window_id = -1;
    const ProError st_window = ProWindowCurrentGet(&window_id);
    ProError st_refresh = PRO_TK_BAD_INPUTS;
    ProError st_repaint = PRO_TK_BAD_INPUTS;
    if (st_window == PRO_TK_NO_ERROR && window_id >= 0) {
        st_refresh = ProWindowRefresh(window_id);
        st_repaint = ProWindowRepaint(window_id);
    }

    LogLine(log_sink,
            "ArrangeBalloons official-rebuild-display-flush phase=%s view=%p view_regen=%d sheet=%d sheet_regen=%d window_get=%d window=%d refresh=%d repaint=%d",
            phase != nullptr ? phase : "unknown",
            static_cast<void *>(view),
            static_cast<int>(st_view_regen),
            sheet,
            static_cast<int>(st_sheet_regen),
            static_cast<int>(st_window),
            window_id,
            static_cast<int>(st_refresh),
            static_cast<int>(st_repaint));
}

void ApplyOfficialLeaderSamplesToCandidates(ProDrawing drawing,
                                            std::vector<OfficialBomLeaderSample> &samples,
                                            std::vector<CustomBalloonCandidate> &candidates,
                                            const Drawing3LogSink &log_sink)
{
    if (drawing == nullptr || samples.empty() || candidates.empty()) {
        return;
    }

    int applied = 0;
    for (OfficialBomLeaderSample &sample : samples) {
        if (sample.matched || sample.model == nullptr || !sample.has_leader_model_item || !sample.has_drawing_point) {
            continue;
        }

        CustomBalloonCandidate *best = nullptr;
        double best_score = 1.0e100;
        for (CustomBalloonCandidate &candidate : candidates) {
            if (!SameModel(candidate.model, sample.model)) {
                continue;
            }
            if (!sample.view_key.empty() && candidate.view_key != sample.view_key) {
                continue;
            }
            if (candidate.has_component_path && candidate.has_leader_model_item && candidate.has_entity_attach_point) {
                continue;
            }
            const double dx = candidate.attach_point[0] - sample.drawing_point[0];
            const double dy = candidate.attach_point[1] - sample.drawing_point[1];
            const double score = dx * dx + dy * dy;
            if (best == nullptr || score < best_score) {
                best = &candidate;
                best_score = score;
            }
        }
        if (best == nullptr) {
            continue;
        }

        best->view = sample.view != nullptr ? sample.view : best->view;
        best->view_key = ViewIdentityKey(drawing, best->view);
        best->component_path = sample.component_path;
        best->has_component_path = sample.has_component_path;
        best->allow_null_component_path = sample.allow_null_component_path;
        best->leader_model_item = sample.leader_model_item;
        best->has_leader_model_item = true;
        best->leader_point[0] = sample.leader_point[0];
        best->leader_point[1] = sample.leader_point[1];
        best->leader_point[2] = sample.leader_point[2];
        best->attach_point[0] = sample.drawing_point[0];
        best->attach_point[1] = sample.drawing_point[1];
        best->attach_point[2] = sample.drawing_point[2];
        best->has_entity_attach_point = true;
        sample.matched = true;
        ++applied;

        LogLine(log_sink,
                "ArrangeBalloons official-bom-leader-applied row=%d label=%s model=%s symbol=%d has_path=%d allow_null=%d leader_item=%d/%d attach=(%.3f,%.3f) leader_point=(%.3f,%.3f,%.3f)",
                best->selection_order,
                autobbox::common::WToA(best->label.c_str()).c_str(),
                autobbox::creo::DefaultModelTag(best->model).c_str(),
                sample.symbol.id,
                best->has_component_path ? 1 : 0,
                best->allow_null_component_path ? 1 : 0,
                static_cast<int>(best->leader_model_item.type),
                best->leader_model_item.id,
                best->attach_point[0],
                best->attach_point[1],
                best->leader_point[0],
                best->leader_point[1],
                best->leader_point[2]);
    }

    LogLine(log_sink,
            "ArrangeBalloons official-bom-leader-sampling applied=%d samples=%zu candidates=%zu",
            applied,
            samples.size(),
            candidates.size());
}

void SampleOfficialBomBalloonLeaders(ProDrawing drawing,
                                     int sheet,
                                     const std::vector<SelectedBomTableSegment> &segments,
                                     const std::vector<ProView> &views,
                                     std::vector<CustomBalloonCandidate> &candidates,
                                     const Drawing3LogSink &log_sink)
{
    if (drawing == nullptr || sheet <= 0 || segments.empty() || views.empty() || candidates.empty()) {
        return;
    }

    std::vector<ProDtlsyminst> before_symbols;
    CollectDrawingSymbols(drawing, sheet, before_symbols);
    const std::unordered_set<std::string> before_keys = SymbolKeySet(before_symbols);

    int create_attempts = 0;
    int create_ok = 0;
    for (const SelectedBomTableSegment &segment : segments) {
        if (segment.table.owner == nullptr) {
            continue;
        }
        const int region_id = segment.segment;
        for (ProView view : views) {
            if (view == nullptr) {
                continue;
            }
            ++create_attempts;
            const ProError st_create = ProBomballoonCreate(drawing, const_cast<ProDwgtable *>(&segment.table), region_id, view);
            if (st_create == PRO_TK_NO_ERROR) {
                ++create_ok;
            }
            LogLine(log_sink,
                    "ArrangeBalloons official-bom-temp-create table=%p region=%d view=%p status=%d",
                    static_cast<void *>(segment.table.owner),
                    region_id,
                    static_cast<void *>(view),
                    static_cast<int>(st_create));
        }
    }

    for (ProView view : views) {
        if (view == nullptr) {
            continue;
        }
        const ProError st_clean = ProBomballoonClean(
            drawing,
            view,
            PRO_B_TRUE,
            PRO_B_FALSE,
            kCustomBalloonOutsideOffset,
            PRO_B_FALSE,
            0.0,
            PRO_B_FALSE,
            kCustomBalloonVerticalSpacing,
            PRO_B_TRUE);
        LogLine(log_sink,
                "ArrangeBalloons official-bom-temp-clean view=%p status=%d attach_to_surface=1",
                static_cast<void *>(view),
                static_cast<int>(st_clean));
    }

    std::vector<ProDtlsyminst> after_symbols;
    CollectDrawingSymbols(drawing, sheet, after_symbols);
    std::vector<ProDtlsyminst> temporary_symbols;
    for (const ProDtlsyminst &symbol : after_symbols) {
        if (before_keys.find(DtlSymbolKey(symbol)) == before_keys.end()) {
            temporary_symbols.push_back(symbol);
        }
    }

    std::vector<OfficialBomLeaderSample> samples;
    for (const ProDtlsyminst &symbol : temporary_symbols) {
        OfficialBomLeaderSample sample = {};
        if (ExtractOfficialBomLeaderSample(drawing, symbol, sample)) {
            samples.push_back(sample);
            LogLine(log_sink,
                    "ArrangeBalloons official-bom-leader-sample symbol=%d view=%p model=%s has_path=%d leader_item=%d/%d attach=(%.3f,%.3f)",
                    symbol.id,
                    static_cast<void *>(sample.view),
                    sample.model != nullptr ? autobbox::creo::DefaultModelTag(sample.model).c_str() : "(null)",
                    sample.has_component_path ? 1 : 0,
                    sample.has_leader_model_item ? static_cast<int>(sample.leader_model_item.type) : -1,
                    sample.has_leader_model_item ? sample.leader_model_item.id : -1,
                    sample.drawing_point[0],
                    sample.drawing_point[1]);
        }
    }

    ApplyOfficialLeaderSamplesToCandidates(drawing, samples, candidates, log_sink);
    DeleteOfficialBomSymbols(temporary_symbols, log_sink);
    LogLine(log_sink,
            "ArrangeBalloons official-bom-leader-sampling create_attempts=%d create_ok=%d before=%zu after=%zu temp=%zu samples=%zu",
            create_attempts,
            create_ok,
            before_symbols.size(),
            after_symbols.size(),
            temporary_symbols.size(),
            samples.size());
}

bool CollectBomTableCandidatesFromSegments(ProDrawing drawing,
                                           int sheet,
                                           const BalloonArrangeOptions &options,
                                           const std::vector<SelectedBomTableSegment> &segments,
                                           ProView target_view,
                                           BalloonArrangeSummary &summary,
                                           std::vector<CustomBalloonCandidate> &candidates,
                                           std::vector<ProView> &views,
                                           const Drawing3LogSink &log_sink)
{
    if (segments.empty()) {
        return true;
    }

    std::vector<ProView> sheet_views = CollectSheetViews(drawing, sheet, log_sink);
    std::unordered_set<std::wstring> seen_rows;
    for (SelectedBomTableSegment segment : segments) {
        int first_row = 0;
        int last_row = 0;
        int first_col = 0;
        int last_col = 0;
        ProError st_extents = ProDwgtableSegExtentsGet(
            &segment.table,
            segment.segment,
            &first_row,
            &last_row,
            &first_col,
            &last_col);
        if (st_extents != PRO_TK_NO_ERROR && segment.segment != PRO_VALUE_UNUSED) {
            st_extents = ProDwgtableSegExtentsGet(
                &segment.table,
                PRO_VALUE_UNUSED,
                &first_row,
                &last_row,
                &first_col,
                &last_col);
        }
        if (st_extents != PRO_TK_NO_ERROR) {
            int n_rows = 0;
            int n_cols = 0;
            if (ProDwgtableRowsCount(&segment.table, &n_rows) == PRO_TK_NO_ERROR &&
                ProDwgtableColumnsCount(&segment.table, &n_cols) == PRO_TK_NO_ERROR) {
                first_row = 1;
                last_row = n_rows;
                first_col = 1;
                last_col = n_cols;
                st_extents = PRO_TK_NO_ERROR;
            }
        }

        LogLine(log_sink,
                "ArrangeBalloons bom-table-extents table=%p segment=%d status=%d rows=%d..%d cols=%d..%d",
                static_cast<void *>(segment.table.owner),
                segment.segment,
                static_cast<int>(st_extents),
                first_row,
                last_row,
                first_col,
                last_col);
        if (st_extents != PRO_TK_NO_ERROR) {
            if (summary.first_error == PRO_TK_NO_ERROR) {
                summary.first_error = st_extents;
            }
            continue;
        }

        for (int row = first_row; row <= last_row; ++row) {
            ProMdl row_model = nullptr;
            ProMdl row_assembly = nullptr;
            ProAsmcomppath component_path;
            std::memset(&component_path, 0, sizeof(component_path));
            bool has_component_path = false;
            int source_col = 0;
            for (int col = first_col; col <= last_col; ++col) {
                if (ResolveBomCellModel(&segment.table,
                                        col,
                                        row,
                                        row_model,
                                        row_assembly,
                                        component_path,
                                        has_component_path)) {
                    source_col = col;
                    break;
                }
            }
            if (row_model == nullptr) {
                continue;
            }

            const std::wstring row_key =
                std::to_wstring(DwgtableKey(segment.table)) + L":" +
                std::to_wstring(segment.segment) + L":" + std::to_wstring(row) + L":" +
                autobbox::creo::ModelName(row_model);
            if (!seen_rows.insert(row_key).second) {
                continue;
            }

            ProView anchor_view = target_view;
            if (anchor_view == nullptr) {
                anchor_view = FindAnchorViewForBomRow(drawing, sheet, row_assembly, row_model, sheet_views);
            }
            if (anchor_view == nullptr) {
                LogLine(log_sink,
                        "SKIP arrange-balloons bom-row row=%d reason=no-anchor-view model=%s",
                        row,
                        autobbox::creo::DefaultModelTag(row_model).c_str());
                continue;
            }

            ProMdl view_model = nullptr;
            ProSolid view_solid = nullptr;
            const ProError st_view_solid = ProDrawingViewSolidGet(drawing, anchor_view, &view_solid);
            if (st_view_solid == PRO_TK_NO_ERROR && view_solid != nullptr) {
                view_model = reinterpret_cast<ProMdl>(view_solid);
            }
            int path_from_view = 0;
            if (!has_component_path && view_model != nullptr) {
                ProAsmcomppath view_component_path;
                std::memset(&view_component_path, 0, sizeof(view_component_path));
                if (FindDisplayedComponentPath(view_model, row_model, view_component_path)) {
                    component_path = view_component_path;
                    has_component_path = true;
                    path_from_view = 1;
                }
            }

            core::Dwg3GroupOutline outline = {};
            if (GetDrawingViewOutlineBox(drawing, anchor_view, outline) != PRO_TK_NO_ERROR) {
                continue;
            }

            CustomBalloonCandidate candidate = {};
            candidate.view = anchor_view;
            candidate.view_key = ViewIdentityKey(drawing, anchor_view);
            candidate.model = row_model;
            candidate.table_assembly = row_assembly;
            candidate.component_path = component_path;
            candidate.has_component_path = has_component_path;
            candidate.allow_null_component_path = (!has_component_path && SameModel(view_model, row_model));
            candidate.from_bom_table = true;
            candidate.label = CustomBalloonLabel(row_model, options);
            if (candidate.label.empty()) {
                continue;
            }
            ProPoint3d entity_attach = {0.0, 0.0, 0.0};
            ProPoint3d leader_point = {0.0, 0.0, 0.0};
            ProModelitem leader_item = {};
            bool has_leader_item = false;
            if (ComputeComponentDrawingAttachPoint(
                    drawing,
                    anchor_view,
                    row_model,
                    component_path,
                    has_component_path,
                    entity_attach,
                    leader_point,
                    leader_item,
                    has_leader_item)) {
                candidate.attach_point[0] = entity_attach[0];
                candidate.attach_point[1] = entity_attach[1];
                candidate.attach_point[2] = entity_attach[2];
                candidate.leader_point[0] = leader_point[0];
                candidate.leader_point[1] = leader_point[1];
                candidate.leader_point[2] = leader_point[2];
                candidate.has_entity_attach_point = true;
                candidate.leader_model_item = leader_item;
                candidate.has_leader_model_item = has_leader_item;
            } else {
                candidate.attach_point[0] = OutlineCenterX(outline);
                candidate.attach_point[1] = OutlineCenterY(outline);
                candidate.attach_point[2] = 0.0;
                candidate.leader_point[0] = candidate.attach_point[0];
                candidate.leader_point[1] = candidate.attach_point[1];
                candidate.leader_point[2] = candidate.attach_point[2];
                candidate.has_entity_attach_point = false;
                candidate.leader_model_item = {};
                candidate.has_leader_model_item = false;
            }
            candidate.selection_order = row;
            candidates.push_back(candidate);

            if (std::find(views.begin(), views.end(), anchor_view) == views.end()) {
                views.push_back(anchor_view);
            }
            LogLine(log_sink,
                    "ArrangeBalloons bom-candidate table=%p row=%d col=%d view=%p view_model=%s model=%s label=%s component_path=%d path_from_view=%d entity_attach=%d leader_item=%d/%d attach=(%.3f,%.3f) leader_point=(%.3f,%.3f,%.3f)",
                    static_cast<void *>(segment.table.owner),
                    row,
                    source_col,
                    static_cast<void *>(anchor_view),
                    view_model != nullptr ? autobbox::creo::DefaultModelTag(view_model).c_str() : "(null)",
                    autobbox::creo::DefaultModelTag(row_model).c_str(),
                    autobbox::common::WToA(candidate.label.c_str()).c_str(),
                    has_component_path ? 1 : 0,
                    path_from_view,
                    candidate.has_entity_attach_point ? 1 : 0,
                    candidate.has_leader_model_item ? static_cast<int>(candidate.leader_model_item.type) : -1,
                    candidate.has_leader_model_item ? candidate.leader_model_item.id : -1,
                    candidate.attach_point[0],
                    candidate.attach_point[1],
                    candidate.leader_point[0],
                    candidate.leader_point[1],
                    candidate.leader_point[2]);
        }
    }

    return true;
}

bool CollectBomTableCandidates(ProDrawing drawing,
                               int sheet,
                               const BalloonArrangeOptions &options,
                               BalloonArrangeSummary &summary,
                               std::vector<CustomBalloonCandidate> &candidates,
                               std::vector<ProView> &views,
                               const Drawing3LogSink &log_sink)
{
    std::vector<SelectedBomTableSegment> segments;
    if (!CollectSelectedBomTableSegments(segments, summary, log_sink)) {
        return false;
    }
    return CollectBomTableCandidatesFromSegments(
        drawing,
        sheet,
        options,
        segments,
        nullptr,
        summary,
        candidates,
        views,
        log_sink);
}

bool CollectCustomBalloonCandidates(ProDrawing drawing,
                                    int sheet,
                                    const BalloonArrangeOptions &options,
                                    BalloonArrangeSummary &summary,
                                    std::vector<CustomBalloonCandidate> &candidates,
                                    const Drawing3LogSink &log_sink)
{
    candidates.clear();

    ProSelection *buffer = nullptr;
    const ProError st = ProSelbufferSelectionsGet(&buffer);
    if (st == PRO_TK_E_NOT_FOUND) {
        LogLine(log_sink, "ArrangeBalloons custom-selection-buffer empty");
        return true;
    }
    if (st != PRO_TK_NO_ERROR) {
        LogLine(log_sink, "FAIL arrange-balloons custom reason=selection-buffer status=%d", static_cast<int>(st));
        summary.first_error = st;
        return false;
    }

    int count = 0;
    ProArraySizeGet(reinterpret_cast<ProArray>(buffer), &count);
    summary.selected_total = std::max(summary.selected_total, count);
    candidates.reserve(static_cast<size_t>(count));

    for (int i = 0; i < count; ++i) {
        ProModelitem model_item = {};
        const ProError st_item = ProSelectionModelitemGet(buffer[i], &model_item);
        if (st_item != PRO_TK_NO_ERROR) {
            LogLine(log_sink,
                    "SKIP arrange-balloons custom selection=%d reason=no-modelitem status=%d",
                    i,
                    static_cast<int>(st_item));
            continue;
        }
        if (model_item.type == PRO_NOTE || model_item.type == PRO_IPAR_NOTE) {
            continue;
        }

        ProView view = nullptr;
        const ProError st_view = ProSelectionViewGet(buffer[i], &view);
        if (st_view != PRO_TK_NO_ERROR || view == nullptr) {
            LogLine(log_sink,
                    "SKIP arrange-balloons custom selection=%d reason=no-view status=%d item_type=%d",
                    i,
                    static_cast<int>(st_view),
                    static_cast<int>(model_item.type));
            continue;
        }

        int view_sheet = 0;
        const ProError st_sheet = ProDrawingViewSheetGet(drawing, view, &view_sheet);
        if (st_sheet != PRO_TK_NO_ERROR || view_sheet != sheet) {
            LogLine(log_sink,
                    "SKIP arrange-balloons custom selection=%d reason=sheet-mismatch status=%d view_sheet=%d current_sheet=%d",
                    i,
                    static_cast<int>(st_sheet),
                    view_sheet,
                    sheet);
            continue;
        }

        ProPoint3d attach_point = {0.0, 0.0, 0.0};
        const ProError st_point = ProSelectionPoint3dGet(buffer[i], attach_point);
        if (st_point != PRO_TK_NO_ERROR) {
            core::Dwg3GroupOutline outline = {};
            if (GetDrawingViewOutlineBox(drawing, view, outline) == PRO_TK_NO_ERROR) {
                attach_point[0] = OutlineCenterX(outline);
                attach_point[1] = OutlineCenterY(outline);
                attach_point[2] = 0.0;
            } else {
                LogLine(log_sink,
                        "SKIP arrange-balloons custom selection=%d reason=no-attach-point status=%d",
                        i,
                        static_cast<int>(st_point));
                continue;
            }
        }

        ProMdl model = ResolveBalloonModelFromSelection(drawing, view, buffer[i], model_item, log_sink, i);
        if (model == nullptr) {
            continue;
        }

        CustomBalloonCandidate candidate = {};
        candidate.view = view;
        candidate.view_key = ViewIdentityKey(drawing, view);
        candidate.model = model;
        candidate.label = CustomBalloonLabel(model, options);
        if (candidate.label.empty()) {
            LogLine(log_sink,
                    "SKIP arrange-balloons custom selection=%d reason=empty-label model=%s",
                    i,
                    autobbox::creo::DefaultModelTag(model).c_str());
            continue;
        }
        candidate.attach_point[0] = attach_point[0];
        candidate.attach_point[1] = attach_point[1];
        candidate.attach_point[2] = attach_point[2];
        candidate.selection_order = i;
        candidate.right_side = true;
        candidates.push_back(candidate);
        LogLine(log_sink,
                "ArrangeBalloons custom-candidate selection=%d view=%p model=%s label=%s attach=(%.3f,%.3f)",
                i,
                static_cast<void *>(view),
                autobbox::creo::DefaultModelTag(model).c_str(),
                autobbox::common::WToA(candidate.label.c_str()).c_str(),
                candidate.attach_point[0],
                candidate.attach_point[1]);
    }

    ProSelectionarrayFree(buffer);
    return true;
}

bool CollectSelectedViews(ProDrawing drawing,
                          int sheet,
                          BalloonArrangeSummary &summary,
                          std::vector<ProView> &views,
                          std::vector<SelectedBalloonNote> &notes,
                          const Drawing3LogSink &log_sink)
{
    views.clear();
    notes.clear();

    ProSelection *buffer = nullptr;
    const ProError st = ProSelbufferSelectionsGet(&buffer);
    if (st == PRO_TK_E_NOT_FOUND) {
        LogLine(log_sink, "ArrangeBalloons selection_buffer empty");
        return true;
    }
    if (st != PRO_TK_NO_ERROR) {
        LogLine(log_sink, "FAIL arrange-balloons reason=selection-buffer status=%d", static_cast<int>(st));
        summary.first_error = st;
        return false;
    }

    int count = 0;
    ProArraySizeGet(reinterpret_cast<ProArray>(buffer), &count);
    summary.selected_total = count;
    views.reserve(static_cast<size_t>(count));
    notes.reserve(static_cast<size_t>(count));

    std::unordered_set<std::wstring> seen_views;
    std::unordered_set<std::uint64_t> seen_notes;
    for (int i = 0; i < count; ++i) {
        ProModelitem model_item = {};
        const ProError st_item = ProSelectionModelitemGet(buffer[i], &model_item);
        const bool is_note = st_item == PRO_TK_NO_ERROR &&
                             (model_item.type == PRO_NOTE || model_item.type == PRO_IPAR_NOTE);

        ProView view = nullptr;
        const ProError st_view = ProSelectionViewGet(buffer[i], &view);
        if ((st_view != PRO_TK_NO_ERROR || view == nullptr) && is_note) {
            ProDtlnote note_for_view = {};
            note_for_view.type = model_item.type;
            note_for_view.id = model_item.id;
            note_for_view.owner = model_item.owner;
            view = ResolveViewFromNote(&note_for_view, log_sink, i);
        }
        if (view == nullptr) {
            LogLine(log_sink,
                    "SKIP arrange-balloons selection=%d reason=no-view status=%d item_status=%d item_type=%d",
                    i,
                    static_cast<int>(st_view),
                    static_cast<int>(st_item),
                    (st_item == PRO_TK_NO_ERROR) ? static_cast<int>(model_item.type) : -1);
            continue;
        }

        int view_sheet = 0;
        const ProError st_sheet = ProDrawingViewSheetGet(drawing, view, &view_sheet);
        if (st_sheet != PRO_TK_NO_ERROR || view_sheet != sheet) {
            LogLine(log_sink,
                    "SKIP arrange-balloons selection=%d reason=sheet-mismatch status=%d view_sheet=%d current_sheet=%d",
                    i,
                    static_cast<int>(st_sheet),
                    view_sheet,
                    sheet);
            continue;
        }

        const std::wstring view_key = ViewIdentityKey(drawing, view);
        if (is_note) {
            const std::uint64_t note_key =
                (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(model_item.owner)) << 32) ^
                static_cast<std::uint64_t>(model_item.id);
            if (seen_notes.insert(note_key).second) {
                SelectedBalloonNote note = {};
                note.note.type = model_item.type;
                note.note.id = model_item.id;
                note.note.owner = model_item.owner;
                note.view = view;
                note.view_key = view_key;
                note.selection_order = i;
                notes.push_back(note);
            } else {
                LogLine(log_sink, "SKIP arrange-balloons selection=%d reason=duplicate-note id=%d", i, model_item.id);
            }
        } else {
            LogLine(log_sink,
                    "ArrangeBalloons selection=%d used-for-view-only item_status=%d item_type=%d",
                    i,
                    static_cast<int>(st_item),
                    (st_item == PRO_TK_NO_ERROR) ? static_cast<int>(model_item.type) : -1);
        }

        if (!seen_views.insert(view_key).second) {
            LogLine(log_sink,
                    "SKIP arrange-balloons selection=%d reason=duplicate-view view=%p",
                    i,
                    static_cast<void *>(view));
            continue;
        }

        views.push_back(view);
        LogLine(log_sink, "ArrangeBalloons selected_view index=%d view=%p", i, static_cast<void *>(view));
    }

    summary.valid_views = static_cast<int>(views.size());
    ProSelectionarrayFree(buffer);
    return true;
}

bool TryReadNoteLayout(const SelectedBalloonNote &selected,
                       BalloonLayoutItem &item,
                       const Drawing3LogSink &log_sink)
{
    item = {};
    item.note = selected.note;
    item.view = selected.view;
    item.view_key = selected.view_key;
    item.selection_order = selected.selection_order;

    ProDtlnotedata note_data = nullptr;
    ProDtlattach attachment = nullptr;
    ProSelection attachment_selection = nullptr;
    ProDtlattachType attachment_type = PRO_DTLATTACHTYPE_UNIMPLEMENTED;
    ProView attachment_view = nullptr;

    ProDtlattach *leaders = nullptr;
    ProSelection leader_selection = nullptr;
    ProDtlattachType leader_type = PRO_DTLATTACHTYPE_UNIMPLEMENTED;
    ProView leader_view = nullptr;

    const ProError st_data = ProDtlnoteDataGet(&item.note, nullptr, PRODISPMODE_SYMBOLIC, &note_data);
    const ProError st_attachment =
        (st_data == PRO_TK_NO_ERROR && note_data != nullptr) ? ProDtlnotedataAttachmentGet(note_data, &attachment)
                                                             : PRO_TK_E_NOT_FOUND;
    const ProError st_attachment_get =
        (st_attachment == PRO_TK_NO_ERROR && attachment != nullptr)
            ? ProDtlattachGet(attachment, &attachment_type, &attachment_view, item.note_location, &attachment_selection)
            : PRO_TK_E_NOT_FOUND;

    int leader_count = 0;
    const ProError st_leaders =
        (st_data == PRO_TK_NO_ERROR && note_data != nullptr) ? ProDtlnotedataLeadersCollect(note_data, &leaders)
                                                             : PRO_TK_E_NOT_FOUND;
    if (st_leaders == PRO_TK_NO_ERROR && leaders != nullptr) {
        ProArraySizeGet(reinterpret_cast<ProArray>(leaders), &leader_count);
    }

    ProError st_leader_get = PRO_TK_E_NOT_FOUND;
    if (leader_count > 0) {
        st_leader_get =
            ProDtlattachGet(leaders[0], &leader_type, &leader_view, item.leader_location, &leader_selection);
        if (st_leader_get == PRO_TK_NO_ERROR && leader_selection != nullptr) {
            ProPoint3d picked_point = {0.0, 0.0, 0.0};
            const ProError st_point = ProSelectionPoint3dGet(leader_selection, picked_point);
            if (st_point == PRO_TK_NO_ERROR) {
                item.leader_location[0] = picked_point[0];
                item.leader_location[1] = picked_point[1];
                item.leader_location[2] = picked_point[2];
            }
        }
    }

    item.valid = st_attachment_get == PRO_TK_NO_ERROR && leader_count > 0 && st_leader_get == PRO_TK_NO_ERROR;
    LogLine(log_sink,
            "ArrangeBalloons read-note id=%d data=%d attach=%d attach_get=%d attach_type=%d leaders=%d leader_get=%d note=(%.3f,%.3f) leader=(%.3f,%.3f) valid=%d",
            item.note.id,
            static_cast<int>(st_data),
            static_cast<int>(st_attachment),
            static_cast<int>(st_attachment_get),
            static_cast<int>(attachment_type),
            leader_count,
            static_cast<int>(st_leader_get),
            item.note_location[0],
            item.note_location[1],
            item.leader_location[0],
            item.leader_location[1],
            item.valid ? 1 : 0);

    if (leader_selection != nullptr) {
        ProSelectionFree(&leader_selection);
    }
    if (leaders != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&leaders));
    }
    if (attachment_selection != nullptr) {
        ProSelectionFree(&attachment_selection);
    }
    if (attachment != nullptr) {
        ProDtlattachFree(attachment);
    }
    if (note_data != nullptr) {
        ProDtlnotedataFree(note_data);
    }
    return item.valid;
}

bool ReadNoteFirstLineTextAndUnderline(ProDtlnote *note,
                                       std::wstring &text_out,
                                       bool &underline_out)
{
    text_out.clear();
    underline_out = false;
    if (note == nullptr) {
        return false;
    }

    ProDtlnotedata note_data = nullptr;
    ProDtlnoteline *lines = nullptr;
    ProDtlnotetext *texts = nullptr;

    ProError st = ProDtlnoteDataGet(note, nullptr, PRODISPMODE_SYMBOLIC, &note_data);
    if (st == PRO_TK_NO_ERROR && note_data != nullptr) {
        st = ProDtlnotedataLinesCollect(note_data, &lines);
    }

    int line_count = 0;
    if (st == PRO_TK_NO_ERROR && lines != nullptr) {
        ProArraySizeGet(reinterpret_cast<ProArray>(lines), &line_count);
    }

    int text_count = 0;
    if (st == PRO_TK_NO_ERROR && line_count > 0) {
        st = ProDtlnotelineTextsCollect(lines[0], &texts);
        if (st == PRO_TK_NO_ERROR && texts != nullptr) {
            ProArraySizeGet(reinterpret_cast<ProArray>(texts), &text_count);
        }
    }

    if (st == PRO_TK_NO_ERROR && text_count > 0) {
        ProLine line_text = {0};
        if (ProDtlnotetextStringGet(texts[0], line_text) == PRO_TK_NO_ERROR) {
            text_out = line_text;
        }
        ProBoolean underlined = PRO_B_FALSE;
        if (ProDtlnotetextUlineGet(texts[0], &underlined) == PRO_TK_NO_ERROR) {
            underline_out = (underlined == PRO_B_TRUE);
        }
    }

    if (texts != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&texts));
    }
    if (lines != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&lines));
    }
    if (note_data != nullptr) {
        ProDtlnotedataFree(note_data);
    }
    return !text_out.empty();
}

std::wstring NormalizeCustomBalloonLabelForMatch(const std::wstring &text)
{
    size_t begin = 0;
    while (begin < text.size() && std::iswspace(text[begin]) != 0) {
        ++begin;
    }

    size_t end = text.size();
    while (end > begin && std::iswspace(text[end - 1]) != 0) {
        --end;
    }

    return text.substr(begin, end - begin);
}

std::uint64_t NoteKey(const ProDtlnote &note)
{
    return (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(note.owner)) << 32) ^
           static_cast<std::uint64_t>(note.id);
}

void CollectSheetLeaderNotesForViews(ProDrawing drawing,
                                     int sheet,
                                     const std::vector<ProView> &views,
                                     std::vector<SelectedBalloonNote> &notes,
                                     const Drawing3LogSink &log_sink)
{
    if (drawing == nullptr || views.empty()) {
        return;
    }

    std::unordered_set<std::wstring> target_view_keys;
    for (ProView view : views) {
        target_view_keys.insert(ViewIdentityKey(drawing, view));
    }

    std::unordered_set<std::uint64_t> seen_note_keys;
    for (const SelectedBalloonNote &note : notes) {
        seen_note_keys.insert(NoteKey(note.note));
    }

    ProDtlnote *sheet_notes = nullptr;
    const ProError st_collect = ProDrawingDtlnotesCollect(drawing, nullptr, sheet, &sheet_notes);
    int count = 0;
    if (st_collect == PRO_TK_NO_ERROR && sheet_notes != nullptr) {
        ProArraySizeGet(reinterpret_cast<ProArray>(sheet_notes), &count);
    }

    LogLine(log_sink,
            "ArrangeBalloons collect-sheet-notes status=%d count=%d selected_notes_before=%zu",
            static_cast<int>(st_collect),
            count,
            notes.size());

    for (int i = 0; i < count; ++i) {
        ProDtlnote note = sheet_notes[i];
        const std::uint64_t note_key = NoteKey(note);
        if (seen_note_keys.find(note_key) != seen_note_keys.end()) {
            continue;
        }

        ProView note_view = ResolveViewFromNote(&note, log_sink, i);
        if (note_view == nullptr) {
            continue;
        }

        const std::wstring note_view_key = ViewIdentityKey(drawing, note_view);
        if (target_view_keys.find(note_view_key) == target_view_keys.end()) {
            continue;
        }

        BalloonLayoutItem layout = {};
        SelectedBalloonNote candidate = {};
        candidate.note = note;
        candidate.view = note_view;
        candidate.view_key = note_view_key;
        candidate.selection_order = 100000 + i;
        if (!TryReadNoteLayout(candidate, layout, log_sink)) {
            continue;
        }

        notes.push_back(candidate);
        seen_note_keys.insert(note_key);
        LogLine(log_sink,
                "ArrangeBalloons add-sheet-note index=%d note_id=%d view=%p",
                i,
                note.id,
                static_cast<void *>(note_view));
    }

    if (sheet_notes != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&sheet_notes));
    }
}

void DeleteExistingCustomNotesForCandidates(ProDrawing drawing,
                                            int sheet,
                                            const std::vector<ProView> &views,
                                            const std::vector<CustomBalloonCandidate> &candidates,
                                            BalloonArrangeSummary &summary,
                                            const Drawing3LogSink &log_sink)
{
    if (drawing == nullptr || views.empty() || candidates.empty()) {
        return;
    }

    std::unordered_set<std::wstring> target_view_keys;
    for (ProView view : views) {
        if (view != nullptr) {
            target_view_keys.insert(ViewIdentityKey(drawing, view));
        }
    }

    std::unordered_set<std::wstring> expected_labels;
    for (const CustomBalloonCandidate &candidate : candidates) {
        if (!candidate.label.empty() && target_view_keys.find(candidate.view_key) != target_view_keys.end()) {
            const std::wstring normalized_label = NormalizeCustomBalloonLabelForMatch(candidate.label);
            if (!normalized_label.empty()) {
                expected_labels.insert(normalized_label);
            }
        }
    }
    if (expected_labels.empty()) {
        return;
    }

    ProDtlnote *sheet_notes = nullptr;
    const ProError st_collect = ProDrawingDtlnotesCollect(drawing, nullptr, sheet, &sheet_notes);
    int count = 0;
    if (st_collect == PRO_TK_NO_ERROR && sheet_notes != nullptr) {
        ProArraySizeGet(reinterpret_cast<ProArray>(sheet_notes), &count);
    }
    LogLine(log_sink,
            "ArrangeBalloons collect-existing-custom-notes status=%d count=%d labels=%zu",
            static_cast<int>(st_collect),
            count,
            expected_labels.size());
    if (st_collect != PRO_TK_NO_ERROR) {
        if (summary.first_error == PRO_TK_NO_ERROR) {
            summary.first_error = st_collect;
        }
        return;
    }

    for (int i = 0; i < count; ++i) {
        ProDtlnote note = sheet_notes[i];
        std::wstring text;
        bool underlined = false;
        if (!ReadNoteFirstLineTextAndUnderline(&note, text, underlined)) {
            continue;
        }
        const std::wstring normalized_text = NormalizeCustomBalloonLabelForMatch(text);
        if (expected_labels.find(normalized_text) == expected_labels.end()) {
            continue;
        }

        ProView note_view = ResolveViewFromNote(&note, log_sink, i);
        std::wstring note_view_key;
        bool view_matches = true;
        if (note_view != nullptr) {
            note_view_key = ViewIdentityKey(drawing, note_view);
            view_matches = target_view_keys.find(note_view_key) != target_view_keys.end();
        }
        if (!view_matches) {
            LogLine(log_sink,
                    "ArrangeBalloons existing-custom-skip-other-view index=%d note_id=%d text=%s view=%p",
                    i,
                    note.id,
                    autobbox::common::WToA(normalized_text.c_str()).c_str(),
                    static_cast<void *>(note_view));
            continue;
        }

        SelectedBalloonNote selected = {};
        selected.note = note;
        selected.view = note_view;
        selected.view_key = note_view_key;
        selected.selection_order = i;
        BalloonLayoutItem layout = {};
        const bool has_readable_leader = TryReadNoteLayout(selected, layout, log_sink);

        const ProError st_delete = ProDtlnoteDelete(&note, nullptr);
        LogLine(log_sink,
                "ArrangeBalloons existing-custom-delete index=%d note_id=%d underline=%d leader=%d view_resolved=%d text=%s status=%d",
                i,
                note.id,
                underlined ? 1 : 0,
                has_readable_leader ? 1 : 0,
                note_view != nullptr ? 1 : 0,
                autobbox::common::WToA(normalized_text.c_str()).c_str(),
                static_cast<int>(st_delete));
        if (st_delete == PRO_TK_NO_ERROR) {
            ++summary.custom_balloons_updated;
        } else {
            ++summary.custom_balloons_failed;
            if (summary.first_error == PRO_TK_NO_ERROR) {
                summary.first_error = st_delete;
            }
        }
    }

    if (sheet_notes != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&sheet_notes));
    }
}

ProError MoveNoteTo(ProDtlnote note, const ProVector target_location)
{
    ProDtlnotedata note_data = nullptr;
    ProDtlattach attachment = nullptr;
    ProSelection attachment_selection = nullptr;
    ProDtlattachType attachment_type = PRO_DTLATTACHTYPE_UNIMPLEMENTED;
    ProView attachment_view = nullptr;
    ProVector current_location = {0.0, 0.0, 0.0};

    ProError st = ProDtlnoteDataGet(&note, nullptr, PRODISPMODE_SYMBOLIC, &note_data);
    if (st != PRO_TK_NO_ERROR || note_data == nullptr) {
        return st;
    }

    auto cleanup = [&]() {
        if (attachment_selection != nullptr) {
            ProSelectionFree(&attachment_selection);
        }
        if (attachment != nullptr) {
            ProDtlattachFree(attachment);
        }
        if (note_data != nullptr) {
            ProDtlnotedataFree(note_data);
        }
    };

    st = ProDtlnotedataAttachmentGet(note_data, &attachment);
    if (st != PRO_TK_NO_ERROR || attachment == nullptr) {
        cleanup();
        return st;
    }

    st = ProDtlattachGet(attachment, &attachment_type, &attachment_view, current_location, &attachment_selection);
    if (st != PRO_TK_NO_ERROR) {
        cleanup();
        return st;
    }

    if (attachment_type != PRO_DTLATTACHTYPE_FREE && attachment_type != PRO_DTLATTACHTYPE_OFFSET) {
        cleanup();
        return PRO_TK_INVALID_TYPE;
    }

    current_location[0] = target_location[0];
    current_location[1] = target_location[1];
    current_location[2] = target_location[2];

    st = ProDtlattachSet(attachment, attachment_type, attachment_view, current_location, attachment_selection);
    if (st == PRO_TK_NO_ERROR) {
        st = ProDtlnotedataAttachmentSet(note_data, attachment);
    }
    if (st == PRO_TK_NO_ERROR) {
        st = ProDtlnoteModify(&note, nullptr, note_data);
    }

    cleanup();
    return st;
}

ProError AddSingleTextLine(ProDtlnotedata note_data, const std::wstring &text)
{
    if (note_data == nullptr || text.empty()) {
        return PRO_TK_BAD_INPUTS;
    }

    ProDtlnoteline line = nullptr;
    ProDtlnotetext note_text = nullptr;
    ProError st = ProDtlnotelineAlloc(&line);
    if (st != PRO_TK_NO_ERROR || line == nullptr) {
        return st;
    }

    st = ProDtlnotetextAlloc(&note_text);
    if (st != PRO_TK_NO_ERROR || note_text == nullptr) {
        return st;
    }

    ProLine pro_text = {0};
    CopyWStr(pro_text, text.c_str());
    st = ProDtlnotetextStringSet(note_text, pro_text);
    if (st == PRO_TK_NO_ERROR) {
        st = ProDtlnotetextUlineSet(note_text, PRO_B_TRUE);
    }
    if (st == PRO_TK_NO_ERROR) {
        st = ProDtlnotelineTextAdd(line, note_text);
    }
    if (st == PRO_TK_NO_ERROR) {
        st = ProDtlnotedataLineAdd(note_data, line);
    }
    return st;
}

ProError ApplyCustomBalloonTextStyle(ProDtlnotedata note_data)
{
    if (note_data == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    ProTextStyle text_style = nullptr;
    ProError st = ProTextStyleAlloc(&text_style);
    if (st != PRO_TK_NO_ERROR || text_style == nullptr) {
        return st;
    }

    auto cleanup = [&]() {
        if (text_style != nullptr) {
            ProTextStyleFree(&text_style);
        }
    };

    st = ProTextStyleHeightSet(text_style, kCustomBalloonTextHeight);
    if (st == PRO_TK_NO_ERROR) {
        st = ProTextStyleHeightInModelUnitsSet(text_style, PRO_B_TRUE);
    }
    if (st == PRO_TK_NO_ERROR) {
        st = ProTextStyleJustificationSet(text_style, PRO_TEXT_HRZJUST_CENTER);
    }
    if (st == PRO_TK_NO_ERROR) {
        st = ProTextStyleVertJustificationSet(text_style, PRO_VERTJUST_MIDDLE);
    }
    if (st == PRO_TK_NO_ERROR) {
        st = ProDtlnotedataTextStyleSet(note_data, text_style);
    }

    cleanup();
    return st;
}

ProError CreateCustomBalloonNote(ProDrawing drawing,
                                 const CustomBalloonCandidate &candidate,
                                 ProDtlnote *created_note,
                                 CustomBalloonCreateDiagnostics *diagnostics = nullptr)
{
    if (drawing == nullptr || candidate.view == nullptr || candidate.label.empty()) {
        return PRO_TK_BAD_INPUTS;
    }
    if (created_note != nullptr) {
        std::memset(created_note, 0, sizeof(*created_note));
    }
    if (diagnostics != nullptr) {
        *diagnostics = {};
    }

    ProDtlnotedata note_data = nullptr;
    ProDtlattach note_attachment = nullptr;
    ProDtlattach leader = nullptr;
    ProSelection leader_selection = nullptr;

    ProError st = ProDtlnotedataAlloc(reinterpret_cast<ProMdl>(drawing), &note_data);
    if (st != PRO_TK_NO_ERROR || note_data == nullptr) {
        return st;
    }

    auto cleanup = [&]() {
        if (leader_selection != nullptr) {
            ProSelectionFree(&leader_selection);
        }
        if (leader != nullptr) {
            ProDtlattachFree(leader);
        }
        if (note_attachment != nullptr) {
            ProDtlattachFree(note_attachment);
        }
        if (note_data != nullptr) {
            ProDtlnotedataFree(note_data);
        }
    };

    st = AddSingleTextLine(note_data, candidate.label);
    if (st == PRO_TK_NO_ERROR) {
        st = ApplyCustomBalloonTextStyle(note_data);
    }

    if (st == PRO_TK_NO_ERROR) {
        ProVector note_location = {candidate.note_point[0], candidate.note_point[1], 0.0};
        st = ProDtlattachAlloc(PRO_DTLATTACHTYPE_FREE, candidate.view, note_location, nullptr, &note_attachment);
    }
    if (st == PRO_TK_NO_ERROR && note_attachment != nullptr) {
        st = ProDtlnotedataAttachmentSet(note_data, note_attachment);
    }

    if (st == PRO_TK_NO_ERROR &&
        candidate.has_entity_attach_point &&
        (candidate.has_component_path || candidate.allow_null_component_path) &&
        candidate.has_leader_model_item) {
        if (diagnostics != nullptr) {
            diagnostics->attempted_parametric = true;
        }
        ProModelitem leader_item = candidate.leader_model_item;
        if (candidate.has_component_path) {
            ProAsmcomppath path = candidate.component_path;
            st = ProSelectionAlloc(&path, &leader_item, &leader_selection);
        } else {
            st = ProSelectionAlloc(nullptr, &leader_item, &leader_selection);
        }
        if (diagnostics != nullptr) {
            diagnostics->selection_alloc_status = st;
        }
        if (st == PRO_TK_NO_ERROR && leader_selection != nullptr) {
            st = ProSelectionViewSet(candidate.view, &leader_selection);
            if (diagnostics != nullptr) {
                diagnostics->selection_view_status = st;
            }
        }
        if (st == PRO_TK_NO_ERROR) {
            ProPoint3d leader_point = {
                candidate.leader_point[0],
                candidate.leader_point[1],
                candidate.leader_point[2]};
            st = ProSelectionPoint3dSet(leader_point, &leader_selection);
            if (diagnostics != nullptr) {
                diagnostics->selection_point_status = st;
            }
        }
        if (st == PRO_TK_NO_ERROR) {
            st = ProDtlattachAlloc(PRO_DTLATTACHTYPE_PARAMETRIC, nullptr, nullptr, leader_selection, &leader);
            if (diagnostics != nullptr) {
                diagnostics->parametric_attach_status = st;
            }
        }
        if (st == PRO_TK_NO_ERROR && leader != nullptr) {
            st = ProDtlnotedataLeaderAdd(note_data, leader);
            if (diagnostics != nullptr) {
                diagnostics->parametric_leader_status = st;
            }
        }
        if (st == PRO_TK_NO_ERROR && leader != nullptr && diagnostics != nullptr) {
            diagnostics->used_parametric = true;
        }
        if (st != PRO_TK_NO_ERROR) {
            if (leader != nullptr) {
                ProDtlattachFree(leader);
                leader = nullptr;
            }
            if (leader_selection != nullptr) {
                ProSelectionFree(&leader_selection);
                leader_selection = nullptr;
            }
            st = PRO_TK_NO_ERROR;
        }
    }
    if (st == PRO_TK_NO_ERROR && leader == nullptr) {
        ProVector leader_location = {candidate.attach_point[0], candidate.attach_point[1], 0.0};
        st = ProDtlattachAlloc(PRO_DTLATTACHTYPE_FREE, candidate.view, leader_location, nullptr, &leader);
        if (diagnostics != nullptr) {
            diagnostics->free_attach_status = st;
        }
        if (st == PRO_TK_NO_ERROR && leader != nullptr) {
            st = ProDtlnotedataLeaderAdd(note_data, leader);
            if (diagnostics != nullptr) {
                diagnostics->free_leader_status = st;
                diagnostics->used_free = (st == PRO_TK_NO_ERROR);
            }
        }
    }
    if (st == PRO_TK_NO_ERROR) {
        (void)ProDtlnotedataElbowlengthSet(note_data, PRO_B_FALSE, kCustomBalloonElbowLength);
        st = ProDtlnotedataDisplayedSet(note_data, PRO_B_TRUE);
    }

    ProDtlnote note = {};
    if (st == PRO_TK_NO_ERROR) {
        st = ProDtlnoteCreate(reinterpret_cast<ProMdl>(drawing), nullptr, note_data, &note);
    }
    if (st == PRO_TK_NO_ERROR) {
        const ProError show_st = ProDtlnoteShow(&note);
        if (show_st != PRO_TK_NO_ERROR) {
            const ProError draw_st = ProDtlnoteDraw(&note);
            st = (draw_st == PRO_TK_NO_ERROR) ? PRO_TK_NO_ERROR : show_st;
        }
    }
    if (st == PRO_TK_NO_ERROR && created_note != nullptr) {
        *created_note = note;
    }

    cleanup();
    return st;
}

void LayoutCustomBalloonCandidates(ProDrawing drawing,
                                   ProView view,
                                   std::vector<CustomBalloonCandidate *> items,
                                   bool right_side)
{
    if (drawing == nullptr || view == nullptr || items.empty()) {
        return;
    }

    std::sort(items.begin(), items.end(), [](const CustomBalloonCandidate *lhs, const CustomBalloonCandidate *rhs) {
        if (std::abs(lhs->attach_point[1] - rhs->attach_point[1]) > 1.0e-9) {
            return lhs->attach_point[1] > rhs->attach_point[1];
        }
        return lhs->selection_order < rhs->selection_order;
    });

    core::Dwg3GroupOutline outline = {};
    if (GetDrawingViewOutlineBox(drawing, view, outline) != PRO_TK_NO_ERROR) {
        return;
    }

    const double x = right_side ? outline.max_x + kCustomBalloonOutsideOffset
                                : outline.min_x - kCustomBalloonOutsideOffset;

    const bool all_have_entity_attach = std::all_of(
        items.begin(),
        items.end(),
        [](const CustomBalloonCandidate *item) {
            return item != nullptr && item->has_entity_attach_point;
        });

    std::sort(items.begin(), items.end(), [all_have_entity_attach, right_side](const CustomBalloonCandidate *lhs,
                                                                              const CustomBalloonCandidate *rhs) {
        if (all_have_entity_attach &&
            std::abs(lhs->attach_point[1] - rhs->attach_point[1]) > 1.0e-9) {
            return lhs->attach_point[1] > rhs->attach_point[1];
        }
        if (all_have_entity_attach &&
            std::abs(lhs->attach_point[0] - rhs->attach_point[0]) > 1.0e-9) {
            return right_side ? (lhs->attach_point[0] > rhs->attach_point[0])
                              : (lhs->attach_point[0] < rhs->attach_point[0]);
        }
        if (lhs->selection_order != rhs->selection_order) {
            return lhs->selection_order < rhs->selection_order;
        }
        return lhs->label < rhs->label;
    });

    const double top_bound = outline.max_y - kCustomBalloonVerticalSpacing;
    const double bottom_bound = outline.min_y + kCustomBalloonVerticalSpacing;
    double previous_y = std::min(items.front()->attach_point[1], top_bound) + kCustomBalloonVerticalSpacing;
    for (size_t i = 0; i < items.size(); ++i) {
        CustomBalloonCandidate *item = items[i];
        double y = all_have_entity_attach ? item->attach_point[1]
                                          : (top_bound - kCustomBalloonVerticalSpacing * static_cast<double>(i));
        y = std::min(y, top_bound);
        if (i > 0) {
            y = std::min(y, previous_y - kCustomBalloonVerticalSpacing);
        }
        previous_y = y;
        item->note_point[0] = x;
        item->note_point[1] = y;
        item->note_point[2] = 0.0;
        item->right_side = right_side;
    }

    if (!items.empty() && items.back()->note_point[1] < bottom_bound) {
        const double shift_up = bottom_bound - items.back()->note_point[1];
        for (CustomBalloonCandidate *item : items) {
            item->note_point[1] += shift_up;
        }
    }
}

void LayoutCustomBalloonCandidates(ProDrawing drawing,
                                   const std::vector<ProView> &views,
                                   std::vector<CustomBalloonCandidate> &candidates)
{
    for (ProView view : views) {
        if (view == nullptr) {
            continue;
        }

        core::Dwg3GroupOutline outline = {};
        if (GetDrawingViewOutlineBox(drawing, view, outline) != PRO_TK_NO_ERROR) {
            continue;
        }
        const double center_x = OutlineCenterX(outline);

        std::vector<CustomBalloonCandidate *> left;
        std::vector<CustomBalloonCandidate *> right;
        std::vector<CustomBalloonCandidate *> bom_rows;
        const std::wstring view_key = ViewIdentityKey(drawing, view);
        for (CustomBalloonCandidate &candidate : candidates) {
            if (candidate.view_key != view_key) {
                continue;
            }
            if (candidate.from_bom_table) {
                bom_rows.push_back(&candidate);
                continue;
            }
            if (candidate.attach_point[0] < center_x) {
                left.push_back(&candidate);
            } else {
                right.push_back(&candidate);
            }
        }

        std::sort(bom_rows.begin(), bom_rows.end(), [](const CustomBalloonCandidate *lhs, const CustomBalloonCandidate *rhs) {
            if (lhs->selection_order != rhs->selection_order) {
                return lhs->selection_order < rhs->selection_order;
            }
            return lhs->label < rhs->label;
        });
        for (size_t i = 0; i < bom_rows.size(); ++i) {
            CustomBalloonCandidate *item = bom_rows[i];
            if (item != nullptr && item->has_entity_attach_point) {
                if (item->attach_point[0] < center_x) {
                    left.push_back(item);
                } else {
                    right.push_back(item);
                }
            } else if ((i % 2) == 0) {
                left.push_back(bom_rows[i]);
            } else {
                right.push_back(bom_rows[i]);
            }
        }

        LayoutCustomBalloonCandidates(drawing, view, left, false);
        LayoutCustomBalloonCandidates(drawing, view, right, true);
    }
}

void CreateCustomBalloonNotes(ProDrawing drawing,
                              const std::vector<ProView> &views,
                              std::vector<CustomBalloonCandidate> &candidates,
                              std::vector<CreatedCustomBalloonNote> &created_notes,
                              BalloonArrangeSummary &summary,
                              const Drawing3LogSink &log_sink,
                              bool apply_layout = true)
{
    created_notes.clear();
    if (drawing == nullptr || candidates.empty()) {
        return;
    }

    if (apply_layout) {
        LayoutCustomBalloonCandidates(drawing, views, candidates);
    }

    for (const CustomBalloonCandidate &candidate : candidates) {
        ProDtlnote created = {};
        CustomBalloonCreateDiagnostics diagnostics = {};
        const ProError st_create = CreateCustomBalloonNote(drawing, candidate, &created, &diagnostics);
        LogLine(log_sink,
                "ArrangeBalloons custom-create selection=%d view=%p side=%s label=%s attach=(%.3f,%.3f) leader_point=(%.3f,%.3f,%.3f) note=(%.3f,%.3f) status=%d note_id=%d leader_mode=%s has_path=%d allow_null_path=%d has_entity=%d leader_item=%d/%d param_attempt=%d param_st=(%d,%d,%d,%d,%d) free_st=(%d,%d)",
                candidate.selection_order,
                static_cast<void *>(candidate.view),
                candidate.right_side ? "right" : "left",
                autobbox::common::WToA(candidate.label.c_str()).c_str(),
                candidate.attach_point[0],
                candidate.attach_point[1],
                candidate.leader_point[0],
                candidate.leader_point[1],
                candidate.leader_point[2],
                candidate.note_point[0],
                candidate.note_point[1],
                static_cast<int>(st_create),
                (st_create == PRO_TK_NO_ERROR) ? created.id : -1,
                diagnostics.used_parametric ? "parametric" : (diagnostics.used_free ? "free" : "none"),
                candidate.has_component_path ? 1 : 0,
                candidate.allow_null_component_path ? 1 : 0,
                candidate.has_entity_attach_point ? 1 : 0,
                candidate.has_leader_model_item ? static_cast<int>(candidate.leader_model_item.type) : -1,
                candidate.has_leader_model_item ? candidate.leader_model_item.id : -1,
                diagnostics.attempted_parametric ? 1 : 0,
                static_cast<int>(diagnostics.selection_alloc_status),
                static_cast<int>(diagnostics.selection_view_status),
                static_cast<int>(diagnostics.selection_point_status),
                static_cast<int>(diagnostics.parametric_attach_status),
                static_cast<int>(diagnostics.parametric_leader_status),
                static_cast<int>(diagnostics.free_attach_status),
                static_cast<int>(diagnostics.free_leader_status));
        if (st_create == PRO_TK_NO_ERROR) {
            ++summary.custom_balloons_created;
            CreatedCustomBalloonNote created_item = {};
            created_item.note = created;
            created_item.view = candidate.view;
            created_item.view_key = candidate.view_key;
            created_item.selection_order = candidate.selection_order;
            created_notes.push_back(created_item);
        } else {
            ++summary.custom_balloons_failed;
            if (summary.first_error == PRO_TK_NO_ERROR) {
                summary.first_error = st_create;
            }
        }
    }
}

void ReorderSide(std::vector<BalloonLayoutItem *> items,
                 BalloonArrangeSummary &summary,
                 const Drawing3LogSink &log_sink)
{
    if (items.size() < 2) {
        return;
    }

    std::sort(items.begin(), items.end(), [](const BalloonLayoutItem *lhs, const BalloonLayoutItem *rhs) {
        if (std::abs(lhs->leader_location[1] - rhs->leader_location[1]) > 1.0e-9) {
            return lhs->leader_location[1] > rhs->leader_location[1];
        }
        return lhs->selection_order < rhs->selection_order;
    });

    double min_y = items.front()->note_location[1];
    double max_y = items.front()->note_location[1];
    double sum_y = 0.0;
    for (const BalloonLayoutItem *item : items) {
        min_y = std::min(min_y, item->note_location[1]);
        max_y = std::max(max_y, item->note_location[1]);
        sum_y += item->note_location[1];
    }

    const double center_y = sum_y / static_cast<double>(items.size());
    const double span = std::max(max_y - min_y,
                                 kSecondaryMinVerticalSpacing * static_cast<double>(items.size() - 1));
    const double step = span / static_cast<double>(items.size() - 1);
    const double start_y = center_y + span * 0.5;

    for (size_t i = 0; i < items.size(); ++i) {
        BalloonLayoutItem *item = items[i];
        ProVector target = {item->note_location[0], start_y - step * static_cast<double>(i), item->note_location[2]};
        if (std::abs(target[1] - item->note_location[1]) < 0.5) {
            continue;
        }

        const ProError st_move = MoveNoteTo(item->note, target);
        LogLine(log_sink,
                "ArrangeBalloons secondary-move note_id=%d from=(%.3f,%.3f) to=(%.3f,%.3f) leader_y=%.3f status=%d",
                item->note.id,
                item->note_location[0],
                item->note_location[1],
                target[0],
                target[1],
                item->leader_location[1],
                static_cast<int>(st_move));
        if (st_move == PRO_TK_NO_ERROR) {
            ++summary.notes_reordered;
        } else {
            ++summary.notes_reorder_failed;
            if (summary.first_error == PRO_TK_NO_ERROR) {
                summary.first_error = st_move;
            }
        }
    }
}

void ReorderSelectedNotesByLeaderOrder(const std::vector<SelectedBalloonNote> &selected_notes,
                                       const std::vector<ProView> &views,
                                       ProDrawing drawing,
                                       BalloonArrangeSummary &summary,
                                       const Drawing3LogSink &log_sink)
{
    std::vector<BalloonLayoutItem> layout_items;
    layout_items.reserve(selected_notes.size());
    for (const SelectedBalloonNote &selected : selected_notes) {
        BalloonLayoutItem item = {};
        if (TryReadNoteLayout(selected, item, log_sink)) {
            layout_items.push_back(item);
        }
    }

    for (ProView view : views) {
        std::vector<BalloonLayoutItem *> left;
        std::vector<BalloonLayoutItem *> right;
        const std::wstring view_key = ViewIdentityKey(drawing, view);
        for (BalloonLayoutItem &item : layout_items) {
            if (item.view_key != view_key) {
                continue;
            }
            if (item.note_location[0] < item.leader_location[0]) {
                left.push_back(&item);
            } else {
                right.push_back(&item);
            }
        }

        LogLine(log_sink,
                "ArrangeBalloons secondary view=%p left=%zu right=%zu",
                static_cast<void *>(view),
                left.size(),
                right.size());
        ReorderSide(left, summary, log_sink);
        ReorderSide(right, summary, log_sink);
    }
}

} // namespace

BalloonArrangeSummary ExecuteArrangeSelectedBalloonsTask(ProDrawing drawing,
                                                        int sheet,
                                                        const BalloonArrangeOptions &options,
                                                        const Drawing3LogSink &log_sink)
{
    BalloonArrangeSummary summary = {};
    summary.sheet = sheet;

    if (drawing == nullptr || sheet <= 0) {
        summary.first_error = PRO_TK_BAD_INPUTS;
        LogLine(log_sink, "FAIL arrange-balloons reason=bad-inputs drawing=%p sheet=%d", static_cast<void *>(drawing), sheet);
        return summary;
    }

    std::vector<ProView> views;
    std::vector<SelectedBalloonNote> selected_notes;
    if (!CollectSelectedViews(drawing, sheet, summary, views, selected_notes, log_sink)) {
        return summary;
    }
    std::vector<CustomBalloonCandidate> custom_candidates;
    if (!CollectBomTableCandidates(drawing, sheet, options, summary, custom_candidates, views, log_sink)) {
        return summary;
    }
    if (custom_candidates.empty()) {
        if (!CollectCustomBalloonCandidates(drawing, sheet, options, summary, custom_candidates, log_sink)) {
            return summary;
        }
        for (const CustomBalloonCandidate &candidate : custom_candidates) {
            const bool seen = std::any_of(views.begin(), views.end(), [&](ProView view) {
                return ViewIdentityKey(drawing, view) == candidate.view_key;
            });
            if (!seen) {
                views.push_back(candidate.view);
            }
        }
    }
    summary.valid_views = static_cast<int>(views.size());

    LogLine(log_sink,
            "ArrangeBalloons begin selected_total=%d valid_views=%d custom_candidates=%zu label_source=%s param=%s note_offset=%.3f note_spacing=%.3f",
            summary.selected_total,
            summary.valid_views,
            custom_candidates.size(),
            options.label_source == BalloonArrangeLabelSource::ModelName ? "model_name" : "parameter",
            autobbox::common::WToA(autobbox::creo::NormalizeParameterName(options.parameter_name).c_str()).c_str(),
            kCustomBalloonOutsideOffset,
            kCustomBalloonVerticalSpacing);

    std::vector<CreatedCustomBalloonNote> created_notes;
    DeleteExistingCustomNotesForCandidates(drawing, sheet, views, custom_candidates, summary, log_sink);
    CreateCustomBalloonNotes(drawing, views, custom_candidates, created_notes, summary, log_sink);
    if (!custom_candidates.empty()) {
        summary.views_arranged = summary.valid_views;
        if (summary.custom_balloons_created <= 0 && summary.custom_balloons_failed > 0) {
            summary.views_failed = summary.valid_views;
        }
    }

    for (const CreatedCustomBalloonNote &created : created_notes) {
        SelectedBalloonNote selected = {};
        selected.note = created.note;
        selected.view = created.view;
        selected.view_key = created.view_key;
        selected.selection_order = created.selection_order;
        selected_notes.push_back(selected);
    }
    CollectSheetLeaderNotesForViews(drawing, sheet, views, selected_notes, log_sink);
    ReorderSelectedNotesByLeaderOrder(selected_notes, views, drawing, summary, log_sink);
    if (summary.views_arranged == 0 && summary.valid_views > 0 &&
        (!selected_notes.empty() || summary.notes_reordered > 0 || summary.custom_balloons_created > 0)) {
        summary.views_arranged = summary.valid_views;
    }

    LogLine(log_sink,
            "ArrangeBalloons end selected_total=%d valid_views=%d arranged=%d failed=%d custom_created=%d custom_failed=%d notes_reordered=%d notes_reorder_failed=%d first_error=%d",
            summary.selected_total,
            summary.valid_views,
            summary.views_arranged,
            summary.views_failed,
            summary.custom_balloons_created,
            summary.custom_balloons_failed,
            summary.notes_reordered,
            summary.notes_reorder_failed,
            static_cast<int>(summary.first_error));
    return summary;
}

BalloonArrangeSummary ExecuteArrangeBomTableNoteBalloonsTask(
    ProDrawing drawing,
    int sheet,
    const BalloonArrangeOptions &options,
    const BalloonArrangeBomTableSelection &bom_table,
    ProView target_view,
    const Drawing3LogSink &log_sink)
{
    BalloonArrangeSummary summary = {};
    summary.sheet = sheet;
    summary.selected_total = 1;

    if (drawing == nullptr || sheet <= 0 || bom_table.table.owner == nullptr || target_view == nullptr) {
        summary.first_error = PRO_TK_BAD_INPUTS;
        LogLine(log_sink,
                "FAIL arrange-balloons-note reason=bad-inputs drawing=%p sheet=%d table_owner=%p view=%p",
                static_cast<void *>(drawing),
                sheet,
                static_cast<void *>(bom_table.table.owner),
                static_cast<void *>(target_view));
        return summary;
    }

    int view_sheet = 0;
    const ProError st_view_sheet = ProDrawingViewSheetGet(drawing, target_view, &view_sheet);
    if (st_view_sheet != PRO_TK_NO_ERROR || view_sheet != sheet) {
        summary.first_error = (st_view_sheet == PRO_TK_NO_ERROR) ? PRO_TK_BAD_INPUTS : st_view_sheet;
        LogLine(log_sink,
                "FAIL arrange-balloons-note reason=view-sheet status=%d view_sheet=%d current_sheet=%d",
                static_cast<int>(st_view_sheet),
                view_sheet,
                sheet);
        return summary;
    }

    SelectedBomTableSegment segment = {};
    segment.table = bom_table.table;
    segment.segment = bom_table.segment;

    std::vector<SelectedBomTableSegment> segments = {segment};
    std::vector<ProView> views = {target_view};
    std::vector<CustomBalloonCandidate> custom_candidates;
    if (!CollectBomTableCandidatesFromSegments(
            drawing,
            sheet,
            options,
            segments,
            target_view,
            summary,
            custom_candidates,
            views,
            log_sink)) {
        return summary;
    }

    summary.valid_views = 1;
    LogLine(log_sink,
            "ArrangeBalloons note-flow begin table=%p segment=%d selected_cell=(%d,%d) target_view=%p candidates=%zu label_source=%s param=%s",
            static_cast<void *>(bom_table.table.owner),
            bom_table.segment,
            bom_table.selected_row,
            bom_table.selected_column,
            static_cast<void *>(target_view),
            custom_candidates.size(),
            options.label_source == BalloonArrangeLabelSource::ModelName ? "model_name" : "parameter",
            autobbox::common::WToA(autobbox::creo::NormalizeParameterName(options.parameter_name).c_str()).c_str());

    if (custom_candidates.empty()) {
        summary.views_failed = 1;
        if (summary.first_error == PRO_TK_NO_ERROR) {
            summary.first_error = PRO_TK_E_NOT_FOUND;
        }
        return summary;
    }

    LogLine(log_sink,
            "ArrangeBalloons note-flow official-bom-sampling begin reason=sample-native-leaders-then-delete-temp");
    SampleOfficialBomBalloonLeaders(drawing, sheet, segments, views, custom_candidates, log_sink);
    DeleteExistingCustomNotesForCandidates(drawing, sheet, views, custom_candidates, summary, log_sink);

    std::vector<CreatedCustomBalloonNote> created_notes;
    CreateCustomBalloonNotes(drawing, views, custom_candidates, created_notes, summary, log_sink);
    if (summary.custom_balloons_created > 0) {
        summary.views_arranged = 1;
    } else if (summary.custom_balloons_failed > 0) {
        summary.views_failed = 1;
    }

    LogLine(log_sink,
            "ArrangeBalloons note-flow end valid_views=%d arranged=%d failed=%d custom_updated=%d custom_created=%d custom_failed=%d first_error=%d",
            summary.valid_views,
            summary.views_arranged,
            summary.views_failed,
            summary.custom_balloons_updated,
            summary.custom_balloons_created,
            summary.custom_balloons_failed,
            static_cast<int>(summary.first_error));
    return summary;
}

BalloonArrangeSummary ExecuteOfficialBomBalloonCreatePocTask(
    ProDrawing drawing,
    int sheet,
    const BalloonArrangeBomTableSelection &bom_table,
    ProView target_view,
    const Drawing3LogSink &log_sink)
{
    BalloonArrangeSummary summary = {};
    summary.sheet = sheet;
    summary.valid_views = target_view != nullptr ? 1 : 0;

    if (drawing == nullptr || sheet <= 0 || target_view == nullptr || bom_table.table.owner == nullptr) {
        summary.first_error = PRO_TK_BAD_INPUTS;
        summary.views_failed = target_view != nullptr ? 1 : 0;
        LogLine(log_sink,
                "FAIL arrange-balloons official-rebuild-poc reason=bad-inputs drawing=%p sheet=%d view=%p table=%p",
                static_cast<void *>(drawing),
                sheet,
                static_cast<void *>(target_view),
                static_cast<void *>(bom_table.table.owner));
        return summary;
    }

    int view_sheet = 0;
    const ProError st_view_sheet = ProDrawingViewSheetGet(drawing, target_view, &view_sheet);
    if (st_view_sheet != PRO_TK_NO_ERROR || view_sheet != sheet) {
        summary.first_error = (st_view_sheet == PRO_TK_NO_ERROR) ? PRO_TK_BAD_INPUTS : st_view_sheet;
        summary.views_failed = 1;
        LogLine(log_sink,
                "FAIL arrange-balloons official-rebuild-poc reason=view-sheet status=%d view_sheet=%d current_sheet=%d",
                static_cast<int>(st_view_sheet),
                view_sheet,
                sheet);
        return summary;
    }

    int first_row = 0;
    int last_row = 0;
    int first_col = 0;
    int last_col = 0;
    ProError st_extents = ProDwgtableSegExtentsGet(
        const_cast<ProDwgtable *>(&bom_table.table),
        bom_table.segment,
        &first_row,
        &last_row,
        &first_col,
        &last_col);
    if (st_extents != PRO_TK_NO_ERROR && bom_table.segment != PRO_VALUE_UNUSED) {
        st_extents = ProDwgtableSegExtentsGet(
            const_cast<ProDwgtable *>(&bom_table.table),
            PRO_VALUE_UNUSED,
            &first_row,
            &last_row,
            &first_col,
            &last_col);
    }

    int cell_region = PRO_VALUE_UNUSED;
    const ProError st_cell_region = ProDwgtableCellRegionGet(
        drawing,
        const_cast<ProDwgtable *>(&bom_table.table),
        std::max(0, bom_table.selected_column - 1),
        std::max(0, bom_table.selected_row - 1),
        &cell_region);

    int region_id = bom_table.segment;
    if (region_id == PRO_VALUE_UNUSED && st_cell_region == PRO_TK_NO_ERROR) {
        region_id = cell_region;
    }

    std::vector<ProDtlsyminst> before_symbols;
    CollectDrawingSymbols(drawing, sheet, before_symbols);
    const std::unordered_set<std::string> before_keys = SymbolKeySet(before_symbols);

    BalloonArrangeOptions options = {};
    SelectedBomTableSegment segment = {};
    segment.table = bom_table.table;
    segment.segment = region_id;
    std::vector<SelectedBomTableSegment> segments = {segment};
    std::vector<ProView> views = {target_view};
    std::vector<CustomBalloonCandidate> candidates;
    (void)CollectBomTableCandidatesFromSegments(
        drawing,
        sheet,
        options,
        segments,
        target_view,
        summary,
        candidates,
        views,
        log_sink);

    CustomBalloonCandidate *candidate = nullptr;
    for (CustomBalloonCandidate &item : candidates) {
        if (item.view_key == ViewIdentityKey(drawing, target_view) && item.selection_order == bom_table.selected_row) {
            candidate = &item;
            break;
        }
    }
    if (candidate == nullptr) {
        for (CustomBalloonCandidate &item : candidates) {
            if (item.view_key == ViewIdentityKey(drawing, target_view)) {
                candidate = &item;
                break;
            }
        }
    }
    if (candidate == nullptr) {
        summary.first_error = PRO_TK_E_NOT_FOUND;
        summary.views_failed = 1;
        LogLine(log_sink,
                "FAIL arrange-balloons official-rebuild-poc reason=no-bom-candidate selected_row=%d candidates=%zu",
                bom_table.selected_row,
                candidates.size());
        return summary;
    }

    core::Dwg3GroupOutline outline = {};
    ProError st_outline = GetDrawingViewOutlineBox(drawing, target_view, outline);
    if (st_outline != PRO_TK_NO_ERROR) {
        summary.first_error = st_outline;
        summary.views_failed = 1;
        LogLine(log_sink,
                "FAIL arrange-balloons official-rebuild-poc reason=view-outline status=%d",
                static_cast<int>(st_outline));
        return summary;
    }

    const double left_x = outline.min_x - kCustomBalloonOutsideOffset;
    const double right_x = outline.max_x + kCustomBalloonOutsideOffset;
    const double top_y = outline.max_y + kCustomBalloonOutsideOffset;
    const double bottom_y = outline.min_y - kCustomBalloonOutsideOffset;
    const double dl = std::abs(candidate->leader_point[0] - left_x);
    const double dr = std::abs(candidate->leader_point[0] - right_x);
    const double dt = std::abs(candidate->leader_point[1] - top_y);
    const double db = std::abs(candidate->leader_point[1] - bottom_y);

    const char *side = "left";
    double best = dl;
    double target[3] = {left_x, std::max(outline.min_y, std::min(outline.max_y, candidate->leader_point[1])), 0.0};
    if (dr < best) {
        best = dr;
        side = "right";
        target[0] = right_x;
        target[1] = std::max(outline.min_y, std::min(outline.max_y, candidate->leader_point[1]));
    }
    if (dt < best) {
        best = dt;
        side = "top";
        target[0] = std::max(outline.min_x, std::min(outline.max_x, candidate->leader_point[0]));
        target[1] = top_y;
    }
    if (db < best) {
        side = "bottom";
        target[0] = std::max(outline.min_x, std::min(outline.max_x, candidate->leader_point[0]));
        target[1] = bottom_y;
    }

    const std::wstring target_view_key = ViewIdentityKey(drawing, target_view);
    bool deleted_existing_symbol = false;
    int deleted_existing_symbol_id = -1;
    ProError deleted_existing_status = PRO_TK_GENERAL_ERROR;
    ProError removed_existing_status = PRO_TK_GENERAL_ERROR;
    double deleted_existing_dist2 = -1.0;
    TraditionalBomBalloonSymbol nearest_existing = {};
    bool have_nearest_existing = false;
    double best_existing_dist2 = 1.0e300;
    for (size_t i = 0; i < before_symbols.size(); ++i) {
        TraditionalBomBalloonSymbol existing = {};
        if (!ExtractTraditionalBomBalloonSymbol(
                drawing,
                target_view,
                target_view_key,
                before_symbols[i],
                static_cast<int>(i),
                existing,
                log_sink)) {
            continue;
        }
        const double dx = existing.leader_point[0] - candidate->leader_point[0];
        const double dy = existing.leader_point[1] - candidate->leader_point[1];
        const double dist2 = dx * dx + dy * dy;
        if (!have_nearest_existing || dist2 < best_existing_dist2) {
            best_existing_dist2 = dist2;
            nearest_existing = existing;
            have_nearest_existing = true;
        }
    }
    if (have_nearest_existing) {
        ProDtlsyminst to_delete = nearest_existing.symbol;
        deleted_existing_status = ProDtlsyminstDelete(&to_delete);
        if (deleted_existing_status != PRO_TK_NO_ERROR) {
            to_delete = nearest_existing.symbol;
            removed_existing_status = ProDtlsyminstRemove(&to_delete);
        }
        deleted_existing_symbol = deleted_existing_status == PRO_TK_NO_ERROR ||
                                  removed_existing_status == PRO_TK_NO_ERROR;
        deleted_existing_symbol_id = nearest_existing.symbol.id;
        deleted_existing_dist2 = best_existing_dist2;
    }
    LogLine(log_sink,
            "ArrangeBalloons official-rebuild-poc-predelete have=%d symbol=%d dist2=%.3f delete=%d remove=%d deleted=%d selected_leader=(%.3f,%.3f) existing_leader=(%.3f,%.3f)",
            have_nearest_existing ? 1 : 0,
            deleted_existing_symbol_id,
            deleted_existing_dist2,
            static_cast<int>(deleted_existing_status),
            static_cast<int>(removed_existing_status),
            deleted_existing_symbol ? 1 : 0,
            candidate->leader_point[0],
            candidate->leader_point[1],
            have_nearest_existing ? nearest_existing.leader_point[0] : 0.0,
            have_nearest_existing ? nearest_existing.leader_point[1] : 0.0);

    int selected_candidate_record_index = 0;
    bool found_selected_record_index = false;
    int first_candidate_row = 0;
    bool have_first_candidate_row = false;
    for (CustomBalloonCandidate &item : candidates) {
        if (item.view_key != target_view_key) {
            continue;
        }
        if (!have_first_candidate_row || item.selection_order < first_candidate_row) {
            first_candidate_row = item.selection_order;
            have_first_candidate_row = true;
        }
        if (&item == candidate) {
            found_selected_record_index = true;
            break;
        }
        ++selected_candidate_record_index;
    }
    if (!found_selected_record_index) {
        selected_candidate_record_index = std::max(0, bom_table.selected_row - 1);
    }

    std::vector<int> record_indices;
    auto add_record_index = [&record_indices](int value) {
        if (value < 0) {
            return;
        }
        if (std::find(record_indices.begin(), record_indices.end(), value) == record_indices.end()) {
            record_indices.push_back(value);
        }
    };
    add_record_index(selected_candidate_record_index);
    if (have_first_candidate_row && bom_table.selected_row >= first_candidate_row) {
        add_record_index(bom_table.selected_row - first_candidate_row);
    }
    if (st_extents == PRO_TK_NO_ERROR && first_row > 0 && bom_table.selected_row >= first_row) {
        add_record_index(bom_table.selected_row - first_row);
    }
    add_record_index(std::max(0, bom_table.selected_row - 1));

    std::vector<int> region_ids;
    auto add_region_id = [&region_ids](int value) {
        if (std::find(region_ids.begin(), region_ids.end(), value) == region_ids.end()) {
            region_ids.push_back(value);
        }
    };
    add_region_id(region_id);
    if (st_cell_region == PRO_TK_NO_ERROR) {
        add_region_id(cell_region);
    }
    add_region_id(bom_table.segment);
    add_region_id(PRO_VALUE_UNUSED);
    add_region_id(0);

    struct OfficialCreateAttemptResult {
        ProError create_status = PRO_TK_GENERAL_ERROR;
        ProError attach_array_status = PRO_TK_GENERAL_ERROR;
        ProError member_array_status = PRO_TK_NO_ERROR;
        int created_symbols = 0;
        size_t after_count = 0;
        int region_id = PRO_VALUE_UNUSED;
        int record_index = -1;
        int location_dim = 0;
        int ref_mode = 0;
        int reference_id = PRO_VALUE_UNUSED;
        ProType reference_type = PRO_TYPE_UNUSED;
    };

    OfficialCreateAttemptResult best_attempt = {};
    bool created = false;
    int attempt_index = 0;
    int attempts = 0;
    const int location_dims[] = {2, 3};
    const int ref_modes[] = {0, 1};

    for (int try_region_id : region_ids) {
        if (created) {
            break;
        }
        for (int try_record_index : record_indices) {
            if (created) {
                break;
            }
            for (int location_dim : location_dims) {
                if (created) {
                    break;
                }
                for (int ref_mode : ref_modes) {
                    if (ref_mode != 0 && !candidate->has_leader_model_item) {
                        continue;
                    }

                    ++attempts;
                    OfficialCreateAttemptResult attempt = {};
                    attempt.region_id = try_region_id;
                    attempt.record_index = try_record_index;
                    attempt.location_dim = location_dim;
                    attempt.ref_mode = ref_mode;

                    double *attach_note_location = nullptr;
                    int *reference_memb_id_tab = nullptr;
                    attempt.attach_array_status = AllocDoubleProArray(target, location_dim, &attach_note_location);
                    attempt.member_array_status = PRO_TK_NO_ERROR;

                    if (ref_mode != 0) {
                        attempt.reference_id = candidate->leader_model_item.id;
                        attempt.reference_type = candidate->leader_model_item.type;
                        if (candidate->has_component_path && candidate->component_path.table_num > 0) {
                            attempt.member_array_status = AllocIntProArray(
                                candidate->component_path.comp_id_table,
                                candidate->component_path.table_num,
                                &reference_memb_id_tab);
                        }
                    }

                    if (attempt.attach_array_status == PRO_TK_NO_ERROR &&
                        attempt.member_array_status == PRO_TK_NO_ERROR) {
                        attempt.create_status = ProBomballoonByRecordCreate(
                            drawing,
                            const_cast<ProDwgtable *>(&bom_table.table),
                            try_region_id,
                            target_view,
                            try_record_index,
                            reference_memb_id_tab,
                            attempt.reference_id,
                            attempt.reference_type,
                            attach_note_location);
                    }

                    std::vector<ProDtlsyminst> attempt_after_symbols;
                    CollectDrawingSymbols(drawing, sheet, attempt_after_symbols);
                    attempt.after_count = attempt_after_symbols.size();
                    for (const ProDtlsyminst &symbol : attempt_after_symbols) {
                        if (before_keys.find(DtlSymbolKey(symbol)) == before_keys.end()) {
                            ++attempt.created_symbols;
                        }
                    }

                    LogLine(log_sink,
                            "ArrangeBalloons official-rebuild-poc-attempt idx=%d region=%d record_index=%d location_dim=%d ref_mode=%s ref=%d/%d has_path=%d path_len=%d arrays=(%d,%d) create=%d before=%zu after=%zu created=%d",
                            attempt_index,
                            try_region_id,
                            try_record_index,
                            location_dim,
                            ref_mode == 0 ? "none" : "leader",
                            static_cast<int>(attempt.reference_type),
                            attempt.reference_id,
                            candidate->has_component_path ? 1 : 0,
                            candidate->has_component_path ? candidate->component_path.table_num : 0,
                            static_cast<int>(attempt.attach_array_status),
                            static_cast<int>(attempt.member_array_status),
                            static_cast<int>(attempt.create_status),
                            before_symbols.size(),
                            attempt.after_count,
                            attempt.created_symbols);

                    if (reference_memb_id_tab != nullptr) {
                        ProArrayFree(reinterpret_cast<ProArray *>(&reference_memb_id_tab));
                    }
                    if (attach_note_location != nullptr) {
                        ProArrayFree(reinterpret_cast<ProArray *>(&attach_note_location));
                    }

                    best_attempt = attempt;
                    ++attempt_index;
                    if (attempt.create_status == PRO_TK_NO_ERROR && attempt.created_symbols > 0) {
                        created = true;
                        break;
                    }
                }
            }
        }
    }

    LogLine(log_sink,
            "ArrangeBalloons official-rebuild-poc table=%p selected_row=%d selected_col=%d seg=%d cell_region_status=%d cell_region=%d extents_status=%d rows=%d..%d cols=%d..%d candidate_record=%d first_candidate_row=%d attempts=%d used_region=%d used_record_index=%d location_dim=%d ref_mode=%s view=%p side=%s target=(%.3f,%.3f,%.3f) model=%s has_path=%d path_len=%d leader_item=%d/%d arrays=(%d,%d) create=%d before=%zu after=%zu created=%d",
            static_cast<void *>(bom_table.table.owner),
            bom_table.selected_row,
            bom_table.selected_column,
            bom_table.segment,
            static_cast<int>(st_cell_region),
            cell_region,
            static_cast<int>(st_extents),
            first_row,
            last_row,
            first_col,
            last_col,
            selected_candidate_record_index,
            have_first_candidate_row ? first_candidate_row : -1,
            attempts,
            best_attempt.region_id,
            best_attempt.record_index,
            best_attempt.location_dim,
            best_attempt.ref_mode == 0 ? "none" : "leader",
            static_cast<void *>(target_view),
            side,
            target[0],
            target[1],
            target[2],
            candidate->model != nullptr ? autobbox::creo::DefaultModelTag(candidate->model).c_str() : "(null)",
            candidate->has_component_path ? 1 : 0,
            candidate->has_component_path ? candidate->component_path.table_num : 0,
            candidate->has_leader_model_item ? static_cast<int>(candidate->leader_model_item.type) : -1,
            candidate->has_leader_model_item ? candidate->leader_model_item.id : -1,
            static_cast<int>(best_attempt.attach_array_status),
            static_cast<int>(best_attempt.member_array_status),
            static_cast<int>(best_attempt.create_status),
            before_symbols.size(),
            best_attempt.after_count,
            best_attempt.created_symbols);

    if (!(best_attempt.create_status == PRO_TK_NO_ERROR && best_attempt.created_symbols > 0) &&
        deleted_existing_symbol) {
        const ProError st_restore = ProBomballoonCreate(
            drawing,
            const_cast<ProDwgtable *>(&bom_table.table),
            region_id,
            target_view);
        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-poc-restore deleted_symbol=%d region=%d status=%d",
                deleted_existing_symbol_id,
                region_id,
                static_cast<int>(st_restore));
    }

    summary.selected_total = 1;
    if (best_attempt.create_status == PRO_TK_NO_ERROR && best_attempt.created_symbols > 0) {
        summary.custom_balloons_created = best_attempt.created_symbols;
        summary.views_arranged = 1;
        summary.first_error = PRO_TK_NO_ERROR;
    } else {
        summary.custom_balloons_failed = 1;
        summary.views_failed = 1;
        summary.first_error = (best_attempt.create_status != PRO_TK_NO_ERROR)
                                  ? best_attempt.create_status
                                  : PRO_TK_GENERAL_ERROR;
    }
    return summary;
}

bool SameComponentPath(const ProAsmcomppath &lhs, const ProAsmcomppath &rhs)
{
    if (lhs.table_num != rhs.table_num) {
        return false;
    }
    for (int i = 0; i < lhs.table_num; ++i) {
        if (lhs.comp_id_table[i] != rhs.comp_id_table[i]) {
            return false;
        }
    }
    return true;
}

bool DeleteOfficialBomSymbol(ProDtlsyminst symbol,
                             ProError &erase_status,
                             ProError &delete_status,
                             ProError &remove_status)
{
    ProDtlsyminst copy = symbol;
    erase_status = ProDtlsyminstErase(&copy);
    copy = symbol;
    delete_status = ProDtlsyminstDelete(&copy);
    remove_status = PRO_TK_GENERAL_ERROR;
    if (delete_status == PRO_TK_NO_ERROR) {
        return true;
    }
    copy = symbol;
    remove_status = ProDtlsyminstRemove(&copy);
    return remove_status == PRO_TK_NO_ERROR;
}

std::vector<int> BuildRecordIndexCandidates(int row,
                                            int order_in_view,
                                            int first_candidate_row,
                                            bool have_first_candidate_row,
                                            int first_row,
                                            ProError st_extents)
{
    std::vector<int> indices;
    auto add = [&indices](int value) {
        if (value < 0) {
            return;
        }
        if (std::find(indices.begin(), indices.end(), value) == indices.end()) {
            indices.push_back(value);
        }
    };

    add(order_in_view);
    if (have_first_candidate_row && row >= first_candidate_row) {
        add(row - first_candidate_row);
    }
    if (st_extents == PRO_TK_NO_ERROR && first_row > 0 && row >= first_row) {
        add(row - first_row);
    }
    add(row - 1);
    return indices;
}

bool CollectRebuildBomRecordCandidates(ProDrawing drawing,
                                       int sheet,
                                       const BalloonArrangeBomTableSelection &bom_table,
                                       ProView target_view,
                                       BalloonArrangeSummary &summary,
                                       std::vector<RebuildBomRecordCandidate> &records,
                                       int &region_id_out,
                                       const Drawing3LogSink &log_sink)
{
    records.clear();
    region_id_out = bom_table.segment;
    if (drawing == nullptr || sheet <= 0 || bom_table.table.owner == nullptr || target_view == nullptr) {
        return false;
    }

    int first_row = 0;
    int last_row = 0;
    int first_col = 0;
    int last_col = 0;
    ProError st_extents = ProDwgtableSegExtentsGet(
        const_cast<ProDwgtable *>(&bom_table.table),
        bom_table.segment,
        &first_row,
        &last_row,
        &first_col,
        &last_col);
    if (st_extents != PRO_TK_NO_ERROR && bom_table.segment != PRO_VALUE_UNUSED) {
        st_extents = ProDwgtableSegExtentsGet(
            const_cast<ProDwgtable *>(&bom_table.table),
            PRO_VALUE_UNUSED,
            &first_row,
            &last_row,
            &first_col,
            &last_col);
    }

    int cell_region = PRO_VALUE_UNUSED;
    const ProError st_cell_region = ProDwgtableCellRegionGet(
        drawing,
        const_cast<ProDwgtable *>(&bom_table.table),
        std::max(0, bom_table.selected_column - 1),
        std::max(0, bom_table.selected_row - 1),
        &cell_region);
    if (region_id_out == PRO_VALUE_UNUSED && st_cell_region == PRO_TK_NO_ERROR) {
        region_id_out = cell_region;
    }

    BalloonArrangeOptions options = {};
    SelectedBomTableSegment segment = {};
    segment.table = bom_table.table;
    segment.segment = region_id_out;
    std::vector<SelectedBomTableSegment> segments = {segment};
    std::vector<ProView> views = {target_view};
    std::vector<CustomBalloonCandidate> candidates;
    (void)CollectBomTableCandidatesFromSegments(
        drawing,
        sheet,
        options,
        segments,
        target_view,
        summary,
        candidates,
        views,
        log_sink);

    const std::wstring target_view_key = ViewIdentityKey(drawing, target_view);
    int first_candidate_row = 0;
    bool have_first_candidate_row = false;
    int order_in_view = 0;
    for (const CustomBalloonCandidate &candidate : candidates) {
        if (candidate.view_key != target_view_key) {
            continue;
        }
        if (!have_first_candidate_row || candidate.selection_order < first_candidate_row) {
            first_candidate_row = candidate.selection_order;
            have_first_candidate_row = true;
        }
    }

    for (const CustomBalloonCandidate &candidate : candidates) {
        if (candidate.view_key != target_view_key) {
            continue;
        }
        RebuildBomRecordCandidate record = {};
        record.bom = candidate;
        record.region_id = region_id_out;
        record.record_indices = BuildRecordIndexCandidates(
            candidate.selection_order,
            order_in_view,
            first_candidate_row,
            have_first_candidate_row,
            first_row,
            st_extents);
        records.push_back(record);

        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-candidate row=%d region=%d record_indices=%zu first_candidate_row=%d extents_status=%d rows=%d..%d model=%s has_path=%d path_len=%d leader_item=%d/%d attach=(%.3f,%.3f)",
                candidate.selection_order,
                region_id_out,
                record.record_indices.size(),
                have_first_candidate_row ? first_candidate_row : -1,
                static_cast<int>(st_extents),
                first_row,
                last_row,
                candidate.model != nullptr ? autobbox::creo::DefaultModelTag(candidate.model).c_str() : "(null)",
                candidate.has_component_path ? 1 : 0,
                candidate.has_component_path ? candidate.component_path.table_num : 0,
                candidate.has_leader_model_item ? static_cast<int>(candidate.leader_model_item.type) : -1,
                candidate.has_leader_model_item ? candidate.leader_model_item.id : -1,
                candidate.attach_point[0],
                candidate.attach_point[1]);
        ++order_in_view;
    }

    LogLine(log_sink,
            "ArrangeBalloons official-rebuild-candidates-end table=%p selected_cell=(%d,%d) segment=%d cell_region_status=%d cell_region=%d region=%d count=%zu",
            static_cast<void *>(bom_table.table.owner),
            bom_table.selected_row,
            bom_table.selected_column,
            bom_table.segment,
            static_cast<int>(st_cell_region),
            cell_region,
            region_id_out,
            records.size());
    return true;
}

RebuildBomRecordCandidate *FindBestRebuildRecordForBalloon(
    TraditionalBomBalloonSymbol &problem,
    std::vector<RebuildBomRecordCandidate> &records,
    double &best_score_out,
    const char *&match_reason_out)
{
    RebuildBomRecordCandidate *best = nullptr;
    best_score_out = 1.0e300;
    match_reason_out = "none";

    for (RebuildBomRecordCandidate &record : records) {
        if (record.used || record.bom.model == nullptr) {
            continue;
        }
        if (problem.model != nullptr && !SameModel(problem.model, record.bom.model)) {
            continue;
        }
        if (problem.has_component_path && record.bom.has_component_path &&
            !SameComponentPath(problem.component_path, record.bom.component_path)) {
            continue;
        }

        const double dx = problem.leader_point[0] - record.bom.attach_point[0];
        const double dy = problem.leader_point[1] - record.bom.attach_point[1];
        double score = dx * dx + dy * dy;
        const char *reason = "leader+model";
        if (problem.has_component_path && record.bom.has_component_path) {
            score *= 0.25;
            reason = "leader+model+path";
        } else if (problem.model == nullptr) {
            score += 1.0e6;
            reason = "leader-only";
        }
        if (best == nullptr || score < best_score_out) {
            best = &record;
            best_score_out = score;
            match_reason_out = reason;
        }
    }
    return best;
}

struct RebuildCreateResult {
    bool created = false;
    ProError create_status = PRO_TK_GENERAL_ERROR;
    ProError attach_array_status = PRO_TK_GENERAL_ERROR;
    ProError member_array_status = PRO_TK_NO_ERROR;
    int created_symbols = 0;
    int created_symbol_id = -1;
    size_t before_count = 0;
    size_t after_count = 0;
    int region_id = PRO_VALUE_UNUSED;
    int record_index = -1;
    int location_dim = 0;
    int ref_mode = 0;
    int point_mode = 0;
    int create_mode = 0;
    int reference_id = PRO_VALUE_UNUSED;
    ProType reference_type = PRO_TYPE_UNUSED;
};

RebuildCreateResult TryCreateRebuiltOfficialBalloon(ProDrawing drawing,
                                                    int sheet,
                                                    const BalloonArrangeBomTableSelection &bom_table,
                                                    ProView target_view,
                                                    const RebuildBomRecordCandidate &record,
                                                    const TraditionalBomBalloonSymbol &problem,
                                                    const Drawing3LogSink &log_sink)
{
    RebuildCreateResult best = {};
    std::vector<ProDtlsyminst> before_symbols;
    CollectDrawingSymbols(drawing, sheet, before_symbols);
    const std::unordered_set<std::string> before_keys = SymbolKeySet(before_symbols);

    const int location_dims[] = {3, 2};
    const int point_modes[] = {0, 1};
    const int ref_modes[] = {1, 0};
    int attempt_index = 0;
    for (int record_index : record.record_indices) {
        if (best.created) {
            break;
        }
        for (int point_mode : point_modes) {
            if (best.created) {
                break;
            }
            for (int location_dim : location_dims) {
                if (best.created) {
                    break;
                }
                for (int ref_mode : ref_modes) {
                    const bool use_record_ref = record.bom.has_leader_model_item;
                    const bool has_reference = use_record_ref || problem.has_leader_model_item;
                    if (ref_mode != 0 && !has_reference) {
                        continue;
                    }
                    if (point_mode == 0 && !problem.has_component_attach_point) {
                        continue;
                    }

                    RebuildCreateResult attempt = {};
                    attempt.before_count = before_symbols.size();
                    attempt.region_id = record.region_id;
                    attempt.record_index = record_index;
                    attempt.location_dim = location_dim;
                    attempt.ref_mode = ref_mode;
                    attempt.point_mode = point_mode;

                    double location[3] = {0.0, 0.0, 0.0};
                    if (point_mode == 0) {
                        /*
                         * Official docs describe attach_note_location as the
                         * attachment point for the balloon on the component.
                         * Use the original leader pick point in model/member
                         * coordinates first; drawing target coordinates are
                         * only a fallback for no-leader/free placement.
                         */
                        location[0] = problem.component_attach_point[0];
                        location[1] = problem.component_attach_point[1];
                        location[2] = problem.component_attach_point[2];
                    } else {
                        location[0] = problem.target_point[0];
                        location[1] = problem.target_point[1];
                        location[2] = problem.target_point[2];
                    }
                    double *attach_note_location = nullptr;
                    int *reference_memb_id_tab = nullptr;
                    attempt.attach_array_status = AllocDoubleProArray(location, location_dim, &attach_note_location);

                    if (ref_mode != 0) {
                        attempt.reference_id = use_record_ref ? record.bom.leader_model_item.id : problem.leader_model_item.id;
                        attempt.reference_type = use_record_ref ? record.bom.leader_model_item.type : problem.leader_model_item.type;
                        const bool has_path = use_record_ref ? record.bom.has_component_path : problem.has_component_path;
                        const ProAsmcomppath &path = use_record_ref ? record.bom.component_path : problem.component_path;
                        if (has_path && path.table_num > 0) {
                            attempt.member_array_status = AllocIntProArray(
                                path.comp_id_table,
                                path.table_num,
                                &reference_memb_id_tab);
                        }
                    }

                    if (attempt.attach_array_status == PRO_TK_NO_ERROR &&
                        attempt.member_array_status == PRO_TK_NO_ERROR) {
                        attempt.create_status = ProBomballoonByRecordCreate(
                            drawing,
                            const_cast<ProDwgtable *>(&bom_table.table),
                            record.region_id,
                            target_view,
                            record_index,
                            reference_memb_id_tab,
                            attempt.reference_id,
                            attempt.reference_type,
                            attach_note_location);
                    }

                    std::vector<ProDtlsyminst> after_symbols;
                    CollectDrawingSymbols(drawing, sheet, after_symbols);
                    attempt.after_count = after_symbols.size();
                    for (const ProDtlsyminst &symbol : after_symbols) {
                        if (before_keys.find(DtlSymbolKey(symbol)) == before_keys.end()) {
                            ++attempt.created_symbols;
                            if (attempt.created_symbol_id < 0) {
                                attempt.created_symbol_id = symbol.id;
                            }
                        }
                    }
                    attempt.created = attempt.created_symbols > 0;

                    LogLine(log_sink,
                            "ArrangeBalloons official-rebuild-create-attempt idx=%d old_symbol=%d row=%d region=%d record_index=%d location_dim=%d point_mode=%s ref_mode=%s ref=%d/%d arrays=(%d,%d) create=%d before=%zu after=%zu created=%d new_symbol=%d location=(%.3f,%.3f,%.3f) target=(%.3f,%.3f,%.3f)",
                            attempt_index,
                            problem.symbol.id,
                            record.bom.selection_order,
                            record.region_id,
                            record_index,
                            location_dim,
                            point_mode == 0 ? "component" : "drawing-target",
                            ref_mode == 0 ? "none" : "leader",
                            static_cast<int>(attempt.reference_type),
                            attempt.reference_id,
                            static_cast<int>(attempt.attach_array_status),
                            static_cast<int>(attempt.member_array_status),
                            static_cast<int>(attempt.create_status),
                            attempt.before_count,
                            attempt.after_count,
                            attempt.created_symbols,
                            attempt.created_symbol_id,
                            location[0],
                            location[1],
                            location[2],
                            problem.target_point[0],
                            problem.target_point[1],
                            problem.target_point[2]);

                    if (reference_memb_id_tab != nullptr) {
                        ProArrayFree(reinterpret_cast<ProArray *>(&reference_memb_id_tab));
                    }
                    if (attach_note_location != nullptr) {
                        ProArrayFree(reinterpret_cast<ProArray *>(&attach_note_location));
                    }

                    best = attempt;
                    ++attempt_index;
                    if (attempt.created) {
                        break;
                    }
                }
            }
        }
    }
    if (!best.created) {
        RebuildCreateResult attempt = {};
        attempt.before_count = before_symbols.size();
        attempt.region_id = record.region_id;
        attempt.record_index = -1;
        attempt.location_dim = 0;
        attempt.ref_mode = 0;
        attempt.point_mode = 0;
        attempt.create_mode = 1;
        attempt.attach_array_status = PRO_TK_NO_ERROR;
        attempt.member_array_status = PRO_TK_NO_ERROR;
        attempt.create_status = ProBomballoonCreate(
            drawing,
            const_cast<ProDwgtable *>(&bom_table.table),
            record.region_id,
            target_view);

        std::vector<ProDtlsyminst> after_symbols;
        CollectDrawingSymbols(drawing, sheet, after_symbols);
        attempt.after_count = after_symbols.size();
        std::vector<ProDtlsyminst> newly_created;
        for (const ProDtlsyminst &symbol : after_symbols) {
            if (before_keys.find(DtlSymbolKey(symbol)) == before_keys.end()) {
                newly_created.push_back(symbol);
            }
        }

        const std::wstring target_view_key = ViewIdentityKey(drawing, target_view);
        int keep_symbol_id = -1;
        double keep_score = 1.0e300;
        const Drawing3LogSink quiet_log;
        for (const ProDtlsyminst &created_symbol : newly_created) {
            TraditionalBomBalloonSymbol created_balloon = {};
            const bool extracted = ExtractTraditionalBomBalloonSymbol(
                drawing,
                target_view,
                target_view_key,
                created_symbol,
                0,
                created_balloon,
                quiet_log);
            double score = 1.0e100;
            bool model_ok = false;
            if (extracted) {
                model_ok = problem.model != nullptr && created_balloon.model != nullptr &&
                           SameModel(problem.model, created_balloon.model);
                const double dx = created_balloon.leader_point[0] - problem.leader_point[0];
                const double dy = created_balloon.leader_point[1] - problem.leader_point[1];
                score = dx * dx + dy * dy + (model_ok ? 0.0 : 1.0e8);
            }
            LogLine(log_sink,
                    "ArrangeBalloons official-rebuild-create-fallback-candidate old_symbol=%d new_symbol=%d extracted=%d model_ok=%d score=%.3f model=%s leader=(%.3f,%.3f)",
                    problem.symbol.id,
                    created_symbol.id,
                    extracted ? 1 : 0,
                    model_ok ? 1 : 0,
                    std::sqrt(std::max(0.0, score)),
                    extracted && created_balloon.model != nullptr ? autobbox::creo::DefaultModelTag(created_balloon.model).c_str() : "(null)",
                    extracted ? created_balloon.leader_point[0] : 0.0,
                    extracted ? created_balloon.leader_point[1] : 0.0);
            if (keep_symbol_id < 0 || score < keep_score) {
                keep_symbol_id = created_symbol.id;
                keep_score = score;
            }
        }

        int deleted_extra = 0;
        for (const ProDtlsyminst &created_symbol : newly_created) {
            if (created_symbol.id == keep_symbol_id) {
                continue;
            }
            ProError st_erase = PRO_TK_GENERAL_ERROR;
            ProError st_delete = PRO_TK_GENERAL_ERROR;
            ProError st_remove = PRO_TK_GENERAL_ERROR;
            const bool deleted = DeleteOfficialBomSymbol(created_symbol, st_erase, st_delete, st_remove);
            if (deleted) {
                ++deleted_extra;
            }
            LogLine(log_sink,
                    "ArrangeBalloons official-rebuild-create-fallback-extra-delete old_symbol=%d new_symbol=%d erase=%d delete=%d remove=%d deleted=%d",
                    problem.symbol.id,
                    created_symbol.id,
                    static_cast<int>(st_erase),
                    static_cast<int>(st_delete),
                    static_cast<int>(st_remove),
                    deleted ? 1 : 0);
        }

        attempt.created_symbol_id = keep_symbol_id;
        attempt.created_symbols = keep_symbol_id >= 0 ? 1 : 0;
        attempt.created = keep_symbol_id >= 0;
        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-create-attempt idx=%d old_symbol=%d row=%d region=%d record_index=-1 location_dim=0 point_mode=fallback-create ref_mode=none ref=-1/-1 arrays=(0,0) create=%d before=%zu after=%zu created_raw=%zu kept=%d deleted_extra=%d",
                attempt_index,
                problem.symbol.id,
                record.bom.selection_order,
                record.region_id,
                static_cast<int>(attempt.create_status),
                attempt.before_count,
                attempt.after_count,
                newly_created.size(),
                attempt.created_symbol_id,
                deleted_extra);
        best = attempt;
    }
    return best;
}

struct OrderedRebuildRecord {
    RebuildBomRecordCandidate *record = nullptr;
    TraditionalBalloonSide side = TraditionalBalloonSide::Right;
    double primary = 0.0;
    double secondary = 0.0;
    core::Dwg3GroupOutline reference_outline = {};
    bool has_reference_outline = false;
};

struct OrderedRecordCreateResult {
    bool created = false;
    ProError create_status = PRO_TK_GENERAL_ERROR;
    ProError attach_array_status = PRO_TK_GENERAL_ERROR;
    ProError member_array_status = PRO_TK_NO_ERROR;
    int created_symbols = 0;
    int created_symbol_id = -1;
    int record_index = -1;
    int point_mode = 0;
    int ref_mode = 0;
    int reference_id = PRO_VALUE_UNUSED;
    ProType reference_type = PRO_TYPE_UNUSED;
};

std::vector<OrderedRebuildRecord> BuildOrderedRebuildRecords(ProDrawing drawing,
                                                             ProView target_view,
                                                             std::vector<RebuildBomRecordCandidate> &records,
                                                             const Drawing3LogSink &log_sink)
{
    std::vector<OrderedRebuildRecord> ordered;
    if (drawing == nullptr || target_view == nullptr || records.empty()) {
        return ordered;
    }

    core::Dwg3GroupOutline view_outline = {};
    const ProError st_outline = GetDrawingViewOutlineBox(drawing, target_view, view_outline);
    if (st_outline != PRO_TK_NO_ERROR) {
        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-record-order outline_status=%d records=%zu",
                static_cast<int>(st_outline),
                records.size());
        return ordered;
    }

    const double left_x = view_outline.min_x - kCustomBalloonOutsideOffset;
    const double right_x = view_outline.max_x + kCustomBalloonOutsideOffset;
    const double top_y = view_outline.max_y + kCustomBalloonOutsideOffset;
    const double bottom_y = view_outline.min_y - kCustomBalloonOutsideOffset;

    ordered.reserve(records.size());
    for (RebuildBomRecordCandidate &record : records) {
        OrderedRebuildRecord item = {};
        item.record = &record;
        if (record.bom.model != nullptr) {
            item.has_reference_outline = ComputeComponentDrawingOutline(
                drawing,
                target_view,
                record.bom.model,
                record.bom.component_path,
                record.bom.has_component_path,
                item.reference_outline);
        }
        if (!item.has_reference_outline) {
            item.reference_outline.min_x = record.bom.attach_point[0];
            item.reference_outline.max_x = record.bom.attach_point[0];
            item.reference_outline.min_y = record.bom.attach_point[1];
            item.reference_outline.max_y = record.bom.attach_point[1];
        }

        const double ref_cx = (item.reference_outline.min_x + item.reference_outline.max_x) * 0.5;
        const double ref_cy = (item.reference_outline.min_y + item.reference_outline.max_y) * 0.5;
        const double dl = std::max(0.0, item.reference_outline.min_x - left_x);
        const double dr = std::max(0.0, right_x - item.reference_outline.max_x);
        const double dt = std::max(0.0, top_y - item.reference_outline.max_y);
        const double db = std::max(0.0, item.reference_outline.min_y - bottom_y);
        double best = dl;
        item.side = TraditionalBalloonSide::Left;
        if (dr < best) {
            best = dr;
            item.side = TraditionalBalloonSide::Right;
        }
        if (dt < best) {
            best = dt;
            item.side = TraditionalBalloonSide::Top;
        }
        if (db < best) {
            best = db;
            item.side = TraditionalBalloonSide::Bottom;
        }

        if (item.side == TraditionalBalloonSide::Left || item.side == TraditionalBalloonSide::Right) {
            item.primary = ref_cy;
            item.secondary = ref_cx;
        } else {
            item.primary = ref_cx;
            item.secondary = ref_cy;
        }

        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-record-order row=%d side=%s primary=%.3f secondary=%.3f ref_outline=%d ref_box=(%.3f,%.3f)-(%.3f,%.3f) d_left=%.3f d_right=%.3f d_top=%.3f d_bottom=%.3f chosen_dist=%.3f",
                record.bom.selection_order,
                TraditionalBalloonSideName(item.side),
                item.primary,
                item.secondary,
                item.has_reference_outline ? 1 : 0,
                item.reference_outline.min_x,
                item.reference_outline.min_y,
                item.reference_outline.max_x,
                item.reference_outline.max_y,
                dl,
                dr,
                dt,
                db,
                best);

        ordered.push_back(item);
    }

    auto side_rank = [](TraditionalBalloonSide side) {
        switch (side) {
        case TraditionalBalloonSide::Left:
            return 0;
        case TraditionalBalloonSide::Top:
            return 1;
        case TraditionalBalloonSide::Right:
            return 2;
        case TraditionalBalloonSide::Bottom:
            return 3;
        }
        return 4;
    };
    std::stable_sort(ordered.begin(), ordered.end(), [&](const OrderedRebuildRecord &lhs,
                                                         const OrderedRebuildRecord &rhs) {
        const int ls = side_rank(lhs.side);
        const int rs = side_rank(rhs.side);
        if (ls != rs) {
            return ls < rs;
        }
        if (std::abs(lhs.primary - rhs.primary) > 1.0e-9) {
            return lhs.primary < rhs.primary;
        }
        if (std::abs(lhs.secondary - rhs.secondary) > 1.0e-9) {
            return lhs.secondary < rhs.secondary;
        }
        const int lrow = lhs.record != nullptr ? lhs.record->bom.selection_order : 0;
        const int rrow = rhs.record != nullptr ? rhs.record->bom.selection_order : 0;
        return lrow < rrow;
    });

    LogLine(log_sink,
            "ArrangeBalloons official-rebuild-record-order-end records=%zu ordered=%zu rule=geometry-side-then-reference-center",
            records.size(),
            ordered.size());
    return ordered;
}

OrderedRecordCreateResult TryCreateOrderedRecordBalloon(ProDrawing drawing,
                                                        int sheet,
                                                        const BalloonArrangeBomTableSelection &bom_table,
                                                        ProView target_view,
                                                        const RebuildBomRecordCandidate &record,
                                                        const std::unordered_set<std::string> &before_keys,
                                                        int ordered_index,
                                                        const Drawing3LogSink &log_sink)
{
    OrderedRecordCreateResult best = {};
    if (drawing == nullptr || sheet <= 0 || bom_table.table.owner == nullptr || target_view == nullptr) {
        best.create_status = PRO_TK_BAD_INPUTS;
        return best;
    }

    const double points[][3] = {
        {record.bom.leader_point[0], record.bom.leader_point[1], record.bom.leader_point[2]},
        {record.bom.attach_point[0], record.bom.attach_point[1], record.bom.attach_point[2]}};
    const int point_modes[] = {0, 1};
    const int ref_modes[] = {record.bom.has_leader_model_item ? 1 : 0, 0};
    int attempt_index = 0;

    for (int record_index : record.record_indices) {
        if (best.created) {
            break;
        }
        for (int point_mode : point_modes) {
            if (best.created) {
                break;
            }
            for (int ref_mode : ref_modes) {
                if (best.created) {
                    break;
                }
                if (ref_mode != 0 && !record.bom.has_leader_model_item) {
                    continue;
                }

                OrderedRecordCreateResult attempt = {};
                attempt.record_index = record_index;
                attempt.point_mode = point_mode;
                attempt.ref_mode = ref_mode;
                attempt.reference_id = PRO_VALUE_UNUSED;
                attempt.reference_type = PRO_TYPE_UNUSED;

                double *attach_note_location = nullptr;
                int *reference_memb_id_tab = nullptr;
                attempt.attach_array_status = AllocDoubleProArray(points[point_mode], 3, &attach_note_location);
                attempt.member_array_status = PRO_TK_NO_ERROR;

                if (ref_mode != 0) {
                    attempt.reference_id = record.bom.leader_model_item.id;
                    attempt.reference_type = record.bom.leader_model_item.type;
                    if (record.bom.has_component_path && record.bom.component_path.table_num > 0) {
                        attempt.member_array_status = AllocIntProArray(
                            record.bom.component_path.comp_id_table,
                            record.bom.component_path.table_num,
                            &reference_memb_id_tab);
                    }
                }

                if (attempt.attach_array_status == PRO_TK_NO_ERROR &&
                    attempt.member_array_status == PRO_TK_NO_ERROR) {
                    attempt.create_status = ProBomballoonByRecordCreate(
                        drawing,
                        const_cast<ProDwgtable *>(&bom_table.table),
                        record.region_id,
                        target_view,
                        record_index,
                        reference_memb_id_tab,
                        attempt.reference_id,
                        attempt.reference_type,
                        attach_note_location);
                }

                std::vector<ProDtlsyminst> after_symbols;
                CollectDrawingSymbols(drawing, sheet, after_symbols);
                for (const ProDtlsyminst &symbol : after_symbols) {
                    if (before_keys.find(DtlSymbolKey(symbol)) == before_keys.end()) {
                        ++attempt.created_symbols;
                        if (attempt.created_symbol_id < 0) {
                            attempt.created_symbol_id = symbol.id;
                        }
                    }
                }
                attempt.created = attempt.created_symbols > 0;
                LogLine(log_sink,
                        "ArrangeBalloons official-rebuild-create-ordered idx=%d attempt=%d row=%d region=%d record_index=%d point_mode=%s ref_mode=%s ref=%d/%d arrays=(%d,%d) create=%d cumulative_created=%d first_new_symbol=%d point=(%.3f,%.3f,%.3f)",
                        ordered_index,
                        attempt_index,
                        record.bom.selection_order,
                        record.region_id,
                        record_index,
                        point_mode == 0 ? "leader" : "attach",
                        ref_mode == 0 ? "none" : "leader",
                        static_cast<int>(attempt.reference_type),
                        attempt.reference_id,
                        static_cast<int>(attempt.attach_array_status),
                        static_cast<int>(attempt.member_array_status),
                        static_cast<int>(attempt.create_status),
                        attempt.created_symbols,
                        attempt.created_symbol_id,
                        points[point_mode][0],
                        points[point_mode][1],
                        points[point_mode][2]);

                if (reference_memb_id_tab != nullptr) {
                    ProArrayFree(reinterpret_cast<ProArray *>(&reference_memb_id_tab));
                }
                if (attach_note_location != nullptr) {
                    ProArrayFree(reinterpret_cast<ProArray *>(&attach_note_location));
                }

                best = attempt;
                ++attempt_index;
            }
        }
    }
    return best;
}

OrderedRecordCreateResult TryCreateOrderedComponentBalloon(ProDrawing drawing,
                                                           int sheet,
                                                           const BalloonArrangeBomTableSelection &bom_table,
                                                           ProView target_view,
                                                           const RebuildBomRecordCandidate &record,
                                                           const std::unordered_set<std::string> &before_keys,
                                                           int ordered_index,
                                                           const Drawing3LogSink &log_sink)
{
    OrderedRecordCreateResult result = {};
    result.record_index = -1;
    result.point_mode = 2;
    result.ref_mode = 2;
    if (drawing == nullptr || sheet <= 0 || bom_table.table.owner == nullptr || target_view == nullptr) {
        result.create_status = PRO_TK_BAD_INPUTS;
        return result;
    }
    if (!record.bom.has_component_path || record.bom.component_path.table_num <= 0) {
        result.create_status = PRO_TK_BAD_INPUTS;
        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-create-component idx=%d row=%d status=%d reason=no-component-path path_len=%d",
                ordered_index,
                record.bom.selection_order,
                static_cast<int>(result.create_status),
                record.bom.has_component_path ? record.bom.component_path.table_num : 0);
        return result;
    }

    int *component_memb_id_tab = nullptr;
    result.member_array_status = AllocIntProArray(
        record.bom.component_path.comp_id_table,
        record.bom.component_path.table_num,
        &component_memb_id_tab);
    result.attach_array_status = PRO_TK_NO_ERROR;
    if (result.member_array_status == PRO_TK_NO_ERROR) {
        result.create_status = ProBomballoonByComponentCreate(
            drawing,
            const_cast<ProDwgtable *>(&bom_table.table),
            record.region_id,
            target_view,
            component_memb_id_tab);
    }

    std::vector<ProDtlsyminst> after_symbols;
    CollectDrawingSymbols(drawing, sheet, after_symbols);
    for (const ProDtlsyminst &symbol : after_symbols) {
        if (before_keys.find(DtlSymbolKey(symbol)) == before_keys.end()) {
            ++result.created_symbols;
            if (result.created_symbol_id < 0) {
                result.created_symbol_id = symbol.id;
            }
        }
    }
    result.created = result.created_symbols > 0;
    LogLine(log_sink,
            "ArrangeBalloons official-rebuild-create-component idx=%d row=%d region=%d path_len=%d arrays=(%d,%d) create=%d cumulative_created=%d first_new_symbol=%d",
            ordered_index,
            record.bom.selection_order,
            record.region_id,
            record.bom.component_path.table_num,
            static_cast<int>(result.attach_array_status),
            static_cast<int>(result.member_array_status),
            static_cast<int>(result.create_status),
            result.created_symbols,
            result.created_symbol_id);

    if (component_memb_id_tab != nullptr) {
        ProArrayFree(reinterpret_cast<ProArray *>(&component_memb_id_tab));
    }
    return result;
}

BalloonArrangeSummary ExecuteRebuildProblemTraditionalBalloonsTask(
    ProDrawing drawing,
    int sheet,
    const BalloonArrangeBomTableSelection &bom_table,
    ProView target_view,
    const Drawing3LogSink &log_sink)
{
    BalloonArrangeSummary summary = {};
    summary.sheet = sheet;
    summary.valid_views = target_view != nullptr ? 1 : 0;

    if (drawing == nullptr || sheet <= 0 || bom_table.table.owner == nullptr || target_view == nullptr) {
        summary.first_error = PRO_TK_BAD_INPUTS;
        summary.views_failed = target_view != nullptr ? 1 : 0;
        LogLine(log_sink,
                "FAIL arrange-balloons official-rebuild-begin reason=bad-inputs drawing=%p sheet=%d view=%p table=%p",
                static_cast<void *>(drawing),
                sheet,
                static_cast<void *>(target_view),
                static_cast<void *>(bom_table.table.owner));
        return summary;
    }

    int view_sheet = 0;
    const ProError st_view_sheet = ProDrawingViewSheetGet(drawing, target_view, &view_sheet);
    if (st_view_sheet != PRO_TK_NO_ERROR || view_sheet != sheet) {
        summary.first_error = (st_view_sheet == PRO_TK_NO_ERROR) ? PRO_TK_BAD_INPUTS : st_view_sheet;
        summary.views_failed = 1;
        LogLine(log_sink,
                "FAIL arrange-balloons official-rebuild-begin reason=view-sheet status=%d view_sheet=%d current_sheet=%d",
                static_cast<int>(st_view_sheet),
                view_sheet,
                sheet);
        return summary;
    }

    int cell_region = PRO_VALUE_UNUSED;
    const ProError st_cell_region = ProDwgtableCellRegionGet(
        drawing,
        const_cast<ProDwgtable *>(&bom_table.table),
        std::max(0, bom_table.selected_column - 1),
        std::max(0, bom_table.selected_row - 1),
        &cell_region);
    int region_id = bom_table.segment;
    if (region_id == PRO_VALUE_UNUSED && st_cell_region == PRO_TK_NO_ERROR) {
        region_id = cell_region;
    }
    if (region_id == PRO_VALUE_UNUSED) {
        region_id = 0;
    }

    const std::wstring target_view_key = ViewIdentityKey(drawing, target_view);
    std::vector<ProDtlsyminst> symbols_before;
    if (!CollectDrawingSymbols(drawing, sheet, symbols_before)) {
        summary.first_error = PRO_TK_GENERAL_ERROR;
        summary.views_failed = 1;
        LogLine(log_sink,
                "FAIL arrange-balloons official-rebuild-begin reason=collect-symbols sheet=%d",
                sheet);
        return summary;
    }

    std::vector<TraditionalBomBalloonSymbol> existing_balloons;
    existing_balloons.reserve(symbols_before.size());
    const Drawing3LogSink quiet_log;
    for (size_t i = 0; i < symbols_before.size(); ++i) {
        TraditionalBomBalloonSymbol balloon = {};
        if (ExtractTraditionalBomBalloonSymbol(
                drawing,
                target_view,
                target_view_key,
                symbols_before[i],
                static_cast<int>(i),
                balloon,
                quiet_log)) {
            existing_balloons.push_back(balloon);
        }
    }

    LogLine(log_sink,
            "ArrangeBalloons official-rebuild-begin mode=full sheet=%d view=%p table=%p region=%d selected_cell=(%d,%d) existing=%zu max_lines_per_side=2",
            sheet,
            static_cast<void *>(target_view),
            static_cast<void *>(bom_table.table.owner),
            region_id,
            bom_table.selected_row,
            bom_table.selected_column,
            existing_balloons.size());

    std::vector<RebuildBomRecordCandidate> record_candidates;
    int record_region_id = region_id;
    BalloonArrangeSummary record_summary = {};
    record_summary.sheet = sheet;
    (void)CollectRebuildBomRecordCandidates(
        drawing,
        sheet,
        bom_table,
        target_view,
        record_summary,
        record_candidates,
        record_region_id,
        log_sink);
    if (record_region_id != PRO_VALUE_UNUSED) {
        region_id = record_region_id;
    }
    std::vector<OrderedRebuildRecord> ordered_records =
        BuildOrderedRebuildRecords(drawing, target_view, record_candidates, log_sink);
    LogLine(log_sink,
            "ArrangeBalloons official-rebuild-record-create-plan records=%zu ordered=%zu region=%d mode=ordered-by-record-with-createall-fallback",
            record_candidates.size(),
            ordered_records.size(),
            region_id);

    int deleted_count = 0;
    int delete_failed = 0;
    for (const TraditionalBomBalloonSymbol &existing : existing_balloons) {
        ProError st_erase = PRO_TK_GENERAL_ERROR;
        ProError st_delete = PRO_TK_GENERAL_ERROR;
        ProError st_remove = PRO_TK_GENERAL_ERROR;
        const bool deleted = DeleteOfficialBomSymbol(existing.symbol, st_erase, st_delete, st_remove);
        if (deleted) {
            ++deleted_count;
        } else {
            ++delete_failed;
        }
        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-delete symbol=%d erase=%d delete=%d remove=%d deleted=%d",
                existing.symbol.id,
                static_cast<int>(st_erase),
                static_cast<int>(st_delete),
                static_cast<int>(st_remove),
                deleted ? 1 : 0);
    }

    if (delete_failed > 0) {
        summary.first_error = PRO_TK_GENERAL_ERROR;
        summary.views_failed = 1;
        summary.custom_balloons_failed = delete_failed;
        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-end reason=delete-failed existing=%zu deleted=%d failed=%d",
                existing_balloons.size(),
                deleted_count,
                delete_failed);
        return summary;
    }

    if (deleted_count > 0) {
        FlushDrawingViewDisplay(drawing, sheet, target_view, "after-delete-before-create", log_sink);
    }

    std::vector<ProDtlsyminst> symbols_after_delete;
    CollectDrawingSymbols(drawing, sheet, symbols_after_delete);
    const std::unordered_set<std::string> after_delete_keys = SymbolKeySet(symbols_after_delete);

    ProError st_create = PRO_TK_GENERAL_ERROR;
    bool used_ordered_record_create = false;
    int ordered_create_ok = 0;
    int ordered_create_failed = 0;
    int ordered_created_symbols = 0;
    const int expected_created_symbols = !existing_balloons.empty()
                                             ? static_cast<int>(existing_balloons.size())
                                             : static_cast<int>(ordered_records.size());
    if (!ordered_records.empty()) {
        used_ordered_record_create = true;
        std::unordered_set<std::string> cumulative_keys = after_delete_keys;
        for (size_t i = 0; i < ordered_records.size(); ++i) {
            if (ordered_records[i].record == nullptr) {
                continue;
            }
            OrderedRecordCreateResult create_result = TryCreateOrderedComponentBalloon(
                drawing,
                sheet,
                bom_table,
                target_view,
                *ordered_records[i].record,
                cumulative_keys,
                static_cast<int>(i),
                log_sink);
            if (!create_result.created) {
                create_result = TryCreateOrderedRecordBalloon(
                    drawing,
                    sheet,
                    bom_table,
                    target_view,
                    *ordered_records[i].record,
                    cumulative_keys,
                    static_cast<int>(i),
                    log_sink);
            }
            if (create_result.created) {
                ++ordered_create_ok;
                ordered_created_symbols += create_result.created_symbols;
                std::vector<ProDtlsyminst> symbols_after_ordered_record;
                CollectDrawingSymbols(drawing, sheet, symbols_after_ordered_record);
                cumulative_keys = SymbolKeySet(symbols_after_ordered_record);
            } else {
                ++ordered_create_failed;
            }
        }
        st_create = (ordered_create_ok > 0 && ordered_create_failed == 0) ? PRO_TK_NO_ERROR : PRO_TK_GENERAL_ERROR;
        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-create-ordered-end records=%zu ok=%d failed=%d created_symbols=%d expected=%d status=%d",
                ordered_records.size(),
                ordered_create_ok,
                ordered_create_failed,
                ordered_created_symbols,
                expected_created_symbols,
                static_cast<int>(st_create));
    }

    if (!used_ordered_record_create ||
        st_create != PRO_TK_NO_ERROR ||
        (expected_created_symbols > 0 && ordered_created_symbols < expected_created_symbols)) {
        if (used_ordered_record_create && ordered_created_symbols > 0) {
            std::vector<ProDtlsyminst> partial_symbols;
            CollectDrawingSymbols(drawing, sheet, partial_symbols);
            int partial_deleted = 0;
            for (const ProDtlsyminst &symbol : partial_symbols) {
                if (after_delete_keys.find(DtlSymbolKey(symbol)) != after_delete_keys.end()) {
                    continue;
                }
                ProError st_erase = PRO_TK_GENERAL_ERROR;
                ProError st_delete = PRO_TK_GENERAL_ERROR;
                ProError st_remove = PRO_TK_GENERAL_ERROR;
                if (DeleteOfficialBomSymbol(symbol, st_erase, st_delete, st_remove)) {
                    ++partial_deleted;
                }
                LogLine(log_sink,
                        "ArrangeBalloons official-rebuild-create-ordered-partial-delete symbol=%d erase=%d delete=%d remove=%d",
                        symbol.id,
                        static_cast<int>(st_erase),
                        static_cast<int>(st_delete),
                        static_cast<int>(st_remove));
            }
            LogLine(log_sink,
                    "ArrangeBalloons official-rebuild-create-ordered-fallback reason=incomplete ordered_created=%d expected=%d partial_deleted=%d",
                    ordered_created_symbols,
                    expected_created_symbols,
                    partial_deleted);
        }
        st_create = ProBomballoonCreate(
            drawing,
            const_cast<ProDwgtable *>(&bom_table.table),
            region_id,
            target_view);
        used_ordered_record_create = false;
    }
    LogLine(log_sink,
            "ArrangeBalloons official-rebuild-create-all table=%p region=%d view=%p status=%d before_delete=%zu after_delete=%zu ordered_record=%d ordered_ok=%d ordered_failed=%d ordered_created=%d",
            static_cast<void *>(bom_table.table.owner),
            region_id,
            static_cast<void *>(target_view),
            static_cast<int>(st_create),
            symbols_before.size(),
            symbols_after_delete.size(),
            used_ordered_record_create ? 1 : 0,
            ordered_create_ok,
            ordered_create_failed,
            ordered_created_symbols);
    if (st_create != PRO_TK_NO_ERROR) {
        summary.first_error = st_create;
        summary.views_failed = 1;
        summary.custom_balloons_failed = std::max(1, deleted_count);
        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-end reason=create-all-failed status=%d deleted=%d",
                static_cast<int>(st_create),
                deleted_count);
        return summary;
    }

    std::vector<ProDtlsyminst> symbols_after_create;
    if (!CollectDrawingSymbols(drawing, sheet, symbols_after_create)) {
        summary.first_error = PRO_TK_GENERAL_ERROR;
        summary.views_failed = 1;
        LogLine(log_sink, "ArrangeBalloons official-rebuild-end reason=collect-after-create-failed");
        return summary;
    }

    std::vector<TraditionalBomBalloonSymbol> rebuilt_balloons;
    rebuilt_balloons.reserve(symbols_after_create.size());
    int created_new_symbols = 0;
    for (size_t i = 0; i < symbols_after_create.size(); ++i) {
        TraditionalBomBalloonSymbol balloon = {};
        if (ExtractTraditionalBomBalloonSymbol(
                drawing,
                target_view,
                target_view_key,
                symbols_after_create[i],
                static_cast<int>(i),
                balloon,
                quiet_log)) {
            rebuilt_balloons.push_back(balloon);
            if (after_delete_keys.find(DtlSymbolKey(symbols_after_create[i])) == after_delete_keys.end()) {
                ++created_new_symbols;
            }
        }
    }

    summary.selected_total = static_cast<int>(rebuilt_balloons.size());
    summary.custom_balloons_created = created_new_symbols;
    LogLine(log_sink,
            "ArrangeBalloons official-rebuild-created existing_deleted=%d after_create_symbols=%zu rebuilt_candidates=%zu created_new=%d",
            deleted_count,
            symbols_after_create.size(),
            rebuilt_balloons.size(),
            created_new_symbols);

    if (rebuilt_balloons.empty()) {
        summary.first_error = PRO_TK_E_NOT_FOUND;
        summary.views_failed = 1;
        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-end reason=no-created-balloons deleted=%d create_status=%d",
                deleted_count,
                static_cast<int>(st_create));
        return summary;
    }

    LayoutOfficialRebuildBalloonsTwoSnaplines(drawing, target_view, rebuilt_balloons, log_sink);

    int direct_moved = 0;
    int direct_failed = 0;
    for (const TraditionalBomBalloonSymbol &balloon : rebuilt_balloons) {
        TraditionalBomBalloonMoveDiagnostics diag = {};
        const ProError st_move = MoveTraditionalBomBalloonSymbol(balloon, &diag);
        if (st_move == PRO_TK_NO_ERROR) {
            ++direct_moved;
        } else {
            ++direct_failed;
        }
        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-place symbol=%d side=%s target=(%.3f,%.3f) ref_outline=%d status=%d data=%d attach_get=%d attach_set=%d modify=%d fallback=%d/%d show=%d draw=%d rule=shortest-snapline-to-reference-model",
                balloon.symbol.id,
                TraditionalBalloonSideName(balloon.side),
                balloon.target_point[0],
                balloon.target_point[1],
                balloon.has_reference_outline ? 1 : 0,
                static_cast<int>(st_move),
                static_cast<int>(diag.data_status),
                static_cast<int>(diag.attachment_get_status),
                static_cast<int>(diag.attachment_set_status),
                static_cast<int>(diag.modify_status),
                static_cast<int>(diag.fallback_attachment_status),
                static_cast<int>(diag.fallback_modify_status),
                static_cast<int>(diag.show_status),
                static_cast<int>(diag.draw_status));
    }



    ProError st_clean = PRO_TK_NO_ERROR;
    bool native_clean_ran = false;

    /*
     * Official BOM balloons created by ProBomballoonCreate() reject direct
     * ProDtlsyminstModify() attachment writes in this workflow (user report:
     * all generated symbols returned modify=-1). Use the supported official
     * BOM cleanup API as the write path after the delete/recreate pass.
     *
     * The layout plan above computes the requested nearest-side/two-snapline
     * assignment for audit. Because direct symbol modification is rejected by
     * Creo for these generated official BOM balloons, ProBomballoonClean is
     * used as the write path. Use the same native cleanup settings as the
     * existing arrange command: prepare snap lines first, then clean with
     * existing_snap_lines=TRUE and native stagger disabled.
     */
    if (direct_moved != static_cast<int>(rebuilt_balloons.size())) {
        native_clean_ran = true;
        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-direct-place-incomplete moved=%d failed=%d candidates=%zu fallback_write_path=ProBomballoonClean mode=same-as-balloon-arrange official_only=1 note_balloons=0",
                direct_moved,
                direct_failed,
                rebuilt_balloons.size());

        const ProError st_snapline_prepare = ProBomballoonClean(
            drawing,
            target_view,
            PRO_B_FALSE,
            PRO_B_FALSE,
            kCustomBalloonOutsideOffset,
            PRO_B_FALSE,
            0.0,
            PRO_B_TRUE,
            kCustomBalloonVerticalSpacing,
            PRO_B_FALSE);
        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-snapline-prepare view=%p status=%d clean_pos=0 existing_snap_lines=0 offset=%.3f stagger=0 stagger_val=0.000 create_stagger_snap_lines=1 spacing=%.3f attach_to_surface=0 note=same-as-balloon-arrange-prepare",
                static_cast<void *>(target_view),
                static_cast<int>(st_snapline_prepare),
                kCustomBalloonOutsideOffset,
                kCustomBalloonVerticalSpacing);

        /*
         * Match the proven existing arrange cleanup path.  The actual cleanup pass
         * keeps existing_snap_lines hard-set to TRUE as requested, and leaves
         * native stagger disabled because the previous rebuild-specific
         * stagger=1 path produced far more wrong-side/long-leader balloons
         * than the existing arrange command in user reports.
         */
        st_clean = ProBomballoonClean(
            drawing,
            target_view,
            PRO_B_TRUE,
            PRO_B_TRUE,
            kCustomBalloonOutsideOffset,
            PRO_B_FALSE,
            0.0,
            PRO_B_FALSE,
            kCustomBalloonVerticalSpacing,
            PRO_B_TRUE);
        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-native-clean view=%p status=%d candidates=%zu clean_pos=1 existing_snap_lines=1 offset=%.3f stagger=0 stagger_val=0.000 create_stagger_snap_lines=0 spacing=%.3f attach_to_surface=1 requested_max_lines_per_side=2 snapline_prepare=%d note=same-as-balloon-arrange-no-native-stagger",
                static_cast<void *>(target_view),
                static_cast<int>(st_clean),
                rebuilt_balloons.size(),
                kCustomBalloonOutsideOffset,
                kCustomBalloonVerticalSpacing,
                static_cast<int>(st_snapline_prepare));
        if (st_clean != PRO_TK_NO_ERROR) {
            const ProError st_edge_clean = ProBomballoonClean(
                drawing,
                target_view,
                PRO_B_TRUE,
                PRO_B_TRUE,
                kCustomBalloonOutsideOffset,
                PRO_B_FALSE,
                0.0,
                PRO_B_FALSE,
                kCustomBalloonVerticalSpacing,
                PRO_B_FALSE);
            LogLine(log_sink,
                    "ArrangeBalloons official-rebuild-native-clean-retry view=%p status=%d previous_status=%d clean_pos=1 existing_snap_lines=1 stagger=0 stagger_val=0.000 create_stagger_snap_lines=0 attach_to_surface=0 requested_max_lines_per_side=2 note=retry-edge-attach-same-as-balloon-arrange",
                    static_cast<void *>(target_view),
                    static_cast<int>(st_edge_clean),
                    static_cast<int>(st_clean));
            st_clean = st_edge_clean;
        }
    } else {
        LogLine(log_sink,
                "ArrangeBalloons official-rebuild-native-clean-skip reason=direct-placement-complete moved=%d candidates=%zu",
                direct_moved,
                rebuilt_balloons.size());
    }

    if (st_clean == PRO_TK_NO_ERROR) {
        summary.notes_reordered = static_cast<int>(rebuilt_balloons.size());
        summary.notes_reorder_failed = 0;
        summary.views_arranged = 1;
        summary.views_failed = 0;
        summary.first_error = PRO_TK_NO_ERROR;
        TraditionalPostCleanAudit best_audit = AuditTraditionalBomBalloonPostClean(
            drawing,
            sheet,
            target_view,
            target_view_key,
            rebuilt_balloons,
            log_sink);

        if (native_clean_ran && best_audit.leader_crossings > 0) {
            /*
             * Do not run an in-place "edge attach" second cleanup as a crossing
             * optimizer. The latest field report showed that this mutates the
             * already-cleaned view from 45 crossings to 46 crossings, and the
             * follow-up surface restore is not deterministic (it finished at 47
             * crossings). Keep the single proven surface cleanup result instead
             * of performing trial cleanups that cannot be rolled back exactly.
             */
            LogLine(log_sink,
                    "ArrangeBalloons official-rebuild-crossing-optimizer skipped reason=trial-cleanup-is-not-rollback-safe crossings=%d score=%d existing_snap_lines=1 stagger=0 create_stagger_snap_lines=0 attach_to_surface=1",
                    best_audit.leader_crossings,
                    TraditionalAuditScore(best_audit));
        }
    } else {
        summary.notes_reordered = 0;
        summary.notes_reorder_failed = static_cast<int>(rebuilt_balloons.size());
        summary.views_arranged = 0;
        summary.views_failed = 1;
        summary.first_error = st_clean;
    }

    FlushDrawingViewDisplay(drawing, sheet, target_view, "after-native-clean", log_sink);

    LogLine(log_sink,
            "ArrangeBalloons official-rebuild-end mode=full existing=%zu deleted=%d created=%d direct_moved=%d direct_failed=%d native_clean=%d reordered=%d failed=%d max_lines_per_side=2 first_error=%d",
            existing_balloons.size(),
            deleted_count,
            created_new_symbols,
            direct_moved,
            direct_failed,
            native_clean_ran && st_clean == PRO_TK_NO_ERROR ? 1 : 0,
            summary.notes_reordered,
            summary.notes_reorder_failed,
            static_cast<int>(summary.first_error));
    return summary;

}

BalloonArrangeSummary ExecuteArrangeTraditionalBalloonsTask(ProDrawing drawing,
                                                            int sheet,
                                                            ProView target_view,
                                                            const Drawing3LogSink &log_sink)
{
    BalloonArrangeSummary summary = {};
    summary.sheet = sheet;
    summary.valid_views = target_view != nullptr ? 1 : 0;

    if (drawing == nullptr || sheet <= 0 || target_view == nullptr) {
        summary.first_error = PRO_TK_BAD_INPUTS;
        LogLine(log_sink,
                "FAIL arrange-balloons-traditional reason=bad-inputs drawing=%p sheet=%d view=%p",
                static_cast<void *>(drawing),
                sheet,
                static_cast<void *>(target_view));
        return summary;
    }

    int view_sheet = 0;
    const ProError st_view_sheet = ProDrawingViewSheetGet(drawing, target_view, &view_sheet);
    if (st_view_sheet != PRO_TK_NO_ERROR || view_sheet != sheet) {
        summary.first_error = (st_view_sheet == PRO_TK_NO_ERROR) ? PRO_TK_BAD_INPUTS : st_view_sheet;
        LogLine(log_sink,
                "FAIL arrange-balloons-traditional reason=view-sheet status=%d view_sheet=%d current_sheet=%d",
                static_cast<int>(st_view_sheet),
                view_sheet,
                sheet);
        return summary;
    }

    const std::wstring target_view_key = ViewIdentityKey(drawing, target_view);
    std::vector<ProDtlsyminst> symbols;
    if (!CollectDrawingSymbols(drawing, sheet, symbols)) {
        summary.first_error = PRO_TK_GENERAL_ERROR;
        summary.views_failed = 1;
        LogLine(log_sink,
                "FAIL arrange-balloons-traditional reason=collect-symbols sheet=%d",
                sheet);
        return summary;
    }

    std::vector<TraditionalBomBalloonSymbol> balloons;
    balloons.reserve(symbols.size());
    for (size_t i = 0; i < symbols.size(); ++i) {
        TraditionalBomBalloonSymbol balloon = {};
        if (ExtractTraditionalBomBalloonSymbol(
                drawing,
                target_view,
                target_view_key,
                symbols[i],
                static_cast<int>(i),
                balloon,
                log_sink)) {
            balloons.push_back(balloon);
        }
    }

    summary.selected_total = static_cast<int>(balloons.size());
    LogLine(log_sink,
            "ArrangeBalloons traditional-begin sheet=%d view=%p symbols=%zu candidates=%zu offset=%.3f spacing=%.3f",
            sheet,
            static_cast<void *>(target_view),
            symbols.size(),
            balloons.size(),
            kCustomBalloonOutsideOffset,
            kCustomBalloonVerticalSpacing);

    if (balloons.empty()) {
        summary.first_error = PRO_TK_E_NOT_FOUND;
        LogLine(log_sink,
                "ArrangeBalloons traditional-end reason=no-existing-official-balloons view=%p",
                static_cast<void *>(target_view));
        return summary;
    }

    LayoutTraditionalBomBalloonsOnFourSides(drawing, target_view, balloons, log_sink);
    const TraditionalStaggerDecision stagger_decision =
        AnalyzeTraditionalStaggerNeed(balloons, log_sink);
    /*
     * We still compute/log the second-line need, but do not pass stagger to
     * ProBomballoonClean. Creo's native stagger is global and not side-bound;
     * testing showed it can move generated BOM balloons to the opposite or
     * adjacent side even when the nearest-reference side was computed
     * correctly, increasing long/crossing leaders.
     */
    const ProBool use_stagger = PRO_B_FALSE;
    const double stagger_val = 0.0;

    /*
     * Prepare Creo native snap lines first, then run cleanup with
     * existing_snap_lines=TRUE. User testing showed this is the correct mode
     * for keeping generated BOM balloons nearer the intended snap lines. Keep
     * native stagger disabled because the global stagger pass moved more
     * balloons to the wrong side.
     */
    const ProError st_snapline_prepare = ProBomballoonClean(
        drawing,
        target_view,
        PRO_B_FALSE,
        PRO_B_FALSE,
        kCustomBalloonOutsideOffset,
        PRO_B_FALSE,
        0.0,
        PRO_B_TRUE,
        kCustomBalloonVerticalSpacing,
        PRO_B_FALSE);
    LogLine(log_sink,
            "ArrangeBalloons traditional-snapline-prepare view=%p status=%d clean_pos=0 existing_snap_lines=0 offset=%.3f stagger=0 stagger_val=0.000 create_stagger_snap_lines=1 spacing=%.3f attach_to_surface=0",
            static_cast<void *>(target_view),
            static_cast<int>(st_snapline_prepare),
            kCustomBalloonOutsideOffset,
            kCustomBalloonVerticalSpacing);
    const ProBool reuse_prepared_snap_lines = PRO_B_TRUE;
    const ProBool create_snap_lines_during_clean = PRO_B_FALSE;

    /*
     * Generated official BOM balloons reject direct ProDtlsyminstModify()
     * attachment writes in this workflow. Use the supported native BOM cleanup
     * API as the write path: the plugin computes/logs the four-side nearest
     * snap-line assignment above, while Creo performs the official balloon
     * relocation on prepared/existing snap lines. When the preparation pass
     * succeeds, the cleanup pass reuses those lines without creating another
     * set; this avoids extra native snap lines pulling balloons to a
     * neighboring side. ProBomballoonClean's
     * attach_to_surface flag is requested, but for existing generated BOM
     * balloons Creo may keep old edge leader references.
     */
    ProError st_clean = ProBomballoonClean(
        drawing,
        target_view,
        PRO_B_TRUE,
        reuse_prepared_snap_lines,
        kCustomBalloonOutsideOffset,
        use_stagger,
        stagger_val,
        create_snap_lines_during_clean,
        kCustomBalloonVerticalSpacing,
        PRO_B_TRUE);
    LogLine(log_sink,
            "ArrangeBalloons traditional-native-clean view=%p status=%d candidates=%zu clean_pos=1 existing_snap_lines=%d offset=%.3f stagger=%d stagger_val=%.3f create_stagger_snap_lines=%d spacing=%.3f attach_to_surface=1 requested_max_lines_per_side=2 long_leader_risk=%d overlap_risk_sides=%d snapline_prepare=%d note=reuse-prepared-snap-lines-no-extra-create",
            static_cast<void *>(target_view),
            static_cast<int>(st_clean),
            balloons.size(),
            reuse_prepared_snap_lines == PRO_B_TRUE ? 1 : 0,
            kCustomBalloonOutsideOffset,
            use_stagger == PRO_B_TRUE ? 1 : 0,
            stagger_val,
            create_snap_lines_during_clean == PRO_B_TRUE ? 1 : 0,
            kCustomBalloonVerticalSpacing,
            stagger_decision.long_leader_risk,
            stagger_decision.overlap_risk_sides,
            static_cast<int>(st_snapline_prepare));
    if (st_clean != PRO_TK_NO_ERROR) {
        const ProError st_surface_clean = ProBomballoonClean(
            drawing,
            target_view,
            PRO_B_TRUE,
            reuse_prepared_snap_lines,
            kCustomBalloonOutsideOffset,
            use_stagger,
            stagger_val,
            PRO_B_TRUE,
            kCustomBalloonVerticalSpacing,
            PRO_B_TRUE);
        LogLine(log_sink,
                "ArrangeBalloons traditional-native-clean-retry view=%p status=%d previous_status=%d existing_snap_lines=%d stagger=%d stagger_val=%.3f create_stagger_snap_lines=1 attach_to_surface=1 requested_max_lines_per_side=2 note=retry-create-snap-lines",
                static_cast<void *>(target_view),
                static_cast<int>(st_surface_clean),
                static_cast<int>(st_clean),
                reuse_prepared_snap_lines == PRO_B_TRUE ? 1 : 0,
                use_stagger == PRO_B_TRUE ? 1 : 0,
                stagger_val);
        st_clean = st_surface_clean;
    }
    if (st_clean == PRO_TK_NO_ERROR) {
        summary.notes_reordered = summary.selected_total;
        summary.notes_reorder_failed = 0;
        summary.views_arranged = 1;
        summary.views_failed = 0;
        summary.first_error = PRO_TK_NO_ERROR;
        AuditTraditionalBomBalloonPostClean(
            drawing,
            sheet,
            target_view,
            target_view_key,
            balloons,
            log_sink);
    } else {
        summary.notes_reordered = 0;
        summary.notes_reorder_failed = summary.selected_total;
        summary.views_arranged = 0;
        summary.views_failed = 1;
        summary.first_error = st_clean;
    }

    if (summary.notes_reordered > 0) {
        summary.views_arranged = 1;
    }
    if (summary.notes_reorder_failed > 0 && summary.notes_reordered == 0) {
        summary.views_failed = 1;
    }

    LogLine(log_sink,
            "ArrangeBalloons traditional-end valid_views=%d arranged=%d failed=%d candidates=%d moved=%d move_failed=%d first_error=%d native_clean=%d",
            summary.valid_views,
            summary.views_arranged,
            summary.views_failed,
            summary.selected_total,
            summary.notes_reordered,
            summary.notes_reorder_failed,
            static_cast<int>(summary.first_error),
            st_clean == PRO_TK_NO_ERROR ? 1 : 0);
    return summary;
}

} // namespace autobbox::application



