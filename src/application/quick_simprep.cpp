#include "autobbox/application/quick_simprep.h"

#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/creo/parameter_api.h"

#include <ProAsmcomp.h>
#include <ProAsmcomppath.h>
#include <ProFeatType.h>
#include <ProFeature.h>
#include <ProMdl.h>
#include <ProSimprep.h>
#include <ProSimprepdata.h>
#include <ProSolid.h>
#include <ProToolkit.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cwctype>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace autobbox::application {

namespace {

constexpr const wchar_t *kCommonNameParam = L"PTC_COMMON_NAME";

void LogLine(const std::function<void(const std::string &line)> &log_sink, const char *fmt, ...)
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

std::wstring TrimText(const std::wstring &value)
{
    size_t begin = 0;
    while (begin < value.size() && std::iswspace(value[begin]) != 0) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::iswspace(value[end - 1]) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::wstring UppercaseAscii(const std::wstring &value)
{
    std::wstring out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](wchar_t ch) {
        if (ch >= L'a' && ch <= L'z') {
            return static_cast<wchar_t>(ch - L'a' + L'A');
        }
        return ch;
    });
    return out;
}

std::wstring MdlTypeLabel(ProMdlType type)
{
    switch (type) {
    case PRO_MDL_PART:
        return L"PRT";
    case PRO_MDL_ASSEMBLY:
        return L"ASM";
    default:
        return L"MDL";
    }
}

struct CollectCtx {
    ProSolid owner = nullptr;
    core::QuickSimprepCollectResult *result = nullptr;
    std::map<std::wstring, size_t> category_index_by_common_name;
};

