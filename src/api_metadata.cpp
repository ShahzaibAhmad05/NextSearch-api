#include "api_metadata.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace cord19 {

// Parse a CSV line into individual columns
static std::vector<std::string> csv_row(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inq = false;

    // Iterate over each character in the line
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];

        // Toggle quoted section
        if (c == '"') {
            inq = !inq;
            continue;
        }

        // Split on comma only if not inside quotes
        if (!inq && c == ',') {
            out.push_back(cur);
            cur.clear();
            continue;
        }

        // Append character to current column
        cur.push_back(c);
    }

    // Push last column
    out.push_back(cur);
    return out;
}

// Trim whitespace from both ends of a string
static inline std::string trim_copy(std::string s) {
    auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };

    // Remove leading spaces
    while (!s.empty() && is_ws((unsigned char)s.front()))
        s.erase(s.begin());

    // Remove trailing spaces
    while (!s.empty() && is_ws((unsigned char)s.back()))
        s.pop_back();

    return s;
}

// Extract first author surname and append "et al."
static std::string first_author_et_al(const std::string& authors_raw) {
    std::string s = trim_copy(authors_raw);
    if (s.empty()) return "";

    // Take first author before semicolon
    size_t semi = s.find(';');
    std::string first = (semi == std::string::npos) ? s : s.substr(0, semi);
    first = trim_copy(first);

    // Clean trailing commas and spaces
    while (!first.empty() &&
          (first.back() == ',' || std::isspace((unsigned char)first.back()))) {
        first.pop_back();
    }
    first = trim_copy(first);
    if (first.empty()) return "";

    // Handle romanized name inside parentheses
    if (!first.empty() && first.front() == '(') {
        size_t close = first.find(')');
        if (close != std::string::npos && close > 1) {
            std::string inside = first.substr(1, close - 1);
            inside = trim_copy(inside);
            if (!inside.empty()) first = inside;
        }
    }

    std::string surname;

    // If comma exists, surname is before comma
    size_t comma = first.find(',');
    if (comma != std::string::npos) {
        surname = trim_copy(first.substr(0, comma));
    } else {
        // Otherwise take last word as surname
        std::string tmp = trim_copy(first);
        size_t sp = tmp.find_last_of(" \t");
        surname = (sp == std::string::npos)
                    ? tmp
                    : trim_copy(tmp.substr(sp + 1));
    }

    surname = trim_copy(surname);
    if (surname.empty()) return "";

    return surname + " et al.";
}

// Load metadata CSV byte positions and map cord_uid to file positions
void load_metadata_uid_meta(const fs::path& metadata_csv,
                            std::unordered_map<std::string, MetaInfo>& uid_to_meta) {

    // Open metadata CSV file
    std::ifstream in(metadata_csv, std::ios::binary);
    if (!in) {
        std::cerr << "[metadata] FAILED open: "
                  << metadata_csv.string() << "\n";
        return;
    }

    // Read header line and track its position
    std::string header;
    uint64_t current_pos = 0;
    
    if (!std::getline(in, header)) {
        std::cerr << "[metadata] FAILED read header\n";
        return;
    }
    
    // Move past header (including newline)
    current_pos = in.tellg();

    // Parse column names
    auto cols = csv_row(header);
    int uid_i = -1;

    // Identify cord_uid column index
    for (int i = 0; i < (int)cols.size(); i++) {
        if (cols[i] == "cord_uid") uid_i = i;
    }

    // Validate required column
    if (uid_i < 0) {
        std::cerr << "[metadata] missing cord_uid column in header\n";
        return;
    }

    std::string line;
    size_t loaded = 0, bad = 0;

    // Read metadata rows and store byte positions
    while (std::getline(in, line)) {
        uint64_t line_start = current_pos;
        uint32_t line_length = static_cast<uint32_t>(line.length() + 1); // +1 for newline
        
        auto r = csv_row(line);

        // Skip malformed rows
        if ((int)r.size() <= uid_i) {
            bad++;
            current_pos += line_length;
            continue;
        }

        auto uid = r[uid_i];
        if (uid.empty()) {
            current_pos += line_length;
            continue;
        }

        // Store byte position for this cord_uid (only first occurrence)
        if (uid_to_meta.find(uid) == uid_to_meta.end()) {
            MetaInfo& entry = uid_to_meta[uid];
            entry.file_offset = line_start;
            entry.row_length = line_length;
            loaded++;
        }
        
        current_pos += line_length;
    }

    // Print loading summary
    std::cerr << "[metadata] loaded=" << loaded
              << " bad_rows=" << bad
              << " map_size=" << uid_to_meta.size() << "\n";
}

// Fetch full metadata from file on-demand using stored byte position
MetaData fetch_metadata(const fs::path& metadata_csv, const MetaInfo& meta_info) {
    MetaData result;
    
    // Open file and seek to the stored position
    std::ifstream in(metadata_csv, std::ios::binary);
    if (!in) {
        std::cerr << "[metadata] FAILED to open file for fetch: "
                  << metadata_csv.string() << "\n";
        return result;
    }
    
    // Seek to the row position
    in.seekg(meta_info.file_offset);
    
    // Read the row
    std::string line;
    if (!std::getline(in, line)) {
        std::cerr << "[metadata] FAILED to read row at offset: "
                  << meta_info.file_offset << "\n";
        return result;
    }
    
    // Parse the CSV row
    auto r = csv_row(line);
    
    // Read header to get column indices (cache this in production for better performance)
    in.seekg(0);
    std::string header;
    if (!std::getline(in, header)) {
        std::cerr << "[metadata] FAILED to read header\n";
        return result;
    }
    
    auto cols = csv_row(header);
    int url_i = -1, pub_i = -1, auth_i = -1, title_i = -1, abstract_i = -1;
    
    for (int i = 0; i < (int)cols.size(); i++) {
        if (cols[i] == "url") url_i = i;
        if (cols[i] == "publish_time") pub_i = i;
        if (cols[i] == "authors") auth_i = i;
        if (cols[i] == "title") title_i = i;
        if (cols[i] == "abstract") abstract_i = i;
    }
    
    // Extract fields
    if (url_i >= 0 && (int)r.size() > url_i)
        result.url = r[url_i];
    
    if (pub_i >= 0 && (int)r.size() > pub_i)
        result.publish_time = r[pub_i];
    
    if (auth_i >= 0 && (int)r.size() > auth_i)
        result.author = first_author_et_al(r[auth_i]);
    
    if (title_i >= 0 && (int)r.size() > title_i)
        result.title = r[title_i];
    
    if (abstract_i >= 0 && (int)r.size() > abstract_i)
        result.abstract = r[abstract_i];
    
    return result;
}

} // namespace cord19
