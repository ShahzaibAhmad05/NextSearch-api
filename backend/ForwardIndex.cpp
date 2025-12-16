#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <algorithm>

#include "cordjson.hpp"
#include "textutil.hpp"
#include "indexio.hpp"

namespace fs = std::filesystem;

struct DocInfo {
    std::string cord_uid;
    std::string title;
    std::string json_relpath;
    uint32_t doc_len;
};

static std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> cols;
    std::string cur;
    bool in_quotes = false;
    for (char c : line) {
        if (c == '"') in_quotes = !in_quotes;
        else if (c == ',' && !in_quotes) { cols.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    cols.push_back(cur);
    return cols;
}

static std::string pick_first_path(const std::string& s) {
    size_t pos = s.find(';');
    std::string first = (pos == std::string::npos) ? s : s.substr(0, pos);
    while (!first.empty() && (first.back()==' ' || first.back()=='\r')) first.pop_back();
    while (!first.empty() && first.front()==' ') first.erase(first.begin());
    return first;
}

int main(int argc, char** argv) {
    if (argc < 3) {     // enforce cli args are provided correctly
        std::cerr << "Usage: forwardindex <CORD_ROOT> <SEGMENT_DIR>\n";
        return 1;
    }

    fs::path root = fs::path(argv[1]);
    fs::path seg  = fs::path(argv[2]);
    fs::create_directories(seg);

    fs::path meta = root / "metadata.csv";
    if (!fs::exists(meta)) {        // verify metadata exists
        std::cerr << "metadata.csv not found: " << meta << "\n";
        return 1;
    }

    std::ifstream in(meta);
    std::string header;
    std::getline(in, header);

    auto header_cols = split_csv_line(header);
    auto idx_of = [&](const std::string& name)->int{
        for (int i=0;i<(int)header_cols.size();i++) if (header_cols[i]==name) return i;
        return -1;
    };

    int i_uid   = idx_of("cord_uid");
    int i_title = idx_of("title");
    int i_pdf   = idx_of("pdf_json_files");
    int i_pmc   = idx_of("pmc_json_files");

    if (i_uid<0 || i_title<0 || i_pdf<0 || i_pmc<0) {
        std::cerr << "metadata.csv missing required columns.\n";
        return 1;
    }

    // term -> termId
    std::unordered_map<std::string, uint32_t> term_to_id;
    term_to_id.reserve(400000);
    std::vector<std::string> id_to_term;

    std::vector<DocInfo> docs;
    std::vector<std::vector<std::pair<uint32_t,uint32_t>>> forward; // doc -> (termId, tf)
    uint64_t total_len = 0;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto cols = split_csv_line(line);
        if ((int)cols.size() <= std::max({i_uid,i_title,i_pdf,i_pmc})) continue;

        std::string cord_uid = cols[i_uid];
        std::string title    = cols[i_title];

        std::string pmc_rel = pick_first_path(cols[i_pmc]);
        std::string pdf_rel = pick_first_path(cols[i_pdf]);
        std::string rel = !pmc_rel.empty() ? pmc_rel : pdf_rel;
        if (rel.empty()) continue;

        fs::path json_path = root / fs::path(rel);
        if (!fs::exists(json_path)) continue;

        std::string raw = read_file_all(json_path);
        if (raw.empty()) continue;

        json j;
        try { j = json::parse(raw); } catch(...) { continue; }

        std::string text = extract_text_from_cord_json(j);
        if (text.empty()) continue;

        auto toks = tokenize(text);

        std::unordered_map<std::string, uint32_t> tf;
        tf.reserve(toks.size()/2 + 8);

        uint32_t doc_len = 0;
        for (auto& t : toks) {
            if (t.size() < 2) continue;
            if (is_stopword(t)) continue;
            tf[t] += 1;
            doc_len += 1;
        }
        if (doc_len == 0) continue;

        uint32_t docId = (uint32_t)docs.size();
        docs.push_back(DocInfo{cord_uid, title, rel, doc_len});
        total_len += doc_len;

        std::vector<std::pair<uint32_t,uint32_t>> postings;
        postings.reserve(tf.size());

        for (auto& kv : tf) {
            auto it = term_to_id.find(kv.first);
            uint32_t tid;
            if (it == term_to_id.end()) {
                tid = (uint32_t)id_to_term.size();
                term_to_id.emplace(kv.first, tid);
                id_to_term.push_back(kv.first);
            } else tid = it->second;

            postings.push_back({tid, kv.second});
        }

        std::sort(postings.begin(), postings.end());
        forward.push_back(std::move(postings));

        if (docId % 1000 == 0) std::cerr << "Docs: " << docId << "\n";
    }

    float avgdl = docs.empty() ? 0.0f : (float)total_len / (float)docs.size();

    // Write docs.bin
    {
        std::ofstream out(seg / "docs.bin", std::ios::binary);
        write_u32(out, (uint32_t)docs.size());
        for (auto& d : docs) {
            write_string(out, d.cord_uid);
            write_string(out, d.title);
            write_string(out, d.json_relpath);
            write_u32(out, d.doc_len);
        }
    }

    // Write stats.bin
    {
        std::ofstream out(seg / "stats.bin", std::ios::binary);
        write_u32(out, (uint32_t)docs.size());
        write_f32(out, avgdl);
    }

    // Write forward.bin
    {
        std::ofstream out(seg / "forward.bin", std::ios::binary);
        write_u32(out, (uint32_t)forward.size());
        for (auto& vec : forward) {
            write_u32(out, (uint32_t)vec.size());
            for (auto& [tid, tfv] : vec) {
                write_u32(out, tid);
                write_u32(out, tfv);
            }
        }
    }

    // Write terms.bin (termId -> term string)
    {
        std::ofstream out(seg / "terms.bin", std::ios::binary);
        write_u32(out, (uint32_t)id_to_term.size());
        for (auto& t : id_to_term) write_string(out, t);
    }

    std::cerr << "Wrote forward+terms+docs+stats to segment: " << seg << "\n";
    std::cerr << "Now run: lexicon.exe " << seg << "\n";
    return 0;
}
