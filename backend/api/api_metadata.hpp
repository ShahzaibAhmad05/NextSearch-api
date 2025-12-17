#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include "api_types.hpp"

namespace cord19 {

// Load metadata.csv into a map keyed by cord_uid.
void load_metadata_uid_meta(
    const fs::path& metadata_csv,
    std::unordered_map<std::string, MetaInfo>& uid_to_meta
);

} // namespace cord19
