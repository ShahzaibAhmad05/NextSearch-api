#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "barrels.hpp"
#include "third_party/nlohmann/json.hpp"

namespace cord19 {

namespace fs = std::filesystem;
using json = nlohmann::json;

struct DocInfo {
    std::string cord_uid;
    std::string title;
    std::string json_relpath;
    uint32_t doc_len = 0;
};

struct LexEntry {
    uint32_t termId = 0;
    uint32_t df = 0;
    uint64_t offset = 0;
    uint32_t count = 0;
    uint32_t barrelId = 0; // used only when barrels enabled
};

// Store byte positions in metadata.csv file for on-demand loading
struct MetaInfo {
    uint64_t file_offset = 0;  // Byte position where this row starts in metadata.csv
    uint32_t row_length = 0;   // Length of the row in bytes
};

// Full metadata loaded on-demand from file
struct MetaData {
    std::string url;
    std::string publish_time; // "YYYY-MM-DD" string
    std::string author;       // display: "Smith et al."
    std::string title;
    std::string abstract;
};

struct Segment {
    fs::path dir;
    uint32_t N = 0;
    float avgdl = 0.0f;
    std::vector<DocInfo> docs;
    std::unordered_map<std::string, LexEntry> lex;

    // legacy
    std::ifstream inv;

    // barrels
    bool use_barrels = false;
    BarrelParams barrel_params{};
    std::vector<std::ifstream> inv_barrels;
};

} // namespace cord19
