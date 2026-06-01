#include "autobbox/application/model_metrics.h"

#include "autobbox/creo/family_table_api.h"
#include "autobbox/creo/model_info.h"

#include <ProFamtable.h>
#include <ProFeature.h>
#include <ProFeatType.h>
#include <ProAsmcomppath.h>
#include <ProMdl.h>
#include <ProMdlUnits.h>
#include <ProSolid.h>
#include <ProSolidBody.h>
#include <ProUtil.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace autobbox::application {

namespace {

bool HasFamtable(ProMdl mdl)
{
    ProFamtable ft;
    ProError st = ProFamtableInit(mdl, &ft);
    if (st != PRO_TK_NO_ERROR) {
        return false;
    }
    st = ProFamtableCheck(&ft);
    return st == PRO_TK_NO_ERROR || st == PRO_TK_EMPTY;
}

bool IsGenericByFamtable(ProMdl mdl)
{
    if (creo::IsFamilyInstanceQuick(mdl)) {
        return false;
    }
    return HasFamtable(mdl);
}

struct BoundsAccumulator {
    bool have = false;
    double min[3] = {0.0, 0.0, 0.0};
    double max[3] = {0.0, 0.0, 0.0};
};

void AccumulatePoint(BoundsAccumulator &bounds, const double point[3])
{
    if (!bounds.have) {
        for (int axis = 0; axis < 3; ++axis) {
            bounds.min[axis] = point[axis];
            bounds.max[axis] = point[axis];
        }
        bounds.have = true;
        return;
    }

    for (int axis = 0; axis < 3; ++axis) {
        bounds.min[axis] = std::min(bounds.min[axis], point[axis]);
        bounds.max[axis] = std::max(bounds.max[axis], point[axis]);
    }
}

bool AccumulateSolidBodyOutlines(ProMdl mdl,
                                 const ProMatrix *transform,
                                 BoundsAccumulator &bounds)
{
    if (mdl == nullptr) {
        return false;
    }

    ProSolidBody *bodies = nullptr;
    ProError st = ProSolidBodiesCollect(reinterpret_cast<ProSolid>(mdl), &bodies);
    if (st != PRO_TK_NO_ERROR || bodies == nullptr) {
        return false;
    }

    int count = 0;
    ProArraySizeGet(reinterpret_cast<ProArray>(bodies), &count);

    bool added = false;
    for (int i = 0; i < count; ++i) {
        ProSolidBody *body = &bodies[i];

        ProSolidBodyState state = PRO_BODY_STATE_NONE;
        ProSolidBodyStateGet(body, &state);
        if (state != PRO_BODY_STATE_ACTIVE) {
            continue;
        }

        ProBoolean is_construction = PRO_B_FALSE;
        ProSolidBodyIsConstruction(body, &is_construction);
        if (is_construction == PRO_B_TRUE) {
            continue;
        }

        ProOutline outline = {{0}};
        st = ProSolidBodyOutlineGet(body, outline);
        if (st != PRO_TK_NO_ERROR) {
            continue;
        }

        const double dx = std::fabs(outline[1][0] - outline[0][0]);
        const double dy = std::fabs(outline[1][1] - outline[0][1]);
        const double dz = std::fabs(outline[1][2] - outline[0][2]);
        if (dx <= 1e-12 && dy <= 1e-12 && dz <= 1e-12) {
            continue;
        }

        for (int ix = 0; ix < 2; ++ix) {
            for (int iy = 0; iy < 2; ++iy) {
                for (int iz = 0; iz < 2; ++iz) {
                    ProVector point = {
                        outline[ix][0],
                        outline[iy][1],
                        outline[iz][2],
                    };
                    ProVector transformed = {point[0], point[1], point[2]};
                    if (transform != nullptr &&
                        ProPntTrfEval(point, const_cast<ProMatrix &>(*transform), transformed) != PRO_TK_NO_ERROR) {
                        continue;
                    }
                    AccumulatePoint(bounds, transformed);
                    added = true;
                }
            }
        }
    }

    ProArrayFree(reinterpret_cast<ProArray *>(&bodies));
    return added;
}

bool IsSurfaceFeatureType(ProFeattype type)
{
    switch (type) {
#ifdef PRO_FEAT_DATUM_SURF
    case PRO_FEAT_DATUM_SURF:
#endif
#ifdef PRO_FEAT_REPLACE_SURF
    case PRO_FEAT_REPLACE_SURF:
#endif
#ifdef PRO_FEAT_DATUM_QUILT
    case PRO_FEAT_DATUM_QUILT:
#endif
#ifdef PRO_FEAT_SRF_MDL
    case PRO_FEAT_SRF_MDL:
#endif
#ifdef PRO_FEAT_SPLIT_SURF
    case PRO_FEAT_SPLIT_SURF:
#endif
#ifdef PRO_FEAT_PUNCH_QUILT
    case PRO_FEAT_PUNCH_QUILT:
#endif
#ifdef PRO_FEAT_DRV_TOOL_SURF
    case PRO_FEAT_DRV_TOOL_SURF:
#endif
#ifdef PRO_FEAT_REMOVE_SURFS
    case PRO_FEAT_REMOVE_SURFS:
#endif
#ifdef PRO_FEAT_IPM_QUILT
    case PRO_FEAT_IPM_QUILT:
#endif
#ifdef PRO_FEAT_SUPERQUILT
    case PRO_FEAT_SUPERQUILT:
#endif
#ifdef PRO_FEAT_RM_SURF
    case PRO_FEAT_RM_SURF:
#endif
#ifdef PRO_FEAT_SPLIT_SRF_RGN
    case PRO_FEAT_SPLIT_SRF_RGN:
#endif
#ifdef PRO_FEAT_UNIFYSRF
    case PRO_FEAT_UNIFYSRF:
#endif
#ifdef PRO_FEAT_CMPST_DESIGN_SURF
    case PRO_FEAT_CMPST_DESIGN_SURF:
#endif
        return true;
    default:
        return false;
    }
}

bool IsCurveFeatureType(ProFeattype type)
{
    switch (type) {
#ifdef PRO_FEAT_CURVE
    case PRO_FEAT_CURVE:
#endif
#ifdef PRO_FEAT_DRV_TOOL_CURVE
    case PRO_FEAT_DRV_TOOL_CURVE:
#endif
#ifdef PRO_FEAT_PM_BELT_CURVE
    case PRO_FEAT_PM_BELT_CURVE:
#endif
#ifdef PRO_FEAT_CRV_FROM_PNT
    case PRO_FEAT_CRV_FROM_PNT:
#endif
#ifdef PRO_FEAT_PLY_AUTO_CRVS_HEAD
    case PRO_FEAT_PLY_AUTO_CRVS_HEAD:
#endif
#ifdef PRO_FEAT_PLY_AUTO_CRV
    case PRO_FEAT_PLY_AUTO_CRV:
#endif
        return true;
    default:
        return false;
    }
}

struct OutlineFeatureFilterOptions {
    bool include_surface = true;
    bool include_curve = true;
};

ProError OutlineFeatureFilter(ProFeature *feature, ProAppData app_data)
{
    if (feature == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    const OutlineFeatureFilterOptions *options =
        reinterpret_cast<const OutlineFeatureFilterOptions *>(app_data);
    ProFeattype type = 0;
    if (ProFeatureTypeGet(feature, &type) != PRO_TK_NO_ERROR) {
        return PRO_TK_NO_ERROR;
    }

    if (!options->include_surface && IsSurfaceFeatureType(type)) {
        return PRO_TK_CONTINUE;
    }
    if (!options->include_curve && IsCurveFeatureType(type)) {
        return PRO_TK_CONTINUE;
    }
    return PRO_TK_NO_ERROR;
}

bool ComputeOutlineSolid(ProMdl mdl,
                         bool include_surface,
                         bool include_curve,
                         double out_min[3],
                         double out_max[3])
{
    Pro3dPnt outline[2] = {{0}};
    ProSolidOutlExclTypes excludes[5];
    int exclude_count = 0;
    excludes[exclude_count++] = PRO_OUTL_EXC_DATUM_PLANE;
    excludes[exclude_count++] = PRO_OUTL_EXC_DATUM_POINT;
    excludes[exclude_count++] = PRO_OUTL_EXC_DATUM_CSYS;
    excludes[exclude_count++] = PRO_OUTL_EXC_DATUM_AXES;
    if (!include_curve) {
        excludes[exclude_count++] = PRO_OUTL_EXC_ALL_CRVS;
    }

    ProMatrix matrix = {{0}};
    matrix[0][0] = 1.0;
    matrix[1][1] = 1.0;
    matrix[2][2] = 1.0;
    matrix[3][3] = 1.0;

    ProError st = PRO_TK_GENERAL_ERROR;
    if (!include_surface || !include_curve) {
        OutlineFeatureFilterOptions options;
        options.include_surface = include_surface;
        options.include_curve = include_curve;
        st = ProSolidOutlineWithOptionsCompute(
            reinterpret_cast<ProSolid>(mdl),
            matrix,
            excludes,
            exclude_count,
            OutlineFeatureFilter,
            &options,
            outline);
    } else {
        st = ProSolidOutlineCompute(
            reinterpret_cast<ProSolid>(mdl), matrix, excludes, exclude_count, outline);
    }
    if (st != PRO_TK_NO_ERROR) {
        return false;
    }

    for (int i = 0; i < 3; ++i) {
        out_min[i] = outline[0][i];
        out_max[i] = outline[1][i];
    }
    return true;
}

bool ComputeOutlineSolidBodiesOnly(ProMdl mdl, double out_min[3], double out_max[3])
{
    BoundsAccumulator bounds;
    if (!AccumulateSolidBodyOutlines(mdl, nullptr, bounds)) {
        return false;
    }

    for (int axis = 0; axis < 3; ++axis) {
        out_min[axis] = bounds.min[axis];
        out_max[axis] = bounds.max[axis];
    }
    return true;
}

struct AssemblyBodyOutlineCtx {
    BoundsAccumulator bounds;
};

ProError AssemblyBodyVisitAction(ProAsmcomppath *path,
                                 ProSolid handle,
                                 ProBoolean down,
                                 ProAppData app_data)
{
    if (down != PRO_B_TRUE || path == nullptr || handle == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    AssemblyBodyOutlineCtx *ctx = reinterpret_cast<AssemblyBodyOutlineCtx *>(app_data);
    ProMatrix transform = {{0}};
    if (ProAsmcomppathTrfGet(path, PRO_B_TRUE, transform) != PRO_TK_NO_ERROR) {
        return PRO_TK_NO_ERROR;
    }
    AccumulateSolidBodyOutlines(reinterpret_cast<ProMdl>(handle), &transform, ctx->bounds);
    return PRO_TK_NO_ERROR;
}

bool ComputeAssemblySolidBodiesOnly(ProMdl mdl, double out_min[3], double out_max[3])
{
    if (mdl == nullptr || autobbox::creo::ModelType(mdl) != PRO_MDL_ASSEMBLY) {
        return false;
    }

    AssemblyBodyOutlineCtx ctx;
    AccumulateSolidBodyOutlines(mdl, nullptr, ctx.bounds);
    ProSolidDispCompVisit(reinterpret_cast<ProSolid>(mdl), AssemblyBodyVisitAction, nullptr, &ctx);
    if (!ctx.bounds.have) {
        return false;
    }

    for (int axis = 0; axis < 3; ++axis) {
        out_min[axis] = ctx.bounds.min[axis];
        out_max[axis] = ctx.bounds.max[axis];
    }
    return true;
}

bool ComputeSolidOnlyOutline(ProMdl mdl, double out_min[3], double out_max[3])
{
    const ProMdlType type = autobbox::creo::ModelType(mdl);
    if (type == PRO_MDL_PART) {
        return ComputeOutlineSolidBodiesOnly(mdl, out_min, out_max);
    }
    if (type == PRO_MDL_ASSEMBLY) {
        return ComputeAssemblySolidBodiesOnly(mdl, out_min, out_max);
    }
    return false;
}

bool ConvertVolumeToM3(ProMdl mdl, double raw_volume, double &out_m3)
{
    ProUnitsystem system;
    if (ProMdlPrincipalunitsystemGet(mdl, &system) != PRO_TK_NO_ERROR) {
        return false;
    }

    ProUnititem from_vol;
    if (ProUnitsystemUnitGet(&system, PRO_UNITTYPE_VOLUME, &from_vol) != PRO_TK_NO_ERROR) {
        return false;
    }

    ProPath expr = {0};
    ProStringToWstring(expr, const_cast<char *>("m^3"));

    ProUnititem to_vol;
    if (ProUnitInitByExpressionAndType(mdl, expr, PRO_UNITTYPE_VOLUME, &to_vol) != PRO_TK_NO_ERROR) {
        return false;
    }

    ProUnitConversion conversion;
    std::memset(&conversion, 0, sizeof(conversion));
    if (ProUnitConversionCalculate(&from_vol, &to_vol, &conversion) != PRO_TK_NO_ERROR) {
        return false;
    }

    out_m3 = raw_volume * conversion.scale + conversion.offset;
    return true;
}

} // namespace

bool ComputeBBoxLwh(ProMdl mdl,
                    bool include_surface,
                    bool include_curve,
                    double &length_out,
                    double &width_out,
                    double &height_out)
{
    double pmin[3] = {0, 0, 0};
    double pmax[3] = {0, 0, 0};

    const bool solid_only = !include_surface && !include_curve;
    const bool ok = solid_only
                        ? ComputeSolidOnlyOutline(mdl, pmin, pmax)
                        : ComputeOutlineSolid(mdl, include_surface, include_curve, pmin, pmax);

    if (!ok) {
        return false;
    }

    const double dx = std::fabs(pmax[0] - pmin[0]);
    const double dy = std::fabs(pmax[1] - pmin[1]);
    const double dz = std::fabs(pmax[2] - pmin[2]);
    std::array<double, 3> dims = {dx, dy, dz};
    std::sort(dims.begin(), dims.end(), std::greater<double>());
    length_out = dims[0];
    width_out = dims[1];
    height_out = dims[2];
    return true;
}

bool ComputeBBoxAxes(ProMdl mdl,
                     bool include_surface,
                     bool include_curve,
                     double &size_x_out,
                     double &size_y_out,
                     double &size_z_out)
{
    double pmin[3] = {0, 0, 0};
    double pmax[3] = {0, 0, 0};

    const bool solid_only = !include_surface && !include_curve;
    const bool ok = solid_only
                        ? ComputeSolidOnlyOutline(mdl, pmin, pmax)
                        : ComputeOutlineSolid(mdl, include_surface, include_curve, pmin, pmax);

    if (!ok) {
        return false;
    }

    size_x_out = std::fabs(pmax[0] - pmin[0]);
    size_y_out = std::fabs(pmax[1] - pmin[1]);
    size_z_out = std::fabs(pmax[2] - pmin[2]);
    return true;
}

bool ComputeVolumeM3(ProMdl mdl, double &volume_out)
{
    double size_x = 0.0;
    double size_y = 0.0;
    double size_z = 0.0;
    if (!ComputeBBoxAxes(mdl,
                         false,
                         false,
                         size_x,
                         size_y,
                         size_z)) {
        return false;
    }

    const double envelope_volume = size_x * size_y * size_z;
    double converted = 0.0;
    if (ConvertVolumeToM3(mdl, envelope_volume, converted)) {
        volume_out = converted;
        return true;
    }

    volume_out = envelope_volume / 1.0e9;
    return true;
}

std::wstring IntLwhString(double length, double width, double height)
{
    long long li = llround(length);
    long long wi = llround(width);
    long long hi = llround(height);
    if (li < 0) {
        li = 0;
    }
    if (wi < 0) {
        wi = 0;
    }
    if (hi < 0) {
        hi = 0;
    }

    wchar_t buffer[128] = {0};
    std::swprintf(buffer,
                  sizeof(buffer) / sizeof(buffer[0]) - 1,
                  L"%lldX%lldX%lld",
                  li,
                  wi,
                  hi);
    return std::wstring(buffer);
}

std::wstring FormatVol(double value)
{
    wchar_t buffer[128] = {0};
    std::swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]) - 1, L"%.6f", value);
    std::wstring text(buffer);
    while (!text.empty() && text.back() == L'0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == L'.') {
        text.pop_back();
    }
    if (text.empty()) {
        text = L"0";
    }
    return text;
}

bool ShouldSkipModel(ProMdl mdl)
{
    if (!autobbox::creo::IsPartOrAsm(mdl)) {
        return true;
    }
    const std::wstring name = autobbox::creo::ModelName(mdl, L"");
    return name == L"UPRIGHT_POST" && IsGenericByFamtable(mdl);
}

bool HasFailedRegeneration(ProMdl mdl)
{
    if (!autobbox::creo::IsPartOrAsm(mdl)) {
        return false;
    }

    ProSolidRegenerationStatus regen = PRO_SOLID_REGENERATED;
    const ProError st = ProSolidRegenerationstatusGet(reinterpret_cast<ProSolid>(mdl), &regen);
    if (st == PRO_TK_NO_ERROR) {
        return regen == PRO_SOLID_FAILED_REGENERATION;
    }
    return false;
}

} // namespace autobbox::application
