#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include "api_types.hpp"

namespace cord19 {

// Load metadata.csv byte positions into a map keyed by cord_uid.
void load_metadata_uid_meta(
    const fs::path& metadata_csv,
    std::unordered_map<std::string, MetaInfo>& uid_to_meta
);

// Fetch metadata for a specific cord_uid from file on-demand
MetaData fetch_metadata(
    const fs::path& metadata_csv,
    const MetaInfo& meta_info
);

} // namespace cord19
