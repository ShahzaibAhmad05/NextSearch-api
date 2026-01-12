#include "api_add_document.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <cctype>

#include "api_http.hpp"
#include "api_segment.hpp"
#include "barrels.hpp"
#include "cordjson.hpp"
#include "indexio.hpp"
#include "textutil.hpp"

namespace cord19 {

namespace fs = std::filesystem;

// IMPORTANT: api_types.hpp already defines cord19::DocInfo,
// so we must NOT redefine it here.
struct SliceDocInfo {
    std::string cord_uid;
    std::string title;
    std::string json_relpath;
    uint32_t doc_len = 0;
};

static std::string rand_hex(size_t n) {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint32_t> dist(0, 15);
    std::string s;
    s.reserve(n);
    const char* hexd = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) s.push_back(hexd[dist(rng)]);
    return s;
}

static bool write_bytes(const fs::path& p, const std::string& bytes) {
    std::ofstream out(p, std::ios::binary);
    if (!out) return false;
    out.write(bytes.data(), (std::streamsize)bytes.size());
    return (bool)out;
}

/* -------------------------
   Helpers
   ------------------------- */

static inline std::string to_lower_copy(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static inline void trim_inplace(std::string& s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i) s.erase(0, i);
}

/*
    ✅ FIXED multipart extractor

    Root cause of your crash:
      - For multipart/form-data, cpp-httplib uses ContentReader::formdata_reader_.
      - Calling ContentReader(ContentReceiver) uses reader_ which can be empty -> std::bad_function_call.

    Therefore we MUST use:
      reader(FormDataHeader, ContentReceiver)
*/
static bool stream_multipart_file_field_to_path(
    const httplib::Request& req,
    const httplib::ContentReader& reader,
    const std::string& field_name,
    const fs::path& out_path,
    std::string& out_filename,
    std::string& err
) {
    out_filename.clear();
    err.clear();

    std::string ct;
    try { ct = req.get_header_value("Content-Type"); }
    catch (...) {
        try { ct = req.get_header_value("content-type"); } catch (...) {}
    }

    if (to_lower_copy(ct).find("multipart/form-data") == std::string::npos) {
        err = "Content-Type is not multipart/form-data";
        return false;
    }

    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        err = "failed to open output file for writing";
        return false;
    }

    bool in_target_part = false;
    bool saw_target_part = false;

    auto on_header = [&](const httplib::FormData& part) -> bool {
        if (part.name == field_name) {
            in_target_part = true;
            saw_target_part = true;
            out_filename = part.filename;
        } else {
            in_target_part = false;
        }
        return true;
    };

    auto on_bytes = [&](const char* data, size_t len) -> bool {
        if (in_target_part && len > 0) {
            out.write(data, (std::streamsize)len);
            if (!out) {
                err = "failed while writing upload to disk";
                return false;
            }
        }
        return true;
    };

    bool ok = false;
    try {
        ok = reader(on_header, on_bytes);
    } catch (const std::exception& e) {
        err = std::string("multipart reader threw: ") + e.what();
        ok = false;
    } catch (...) {
        err = "multipart reader threw unknown exception";
        ok = false;
    }

    out.close();

    if (!ok) {
        if (err.empty()) err = "multipart reader aborted";
        return false;
    }

    if (!saw_target_part) {
        err = "multipart did not contain file field '" + field_name + "'";
        return false;
    }

    std::error_code ec;
    auto sz = fs::file_size(out_path, ec);
    if (ec || sz == 0) {
        err = "uploaded file was empty";
        return false;
    }

    return true;
}

/* -------------------------
   CSV + indexing code (unchanged)
   ------------------------- */

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
    while (!first.empty() && (first.back() == ' ' || first.back() == '\r' || first.back()=='\n')) first.pop_back();
    while (!first.empty() && first.front() == ' ') first.erase(first.begin());
    return first;
}

static std::string seg_name_local(uint32_t id) {
    std::ostringstream ss;
    ss << "seg_" << std::setw(6) << std::setfill('0') << id;
    return ss.str();
}