ProError CollectTopLevelFeatureVisitAction(ProFeature *feature,
                                           ProError status,
                                           ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || feature == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    auto *ctx = reinterpret_cast<CollectCtx *>(app_data);
    if (ctx == nullptr || ctx->owner == nullptr || ctx->result == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    ProBoolean visible = PRO_B_FALSE;
    if (ProFeatureVisibilityGet(feature, &visible) != PRO_TK_NO_ERROR || visible != PRO_B_TRUE) {
        return PRO_TK_NO_ERROR;
    }

    ProFeattype feat_type = 0;
    if (ProFeatureTypeGet(feature, &feat_type) != PRO_TK_NO_ERROR || feat_type != PRO_FEAT_COMPONENT) {
        return PRO_TK_NO_ERROR;
    }

    ProMdl mdl = nullptr;
    ProAsmcomp asmcomp = *feature;
    if (ProAsmcompMdlGet(&asmcomp, &mdl) != PRO_TK_NO_ERROR || mdl == nullptr) {
        ++ctx->result->skipped_unreadable_common_name;
        return PRO_TK_NO_ERROR;
    }
    if (!autobbox::creo::IsPartOrAsm(mdl)) {
        return PRO_TK_NO_ERROR;
    }

    ProIdTable comp_id_table = {0};
    comp_id_table[0] = feature->id;
    ProAsmcomppath comp_path = {};
    if (ProAsmcomppathInit(ctx->owner, comp_id_table, 1, &comp_path) != PRO_TK_NO_ERROR) {
        ++ctx->result->skipped_unreadable_common_name;
        return PRO_TK_NO_ERROR;
    }

    ++ctx->result->direct_component_count;

    std::wstring common_name;
    if (!autobbox::creo::ReadStringParamOnModel(mdl, kCommonNameParam, common_name)) {
        ++ctx->result->skipped_unreadable_common_name;
        return PRO_TK_NO_ERROR;
    }

    common_name = TrimText(common_name);
    if (common_name.empty()) {
        ++ctx->result->skipped_missing_common_name;
        return PRO_TK_NO_ERROR;
    }

    size_t category_index = 0;
    const auto found = ctx->category_index_by_common_name.find(common_name);
    if (found == ctx->category_index_by_common_name.end()) {
        core::QuickSimprepCategory category;
        category.item_name = "qsr_" + std::to_string(ctx->result->categories.size());
        category.common_name = common_name;
        category.status_text = L"\u5f85\u521b\u5efa";
        ctx->result->categories.push_back(category);
        category_index = ctx->result->categories.size() - 1;
        ctx->category_index_by_common_name.emplace(common_name, category_index);
    } else {
        category_index = found->second;
    }

    core::QuickSimprepOccurrence occurrence;
    occurrence.path = comp_path;
    occurrence.feat_id = feature->id;
    occurrence.mdl = mdl;
    occurrence.type = autobbox::creo::ModelType(mdl);
    occurrence.model_name = autobbox::creo::ModelName(mdl, L"");
    ctx->result->categories[category_index].occurrences.push_back(occurrence);
    ++ctx->result->grouped_component_count;
    return PRO_TK_NO_ERROR;
}

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

struct FindSimprepByNameCtx {
    std::wstring wanted_upper;
    bool matched = false;
    ProSimprep found = {};
};

ProError FindSimprepByNameAction(ProGeomitem *handle, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || handle == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    auto *ctx = reinterpret_cast<FindSimprepByNameCtx *>(app_data);
    ProSimprep *simprep = reinterpret_cast<ProSimprep *>(handle);
    std::wstring name;
    if (TryReadSimprepName(simprep, name) && UppercaseAscii(name) == ctx->wanted_upper) {
        ctx->found = *simprep;
        ctx->matched = true;
        return PRO_TK_E_FOUND;
    }
    return PRO_TK_NO_ERROR;
}

bool FindExistingSimprepByName(ProSolid owner, const std::wstring &rep_name, ProSimprep &rep_out)
{
    if (owner == nullptr || rep_name.empty()) {
        return false;
    }

    FindSimprepByNameCtx ctx;
    ctx.wanted_upper = UppercaseAscii(rep_name);
    const ProError visit_status = ProSolidSimprepVisit(owner, nullptr, FindSimprepByNameAction, &ctx);
    if ((visit_status == PRO_TK_E_FOUND || visit_status == PRO_TK_NO_ERROR) && ctx.matched) {
        rep_out = ctx.found;
        return true;
    }
    return false;
}

bool IsAllowedNameChar(wchar_t ch)
{
    if (ch == L'\0' || std::iswcntrl(ch) != 0) {
        return false;
    }

    switch (ch) {
    case L'\\':
    case L'/':
    case L':':
    case L'*':
    case L'?':
    case L'"':
    case L'<':
    case L'>':
    case L'|':
    case L',':
    case L';':
        return false;
    default:
        return true;
    }
}

std::wstring SanitizeRepNameBase(const std::wstring &label)
{
    ProName probe = {0};
    const size_t max_chars = (sizeof(probe) / sizeof(probe[0])) - 1;
    const size_t reserve_for_suffix = 8;
    const size_t max_body = max_chars > reserve_for_suffix ? max_chars - reserve_for_suffix : max_chars;

    std::wstring out;
    for (wchar_t ch : label) {
        if (out.size() >= max_body) {
            break;
        }
        out.push_back(IsAllowedNameChar(ch) ? ch : L'_');
    }

    while (!out.empty() && (out.back() == L'_' || std::iswspace(out.back()) != 0)) {
        out.pop_back();
    }
    while (!out.empty() && (out.front() == L'_' || std::iswspace(out.front()) != 0)) {
        out.erase(out.begin());
    }
    if (out.empty()) {
        out = L"PTC_COMMON_NAME";
    }
    if (out.size() > max_body) {
        out.resize(max_body);
    }
    return out;
}

std::wstring TruncateRepName(const std::wstring &base)
{
    ProName probe = {0};
    const size_t max_chars = (sizeof(probe) / sizeof(probe[0])) - 1;
    std::wstring clean_base = base.empty() ? L"PTC_COMMON_NAME" : base;
    if (clean_base.size() > max_chars) {
        clean_base.resize(max_chars);
    }
    return clean_base;
}

std::wstring BuildMergedRepNameBase(const std::vector<core::QuickSimprepCategory> &categories)
{
    std::wstring label;
    for (const core::QuickSimprepCategory &category : categories) {
        if (!category.selected) {
            continue;
        }
        if (!label.empty()) {
            label += L"_";
        }
        label += category.common_name;
    }
    return SanitizeRepNameBase(label);
}

void CopyWToProName(ProName dest, const std::wstring &src)
{
    const size_t capacity = sizeof(ProName) / sizeof(wchar_t);
    if (capacity == 0) {
        return;
    }
    size_t i = 0;
    while (i + 1 < capacity && i < src.size()) {
        dest[i] = src[i];
        ++i;
    }
    dest[i] = L'\0';
}

ProError CreateOrUpdateRep(ProSolid owner,
                           const std::wstring &rep_name,
                           const std::vector<core::QuickSimprepOccurrence> &occurrences,
                           ProSimprep *result_rep,
                           bool *updated_existing)
{
    if (owner == nullptr || occurrences.empty()) {
        return PRO_TK_BAD_INPUTS;
    }
    if (updated_existing != nullptr) {
        *updated_existing = false;
    }

    ProName name = {0};
    CopyWToProName(name, rep_name);

    ProSimprepdata *data = nullptr;
    ProError st = ProSimprepdataAlloc(name, PRO_B_FALSE, PRO_SIMPREP_EXCLUDE, &data);
    if (st != PRO_TK_NO_ERROR || data == nullptr) {
        return st;
    }

    ProSimprepAction include_action = {};
    st = ProSimprepActionInit(PRO_SIMPREP_INCLUDE, nullptr, &include_action);
    if (st == PRO_TK_NO_ERROR) {
        for (const core::QuickSimprepOccurrence &occurrence : occurrences) {
            ProAsmcomppath path = occurrence.path;
            if (path.table_num != 1) {
                continue;
            }

            ProSimprepitem item = {};
            st = ProSimprepdataitemInit(
                path.comp_id_table,
                path.table_num,
                PRO_VALUE_UNUSED,
                &include_action,
                &item);
            if (st != PRO_TK_NO_ERROR) {
                break;
            }

            st = ProSimprepdataitemAdd(data, &item);
            if (st != PRO_TK_NO_ERROR) {
                break;
            }
        }
    }

    if (st == PRO_TK_NO_ERROR) {
        ProSimprep existing = {};
        if (FindExistingSimprepByName(owner, rep_name, existing)) {
            st = ProSimprepdataSet(&existing, data);
            if (result_rep != nullptr) {
                *result_rep = existing;
            }
            if (updated_existing != nullptr && st == PRO_TK_NO_ERROR) {
                *updated_existing = true;
            }
        } else {
            ProSimprep rep = {};
            st = ProSimprepCreate(owner, data, &rep);
            if (result_rep != nullptr) {
                *result_rep = rep;
            }
        }
    }

    ProSimprepdataFree(&data);
    return st;
}

std::wstring FormatStatus(ProError status)
{
    return L"status=" + std::to_wstring(static_cast<int>(status));
}

std::vector<core::QuickSimprepOccurrence> MergeSelectedOccurrences(
    const std::vector<core::QuickSimprepCategory> &categories)
{
    std::vector<core::QuickSimprepOccurrence> merged;
    for (const core::QuickSimprepCategory &category : categories) {
        if (!category.selected) {
            continue;
        }
        merged.insert(merged.end(), category.occurrences.begin(), category.occurrences.end());
    }
    return merged;
}

void FinalizeSummary(core::QuickSimprepCreateSummary &summary)
{
    summary.summary_text =
        L"\u8bf7\u6c42\u5904\u7406\uff1a" + std::to_wstring(summary.requested) +
        L"\n\u65b0\u5efa\uff1a" + std::to_wstring(summary.created) +
        L"\n\u66f4\u65b0\uff1a" + std::to_wstring(summary.updated) +
        L"\n\u5931\u8d25\uff1a" + std::to_wstring(summary.failed) +
        L"\n\u5305\u542b\u76f4\u63a5\u7ec4\u4ef6\uff1a" + std::to_wstring(summary.included_count);
    for (const core::QuickSimprepCreatedRep &rep : summary.reps) {
        summary.summary_text += L"\n";
        summary.summary_text += rep.status == PRO_TK_NO_ERROR ? L"\u2713 " : L"\u2717 ";
        if (rep.status == PRO_TK_NO_ERROR) {
            summary.summary_text += rep.updated ? L"\u66f4\u65b0 " : L"\u65b0\u5efa ";
        }
        summary.summary_text += rep.source_label;
        summary.summary_text += L" -> ";
        summary.summary_text += rep.rep_name.empty() ? FormatStatus(rep.status) : rep.rep_name;
    }
}


struct SimprepItemScan {
    ProSimprepActionType default_action = PRO_SIMPREP_EXCLUDE;
    std::unordered_map<int, ProSimprepActionType> action_by_top_feat_id;
    int visited_items = 0;
};

bool DefaultActionIncludes(ProSimprepActionType action)
{
    return action == PRO_SIMPREP_INCLUDE;
}

bool SimprepActionIncludes(ProSimprepActionType action, ProSimprepActionType default_action)
{
    switch (action) {
    case PRO_SIMPREP_INCLUDE:
    case PRO_SIMPREP_SUBSTITUTE:
    case PRO_SIMPREP_GEOM:
    case PRO_SIMPREP_GRAPHICS:
    case PRO_SIMPREP_SYMB:
    case PRO_SIMPREP_BOUNDBOX:
    case PRO_SIMPREP_DEFENV:
    case PRO_SIMPREP_LIGHT_GRAPH:
    case PRO_SIMPREP_AUTO:
        return true;
    case PRO_SIMPREP_REVERSE:
        return !DefaultActionIncludes(default_action);
    case PRO_SIMPREP_EXCLUDE:
    case PRO_SIMPREP_NONE:
    default:
        return false;
    }
}

ProError ScanSimprepItemAction(ProSimprepitem *item, ProError status, ProAppData app_data)
{
    if (status != PRO_TK_NO_ERROR || item == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    auto *scan = reinterpret_cast<SimprepItemScan *>(app_data);
    ++scan->visited_items;
    if (item->item_path.path_size == 1) {
        const int feat_id = item->item_path.comp_path[0];
        if (feat_id != PRO_VALUE_UNUSED) {
            scan->action_by_top_feat_id[feat_id] = item->action.type;
        }
    }
    return PRO_TK_NO_ERROR;
}

int ApplySimprepSelectionToCategories(ProSimprep *simprep,
                                      std::vector<core::QuickSimprepCategory> &categories)
{
    for (core::QuickSimprepCategory &category : categories) {
        category.selected = false;
        category.status_text = L"\u672a\u5305\u542b";
        category.has_error = false;
    }

    if (simprep == nullptr) {
        return 0;
    }

    ProSimprepdata *data = nullptr;
    if (ProSimprepdataGet(simprep, &data) != PRO_TK_NO_ERROR || data == nullptr) {
        return 0;
    }

    SimprepItemScan scan;
    ProSimprepdataDefltGet(data, &scan.default_action);
    ProSimprepdataitemsVisit(data, nullptr, (ProFunction)ScanSimprepItemAction, &scan);
    ProSimprepdataFree(&data);

    int included_total = 0;
    for (core::QuickSimprepCategory &category : categories) {
        int included_count = 0;
        for (const core::QuickSimprepOccurrence &occurrence : category.occurrences) {
            ProSimprepActionType action = scan.default_action;
            const auto found = scan.action_by_top_feat_id.find(occurrence.feat_id);
            if (found != scan.action_by_top_feat_id.end()) {
                action = found->second;
            }
            if (SimprepActionIncludes(action, scan.default_action)) {
                ++included_count;
            }
        }

        included_total += included_count;
        category.selected = included_count > 0;
        if (included_count <= 0) {
            category.status_text = L"\u672a\u5305\u542b";
        } else if (included_count == static_cast<int>(category.occurrences.size())) {
            category.status_text = L"\u5df2\u5305\u542b";
        } else {
            category.status_text = L"\u90e8\u5206\u5305\u542b " + std::to_wstring(included_count) +
                                   L"/" + std::to_wstring(static_cast<int>(category.occurrences.size()));
        }
    }
    return included_total;
}

std::unordered_set<std::wstring> UpperNameSet(const std::vector<std::wstring> &names)
{
    std::unordered_set<std::wstring> result;
    for (const std::wstring &name : names) {
        result.insert(UppercaseAscii(TrimText(name)));
    }
    return result;
}

ProError BuildDataFromCategoryNames(const std::wstring &rep_name,
                                    const std::vector<core::QuickSimprepCategory> &categories,
                                    const std::unordered_set<std::wstring> &included_names,
                                    ProSimprepdata **data_out,
                                    int &included_count)
{
    if (data_out == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }
    *data_out = nullptr;
    included_count = 0;

    ProName rep_name_w = {0};
    CopyWToProName(rep_name_w, rep_name);

    ProSimprepdata *new_data = nullptr;
    ProError st = ProSimprepdataAlloc(rep_name_w, PRO_B_FALSE, PRO_SIMPREP_EXCLUDE, &new_data);
    if (st != PRO_TK_NO_ERROR || new_data == nullptr) {
        return st;
    }

    ProSimprepAction include_action = {};
    st = ProSimprepActionInit(PRO_SIMPREP_INCLUDE, nullptr, &include_action);
    ProSimprepAction none_action = {};
    if (st == PRO_TK_NO_ERROR) {
        st = ProSimprepActionInit(PRO_SIMPREP_NONE, nullptr, &none_action);
    }
    if (st == PRO_TK_NO_ERROR) {
        for (const core::QuickSimprepCategory &category : categories) {
            const bool include_category =
                included_names.find(UppercaseAscii(category.common_name)) != included_names.end();
            ProSimprepAction *action = include_category ? &include_action : &none_action;
            for (const core::QuickSimprepOccurrence &occurrence : category.occurrences) {
                ProAsmcomppath path = occurrence.path;
                if (path.table_num != 1) {
                    continue;
                }

                ProSimprepitem item = {};
                st = ProSimprepdataitemInit(
                    path.comp_id_table,
                    path.table_num,
                    PRO_VALUE_UNUSED,
                    action,
                    &item);
                if (st != PRO_TK_NO_ERROR) {
                    break;
                }

                st = ProSimprepdataitemAdd(new_data, &item);
                if (st != PRO_TK_NO_ERROR) {
                    break;
                }
                if (include_category) {
                    ++included_count;
                }
            }
            if (st != PRO_TK_NO_ERROR) {
                break;
            }
        }
    }

    if (st != PRO_TK_NO_ERROR) {
        ProSimprepdataFree(&new_data);
        return st;
    }

    *data_out = new_data;
    return PRO_TK_NO_ERROR;
}

} // namespace

core::QuickSimprepCollectResult CollectQuickSimprepCategories()
{
    core::QuickSimprepCollectResult result;

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        autobbox::creo::ModelType(current) != PRO_MDL_ASSEMBLY) {
        return result;
    }

    CollectCtx ctx;
    ctx.owner = reinterpret_cast<ProSolid>(current);
    ctx.result = &result;
    ProSolidFeatVisit(
        ctx.owner,
        CollectTopLevelFeatureVisitAction,
        nullptr,
        &ctx);

    std::sort(result.categories.begin(), result.categories.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.common_name < rhs.common_name;
    });
    for (size_t i = 0; i < result.categories.size(); ++i) {
        result.categories[i].item_name = "qsr_" + std::to_string(i);
    }
    return result;
}

