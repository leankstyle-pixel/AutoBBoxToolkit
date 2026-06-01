#include "autobbox/ui/model_structure_analyzer_dialog.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/ui/message_dialog.h"

#include <ProArray.h>
#include <ProUILabel.h>
#include <ProUIPushbutton.h>
#include <ProUITable.h>
#include <ProUIDialog.h>
#include <ProUIMessage.h>
#include <ProUtil.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace autobbox::ui {

namespace {

using autobbox::application::ModelStructureReport;

struct DialogConfig {
    const char *dialog = "autobbox_model_structure_analyzer_inst";
    const char *resource = "autobbox_model_structure_analyzer";
    const char *summary_label = "SummaryLabel";
    const char *node_table = "NodeTable";
    const char *detail_label = "DetailLabel";
    const char *detail_table = "DetailTable";
    const char *status_label = "StatusLabel";
    const char *overview_btn = "OverviewBtn";
    const char *constraints_btn = "ConstraintsBtn";
    const char *params_btn = "ParamsBtn";
    const char *family_btn = "FamilyBtn";
    const char *relations_btn = "RelationsBtn";
    const char *features_btn = "FeaturesBtn";
    const char *dims_btn = "DimsBtn";
    const char *refs_btn = "RefsBtn";
    const char *save_btn = "SaveJsonBtn";
    const char *refresh_btn = "RefreshBtn";
    const char *close_btn = "CloseBtn";
    int status_close = 0;
    int status_refresh = 2;
};

enum class DetailMode {
    Overview,
    Constraints,
    Parameters,
    Family,
    Relations,
    Features,
    Dimensions,
    References
};

struct DialogState {
    const ModelStructureReport *report = nullptr;
    std::unordered_map<std::string, size_t> node_row_to_index;
    size_t selected_node = 0;
    DetailMode mode = DetailMode::Overview;
    std::wstring default_json_path;
};

struct DialogRuntime {
    const DialogConfig *config = nullptr;
    DialogState *state = nullptr;
    std::function<void(const std::string &line)> log_sink;
};

void LogLine(const std::function<void(const std::string &line)> &log_sink, const char *fmt, ...)
{
    if (!log_sink || fmt == nullptr) return;
    char buffer[2048] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_sink(buffer);
}

ProError TryCreateDialog(const DialogConfig &config,
                         const std::function<void(const std::string &line)> &log_sink)
{
    const std::string base = config.resource;
    const std::vector<std::string> rel = {
        base,
        base + ".res",
        "resource\\" + base,
        "resource\\" + base + ".res",
        "text\\resource\\" + base,
        "text\\resource\\" + base + ".res",
    };
    ProError last = PRO_TK_GENERAL_ERROR;
    for (const std::string &candidate : rel) {
        last = ProUIDialogCreate(const_cast<char *>(config.dialog),
                                 const_cast<char *>(candidate.c_str()));
        LogLine(log_sink, "model-structure-dialog create try=%s status=%d", candidate.c_str(), static_cast<int>(last));
        if (last == PRO_TK_NO_ERROR) return last;
    }

    ProPath text_root_w = {0};
    if (ProToolkitApplTextPathGet(text_root_w) == PRO_TK_NO_ERROR) {
        const std::string text_root = autobbox::common::WToA(text_root_w);
        const std::vector<std::string> abs = {
            text_root + "\\resource\\" + base,
            text_root + "\\resource\\" + base + ".res",
            text_root + "\\text\\resource\\" + base,
            text_root + "\\text\\resource\\" + base + ".res",
        };
        for (const std::string &candidate : abs) {
            if (!autobbox::common::FileExistsA(candidate)) continue;
            last = ProUIDialogCreate(const_cast<char *>(config.dialog),
                                     const_cast<char *>(candidate.c_str()));
            LogLine(log_sink, "model-structure-dialog create try=%s status=%d", candidate.c_str(), static_cast<int>(last));
            if (last == PRO_TK_NO_ERROR) return last;
        }
    }
    return last;
}

std::wstring BoolText(bool value)
{
    return value ? L"YES" : L"NO";
}

std::wstring StatusText(ProError st)
{
    return L"st=" + std::to_wstring(static_cast<int>(st));
}

std::wstring Truncate(const std::wstring &value, size_t max_chars)
{
    if (value.size() <= max_chars) return value;
    return value.substr(0, max_chars > 1 ? max_chars - 1 : max_chars) + L"…";
}

std::wstring MatrixText(const double m[4][4])
{
    wchar_t buf[256] = {0};
    std::swprintf(buf,
                  sizeof(buf) / sizeof(buf[0]),
                  L"[%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f]",
                  m[0][0], m[0][1], m[0][2], m[0][3],
                  m[1][0], m[1][1], m[1][2], m[1][3],
                  m[2][0], m[2][1], m[2][2], m[2][3]);
    return buf;
}

std::wstring DefaultJsonPath(const ModelStructureReport &report)
{
    std::wstring dir = autobbox::common::CurrentWorkingDirectoryW();
    if (dir.empty()) {
        dir = autobbox::common::ResolveUserStateDirectoryW();
    }
    const std::wstring out_dir = autobbox::common::JoinPath(dir, L"model_structure");
    autobbox::common::EnsureDirectoryW(out_dir);
    std::wstring stem = report.root_name.empty() ? L"model" : report.root_name;
    for (wchar_t &ch : stem) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' ||
            ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|') {
            ch = L'_';
        }
    }
    return autobbox::common::JoinPath(out_dir, (stem + L"_semantic_model_structure.json").c_str());
}