static bool extract_zip_to(const fs::path& zip_path, const fs::path& dest_dir, std::string& err) {
    fs::create_directories(dest_dir);

#ifdef _WIN32
    std::ostringstream cmd;
    cmd << "powershell -NoProfile -Command \""
        << "Expand-Archive -Force -Path '" << zip_path.string() << "' -DestinationPath '" << dest_dir.string() << "'"
        << "\"";
    int rc = std::system(cmd.str().c_str());
    if (rc != 0) { err = "Expand-Archive failed"; return false; }
#else
    std::ostringstream cmd;
    cmd << "unzip -qq -o "
        << "'" << zip_path.string() << "'"
        << " -d "
        << "'" << dest_dir.string() << "'";
    int rc = std::system(cmd.str().c_str());
    if (rc != 0) { err = "unzip failed (is unzip installed?)"; return false; }
#endif
    return true;
}

static bool find_slice_root(const fs::path& extracted_root, fs::path& slice_root_out) {
    auto looks_like_root = [](const fs::path& p) -> bool {
        return fs::exists(p / "metadata.csv") &&
               fs::exists(p / "document_parses") &&
               fs::is_directory(p / "document_parses");
    };

    if (looks_like_root(extracted_root)) {
        slice_root_out = extracted_root;
        return true;
    }

    std::vector<fs::path> dirs;
    for (auto& e : fs::directory_iterator(extracted_root)) {
        if (e.is_directory()) dirs.push_back(e.path());
    }
    if (dirs.size() == 1 && looks_like_root(dirs[0])) {
        slice_root_out = dirs[0];
        return true;
    }

    for (auto& e : fs::recursive_directory_iterator(extracted_root)) {
        if (!e.is_regular_file()) continue;
        if (e.path().filename() != "metadata.csv") continue;
        fs::path cand = e.path().parent_path();
        if (looks_like_root(cand)) {
            slice_root_out = cand;
            return true;
        }
    }

    return false;
}