bool CreateQuickSimpreps(std::vector<core::QuickSimprepCategory> &categories,
                         core::QuickSimprepCreateMode mode,
                         core::QuickSimprepCreateSummary &summary,
                         const std::function<void(const std::string &line)> &log_sink)
{
    summary = {};

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        autobbox::creo::ModelType(current) != PRO_MDL_ASSEMBLY) {
        summary.summary_text = L"\u5f53\u524d\u6a21\u578b\u4e0d\u662f\u88c5\u914d\u3002";
        return false;
    }

    ProSolid owner = reinterpret_cast<ProSolid>(current);

    const int selected_count = static_cast<int>(std::count_if(
        categories.begin(),
        categories.end(),
        [](const core::QuickSimprepCategory &category) { return category.selected; }));
    if (selected_count <= 0) {
        summary.summary_text = L"\u8bf7\u81f3\u5c11\u9009\u62e9\u4e00\u4e2a PTC_COMMON_NAME \u5206\u7c7b\u3002";
        return false;
    }

    if (mode == core::QuickSimprepCreateMode::Merged) {
        const std::vector<core::QuickSimprepOccurrence> merged = MergeSelectedOccurrences(categories);
        summary.requested = 1;
        const std::wstring base_name = BuildMergedRepNameBase(categories);
        const std::wstring rep_name = TruncateRepName(base_name);
        ProSimprep rep = {};
        bool updated_existing = false;
        const ProError st = CreateOrUpdateRep(owner, rep_name, merged, &rep, &updated_existing);

        core::QuickSimprepCreatedRep created;
        created.source_label = L"\u5408\u5e76";
        created.rep_name = rep_name;
        created.included_count = static_cast<int>(merged.size());
        created.status = st;
        created.updated = updated_existing;
        summary.reps.push_back(created);

        if (st == PRO_TK_NO_ERROR) {
            if (updated_existing) {
                summary.updated = 1;
            } else {
                summary.created = 1;
            }
            summary.included_count = static_cast<int>(merged.size());
            for (core::QuickSimprepCategory &category : categories) {
                if (category.selected) {
                    category.status_text = (updated_existing ? L"\u5df2\u66f4\u65b0\u5408\u5e76\uff1a" : L"\u5df2\u65b0\u5efa\u5408\u5e76\uff1a") + rep_name;
                    category.has_error = false;
                }
            }
            LogLine(log_sink,
                    "quick-simprep %s merged rep=%s categories=%d occurrences=%d",
                    updated_existing ? "update" : "create",
                    autobbox::common::WToA(rep_name.c_str()).c_str(),
                    selected_count,
                    static_cast<int>(merged.size()));
        } else {
            summary.failed = 1;
            for (core::QuickSimprepCategory &category : categories) {
                if (category.selected) {
                    category.status_text = L"\u5904\u7406\u5931\u8d25\uff1a" + FormatStatus(st);
                    category.has_error = true;
                }
            }
            LogLine(log_sink,
                    "quick-simprep create-or-update merged failed rep=%s status=%d categories=%d occurrences=%d",
                    autobbox::common::WToA(rep_name.c_str()).c_str(),
                    static_cast<int>(st),
                    selected_count,
                    static_cast<int>(merged.size()));
        }
        FinalizeSummary(summary);
        return summary.failed == 0;
    }

    summary.requested = selected_count;
    for (core::QuickSimprepCategory &category : categories) {
        if (!category.selected) {
            continue;
        }

        const std::wstring base_name = SanitizeRepNameBase(category.common_name);
        const std::wstring rep_name = TruncateRepName(base_name);
        ProSimprep rep = {};
        bool updated_existing = false;
        const ProError st = CreateOrUpdateRep(owner, rep_name, category.occurrences, &rep, &updated_existing);

        core::QuickSimprepCreatedRep created;
        created.source_label = category.common_name;
        created.rep_name = rep_name;
        created.included_count = static_cast<int>(category.occurrences.size());
        created.status = st;
        created.updated = updated_existing;
        summary.reps.push_back(created);

        if (st == PRO_TK_NO_ERROR) {
            if (updated_existing) {
                ++summary.updated;
            } else {
                ++summary.created;
            }
            summary.included_count += static_cast<int>(category.occurrences.size());
            category.status_text = (updated_existing ? L"\u5df2\u66f4\u65b0\uff1a" : L"\u5df2\u65b0\u5efa\uff1a") + rep_name;
            category.has_error = false;
            LogLine(log_sink,
                    "quick-simprep %s category=%s rep=%s occurrences=%d",
                    updated_existing ? "update" : "create",
                    autobbox::common::WToA(category.common_name.c_str()).c_str(),
                    autobbox::common::WToA(rep_name.c_str()).c_str(),
                    static_cast<int>(category.occurrences.size()));
        } else {
            ++summary.failed;
            category.status_text = L"\u5904\u7406\u5931\u8d25\uff1a" + FormatStatus(st);
            category.has_error = true;
            LogLine(log_sink,
                    "quick-simprep create-or-update failed category=%s rep=%s status=%d occurrences=%d",
                    autobbox::common::WToA(category.common_name.c_str()).c_str(),
                    autobbox::common::WToA(rep_name.c_str()).c_str(),
                    static_cast<int>(st),
                    static_cast<int>(category.occurrences.size()));
        }
    }

    FinalizeSummary(summary);
    return summary.failed == 0;
}