void SetStatus(const DialogConfig &config, const std::wstring &text)
{
    ProUILabelTextSet(const_cast<char *>(config.dialog),
                      const_cast<char *>(config.status_label),
                      const_cast<wchar_t *>(text.c_str()));
}

void SetTable(const DialogConfig &config,
              const char *table,
              const std::vector<std::string> &column_names_storage,
              const std::vector<std::wstring> &column_labels_storage,
              const std::vector<int> &column_widths,
              const std::vector<std::vector<std::wstring>> &rows)
{
    std::vector<char *> column_names;
    std::vector<wchar_t *> column_labels;
    for (const std::string &name : column_names_storage) column_names.push_back(const_cast<char *>(name.c_str()));
    for (const std::wstring &label : column_labels_storage) column_labels.push_back(const_cast<wchar_t *>(label.c_str()));

    std::vector<std::string> row_names_storage;
    std::vector<std::wstring> row_labels_storage;
    row_names_storage.reserve(rows.size());
    row_labels_storage.reserve(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        row_names_storage.push_back("r" + std::to_string(i));
        row_labels_storage.emplace_back();
    }
    std::vector<char *> row_names;
    std::vector<wchar_t *> row_labels;
    for (std::string &name : row_names_storage) row_names.push_back(const_cast<char *>(name.c_str()));
    for (std::wstring &label : row_labels_storage) row_labels.push_back(const_cast<wchar_t *>(label.c_str()));

    char *dialog = const_cast<char *>(config.dialog);
    char *table_name = const_cast<char *>(table);
    ProUITableColumnnamesSet(dialog, table_name, static_cast<int>(column_names.size()), column_names.empty() ? nullptr : column_names.data());
    ProUITableColumnlabelsSet(dialog, table_name, static_cast<int>(column_labels.size()), column_labels.empty() ? nullptr : column_labels.data());
    ProUITableColumnwidthsSet(dialog, table_name, static_cast<int>(column_widths.size()), const_cast<int *>(column_widths.data()));
    ProUITableRownamesSet(dialog, table_name, static_cast<int>(row_names.size()), row_names.empty() ? nullptr : row_names.data());
    ProUITableRowlabelsSet(dialog, table_name, static_cast<int>(row_labels.size()), row_labels.empty() ? nullptr : row_labels.data());
    ProUITableSelectionpolicySet(dialog, table_name, PROUISELPOLICY_SINGLE);
    ProUITableColumnselectionpolicySet(dialog, table_name, PROUISELPOLICY_NONE);
    ProUITableShowgridSet(dialog, table_name, PRO_B_TRUE);
    ProUITableAutohighlightEnable(dialog, table_name);
    ProUITableVisiblerowsSet(dialog, table_name, std::min(18, std::max(4, static_cast<int>(rows.size()) + 1)));
    ProUITableMinrowsSet(dialog, table_name, std::min(14, std::max(4, static_cast<int>(rows.size()))));

    for (size_t r = 0; r < rows.size(); ++r) {
        for (size_t c = 0; c < column_names_storage.size(); ++c) {
            const std::wstring value = c < rows[r].size() ? rows[r][c] : L"";
            ProUITableCellLabelSet(dialog,
                                   table_name,
                                   const_cast<char *>(row_names_storage[r].c_str()),
                                   const_cast<char *>(column_names_storage[c].c_str()),
                                   const_cast<wchar_t *>(value.c_str()));
        }
    }
}

