#pragma once

#include <string>
#include <vector>

namespace autobbox::application {

struct AfxLibrarySearchItem {
    std::wstring display_name;
    std::wstring file_name;
    std::wstring relative_directory;
    std::wstring absolute_path;
    std::vector<std::string> directory_nodes;
    std::vector<std::wstring> directory_labels;
    std::string list_item_name;
    std::wstring searchable_text;
};

struct AfxLibrarySearchResult {
    std::vector<AfxLibrarySearchItem> matches;
};

std::wstring DefaultAfxEquipmentRoot();
AfxLibrarySearchResult SearchAfxEquipmentLibrary(const std::wstring &query);

} // namespace autobbox::application