std::wstring BuildQuickSimprepCollectSummary(const core::QuickSimprepCollectResult &result)
{
    std::wstring text =
        L"\u76f4\u63a5\u7ec4\u4ef6\uff1a" + std::to_wstring(result.direct_component_count) +
        L"  \u5df2\u5206\u7c7b\uff1a" + std::to_wstring(result.grouped_component_count) +
        L"  \u5206\u7c7b\u6570\uff1a" + std::to_wstring(result.categories.size());
    if (result.skipped_missing_common_name > 0 || result.skipped_unreadable_common_name > 0) {
        text += L"  \u8df3\u8fc7\uff1a" +
                std::to_wstring(result.skipped_missing_common_name + result.skipped_unreadable_common_name);
    }
    return text;
}

core::QuickSimprepExistingRepsResult EnumerateExistingSimpreps()
{
    core::QuickSimprepExistingRepsResult result;

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        autobbox::creo::ModelType(current) != PRO_MDL_ASSEMBLY) {
        return result;
    }

    ProSolid owner = reinterpret_cast<ProSolid>(current);
    core::QuickSimprepCollectResult collect = CollectQuickSimprepCategories();

    ProSimprep active_rep = {};
    std::wstring active_name;
    if (ProSimprepActiveGet(owner, &active_rep) == PRO_TK_NO_ERROR) {
        TryReadSimprepName(&active_rep, active_name);
    }
    const std::wstring active_upper = UppercaseAscii(active_name);

    struct EnumCtx {
        core::QuickSimprepExistingRepsResult *result = nullptr;
        const core::QuickSimprepCollectResult *collect = nullptr;
        std::wstring active_upper;
    };

    EnumCtx ctx;
    ctx.result = &result;
    ctx.collect = &collect;
    ctx.active_upper = active_upper;

    auto visit_action = [](ProGeomitem *handle, ProError status, ProAppData app_data) -> ProError {
        if (status != PRO_TK_NO_ERROR || handle == nullptr || app_data == nullptr) {
            return PRO_TK_NO_ERROR;
        }

        auto *ectx = reinterpret_cast<EnumCtx *>(app_data);
        ProSimprep *simprep = reinterpret_cast<ProSimprep *>(handle);

        ProSimprepType type = PRO_SIMPREP_MASTER_REP;
        if (ProSimprepTypeGet(simprep, &type) != PRO_TK_NO_ERROR || type != PRO_SIMPREP_USER_DEFINED) {
            return PRO_TK_NO_ERROR;
        }

        std::wstring name;
        if (!TryReadSimprepName(simprep, name) || name.empty()) {
            return PRO_TK_NO_ERROR;
        }

        core::QuickSimprepExistingRep existing;
        existing.rep_name = name;
        existing.handle = *simprep;
        existing.type = type;
        existing.is_active = !ectx->active_upper.empty() && UppercaseAscii(name) == ectx->active_upper;
        existing.categories = ectx->collect->categories;
        existing.item_count = ApplySimprepSelectionToCategories(&existing.handle, existing.categories);

        ectx->result->reps.push_back(existing);
        return PRO_TK_NO_ERROR;
    };

    ProSolidSimprepVisit(owner, nullptr, visit_action, &ctx);

    std::sort(result.reps.begin(), result.reps.end(), [](const auto &lhs, const auto &rhs) {
        if (lhs.is_active != rhs.is_active) {
            return lhs.is_active;
        }
        return lhs.rep_name < rhs.rep_name;
    });

    result.total_count = static_cast<int>(result.reps.size());
    return result;
}