void PopulateNodeTable(const DialogConfig &config, DialogState &state)
{
    std::vector<std::vector<std::wstring>> rows;
    state.node_row_to_index.clear();
    if (state.report != nullptr) {
        for (size_t i = 0; i < state.report->nodes.size(); ++i) {
            const auto &node = state.report->nodes[i];
            std::wstring indent(static_cast<size_t>(std::max(0, node.depth)) * 2, L' ');
            rows.push_back({
                indent + node.model_name,
                node.model_type_label,
                L"P" + std::to_wstring(node.parameter_count) +
                    L" C" + std::to_wstring(node.constraint_count) +
                    L" F" + std::to_wstring(node.family_row_count) +
                    L" R" + std::to_wstring(node.relation_line_count)
            });
            state.node_row_to_index["r" + std::to_string(i)] = i;
        }
    }
    SetTable(config,
             config.node_table,
             {"MODEL", "TYPE", "COUNTS"},
             {L"模型结构", L"类型", L"统计"},
             {28, 8, 16},
             rows);
}

const autobbox::application::ModelStructureFamilyTable *FindFamilyTable(const ModelStructureReport &report, int node_index)
{
    for (const auto &ft : report.family_tables) {
        if (ft.node_index == node_index) return &ft;
    }
    return nullptr;
}

const autobbox::application::ModelStructureRelationSet *FindRelationSet(const ModelStructureReport &report, int node_index)
{
    for (const auto &rs : report.relation_sets) {
        if (rs.node_index == node_index) return &rs;
    }
    return nullptr;
}

const autobbox::application::ModelStructureNode *FindNode(const ModelStructureReport &report, int node_index)
{
    for (const auto &node : report.nodes) {
        if (node.index == node_index) return &node;
    }
    return nullptr;
}

bool NodeInSelectedSubtree(const ModelStructureReport &report, size_t selected, int node_index)
{
    if (selected >= report.nodes.size()) return false;
    const auto *candidate = FindNode(report, node_index);
    if (candidate == nullptr) return false;
    const auto &root = report.nodes[selected];
    if (root.occurrence_path == L"ROOT" || root.index == candidate->index) return true;
    const std::wstring prefix = root.occurrence_path + L"/";
    return candidate->occurrence_path.size() > prefix.size() &&
           candidate->occurrence_path.compare(0, prefix.size(), prefix) == 0;
}