static bool build_forward_terms_docs_stats_from_slice(
    const fs::path& slice_root,
    const fs::path& segdir,
    uint32_t& out_num_docs,
    std::string& err
) {
    fs::create_directories(segdir);

    fs::path meta = slice_root / "metadata.csv";
    if (!fs::exists(meta)) { err = "metadata.csv not found in uploaded slice"; return false; }

    std::ifstream in(meta);
    if (!in) { err = "failed to open metadata.csv"; return false; }

    std::string header;
    if (!std::getline(in, header)) { err = "metadata.csv empty"; return false; }
    auto cols = split_csv_line(header);

    auto idx_of = [&](const std::string& name)->int {
        for (int i=0;i<(int)cols.size();i++) if (cols[i] == name) return i;
        return -1;
    };

    int i_uid   = idx_of("cord_uid");
    int i_title = idx_of("title");
    int i_pdf   = idx_of("pdf_json_files");
    int i_pmc   = idx_of("pmc_json_files");

    if (i_uid<0 || i_title<0 || i_pdf<0 || i_pmc<0) {
        err = "metadata.csv missing required columns (cord_uid,title,pdf_json_files,pmc_json_files)";
        return false;
    }

    std::unordered_map<std::string, uint32_t> term_to_id;
    term_to_id.reserve(200000);
    std::vector<std::string> id_to_term;

    std::vector<SliceDocInfo> docs;
    docs.reserve(50000);

    std::vector<std::vector<std::pair<uint32_t,uint32_t>>> forward;
    forward.reserve(50000);

    auto get_term_id = [&](const std::string& term)->uint32_t {
        auto it = term_to_id.find(term);
        if (it != term_to_id.end()) return it->second;
        uint32_t id = (uint32_t)id_to_term.size();
        id_to_term.push_back(term);
        term_to_id.emplace(term, id);
        return id;
    };

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto row = split_csv_line(line);
        if ((int)row.size() <= std::max({i_uid,i_title,i_pdf,i_pmc})) continue;

        std::string uid   = row[i_uid];
        std::string title = row[i_title];

        std::string pdf_rel = pick_first_path(row[i_pdf]);
        std::string pmc_rel = pick_first_path(row[i_pmc]);

        std::string rel = pdf_rel;
        if (rel.empty() || rel == "nan") rel.clear();
        if (!rel.empty() && !fs::exists(slice_root / fs::path(rel))) rel.clear();

        if (rel.empty()) {
            rel = pmc_rel;
            if (rel.empty() || rel == "nan") rel.clear();
            if (!rel.empty() && !fs::exists(slice_root / fs::path(rel))) rel.clear();
        }
        if (rel.empty()) continue;

        std::string raw = read_file_all(slice_root / fs::path(rel));
        if (raw.empty()) continue;

        json jdoc;
        try { jdoc = json::parse(raw); }
        catch (...) { continue; }

        std::string text = extract_text_from_cord_json(jdoc);
        auto toks = tokenize(text);

        std::unordered_map<std::string, uint32_t> tf;
        tf.reserve(toks.size());

        uint32_t doc_len = 0;
        for (auto& t : toks) {
            if (t.size() < 2) continue;
            if (is_stopword(t)) continue;
            tf[t] += 1;
            doc_len++;
        }
        if (doc_len == 0) continue;

        std::vector<std::pair<uint32_t,uint32_t>> fwd;
        fwd.reserve(tf.size());
        for (auto& kv : tf) {
            uint32_t tid = get_term_id(kv.first);
            fwd.push_back({tid, kv.second});
        }
        std::sort(fwd.begin(), fwd.end(),
                  [](auto& a, auto& b){ return a.first < b.first; });

        SliceDocInfo di;
        di.cord_uid = uid;
        di.title = title;
        di.json_relpath = rel;
        di.doc_len = doc_len;

        docs.push_back(std::move(di));
        forward.push_back(std::move(fwd));
    }

    out_num_docs = (uint32_t)docs.size();
    if (docs.empty()) { err = "no documents could be parsed from metadata.csv paths"; return false; }

    // docs.bin
    {
        std::ofstream out(segdir / "docs.bin", std::ios::binary);
        if (!out) { err = "failed to write docs.bin"; return false; }
        write_u32(out, (uint32_t)docs.size());
        for (auto& d : docs) {
            write_string(out, d.cord_uid);
            write_string(out, d.title);
            write_string(out, d.json_relpath);
            write_u32(out, d.doc_len);
        }
    }

    // stats.bin
    {
        std::ofstream out(segdir / "stats.bin", std::ios::binary);
        if (!out) { err = "failed to write stats.bin"; return false; }
        write_u32(out, (uint32_t)docs.size());
        double sumdl = 0.0;
        for (auto& d : docs) {
            write_f32(out, (float)d.doc_len);
            sumdl += (double)d.doc_len;
        }
        float avgdl = (docs.empty()) ? 0.0f : (float)(sumdl / (double)docs.size());
        write_f32(out, avgdl);
    }

    // forward.bin
    {
        std::ofstream out(segdir / "forward.bin", std::ios::binary);
        if (!out) { err = "failed to write forward.bin"; return false; }
        write_u32(out, (uint32_t)forward.size());
        for (auto& vec : forward) {
            write_u32(out, (uint32_t)vec.size());
            for (auto& p : vec) {
                write_u32(out, p.first);
                write_u32(out, p.second);
            }
        }
    }

    // terms.bin
    {
        std::ofstream out(segdir / "terms.bin", std::ios::binary);
        if (!out) { err = "failed to write terms.bin"; return false; }
        write_u32(out, (uint32_t)id_to_term.size());
        for (auto& t : id_to_term) write_string(out, t);
    }

    return true;
}

// Posting list element
struct Posting { uint32_t docId; uint32_t tf; };