bool AddCategoriesToExistingRep(
    const std::vector<core::QuickSimprepCategory> &categories,
    const core::QuickSimprepExistingRep &target_rep,
    core::QuickSimprepCreateSummary &summary,
    const std::function<void(const std::string &line)> &log_sink)
{
    summary = {};

    std::vector<std::wstring> final_names;
    for (const core::QuickSimprepCategory &category : target_rep.categories) {
        if (category.selected) {
            final_names.push_back(category.common_name);
        }
    }
    for (const core::QuickSimprepCategory &category : categories) {
        if (category.selected) {
            const std::wstring upper = UppercaseAscii(category.common_name);
            const bool exists = std::any_of(final_names.begin(), final_names.end(), [&](const std::wstring &name) {
                return UppercaseAscii(name) == upper;
            });
            if (!exists) {
                final_names.push_back(category.common_name);
            }
        }
    }

    core::QuickSimprepManageSummary manage_summary;
    const bool ok = UpdateCategoriesInRep(target_rep, final_names, manage_summary, log_sink);

    summary.requested = 1;
    summary.updated = ok ? 1 : 0;
    summary.failed = ok ? 0 : 1;
    summary.summary_text = manage_summary.summary_text;
    return ok;
}

bool DeleteCategoriesFromRep(
    const core::QuickSimprepExistingRep &rep,
    const std::vector<std::wstring> &category_names,
    core::QuickSimprepManageSummary &summary,
    const std::function<void(const std::string &line)> &log_sink)
{
    std::unordered_set<std::wstring> remove_names = UpperNameSet(category_names);
    std::vector<std::wstring> keep_names;
    for (const core::QuickSimprepCategory &category : rep.categories) {
        if (category.selected && remove_names.find(UppercaseAscii(category.common_name)) == remove_names.end()) {
            keep_names.push_back(category.common_name);
        }
    }
    return UpdateCategoriesInRep(rep, keep_names, summary, log_sink);
}

