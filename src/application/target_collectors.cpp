#include "autobbox/application/target_collectors.h"

#include "autobbox/common/strings.h"
#include "autobbox/creo/family_table_api.h"
#include "autobbox/creo/model_info.h"

#include <ProArray.h>
#include <ProAsmcomppath.h>
#include <ProAsmcomp.h>
#include <ProFaminstance.h>
#include <ProMdl.h>
#include <ProRule.h>
#include <ProSimprep.h>
#include <ProSolid.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cwchar>
#include <set>
#include <unordered_set>

namespace autobbox::application {

namespace {

const wchar_t *MdlTypeShortLabel(ProMdlType type)
{
    switch (type) {
    case PRO_MDL_PART:
        return L"PRT";
    case PRO_MDL_ASSEMBLY:
        return L"ASM";
    case PRO_MDL_DRAWING:
        return L"DRW";
    default:
        return L"MDL";
    }
}

std::wstring GetImmediateGenericName(ProMdl mdl)
{
    if (mdl == nullptr) {
        return std::wstring();
    }

    ProMdl generic = nullptr;
    if (ProFaminstanceGenericGet(mdl, PRO_B_TRUE, &generic) == PRO_TK_NO_ERROR &&
        generic != nullptr &&
        generic != mdl &&
        autobbox::creo::ModelName(generic, L"") != autobbox::creo::ModelName(mdl, L"")) {
        return autobbox::creo::ModelName(generic, L"");
    }

    ProName gen_name = {0};
    ProMdlType gen_type = PRO_MDL_UNUSED;
    if (ProFaminstanceImmediategenericinfoGet(mdl, gen_name, &gen_type) == PRO_TK_NO_ERROR &&
        gen_name[0] != L'\0') {
        return std::wstring(gen_name);
    }
    return std::wstring();
}

std::wstring MakeSplitCandidateLabel(const core::SplitCandidate &cand, bool current_model)
{
    std::wstring out = cand.model_name;
    out += L" (";
    out += MdlTypeShortLabel(cand.type);
    out += L")";
    if (!cand.generic_name.empty()) {
        out += L" <- ";
        out += cand.generic_name;
    }
    if (current_model) {
        out += L" [当前模型]";
    } else {
        out += L" [组件ID ";
        out += std::to_wstring(cand.feat_id);
        out += L"]";
    }
    return out;
}

struct TraverseCtx {
    std::vector<ProMdl> targets;
    std::unordered_set<std::wstring> seen;
    bool want_parts = true;
    bool want_asms = true;
    bool top_level_only = false;
};

std::wstring TargetDedupKey(ProMdl mdl)
{
    if (mdl == nullptr) {
        return std::wstring();
    }

    ProName name = {0};
    ProMdlNameGet(mdl, name);
    return std::to_wstring(static_cast<int>(autobbox::creo::ModelType(mdl))) +
           L":" +
           std::wstring(name);
}

bool AcceptType(ProMdl mdl, const TraverseCtx &ctx)
{
    const ProMdlType type = autobbox::creo::ModelType(mdl);
    return (type == PRO_MDL_PART && ctx.want_parts) ||
           (type == PRO_MDL_ASSEMBLY && ctx.want_asms);
}

ProError CompVisitAction(ProAsmcomppath *p_path,
                         ProSolid handle,
                         ProBoolean down,
                         ProAppData app_data)
{
    if (down != PRO_B_TRUE || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    TraverseCtx *ctx = reinterpret_cast<TraverseCtx *>(app_data);
    const int depth = (p_path == nullptr) ? 0 : p_path->table_num;
    if (ctx->top_level_only && depth != 1) {
        return PRO_TK_NO_ERROR;
    }

    ProMdl mdl = reinterpret_cast<ProMdl>(handle);
    if (!AcceptType(mdl, *ctx)) {
        return PRO_TK_NO_ERROR;
    }

    const std::wstring key = TargetDedupKey(mdl);
    if (!key.empty() && ctx->seen.insert(key).second) {
        ctx->targets.push_back(mdl);
    }
    return PRO_TK_NO_ERROR;
}

struct SplitCollectCtx {
    ProSolid owner = nullptr;
    std::vector<core::SplitCandidate> candidates;
    std::unordered_set<int> seen_feat_ids;
};

ProError SplitCompVisitAction(ProAsmcomppath *p_path,
                              ProSolid handle,
                              ProBoolean down,
                              ProAppData app_data)
{
    if (down != PRO_B_TRUE || p_path == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }
    if (p_path->table_num != 1) {
        return PRO_TK_NO_ERROR;
    }

    SplitCollectCtx *ctx = reinterpret_cast<SplitCollectCtx *>(app_data);
    ProMdl mdl = reinterpret_cast<ProMdl>(handle);
    if (!autobbox::creo::IsPartOrAsm(mdl) || !creo::IsFamilyInstanceQuick(mdl)) {
        return PRO_TK_NO_ERROR;
    }

    const int feat_id = p_path->comp_id_table[0];
    if (!ctx->seen_feat_ids.insert(feat_id).second) {
        return PRO_TK_NO_ERROR;
    }

    core::SplitCandidate cand;
    cand.mdl = mdl;
    cand.type = autobbox::creo::ModelType(mdl);
    cand.owner = ctx->owner;
    cand.comp_path = *p_path;
    cand.has_owner = (ctx->owner != nullptr);
    cand.feat_id = feat_id;
    cand.model_name = autobbox::creo::ModelName(mdl, L"");
    cand.generic_name = GetImmediateGenericName(mdl);
    cand.label = MakeSplitCandidateLabel(cand, false);
    cand.item_name = "split_" + std::to_string(ctx->candidates.size());
    ctx->candidates.push_back(cand);
    return PRO_TK_NO_ERROR;
}

std::wstring BomTypeLabel(ProParamvalueType type)
{
    switch (type) {
    case PRO_PARAM_STRING:
        return L"字符串";
    case PRO_PARAM_INTEGER:
        return L"整数";
    case PRO_PARAM_DOUBLE:
        return L"实数";
    case PRO_PARAM_BOOLEAN:
        return L"是/否";
    case PRO_PARAM_NOTE_ID:
        return L"注释ID";
    case PRO_PARAM_VOID:
        return L"空值";
    case PRO_PARAM_NOT_SET:
        return L"未设定";
    default:
        return L"未知";
    }
}

std::wstring JoinBomTypeLabels(const std::set<ProParamvalueType> &types)
{
    if (types.empty()) {
        return L"未知";
    }
    if (types.size() == 1) {
        return BomTypeLabel(*types.begin());
    }
    return L"混合类型";
}

struct BomTraverseCtx {
    std::vector<core::BomTarget> targets;
    bool want_parts = true;
    bool want_assemblies = true;
    int max_level = 1;
};

ProError CollectBomVisitAction(ProAsmcomppath *p_path,
                               ProSolid handle,
                               ProBoolean down,
                               ProAppData data)
{
    if (down != PRO_B_TRUE || data == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    BomTraverseCtx *ctx = reinterpret_cast<BomTraverseCtx *>(data);
    const int depth = (p_path == nullptr) ? 0 : p_path->table_num;
    if (depth <= 0 || depth > ctx->max_level) {
        return PRO_TK_NO_ERROR;
    }

    ProMdl mdl = reinterpret_cast<ProMdl>(handle);
    const ProMdlType type = autobbox::creo::ModelType(mdl);
    if ((type == PRO_MDL_PART && ctx->want_parts) ||
        (type == PRO_MDL_ASSEMBLY && ctx->want_assemblies)) {
        core::BomTarget target;
        target.mdl = mdl;
        target.level = depth;
        ctx->targets.push_back(target);
    }
    return PRO_TK_NO_ERROR;
}

} // namespace

std::vector<ProMdl> CollectTargetsFromCurrentModel(ProBoolean parts,
                                                   ProBoolean assemblies,
                                                   ProBoolean top_level_only)
{
    std::vector<ProMdl> result;
    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR || current == nullptr) {
        return result;
    }

    const ProMdlType current_type = autobbox::creo::ModelType(current);
    if (current_type == PRO_MDL_PART) {
        if (parts == PRO_B_TRUE) {
            result.push_back(current);
        }
        return result;
    }

    if (current_type != PRO_MDL_ASSEMBLY) {
        return result;
    }

    TraverseCtx ctx;
    ctx.want_parts = (parts == PRO_B_TRUE);
    ctx.want_asms = (assemblies == PRO_B_TRUE);
    ctx.top_level_only = (top_level_only == PRO_B_TRUE);
    if (!ctx.top_level_only && ctx.want_asms) {
        ctx.targets.push_back(current);
        ctx.seen.insert(TargetDedupKey(current));
    }

    ProSolidDispCompVisit(reinterpret_cast<ProSolid>(current), CompVisitAction, nullptr, &ctx);
    return ctx.targets;
}


struct BomSimprepCollectCtx {
    std::vector<core::Dwg3SimprepOption> *options = nullptr;
    std::wstring active_user_rep_name;
};

bool TryReadSimprepName(ProSimprep *simprep, std::wstring &name_out)
{
    name_out.clear();
    if (simprep == nullptr) {
        return false;
    }

    ProSimprepdata *data = nullptr;
    if (ProSimprepdataGet(simprep, &data) != PRO_TK_NO_ERROR || data == nullptr) {
        return false;
    }

    ProName name = {0};
    const ProError st_name = ProSimprepdataNameGet(data, name);
    ProSimprepdataFree(&data);
    if (st_name != PRO_TK_NO_ERROR || name[0] == L'\0') {
        return false;
    }

    name_out.assign(name);
    return true;
}

ProError CollectBomSimprepVisitAction(ProGeomitem *handle, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || handle == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    BomSimprepCollectCtx *ctx = reinterpret_cast<BomSimprepCollectCtx *>(app_data);
    if (ctx->options == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    ProSimprep *simprep = reinterpret_cast<ProSimprep *>(handle);
    ProSimprepType type = PRO_SIMPREP_MASTER_REP;
    if (ProSimprepTypeGet(simprep, &type) != PRO_TK_NO_ERROR || type != PRO_SIMPREP_USER_DEFINED) {
        return PRO_TK_NO_ERROR;
    }

    std::wstring rep_name;
    if (!TryReadSimprepName(simprep, rep_name) || rep_name.empty()) {
        return PRO_TK_NO_ERROR;
    }

    core::Dwg3SimprepOption option;
    option.display_label = rep_name;
    option.rep_name = rep_name;
    option.use_current_active = false;
    option.is_active = (!ctx->active_user_rep_name.empty() && ctx->active_user_rep_name == rep_name);
    ctx->options->push_back(option);
    return PRO_TK_NO_ERROR;
}

bool TryGetActiveBomSimprep(ProSolid root_solid,
                           ProSimprep &active_rep,
                           ProSimprepType &active_type,
                           std::wstring &active_name)
{
    active_rep = {};
    active_type = PRO_SIMPREP_MASTER_REP;
    active_name.clear();
    if (root_solid == nullptr ||
        autobbox::creo::ModelType(reinterpret_cast<ProMdl>(root_solid)) != PRO_MDL_ASSEMBLY) {
        return false;
    }

    if (ProSimprepActiveGet(root_solid, &active_rep) != PRO_TK_NO_ERROR) {
        return false;
    }

    if (ProSimprepTypeGet(&active_rep, &active_type) != PRO_TK_NO_ERROR) {
        active_type = PRO_SIMPREP_MASTER_REP;
    }
    if (active_type == PRO_SIMPREP_USER_DEFINED) {
        TryReadSimprepName(&active_rep, active_name);
    }
    return true;
}

std::vector<core::Dwg3SimprepOption> CollectBomSimprepOptionsFromRoot(ProSolid root_solid)
{
    std::vector<core::Dwg3SimprepOption> options;
    if (root_solid == nullptr) {
        return options;
    }

    ProSimprep active_rep = {};
    ProSimprepType active_type = PRO_SIMPREP_MASTER_REP;
    std::wstring active_name;
    const bool has_active_rep = TryGetActiveBomSimprep(root_solid, active_rep, active_type, active_name);

    core::Dwg3SimprepOption master_option;
    master_option.display_label = L"Master Rep";
    master_option.use_master_rep = true;
    master_option.is_active = (!has_active_rep || active_type == PRO_SIMPREP_MASTER_REP);
    options.push_back(master_option);

    if (autobbox::creo::ModelType(reinterpret_cast<ProMdl>(root_solid)) != PRO_MDL_ASSEMBLY) {
        return options;
    }

    BomSimprepCollectCtx ctx;
    ctx.options = &options;
    if (active_type == PRO_SIMPREP_USER_DEFINED) {
        ctx.active_user_rep_name = active_name;
    }

    const ProError st = ProSolidSimprepVisit(root_solid, nullptr, CollectBomSimprepVisitAction, &ctx);
    if (st != PRO_TK_NO_ERROR && st != PRO_TK_E_NOT_FOUND) {
        return options;
    }

    bool has_active_option = false;
    for (const core::Dwg3SimprepOption &option : options) {
        if (option.is_active) {
            has_active_option = true;
            break;
        }
    }
    if (!has_active_option && !options.empty()) {
        options[0].is_active = true;
    }
    return options;
}

const core::Dwg3SimprepOption *FindBomSimprepOptionByLabel(const std::vector<core::Dwg3SimprepOption> &options,
                                                          const std::wstring &label)
{
    for (const core::Dwg3SimprepOption &option : options) {
        if (option.display_label == label) {
            return &option;
        }
    }
    return nullptr;
}

std::vector<core::BomTarget> CollectBomTargetsFromCurrentModel(const core::BomToolState &state)
{
    std::vector<core::BomTarget> result;
    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR || current == nullptr) {
        return result;
    }

    const ProMdlType current_type = autobbox::creo::ModelType(current);
    if (current_type == PRO_MDL_PART) {
        if (state.parts_option == PRO_B_TRUE && state.max_bom_level >= 1) {
            core::BomTarget target;
            target.mdl = current;
            target.level = 1;
            result.push_back(target);
        }
        return result;
    }

    if (current_type != PRO_MDL_ASSEMBLY) {
        return result;
    }

    BomTraverseCtx ctx;
    ctx.want_parts = (state.parts_option == PRO_B_TRUE);
    ctx.want_assemblies = (state.assemblies_option == PRO_B_TRUE);
    ctx.max_level = std::max(1, state.max_bom_level);
    ProSolidDispCompVisit(reinterpret_cast<ProSolid>(current), CollectBomVisitAction, nullptr, &ctx);
    return ctx.targets;
}

std::vector<core::BomTarget> CollectBomTargetsFromSimprep(const core::BomToolState &state,
                                                          const core::Dwg3SimprepOption &option)
{
    std::vector<core::BomTarget> result;
    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR || current == nullptr) {
        return result;
    }