void PopulateDetailTable(const DialogConfig &config, DialogState &state)
{
    if (state.report == nullptr || state.report->nodes.empty()) return;
    const size_t selected = std::min(state.selected_node, state.report->nodes.size() - 1);
    const auto &node = state.report->nodes[selected];
    const std::wstring title_prefix = node.model_name + L" / ";
    std::vector<std::vector<std::wstring>> rows;
    std::vector<std::string> names;
    std::vector<std::wstring> labels;
    std::vector<int> widths;

    if (state.mode == DetailMode::Overview) {
        ProUILabelTextSet(const_cast<char *>(config.dialog), const_cast<char *>(config.detail_label), const_cast<wchar_t *>((title_prefix + L"概览").c_str()));
        names = {"KEY", "VALUE"};
        labels = {L"项目", L"值"};
        widths = {24, 86};
        rows = {
            {L"模型名", node.model_name},
            {L"模型类型", node.model_type_label},
            {L"显示名", node.display_name},
            {L"自定义名", node.custom_name.empty() ? L"-" : node.custom_name},
            {L"单位系统", node.unit_system + L" / " + node.length_unit + L" / " + node.mass_unit},
            {L"组件路径", node.occurrence_path},
            {L"模型文件路径", node.model_path},
            {L"组件特征 ID", std::to_wstring(node.component_feature_id)},
            {L"父装配", node.parent_assembly.empty() ? L"-" : node.parent_assembly},
            {L"层级", std::to_wstring(node.depth)},
            {L"是否抑制", BoolText(node.suppressed)},
            {L"是否隐藏", BoolText(node.hidden)},
            {L"是否族表实例", BoolText(node.family_instance)},
            {L"是否重复装配", BoolText(node.duplicate_assembly)},
            {L"封装 packaged", BoolText(node.packaged) + L" (" + StatusText(node.packaged_status) + L")"},
            {L"欠约束 underconstrained", BoolText(node.underconstrained) + L" (" + StatusText(node.underconstrained_status) + L")"},
            {L"冻结 frozen", BoolText(node.frozen) + L" (" + StatusText(node.frozen_status) + L")"},
            {L"变换矩阵", node.transform_status == PRO_TK_NO_ERROR ? MatrixText(node.transform) : StatusText(node.transform_status)},
            {L"参数数", std::to_wstring(node.parameter_count)},
            {L"族表", node.has_family_table ? (std::to_wstring(node.family_column_count) + L" 列 / " + std::to_wstring(node.family_row_count) + L" 行") : L"无"},
            {L"特征 / 尺寸", std::to_wstring(node.feature_count) + L" / " + std::to_wstring(node.dimension_count)},
            {L"关系式行", std::to_wstring(node.relation_line_count)}
        };
        if (state.report->selected_occurrence.has_selection_path &&
            (selected == 0 || node.occurrence_path == state.report->selected_occurrence.occurrence_path)) {
            const auto &occ = state.report->selected_occurrence;
            rows.push_back({L"选中项上下文根装配", occ.context_root_model.empty() ? L"-" : occ.context_root_model});
            rows.push_back({L"选中项父装配", occ.parent_assembly.empty() ? L"-" : occ.parent_assembly});
            rows.push_back({L"选中项装配路径", occ.occurrence_path});
            rows.push_back({L"选中项父路径", occ.parent_occurrence_path});
            rows.push_back({L"选中项装配约束数", std::to_wstring(state.report->selected_occurrence_constraints.size())});
        }
    } else if (state.mode == DetailMode::Constraints) {
        ProUILabelTextSet(const_cast<char *>(config.dialog), const_cast<char *>(config.detail_label), const_cast<wchar_t *>((title_prefix + L"装配约束").c_str()));
        names = {"IDX", "TYPE", "ASMREF", "COMPREF", "OFFSET", "ATTR", "STATUS"};
        labels = {L"#", L"类型", L"装配参考", L"元件参考", L"偏移", L"属性", L"状态"};
        widths = {5, 15, 30, 30, 12, 10, 12};
        for (const auto &c : state.report->constraints) {
            if (c.node_index != static_cast<int>(selected)) continue;
            rows.push_back({
                c.index < 0 ? L"-" : std::to_wstring(c.index),
                c.type_label.empty() ? StatusText(c.read_status) : c.type_label,
                Truncate(c.asm_reference, 42),
                Truncate(c.comp_reference, 42),
                c.offset_status == PRO_TK_NO_ERROR ? std::to_wstring(c.offset) : StatusText(c.offset_status),
                c.attributes_status == PRO_TK_NO_ERROR ? std::to_wstring(c.attributes) : StatusText(c.attributes_status),
                StatusText(c.read_status)
            });
        }
    } else if (state.mode == DetailMode::Parameters) {
        ProUILabelTextSet(const_cast<char *>(config.dialog), const_cast<char *>(config.detail_label), const_cast<wchar_t *>((title_prefix + L"参数").c_str()));
        names = {"NAME", "TYPE", "VALUE", "OWNER", "FLAGS", "DESC"};
        labels = {L"名称", L"类型", L"值", L"所在对象", L"状态", L"说明"};
        widths = {24, 10, 34, 16, 30, 28};
        for (const auto &p : state.report->parameters) {
            if (p.node_index != static_cast<int>(selected)) continue;
            std::wstring flags;
            flags += p.exists ? L"已有 " : L"缺失 ";
            flags += p.writable ? L"可改 " : L"不可改 ";
            if (p.designated) flags += L"指定 ";
            if (p.table_driven) flags += L"族表 ";
            if (p.driver != PRO_PARAMDRIVER_NONE) flags += L"驱动 ";
            if (p.lock_status != PRO_PARAMLOCKSTATUS_UNLOCKED) flags += L"锁定 ";
            rows.push_back({p.name, p.type_label, Truncate(p.value, 48), p.owner_scope, flags, Truncate(p.description, 40)});
        }
    } else if (state.mode == DetailMode::Family) {
        ProUILabelTextSet(const_cast<char *>(config.dialog), const_cast<char *>(config.detail_label), const_cast<wchar_t *>((title_prefix + L"族表：当前节点及子层级").c_str()));
        names = {"MODEL", "INSTANCE", "COLUMN", "TYPE", "VALUE", "FLAGS"};
        labels = {L"节点模型", L"实例", L"列", L"类型", L"值", L"标记"};
        widths = {22, 22, 22, 12, 34, 40};
        bool any_table = false;
        for (const auto &ft : state.report->family_tables) {
            if (!NodeInSelectedSubtree(*state.report, selected, ft.node_index)) continue;
            any_table = true;
            const auto *ft_node = FindNode(*state.report, ft.node_index);
            const std::wstring node_label =
                ft_node == nullptr ? (L"#" + std::to_wstring(ft.node_index)) :
                                      (ft_node->model_name + L" [" + ft_node->occurrence_path + L"]");
            std::wstring table_flags = L"表模型=" + (ft.table_model_name.empty() ? L"-" : ft.table_model_name) +
                                       L" 层=" + std::to_wstring(ft.generation);
            if (ft.source_is_generic) table_flags += L" 泛型";
            if (!ft.immediate_generic_name.empty()) table_flags += L" immediate=" + ft.immediate_generic_name;
            if (!ft.top_generic_name.empty()) table_flags += L" top=" + ft.top_generic_name;
            if (!ft.has_family_table) {
                rows.push_back({node_label,
                                ft.selected_instance_name.empty() ? L"-" : ft.selected_instance_name,
                                ft.table_model_name.empty() ? L"-" : ft.table_model_name,
                                L"-",
                                L"无族表 / init " + StatusText(ft.init_status) + L" / check " + StatusText(ft.check_status),
                                table_flags});
                continue;
            }
            const size_t rows_before_table = rows.size();
            for (const auto &row : ft.rows) {
                std::wstring flags;
                flags += table_flags + L" ";
                if (row.locked) flags += L"锁定 ";
                if (row.ext_locked) flags += L"外部锁定 ";
                for (const auto &cell : row.cells) {
                    rows.push_back({node_label, row.instance_name, cell.column_key, std::to_wstring(static_cast<int>(cell.type)), Truncate(cell.value, 48), flags});
                }
            }
            if (rows.size() == rows_before_table) {
                for (const auto &col : ft.columns) {
                    rows.push_back({node_label, L"<无实例>", col.key, col.type_label, L"", table_flags});
                }
            }
        }
        if (!any_table) {
            rows.push_back({L"-", L"-", L"-", L"-", L"无族表", L""});
        }
    } else if (state.mode == DetailMode::Relations) {
        ProUILabelTextSet(const_cast<char *>(config.dialog), const_cast<char *>(config.detail_label), const_cast<wchar_t *>((title_prefix + L"关系式").c_str()));
        names = {"LINE", "KIND", "TEXT"};
        labels = {L"行号", L"类型", L"文本"};
        widths = {8, 16, 90};
        const auto *rs = FindRelationSet(*state.report, static_cast<int>(selected));
        if (rs == nullptr || !rs->has_main_relset) {
            rows.push_back({L"-", L"-", L"无主关系式集"});
        } else if (rs->read_status != PRO_TK_NO_ERROR) {
            rows.push_back({L"-", L"读取失败", StatusText(rs->read_status)});
        } else {
            for (const auto &line : rs->lines) {
                std::wstring kind = line.blank ? L"空行" : (line.comment ? L"注释" : (line.conditional ? L"条件" : L"关系式"));
                rows.push_back({std::to_wstring(line.line_number), kind, Truncate(line.text, 120)});
            }
        }
    } else if (state.mode == DetailMode::Features) {
        ProUILabelTextSet(const_cast<char *>(config.dialog), const_cast<char *>(config.detail_label), const_cast<wchar_t *>((title_prefix + L"特征").c_str()));
        names = {"ID", "NAME", "TYPE", "STATUS", "VISIBLE"};
        labels = {L"ID", L"特征名", L"特征类型", L"状态", L"可见"};
        widths = {8, 28, 28, 16, 10};
        for (const auto &f : state.report->features) {
            if (f.node_index != static_cast<int>(selected)) continue;
            rows.push_back({
                std::to_wstring(f.id),
                Truncate(f.name, 40),
                Truncate(f.type_name, 40),
                f.suppressed ? L"抑制" : L"正常",
                BoolText(f.visible)
            });
        }
    } else if (state.mode == DetailMode::Dimensions) {
        ProUILabelTextSet(const_cast<char *>(config.dialog), const_cast<char *>(config.detail_label), const_cast<wchar_t *>((title_prefix + L"尺寸").c_str()));
        names = {"FEAT", "ID", "SYMBOL", "VALUE", "UNIT", "FLAGS"};
        labels = {L"特征名", L"特征ID", L"尺寸符号", L"尺寸值", L"单位", L"标记"};
        widths = {28, 8, 18, 18, 14, 24};
        for (const auto &d : state.report->dimensions) {
            if (d.node_index != static_cast<int>(selected)) continue;
            std::wstring flags;
            if (d.pattern_quantity) flags += L"阵列数量 ";
            if (d.family_table_column) flags += L"族表列 ";
            rows.push_back({
                Truncate(d.feature_name, 40),
                std::to_wstring(d.feature_id),
                d.symbol,
                std::to_wstring(d.value),
                d.unit,
                flags
            });
        }
    } else {
        ProUILabelTextSet(const_cast<char *>(config.dialog), const_cast<char *>(config.detail_label), const_cast<wchar_t *>((title_prefix + L"参考").c_str()));
        names = {"SOURCE", "OWNER", "NAME", "TYPE", "UNIQUE"};
        labels = {L"来源", L"owner", L"参考名", L"参考类型", L"唯一"};
        widths = {20, 28, 22, 20, 10};
        for (const auto &r : state.report->references) {
            if (r.node_index != static_cast<int>(selected)) continue;
            rows.push_back({r.source, r.owner_model, r.name, r.type_label, BoolText(r.unique)});
        }
    }

    if (rows.empty()) {
        rows.push_back({L"-", L"无数据"});
    }
    SetTable(config, config.detail_table, names, labels, widths, rows);
    SetStatus(config, L"当前节点：" + node.model_name);
}