bool UpdateCategoriesInRep(
    const core::QuickSimprepExistingRep &rep,
    const std::vector<std::wstring> &category_names,
    core::QuickSimprepManageSummary &summary,
    const std::function<void(const std::string &line)> &log_sink)
{
    summary = {};
    summary.total = 1;

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        autobbox::creo::ModelType(current) != PRO_MDL_ASSEMBLY) {
        summary.failed = 1;
        summary.summary_text = L"\u5f53\u524d\u6a21\u578b\u4e0d\u662f\u88c5\u914d\u3002";
        return false;
    }

    ProSimprep simprep_handle = rep.handle;
    std::wstring rep_name = rep.rep_name;
    ProSimprepdata *old_data = nullptr;
    if (ProSimprepdataGet(&simprep_handle, &old_data) == PRO_TK_NO_ERROR && old_data != nullptr) {
        ProName old_name = {0};
        if (ProSimprepdataNameGet(old_data, old_name) == PRO_TK_NO_ERROR && old_name[0] != L'\0') {
            rep_name.assign(old_name);
        }
        ProSimprepdataFree(&old_data);
    }

    core::QuickSimprepCollectResult collect = CollectQuickSimprepCategories();
    const std::unordered_set<std::wstring> included_names = UpperNameSet(category_names);

    int included_count = 0;
    ProSimprepdata *new_data = nullptr;
    ProError st = BuildDataFromCategoryNames(rep_name, collect.categories, included_names, &new_data, included_count);
    if (st == PRO_TK_NO_ERROR && new_data != nullptr) {
        st = ProSimprepdataSet(&simprep_handle, new_data);
    }
    if (new_data != nullptr) {
        ProSimprepdataFree(&new_data);
    }

    core::QuickSimprepManageResult result_entry;
    result_entry.rep_name = rep_name;
    result_entry.action = core::QuickSimprepManageAction::UpdateCategory;
    result_entry.status = st;
    if (st == PRO_TK_NO_ERROR) {
        summary.succeeded = 1;
        result_entry.message = L"\u5df2\u66f4\u65b0\uff1a\u5305\u542b " +
                               std::to_wstring(static_cast<int>(included_names.size())) +
                               L" \u4e2a\u5206\u7c7b\uff0c" +
                               std::to_wstring(included_count) + L" \u4e2a\u7ec4\u4ef6";
        LogLine(log_sink,
                "quick-simprep update-existing rep=%s categories=%d occurrences=%d",
                autobbox::common::WToA(rep_name.c_str()).c_str(),
                static_cast<int>(included_names.size()),
                included_count);
    } else {
        summary.failed = 1;
        result_entry.message = L"\u66f4\u65b0\u5931\u8d25\uff1a" + FormatStatus(st);
        LogLine(log_sink,
                "quick-simprep update-existing failed rep=%s status=%d",
                autobbox::common::WToA(rep_name.c_str()).c_str(),
                static_cast<int>(st));
    }
    summary.results.push_back(result_entry);
    summary.summary_text = BuildManageSummaryText(summary);
    return summary.failed == 0;
}

