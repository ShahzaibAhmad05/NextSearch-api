#include "api_metadata.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace cord19 {

static std::vector<std::string> csv_row(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inq = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (c == '"') {
            inq = !inq;
            continue;
        }
        if (!inq && c == ',') {
            out.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

static inline std::string trim_copy(std::string s) {
    auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_ws((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && is_ws((unsigned char)s.back())) s.pop_back();
    return s;
}

static std::string first_author_et_al(const std::string& authors_raw) {
    std::string s = trim_copy(authors_raw);
    if (s.empty()) return "";

    // Take first author chunk (CORD-19 commonly uses ';' separators)
    size_t semi = s.find(';');
    std::string first = (semi == std::string::npos) ? s : s.substr(0, semi);
    first = trim_copy(first);

    // Remove trailing commas/spaces
    while (!first.empty() && (first.back() == ',' || std::isspace((unsigned char)first.back()))) {
        first.pop_back();
    }
    first = trim_copy(first);
    if (first.empty()) return "";

    // If formatted like: "(Jun Oda), 織田 順" → use the romanized name inside parentheses
    if (!first.empty() && first.front() == '(') {
        size_t close = first.find(')');
        if (close != std::string::npos && close > 1) {
            std::string inside = first.substr(1, close - 1);
            inside = trim_copy(inside);
            if (!inside.empty()) first = inside;
        }
    }

    // Now derive a surname
    std::string surname;

    // Case: "Pfaller, Michael A" → surname before comma
    size_t comma = first.find(',');
    if (comma != std::string::npos) {
        surname = trim_copy(first.substr(0, comma));
    } else {
        // Case: "Jun Oda" → last token
        std::string tmp = trim_copy(first);
        size_t sp = tmp.find_last_of(" \t");
        surname = (sp == std::string::npos) ? tmp : trim_copy(tmp.substr(sp + 1));
    }

    surname = trim_copy(surname);
    if (surname.empty()) return "";

    return surname + " et al.";
}

void load_metadata_uid_meta(const fs::path& metadata_csv, std::unordered_map<std::string, MetaInfo>& uid_to_meta) {
    std::ifstream in(metadata_csv);
    if (!in) {
        std::cerr << "[metadata] FAILED open: " << metadata_csv.string() << "\n";
        return;
    }

    std::string header;
    if (!std::getline(in, header)) {
        std::cerr << "[metadata] FAILED read header\n";
        return;
    }

    auto cols = csv_row(header);
    int uid_i = -1, url_i = -1, pub_i = -1, auth_i = -1;
    for (int i = 0; i < (int)cols.size(); i++) {
        if (cols[i] == "cord_uid") uid_i = i;
        if (cols[i] == "url") url_i = i;
        if (cols[i] == "publish_time") pub_i = i;
        if (cols[i] == "authors") auth_i = i;
    }

    std::cerr << "[metadata] columns=" << cols.size() << " uid_i=" << uid_i << " url_i=" << url_i
              << " pub_i=" << pub_i << " auth_i=" << auth_i << "\n";

    if (uid_i < 0 || url_i < 0 || pub_i < 0 || auth_i < 0) {
        std::cerr << "[metadata] missing required columns in header\n";
        return;
    }

    std::string line;
    size_t loaded = 0, bad = 0;
    while (std::getline(in, line)) {
        auto r = csv_row(line);
        if ((int)r.size() <= std::max({uid_i, url_i, pub_i, auth_i})) {
            bad++;
            continue;
        }

        auto uid = r[uid_i];
        auto url = r[url_i];
        auto pub = r[pub_i];
        auto auth = r[auth_i];

        if (uid.empty()) continue;

        // CORD-19 may contain multiple rows per uid; keep first non-empty values
        auto& entry = uid_to_meta[uid];
        if (entry.url.empty() && !url.empty()) entry.url = url;
        if (entry.publish_time.empty() && !pub.empty()) entry.publish_time = pub;

        // Convert raw authors -> "Surname et al."
        if (entry.author.empty() && !auth.empty()) {
            entry.author = first_author_et_al(auth);
        }

        loaded++;
    }

    std::cerr << "[metadata] loaded=" << loaded << " bad_rows=" << bad << " map_size=" << uid_to_meta.size()
              << "\n";
}

} // namespace cord19