bool ResolveFocusedNode(char *dialog, DialogRuntime *runtime, size_t &index_out)
{
    if (dialog == nullptr || runtime == nullptr || runtime->config == nullptr || runtime->state == nullptr) {
        return false;
    }
    char *row = nullptr;
    char *column = nullptr;
    const ProError st = ProUITableFocusCellGet(dialog,
                                               const_cast<char *>(runtime->config->node_table),
                                               &row,
                                               &column);
    if (column != nullptr) ProStringFree(column);
    if (st == PRO_TK_NO_ERROR && row != nullptr) {
        const auto it = runtime->state->node_row_to_index.find(row);
        ProStringFree(row);
        if (it != runtime->state->node_row_to_index.end()) {
            runtime->state->selected_node = it->second;
            index_out = it->second;
            return true;
        }
    } else if (row != nullptr) {
        ProStringFree(row);
    }
    index_out = runtime->state->selected_node;
    return true;
}

void OnNodeSelect(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<DialogRuntime *>(app_data);
    size_t index = 0;
    if (ResolveFocusedNode(dialog, runtime, index)) {
        PopulateDetailTable(*runtime->config, *runtime->state);
    }
}

void SetModeAndRefresh(char *dialog, ProAppData app_data, DetailMode mode)
{
    auto *runtime = reinterpret_cast<DialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->config == nullptr || runtime->state == nullptr) return;
    size_t unused = 0;
    ResolveFocusedNode(dialog, runtime, unused);
    runtime->state->mode = mode;
    PopulateDetailTable(*runtime->config, *runtime->state);
}