static bool build_barrelized_lexicon_from_forward(
    const fs::path& segdir,
    std::string& err
) {
    fs::path fwd_path  = segdir / "forward.bin";
    fs::path term_path = segdir / "terms.bin";
    if (!fs::exists(fwd_path) || !fs::exists(term_path)) {
        err = "segment missing forward.bin or terms.bin";
        return false;
    }

    // Load terms
    std::vector<std::string> terms;
    {
        std::ifstream in(term_path, std::ios::binary);
        if (!in) { err = "failed to open terms.bin"; return false; }
        uint32_t n = read_u32(in);
        terms.resize(n);
        for (uint32_t i = 0; i < n; i++) terms[i] = read_string(in);
    }

    // Build inverted postings
    std::vector<std::vector<Posting>> inverted(terms.size());
    {
        std::ifstream in(fwd_path, std::ios::binary);
        if (!in) { err = "failed to open forward.bin"; return false; }

        uint32_t numDocs = read_u32(in);
        for (uint32_t docId = 0; docId < numDocs; docId++) {
            uint32_t cnt = read_u32(in);
            for (uint32_t i = 0; i < cnt; i++) {
                uint32_t termId = read_u32(in);
                uint32_t tf     = read_u32(in);
                if (termId >= inverted.size()) continue;
                inverted[termId].push_back({docId, tf});
            }
        }
    }

    // Write barrelized inverted + lexicon
    {
        BarrelParams bp;
        bp.barrel_count = BARREL_COUNT;
        uint32_t tcount = (uint32_t)terms.size();
        bp.terms_per_barrel = (tcount + bp.barrel_count - 1) / bp.barrel_count;
        if (bp.terms_per_barrel == 0) bp.terms_per_barrel = 1;

        write_barrels_manifest(segdir, bp);

        std::vector<std::ofstream> inv(bp.barrel_count);
        std::vector<std::ofstream> lex(bp.barrel_count);
        std::vector<uint64_t> offsets(bp.barrel_count, 0);
        std::vector<uint32_t> barrel_term_counts(bp.barrel_count, 0);

        for (uint32_t b = 0; b < bp.barrel_count; b++) {
            inv[b].open(inv_barrel_path(segdir, b), std::ios::binary);
            lex[b].open(lex_barrel_path(segdir, b), std::ios::binary);
            if (!inv[b] || !lex[b]) { err = "failed to open barrel files for writing"; return false; }
            write_u32(lex[b], 0); // placeholder
        }

        for (uint32_t tid = 0; tid < tcount; tid++) {
            auto& plist = inverted[tid];
            if (plist.empty()) continue;

            std::sort(plist.begin(), plist.end(),
                      [](const Posting& a, const Posting& b){ return a.docId < b.docId; });

            uint32_t df = (uint32_t)plist.size();
            uint32_t b  = barrel_for_term(tid, bp);

            barrel_term_counts[b]++;

            write_string(lex[b], terms[tid]);
            write_u32(lex[b], tid);
            write_u32(lex[b], df);
            write_u64(lex[b], offsets[b]);
            write_u32(lex[b], df);

            for (auto& p : plist) {
                write_u32(inv[b], p.docId);
                write_u32(inv[b], p.tf);
            }
            offsets[b] += (uint64_t)df * (sizeof(uint32_t) * 2);
        }

        for (uint32_t b = 0; b < bp.barrel_count; b++) {
            lex[b].flush();
            lex[b].close();

            std::ofstream patch(lex_barrel_path(segdir, b),
                                std::ios::in | std::ios::out | std::ios::binary);
            if (!patch) { err = "failed to patch lexicon barrel"; return false; }
            patch.seekp(0, std::ios::beg);
            write_u32(patch, barrel_term_counts[b]);
            patch.flush();
        }
    }

    return true;
}

void handle_add_document(
    Engine& engine,
    const httplib::Request& req,
    httplib::Response& res,
    const httplib::ContentReader& content_reader
) {
    enable_cors(res);
    
    // Feature disabled in current version
    res.status = 503;
    json j;
    j["error"] = "\"Add Document\" is disabled for the current version";
    res.set_content(j.dump(2), "application/json");
    return;
}

} // namespace cord19