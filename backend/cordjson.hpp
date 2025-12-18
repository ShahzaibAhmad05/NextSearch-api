#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include "third_party/nlohmann/json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

// Read entire file content into a string
inline std::string read_file_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    return std::string(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>()
    );
}

// Extract searchable text fields from CORD-19 JSON
inline std::string extract_text_from_cord_json(const json& j) {
    std::string out;

    // Append simple string fields
    auto append_field = [&](const char* key) {
        if (j.contains(key) && j[key].is_string()) {
            out += j[key].get<std::string>();
            out.push_back('\n');
        }
    };

    append_field("title");

    // Append text from section arrays
    auto append_sections = [&](const char* key) {
        if (!j.contains(key) || !j[key].is_array()) return;
        for (auto& sec : j[key]) {
            if (sec.contains("text") && sec["text"].is_string()) {
                out += sec["text"].get<std::string>();
                out.push_back('\n');
            }
        }
    };

    append_sections("abstract");
    append_sections("body_text");

    return out;
}
