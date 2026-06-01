#pragma once

#include "autobbox/core/quick_simprep_types.h"

#include <functional>
#include <string>
#include <vector>

namespace autobbox::application {

core::QuickSimprepCollectResult CollectQuickSimprepCategories();

bool CreateQuickSimpreps(std::vector<core::QuickSimprepCategory> &categories,
                         core::QuickSimprepCreateMode mode,
                         core::QuickSimprepCreateSummary &summary,
                         const std::function<void(const std::string &line)> &log_sink);

std::wstring BuildQuickSimprepCollectSummary(const core::QuickSimprepCollectResult &result);

core::QuickSimprepExistingRepsResult EnumerateExistingSimpreps();

bool AddCategoriesToExistingRep(
    const std::vector<core::QuickSimprepCategory> &categories,
    const core::QuickSimprepExistingRep &target_rep,
    core::QuickSimprepCreateSummary &summary,
    const std::function<void(const std::string &line)> &log_sink);

bool DeleteCategoriesFromRep(
    const core::QuickSimprepExistingRep &rep,
    const std::vector<std::wstring> &category_names,
    core::QuickSimprepManageSummary &summary,
    const std::function<void(const std::string &line)> &log_sink);

bool UpdateCategoriesInRep(
    const core::QuickSimprepExistingRep &rep,
    const std::vector<std::wstring> &category_names,
    core::QuickSimprepManageSummary &summary,
    const std::function<void(const std::string &line)> &log_sink);

bool RenameSimprep(
    const core::QuickSimprepExistingRep &rep,
    const std::wstring &new_name,
    core::QuickSimprepManageSummary &summary,
    const std::function<void(const std::string &line)> &log_sink);

std::wstring BuildManageSummaryText(const core::QuickSimprepManageSummary &summary);

} // namespace autobbox::application
