#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "api_types.hpp"

namespace cord19 {

std::vector<std::string> load_manifest(const fs::path& manifest_path);
void save_manifest(const fs::path& manifest_path, const std::vector<std::string>& segs);

std::string seg_name(uint32_t id);

bool load_segment(const fs::path& segdir, Segment& s);

// For /add_document (single-doc segment creation)
void write_barrelized_index_files_single_doc(
    const fs::path& segdir,
    const std::vector<std::string>& id_to_term,
    const std::vector<std::pair<uint32_t, uint32_t>>& fwd
);

} // namespace cord19
