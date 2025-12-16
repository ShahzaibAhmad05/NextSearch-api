#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <string>

#include "cordjson.hpp"
#include "textutil.hpp"
#include "indexio.hpp"

namespace fs = std::filesystem;

static std::string seg_name(uint32_t id) {
    std::ostringstream ss;
    ss << "seg_" << std::setw(6) << std::setfill('0') << id;
    return ss.str();
}

static std::vector<std::string> load_manifest(const fs::path& manifest_path) {
    std::vector<std::string> segs;
    if (!fs::exists(manifest_path)) return segs;

    std::ifstream in(manifest_path, std::ios::binary);
    uint32_t n = read_u32(in);
    segs.resize(n);
    for (uint32_t i=0;i<n;i++) segs[i] = read_string(in);
    return segs;
}

static void save_manifest(const fs::path& manifest_path, const std::vector<std::string>& segs) {
    std::ofstream out(manifest_path, std::ios::binary);
    write_u32(out, (uint32_t)segs.size());
    for (auto& s : segs) write_string(out, s);
}

int main(int argc, char** argv) {
    if (argc < 6) {
        std::cerr << "Usage: adddocument <INDEX_DIR> <CORD_ROOT> <JSON_REL_PATH> <CORD_UID> <TITLE>\n";
        return 1;
    }

    fs::path index_dir = fs::path(argv[1]);
    fs::path cord_root = fs::path(argv[2]);
    std::string relpath = argv[3];
    std::string cord_uid = argv[4];
    std::string title = argv[5];

    fs::path manifest = index_dir / "manifest.bin";
    fs::path segments_dir = index_dir / "segments";
    fs::create_directories(segments_dir);

    auto segs = load_manifest(manifest);
    uint32_t new_id = (uint32_t)segs.size() + 1;
    std::string new_seg = seg_name(new_id);
    fs::path segdir = segments_dir / new_seg;
    fs::create_directories(segdir);

    fs::path json_path = cord_root / fs::path(relpath);
    if (!fs::exists(json_path)) {
        std::cerr << "JSON not found: " << json_path << "\n";
        return 1;
    }

    std::string raw = read_file_all(json_path);
    if (raw.empty()) return 1;

    json j;
    try { j = json::parse(raw); } catch(...) { return 1; }

    std::string text = extract_text_from_cord_json(j);
    auto toks = tokenize(text);

    std::unordered_map<std::string, uint32_t> tf;
    uint32_t doc_len = 0;
    for (auto& t : toks) {
        if (t.size() < 2) continue;
        if (is_stopword(t)) continue;
        tf[t] += 1;
        doc_len += 1;
    }
    if (doc_len == 0) return 1;

    // Build term list + forward for single doc
    std::vector<std::string> id_to_term;
    id_to_term.reserve(tf.size());
    std::unordered_map<std::string, uint32_t> term_to_id;
    term_to_id.reserve(tf.size()*2+1);

    std::vector<std::pair<uint32_t,uint32_t>> fwd;
    fwd.reserve(tf.size());

    for (auto& kv : tf) {
        uint32_t tid = (uint32_t)id_to_term.size();
        term_to_id.emplace(kv.first, tid);
        id_to_term.push_back(kv.first);
        fwd.push_back({tid, kv.second});
    }
    std::sort(fwd.begin(), fwd.end());

    // Write docs.bin (1 doc)
    {
        std::ofstream out(segdir / "docs.bin", std::ios::binary);
        write_u32(out, 1);
        write_string(out, cord_uid);
        write_string(out, title);
        write_string(out, relpath);
        write_u32(out, doc_len);
    }

    // stats.bin
    {
        std::ofstream out(segdir / "stats.bin", std::ios::binary);
        write_u32(out, 1);
        write_f32(out, (float)doc_len);
    }

    // forward.bin
    {
        std::ofstream out(segdir / "forward.bin", std::ios::binary);
        write_u32(out, 1);
        write_u32(out, (uint32_t)fwd.size());
        for (auto& [tid, tfv] : fwd) {
            write_u32(out, tid);
            write_u32(out, tfv);
        }
    }

    // terms.bin
    {
        std::ofstream out(segdir / "terms.bin", std::ios::binary);
        write_u32(out, (uint32_t)id_to_term.size());
        for (auto& t : id_to_term) write_string(out, t);
    }

    // Now build lexicon+inverted for this segment (same logic as lexicon.cpp but inline)
    // inverted postings: since only 1 doc, postings list per term is either empty or (doc0, tf)
    {
        std::ofstream inv(segdir / "inverted.bin", std::ios::binary);
        std::ofstream lex(segdir / "lexicon.bin", std::ios::binary);
        write_u32(lex, (uint32_t)id_to_term.size());

        uint64_t offset = 0;
        for (uint32_t tid=0; tid<(uint32_t)id_to_term.size(); tid++) {
            uint32_t df = 0;
            uint32_t tfv = 0;

            // Find tf for tid
            // (we can build a small array instead, but fine for 1 doc)
            for (auto& p : fwd) if (p.first == tid) { df = 1; tfv = p.second; break; }

            write_string(lex, id_to_term[tid]);
            write_u32(lex, tid);
            write_u32(lex, df);
            write_u64(lex, offset);
            write_u32(lex, df);

            if (df == 1) {
                write_u32(inv, 0);      // docId=0
                write_u32(inv, tfv);
                offset += (sizeof(uint32_t) * 2);
            }
        }
    }

    // Update manifest
    segs.push_back(new_seg);
    save_manifest(manifest, segs);

    std::cout << "Added doc into segment: " << new_seg << "\n";
    return 0;
}