void OnOverview(char *dialog, char *, ProAppData app_data) { SetModeAndRefresh(dialog, app_data, DetailMode::Overview); }
void OnConstraints(char *dialog, char *, ProAppData app_data) { SetModeAndRefresh(dialog, app_data, DetailMode::Constraints); }
void OnParams(char *dialog, char *, ProAppData app_data) { SetModeAndRefresh(dialog, app_data, DetailMode::Parameters); }
void OnFamily(char *dialog, char *, ProAppData app_data) { SetModeAndRefresh(dialog, app_data, DetailMode::Family); }
void OnRelations(char *dialog, char *, ProAppData app_data) { SetModeAndRefresh(dialog, app_data, DetailMode::Relations); }
void OnFeatures(char *dialog, char *, ProAppData app_data) { SetModeAndRefresh(dialog, app_data, DetailMode::Features); }
void OnDimensions(char *dialog, char *, ProAppData app_data) { SetModeAndRefresh(dialog, app_data, DetailMode::Dimensions); }
void OnReferences(char *dialog, char *, ProAppData app_data) { SetModeAndRefresh(dialog, app_data, DetailMode::References); }

void OnSaveJson(char *, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<DialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->config == nullptr || runtime->state == nullptr ||
        runtime->state->report == nullptr) {
        return;
    }
    std::string error;
    const bool ok = autobbox::application::SaveModelStructureJson(
        *runtime->state->report,
        runtime->state->default_json_path,
        error);
    if (ok) {
        SetStatus(*runtime->config, L"JSON 已保存：" + runtime->state->default_json_path);
        const std::wstring msg = L"JSON 已保存：\n" + runtime->state->default_json_path;
        ShowSimpleMessageDialog(PROUIMESSAGE_INFO, L"模型结构分析器", msg.c_str());
        LogLine(runtime->log_sink,
                "model-structure save-json path=%s",
                autobbox::common::WToA(runtime->state->default_json_path.c_str()).c_str());
    } else {
        const std::wstring msg = L"保存 JSON 失败：" + autobbox::common::AToW(error.c_str());
        SetStatus(*runtime->config, msg);
        ShowSimpleMessageDialog(PROUIMESSAGE_ERROR, L"模型结构分析器", msg.c_str());
        LogLine(runtime->log_sink, "model-structure save-json failed error=%s", error.c_str());
    }
}