    const ProMdlType type = autobbox::creo::ModelType(current);
    if (type == PRO_MDL_PART) {
        if (state.parts_option == PRO_B_TRUE) {
            core::BomTarget target;
            target.mdl = current;
            target.level = 1;
            result.push_back(target);
        }
        return result;
    }
    if (type != PRO_MDL_ASSEMBLY) {
        return result;
    }

    if (option.use_master_rep) {
        return CollectBomTargetsFromCurrentModel(state);
    }

    if (option.rep_name.empty()) {
        return CollectBomTargetsFromCurrentModel(state);
    }

    ProName rep_name = {0};
    size_t copy_index = 0;
    while (copy_index + 1 < sizeof(rep_name) / sizeof(rep_name[0]) && copy_index < option.rep_name.size()) {
        rep_name[copy_index] = option.rep_name[copy_index];
        ++copy_index;
    }
    rep_name[copy_index] = L'\0';

    ProSimprep simprep = {};
    if (ProSimprepInit(rep_name, PRO_VALUE_UNUSED, reinterpret_cast<ProSolid>(current), &simprep) != PRO_TK_NO_ERROR) {
        return CollectBomTargetsFromCurrentModel(state);
    }

    ProRule rule = {};
    if (ProRuleInitRep(&simprep, &rule) != PRO_TK_NO_ERROR) {
        return CollectBomTargetsFromCurrentModel(state);
    }

