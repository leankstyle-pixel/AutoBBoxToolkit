#pragma once

#include "autobbox/core/random_color_types.h"

#include <ProToolkit.h>

#include <functional>
#include <string>
#include <vector>

namespace autobbox::application {

std::wstring ResolveDefaultRandomColorLibraryPath();

std::vector<core::RandomColorEntry> LoadRandomColorEntriesFromLibraryPath(
    const std::wstring &library_path,
    std::wstring &error_text);

struct RandomColorLibraryAppendResult {
    std::wstring actual_library_path;
    int added_count = 0;
    int duplicate_count = 0;
    int no_current_appearance_count = 0;
    int mixed_appearance_count = 0;
    bool used_fallback_library = false;
    bool write_failed = false;
};

RandomColorLibraryAppendResult AppendCurrentAppearancesToLibrary(
    const std::vector<core::RandomColorCandidate> &candidates,
    const std::wstring &preferred_library_path,
    const std::function<void(const std::string &line)> &log_sink);

struct RandomColorParameterWriteResult {
    int success_count = 0;
    int created_count = 0;
    int updated_count = 0;
    int failure_count = 0;
    int no_current_appearance_count = 0;
    int mixed_appearance_count = 0;
};

RandomColorParameterWriteResult WriteCurrentAppearanceColorParameters(
    const std::vector<core::RandomColorCandidate> &candidates,
    const std::function<void(const std::string &line)> &log_sink);

void RefreshRandomColorCandidateAppearances(std::vector<core::RandomColorCandidate> &candidates);

std::vector<core::RandomColorCandidate> CollectRandomColorCandidates(ProBoolean parts,
                                                                     ProBoolean assemblies,
                                                                     ProBoolean top_level_only);

std::vector<core::RandomColorAssignment> BuildRandomColorAssignments(
    const std::vector<core::RandomColorCandidate> &candidates,
    const std::vector<core::RandomColorEntry> &colors);

std::vector<core::RandomColorParameterPreview> BuildParameterColorPreview(
    const std::vector<core::RandomColorCandidate> &candidates,
    const std::vector<core::RandomColorEntry> &library_colors,
    const std::wstring &parameter_name);

bool ApplyRandomColors(const std::vector<core::RandomColorAssignment> &assignments,
                       std::wstring &summary_text,
                       const std::function<void(const std::string &line)> &log_sink);

bool ApplyParameterColors(const std::vector<core::RandomColorParameterPreview> &previews,
                          std::wstring &summary_text,
                          const std::function<void(const std::string &line)> &log_sink);

bool ClearRandomColors(const std::vector<core::RandomColorCandidate> &targets,
                       std::wstring &summary_text,
                       const std::function<void(const std::string &line)> &log_sink);

} // namespace autobbox::application
