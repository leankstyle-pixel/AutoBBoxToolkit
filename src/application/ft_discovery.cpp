#include "autobbox/application/ft_discovery.h"

#include "autobbox/application/ft_logger.h"
#include "autobbox/application/ft_support_matrix.h"
#include "autobbox/creo/model_info.h"

#include <ProFamtable.h>
#include <ProFaminstance.h>
#include <ProMdl.h>

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <cstring>
#include <set>
#include <vector>

namespace autobbox::application {
namespace {

bool g_force_deep_discovery = false;

std::wstring SafeModelName(ProMdl mdl)
{
    return autobbox::creo::ModelName(mdl, L"<unknown>");
}

std::wstring MakeLevelPath(const std::wstring &parent, const std::wstring &name)
{
    if (parent.empty()) {
        return name.empty() ? L"TOP" : name;
    }
    return parent + L"/" + (name.empty() ? L"<unnamed>" : name);
}

std::wstring ModelKey(ProMdl mdl)
{
    return SafeModelName(mdl) + L"|" + std::to_wstring(static_cast<int>(autobbox::creo::ModelType(mdl)));
}

bool HasUsableFamtable(ProMdl mdl, ProFamtable *out)
{
    if (mdl == nullptr || out == nullptr) {
        return false;
    }
    std::memset(out, 0, sizeof(*out));
    if (ProFamtableInit(mdl, out) != PRO_TK_NO_ERROR) {
        return false;
    }
    const ProError st = ProFamtableCheck(out);
    return st == PRO_TK_NO_ERROR || st == PRO_TK_EMPTY;
}

struct DiscoverCtx {
    core::FtWorkspace *workspace = nullptr;
    size_t parent_index = 0;
    std::set<std::wstring> *visited = nullptr;
    std::vector<std::wstring> instance_names;
};

bool IsDeepDiscoveryEnabled()
{
    if (g_force_deep_discovery) {
        return true;
    }
    static const bool enabled = []() {
        char value[16] = {0};
        size_t copied = 0;
        if (getenv_s(&copied, value, sizeof(value), "AUTO_BBOX_FT_DEEP_DISCOVERY") != 0 || copied == 0) {
            return false;
        }
        return !(value[0] == '0' || value[0] == 'N' || value[0] == 'n' || value[0] == 'F' || value[0] == 'f');
    }();
    return enabled;
}

void DiscoverLevel(ProMdl generic,
                   const std::wstring &level_path,
                   const std::wstring &parent_generic,
                   const std::wstring &parent_instance,
                   core::FtWorkspace &workspace,
                   std::set<std::wstring> &visited,
                   size_t *new_index_out);

ProError VisitInstanceForDiscovery(ProFaminstance *inst, ProError status, ProAppData data)
{
    if (status != PRO_TK_NO_ERROR || inst == nullptr || data == nullptr) {
        return PRO_TK_NO_ERROR;
    }
    auto *ctx = reinterpret_cast<DiscoverCtx *>(data);
    if (ctx->workspace == nullptr || ctx->visited == nullptr || ctx->parent_index >= ctx->workspace->level_nodes.size()) {
        return PRO_TK_NO_ERROR;
    }

    // Keep the visit callback side-effect free.  In Creo 10.0.8.0, calling
    // ProFaminstanceMdlGet() directly from ProFamtableInstanceVisit() has been
    // observed to crash inside Toolkit for unloaded instances.  We only copy
    // the row name here and do optional child-model retrieval after the visitor
    // has returned.
    ctx->instance_names.emplace_back(inst->name);
    return PRO_TK_NO_ERROR;
}

void TryDiscoverChildLevelFromInstanceName(const std::wstring &instance_name,
                                           DiscoverCtx &ctx)
{
    if (ctx.workspace == nullptr || ctx.visited == nullptr || ctx.parent_index >= ctx.workspace->level_nodes.size()) {
        return;
    }

    core::FtLevelNode &parent = ctx.workspace->level_nodes[ctx.parent_index];
    const std::wstring parent_level_path = parent.level_path;
    const std::wstring parent_generic_name = parent.generic_name;
    ProName inst_name = {0};
    wcsncpy_s(inst_name, instance_name.c_str(), _TRUNCATE);

    ProFaminstance inst = {};
    ProError st = ProFaminstanceInit(inst_name, &parent.famtable, &inst);
    if (st != PRO_TK_NO_ERROR) {
        FtLog(ctx.workspace, parent_level_path, L"WARN", L"discover-child", L"ProFaminstanceInit failed for " + instance_name, st);
        return;
    }

    ProMdl inst_mdl = nullptr;
    st = ProFaminstanceMdlGet(&inst, &inst_mdl);
    if (st != PRO_TK_NO_ERROR || inst_mdl == nullptr) {
        if (!IsDeepDiscoveryEnabled()) {
            return;
        }
        // Verified against Creo 10.0.8.0 docs:
        // - ProFaminstanceMdlGet: session-only model handle retrieval.
        // - ProFaminstanceRetrieve: retrieve instance from disk.
        st = ProFaminstanceRetrieve(&inst, &inst_mdl);
        if (st != PRO_TK_NO_ERROR || inst_mdl == nullptr) {
            FtLog(ctx.workspace,
                  parent_level_path,
                  L"WARN",
                  L"discover-child",
                  L"ProFaminstanceRetrieve failed for " + instance_name,
                  st == PRO_TK_NO_ERROR ? PRO_TK_GENERAL_ERROR : st);
            return;
        }
    }

    ProFamtable child_ft = {};
    if (!HasUsableFamtable(inst_mdl, &child_ft)) {
        return;
    }

    const std::wstring child_name(instance_name);
    const std::wstring parent_path = parent_level_path;
    const std::wstring child_path = MakeLevelPath(parent_path, child_name);
    size_t child_index = 0;
    DiscoverLevel(inst_mdl,
                  child_path,
                  parent_generic_name,
                  child_name,
                  *ctx.workspace,
                  *ctx.visited,
                  &child_index);
    if (child_index < ctx.workspace->level_nodes.size()) {
        auto &children = ctx.workspace->level_nodes[ctx.parent_index].children;
        if (std::find(children.begin(), children.end(), child_index) == children.end()) {
            children.push_back(child_index);
        }
    }
}

void DiscoverLevel(ProMdl generic,
                   const std::wstring &level_path,
                   const std::wstring &parent_generic,
                   const std::wstring &parent_instance,
                   core::FtWorkspace &workspace,
                   std::set<std::wstring> &visited,
                   size_t *new_index_out)
{
    if (new_index_out != nullptr) {
        *new_index_out = workspace.level_nodes.size();
    }
    if (generic == nullptr) {
        return;
    }

    const std::wstring key = ModelKey(generic);
    if (visited.find(key) != visited.end()) {
        FtLog(workspace, level_path, L"WARN", L"discover", L"Cycle or duplicate generic skipped: " + key, PRO_TK_NO_ERROR);
        return;
    }
    visited.insert(key);

    core::FtLevelNode level;
    level.level_path = level_path.empty() ? L"TOP" : level_path;
    level.level_depth = static_cast<int>(std::count(level.level_path.begin(), level.level_path.end(), L'/'));
    level.generic_name = SafeModelName(generic);
    level.parent_generic_name = parent_generic;
    level.parent_instance_name = parent_instance;
    level.model_type = autobbox::creo::ModelType(generic);
    level.generic_mdl = generic;
    level.has_family_table = HasUsableFamtable(generic, &level.famtable);
    if (level.has_family_table) {
        ProBoolean can_modify = PRO_B_FALSE;
        if (ProFamtableIsModifiable(&level.famtable, PRO_B_FALSE, &can_modify) == PRO_TK_NO_ERROR) {
            level.famtable_modifiable = can_modify == PRO_B_TRUE;
        }
    }

    const size_t my_index = workspace.level_nodes.size();
    workspace.level_nodes.push_back(level);
    if (new_index_out != nullptr) {
        *new_index_out = my_index;
    }

    if (!workspace.level_nodes[my_index].has_family_table) {
        return;
    }

    DiscoverCtx ctx;
    ctx.workspace = &workspace;
    ctx.parent_index = my_index;
    ctx.visited = &visited;
    const ProError visit_status = ProFamtableInstanceVisit(&workspace.level_nodes[my_index].famtable, VisitInstanceForDiscovery, nullptr, &ctx);
    if (visit_status != PRO_TK_NO_ERROR) {
        FtLog(workspace, workspace.level_nodes[my_index].level_path, L"WARN", L"discover", L"ProFamtableInstanceVisit failed", visit_status);
        return;
    }

    if (!IsDeepDiscoveryEnabled() && !ctx.instance_names.empty()) {
        FtLog(workspace,
              workspace.level_nodes[my_index].level_path,
              L"INFO",
              L"discover",
              L"Deep child-level disk retrieval is disabled by AUTO_BBOX_FT_DEEP_DISCOVERY=0",
              PRO_TK_NO_ERROR);
    }

    for (const std::wstring &instance_name : ctx.instance_names) {
        TryDiscoverChildLevelFromInstanceName(instance_name, ctx);
    }
}

} // namespace

ProError DiscoverFamilyTableWorkspace(ProMdl current, core::FtWorkspace &workspace)
{
    workspace.level_nodes.clear();
    workspace.original_snapshot.clear();
    workspace.known_leaf_level_paths.clear();
    workspace.diff_result = core::FtDiff{};
    workspace.support_matrix = BuildDefaultFtSupportMatrix();
    workspace.logs.clear();
    workspace.dirty = false;

    if (current == nullptr) {
        FtLog(workspace, L"", L"ERROR", L"discover", L"Current model is null", PRO_TK_BAD_INPUTS);
        return PRO_TK_BAD_INPUTS;
    }

    ProMdl top_generic = nullptr;
    ProError st = ProFaminstanceGenericGet(current, PRO_B_FALSE, &top_generic);
    if (st != PRO_TK_NO_ERROR || top_generic == nullptr) {
        top_generic = current;
    }

    std::set<std::wstring> visited;
    size_t top_index = 0;
    DiscoverLevel(top_generic, L"TOP", L"", L"", workspace, visited, &top_index);
    if (!workspace.level_nodes.empty()) {
        workspace.active_level_path = workspace.level_nodes.front().level_path;
    }
    FtLog(workspace, workspace.active_level_path, L"INFO", L"discover", L"Discovered family-table levels=" + std::to_wstring(workspace.level_nodes.size()), PRO_TK_NO_ERROR);
    return PRO_TK_NO_ERROR;
}

ProError DiscoverFamilyTableWorkspaceDeep(ProMdl current, core::FtWorkspace &workspace)
{
    const bool previous = g_force_deep_discovery;
    g_force_deep_discovery = true;
    const ProError st = DiscoverFamilyTableWorkspace(current, workspace);
    g_force_deep_discovery = previous;
    return st;
}

} // namespace autobbox::application