    ProAsmcomppath *comp_paths = nullptr;
    int path_count = 0;
    const ProError st_eval = ProRuleEval(reinterpret_cast<ProSolid>(current), &rule, &comp_paths, &path_count);
    if (st_eval != PRO_TK_NO_ERROR) {
        if (comp_paths != nullptr) {
            ProArrayFree(reinterpret_cast<ProArray *>(&comp_paths));
        }
        return CollectBomTargetsFromCurrentModel(state);
    }

    if (comp_paths == nullptr || path_count <= 0) {
        return result;
    }

    for (int i = 0; i < path_count; ++i) {
        const int level = comp_paths[i].table_num;
        if (level <= 0 || level > std::max(1, state.max_bom_level)) {
            continue;
        }

        ProMdl mdl = nullptr;
        if (ProAsmcomppathMdlGet(&comp_paths[i], &mdl) != PRO_TK_NO_ERROR || mdl == nullptr) {
            continue;
        }

        const ProMdlType mdl_type = autobbox::creo::ModelType(mdl);
        if (mdl_type != PRO_MDL_PART && mdl_type != PRO_MDL_ASSEMBLY) {
            continue;
        }

        if ((mdl_type == PRO_MDL_PART && state.parts_option == PRO_B_TRUE) ||
            (mdl_type == PRO_MDL_ASSEMBLY && state.assemblies_option == PRO_B_TRUE)) {
            core::BomTarget target;
            target.mdl = mdl;
            target.level = level;
            result.push_back(target);
        }
    }