bool RenameSimprep(
    const core::QuickSimprepExistingRep &rep,
    const std::wstring &new_name,
    core::QuickSimprepManageSummary &summary,
    const std::function<void(const std::string &line)> &log_sink)
{
    summary = {};
    summary.total = 1;

    const std::wstring requested = TrimText(new_name);
    if (requested.empty()) {
        summary.failed = 1;
        summary.summary_text = L"\u65b0\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002";
        return false;
    }

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR || current == nullptr) {
        summary.failed = 1;
        summary.summary_text = L"\u65e0\u6cd5\u83b7\u53d6\u5f53\u524d\u6a21\u578b\u3002";
        return false;
    }

    const std::wstring sanitized = SanitizeRepNameBase(requested);
    const std::wstring truncated = TruncateRepName(sanitized);
    if (truncated.empty()) {
        summary.failed = 1;
        summary.summary_text = L"\u65b0\u540d\u79f0\u65e0\u6548\u3002";
        return false;
    }

    const std::wstring current_upper = UppercaseAscii(rep.rep_name);
    const std::wstring new_upper = UppercaseAscii(truncated);
    ProSimprep existing = {};
    if (new_upper != current_upper && FindExistingSimprepByName(reinterpret_cast<ProSolid>(current), truncated, existing)) {
        summary.failed = 1;
        summary.summary_text = L"\u540d\u79f0\u5df2\u5b58\u5728\uff1a" + truncated;
        return false;
    }

    ProSimprep simprep_handle = rep.handle;
    ProSimprepdata *data = nullptr;
    ProError st = ProSimprepdataGet(&simprep_handle, &data);
    if (st != PRO_TK_NO_ERROR || data == nullptr) {
        summary.failed = 1;
        summary.summary_text = L"\u8bfb\u53d6\u7b80\u5316\u8868\u793a\u6570\u636e\u5931\u8d25\u3002";
        return false;
    }

    ProName new_name_w = {0};
    CopyWToProName(new_name_w, truncated);
    st = ProSimprepdataNameSet(data, new_name_w);
    if (st == PRO_TK_NO_ERROR) {
        st = ProSimprepdataSet(&simprep_handle, data);
    }
    ProSimprepdataFree(&data);

    core::QuickSimprepManageResult result_entry;
    result_entry.rep_name = rep.rep_name;
    result_entry.action = core::QuickSimprepManageAction::Rename;
    result_entry.status = st;
    if (st == PRO_TK_NO_ERROR) {
        summary.succeeded = 1;
        result_entry.message = rep.rep_name + L" -> " + truncated;
        LogLine(log_sink,
                "quick-simprep rename rep=%s new=%s",
                autobbox::common::WToA(rep.rep_name.c_str()).c_str(),
                autobbox::common::WToA(truncated.c_str()).c_str());
    } else {
        summary.failed = 1;
        result_entry.message = L"\u91cd\u547d\u540d\u5931\u8d25\uff1a" + FormatStatus(st);
    }
    summary.results.push_back(result_entry);
    summary.summary_text = BuildManageSummaryText(summary);
    return summary.failed == 0;
}

std::wstring BuildManageSummaryText(const core::QuickSimprepManageSummary &summary)
{
    std::wstring text =
        L"\u64cd\u4f5c\u6570\uff1a" + std::to_wstring(summary.total) +
        L"  \u6210\u529f\uff1a" + std::to_wstring(summary.succeeded) +
        L"  \u5931\u8d25\uff1a" + std::to_wstring(summary.failed);
    for (const auto &res : summary.results) {
        text += L"\n";
        text += res.status == PRO_TK_NO_ERROR ? L"\u2713 " : L"\u2717 ";
        text += res.rep_name;
        if (!res.category_name.empty()) {
            text += L" / " + res.category_name;
        }
        text += L": " + res.message;
    }
    return text;
}

} // namespace autobbox::application