void OnRefresh(char *dialog, char *, ProAppData)
{
    if (dialog != nullptr) ProUIDialogExit(dialog, DialogConfig().status_refresh);
}

void OnClose(char *dialog, char *, ProAppData)
{
    if (dialog != nullptr) ProUIDialogExit(dialog, DialogConfig().status_close);
}

} // namespace

ModelStructureAnalyzerDialogResult PromptModelStructureAnalyzerDialog(
    const autobbox::application::ModelStructureReport &report,
    const std::function<void(const std::string &line)> &log_sink)
{
    const DialogConfig config;
    const ProError create_st = TryCreateDialog(config, log_sink);
    if (create_st != PRO_TK_NO_ERROR) {
        ShowSimpleMessageDialog(PROUIMESSAGE_ERROR, L"模型结构分析器", L"打开模型结构分析器窗口失败。");
        return ModelStructureAnalyzerDialogResult::Closed;
    }

    DialogState state;
    state.report = &report;
    state.default_json_path = DefaultJsonPath(report);
    if (report.selected_occurrence.has_selection_path) {
        for (size_t i = 0; i < report.nodes.size(); ++i) {
            if (report.nodes[i].occurrence_path == report.selected_occurrence.occurrence_path) {
                state.selected_node = i;
                break;
            }
        }
    }
    DialogRuntime runtime;
    runtime.config = &config;
    runtime.state = &state;
    runtime.log_sink = log_sink;

    char *dialog = const_cast<char *>(config.dialog);
    ProUIDialogTitleSet(dialog, const_cast<wchar_t *>(L"模型结构分析器"));
    const std::wstring summary = autobbox::application::BuildModelStructureSummary(report);
    ProUILabelTextSet(dialog, const_cast<char *>(config.summary_label), const_cast<wchar_t *>(summary.c_str()));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.overview_btn), const_cast<wchar_t *>(L"概览"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.constraints_btn), const_cast<wchar_t *>(L"装配约束"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.params_btn), const_cast<wchar_t *>(L"参数"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.family_btn), const_cast<wchar_t *>(L"族表"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.relations_btn), const_cast<wchar_t *>(L"关系式"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.features_btn), const_cast<wchar_t *>(L"特征"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.dims_btn), const_cast<wchar_t *>(L"尺寸"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.refs_btn), const_cast<wchar_t *>(L"参考"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.save_btn), const_cast<wchar_t *>(L"保存 JSON"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.refresh_btn), const_cast<wchar_t *>(L"重新分析"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.close_btn), const_cast<wchar_t *>(L"关闭"));

    PopulateNodeTable(config, state);
    PopulateDetailTable(config, state);
    ProUITableSelectActionSet(dialog, const_cast<char *>(config.node_table), OnNodeSelect, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.overview_btn), OnOverview, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.constraints_btn), OnConstraints, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.params_btn), OnParams, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.family_btn), OnFamily, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.relations_btn), OnRelations, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.features_btn), OnFeatures, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.dims_btn), OnDimensions, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.refs_btn), OnReferences, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.save_btn), OnSaveJson, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.refresh_btn), OnRefresh, nullptr);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.close_btn), OnClose, nullptr);
    ProUIDialogCloseActionSet(dialog, OnClose, nullptr);

    int dialog_status = config.status_close;
    const ProError activate_st = ProUIDialogActivate(dialog, &dialog_status);
    LogLine(log_sink, "model-structure-dialog activate status=%d dialog_status=%d", static_cast<int>(activate_st), dialog_status);
    ProUIDialogDestroy(dialog);
    if (activate_st == PRO_TK_NO_ERROR && dialog_status == config.status_refresh) {
        return ModelStructureAnalyzerDialogResult::RequestRefresh;
    }
    return ModelStructureAnalyzerDialogResult::Closed;
}

} // namespace autobbox::ui
