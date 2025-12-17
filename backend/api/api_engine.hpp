#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "api_types.hpp"

namespace cord19 {

struct Engine {
    fs::path index_dir;
    std::vector<std::string> seg_names;
    std::vector<Segment> segments;

    std::unordered_map<std::string, MetaInfo> uid_to_meta;

    std::mutex mtx;

    bool reload();
    json search(const std::string& query, int k);
};

} // namespace cord19