    ProArrayFree(reinterpret_cast<ProArray *>(&comp_paths));
    return result;
}

std::vector<core::SplitCandidate> CollectSplitCandidatesFromCurrentModel()
{
    std::vector<core::SplitCandidate> result;
    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        !autobbox::creo::IsPartOrAsm(current)) {
        return result;
    }

    if (creo::IsFamilyInstanceQuick(current)) {
        core::SplitCandidate cand;
        cand.mdl = current;
        cand.type = autobbox::creo::ModelType(current);
        cand.model_name = autobbox::creo::ModelName(current, L"");
        cand.generic_name = GetImmediateGenericName(current);
        cand.label = MakeSplitCandidateLabel(cand, true);
        cand.item_name = "split_current";
        result.push_back(cand);
    }

    if (autobbox::creo::ModelType(current) != PRO_MDL_ASSEMBLY) {
        return result;
    }

    SplitCollectCtx ctx;
    ctx.owner = reinterpret_cast<ProSolid>(current);
    ProSolidDispCompVisit(reinterpret_cast<ProSolid>(current), SplitCompVisitAction, nullptr, &ctx);
    result.insert(result.end(), ctx.candidates.begin(), ctx.candidates.end());
    return result;
}

std::vector<core::Dwg3SimprepOption> CollectBomSimprepOptions()
{
    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR || current == nullptr) {
        return {};
    }
    return CollectBomSimprepOptionsFromRoot(reinterpret_cast<ProSolid>(current));
}

std::vector<core::BomTarget> CollectBomTargets(const core::BomToolState &state)
{
    if (state.simprep_options.empty()) {
        return CollectBomTargetsFromCurrentModel(state);
    }

    const core::Dwg3SimprepOption *active_option = nullptr;
    if (!state.active_simprep_label.empty()) {
        active_option = FindBomSimprepOptionByLabel(state.simprep_options, state.active_simprep_label);
    }
    if (active_option == nullptr) {
        for (const core::Dwg3SimprepOption &option : state.simprep_options) {
            if (option.is_active) {
                active_option = &option;
                break;
            }
        }
    }
    if (active_option == nullptr && !state.simprep_options.empty()) {
        active_option = &state.simprep_options.front();
    }
    if (active_option == nullptr) {
        return CollectBomTargetsFromCurrentModel(state);
    }
    return CollectBomTargetsFromSimprep(state, *active_option);
}

std::wstring BuildBomAvailableLabel(const core::BomAvailableParam &entry)
{
    std::wstring label = entry.name;
    label += L"\t";
    label += JoinBomTypeLabels(entry.types);
    label += L"\t";
    label += std::to_wstring(entry.hit_count);
    return label;
}

} // namespace autobbox::application
