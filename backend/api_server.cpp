#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <queue>
#include <cmath>
#include <algorithm>
#include <mutex>
#include <string>
#include <chrono>

#include "textutil.hpp"
#include "indexio.hpp"
#include "barrels.hpp"
#include "third_party/httplib.h"
#include "third_party/nlohmann/json.hpp"

#include <iomanip>
#include <sstream>
#include "cordjson.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

struct DocInfo {
    std::string cord_uid;
    std::string title;
    std::string json_relpath;
    uint32_t doc_len;
};

struct LexEntry {
    uint32_t termId;
    uint32_t df;
    uint64_t offset;
    uint32_t count;
    uint32_t barrelId = 0;
};

static float bm25_idf(uint32_t N, uint32_t df) {
    return std::log((((N - df + 0.5f) / (df + 0.5f)) + 1.0f));
}

static std::vector<std::string> load_manifest(const fs::path& manifest_path) {
    std::vector<std::string> segs;
    if (!fs::exists(manifest_path)) return segs;
    std::ifstream in(manifest_path, std::ios::binary);
    if (!in) return segs;
    uint32_t n = read_u32(in);
    segs.resize(n);
    for (uint32_t i=0;i<n;i++) segs[i] = read_string(in);
    return segs;
}

struct Segment {
    fs::path dir;
    uint32_t N = 0;
    float avgdl = 0;
    std::vector<DocInfo> docs;
    std::unordered_map<std::string, LexEntry> lex;

    // legacy
    std::ifstream inv;

    // barrels
    bool use_barrels = false;
    BarrelParams barrel_params{};
    std::vector<std::ifstream> inv_barrels;
};

static bool load_segment_legacy(const fs::path& segdir, Segment& s) {
    std::ifstream in(segdir / "lexicon.bin", std::ios::binary);
    if (!in) return false;

    uint32_t tcount = read_u32(in);
    s.lex.reserve(tcount * 2 + 1);

    for (uint32_t i=0;i<tcount;i++) {
        std::string term = read_string(in);
        LexEntry e;
        e.termId = read_u32(in);
        e.df     = read_u32(in);
        e.offset = read_u64(in);
        e.count  = read_u32(in);
        s.lex.emplace(std::move(term), e);
    }

    s.inv.open(segdir / "inverted.bin", std::ios::binary);
    s.use_barrels = false;
    return (bool)s.inv;
}

static bool load_segment_barrels(const fs::path& segdir, Segment& s) {
    s.use_barrels = true;
    if (!read_barrels_manifest(segdir, s.barrel_params)) return false;

    s.inv_barrels.resize(s.barrel_params.barrel_count);
    for (uint32_t b=0; b<s.barrel_params.barrel_count; b++) {
        s.inv_barrels[b].open(inv_barrel_path(segdir, b), std::ios::binary);
        if (!s.inv_barrels[b]) return false;
    }

    s.lex.clear();
    for (uint32_t b=0; b<s.barrel_params.barrel_count; b++) {
        std::ifstream in(lex_barrel_path(segdir, b), std::ios::binary);
        if (!in) return false;

        uint32_t tcount = read_u32(in);
        for (uint32_t i=0;i<tcount;i++) {
            std::string term = read_string(in);
            LexEntry e;
            e.termId  = read_u32(in);
            e.df      = read_u32(in);
            e.offset  = read_u64(in);
            e.count   = read_u32(in);
            e.barrelId = b;
            s.lex.emplace(std::move(term), e);
        }
    }
    return true;
}

static bool load_segment(const fs::path& segdir, Segment& s) {
    s = Segment{};
    s.dir = segdir;

    {
        std::ifstream in(segdir / "stats.bin", std::ios::binary);
        if (!in) return false;
        s.N = read_u32(in);
        s.avgdl = read_f32(in);
    }

    {
        std::ifstream in(segdir / "docs.bin", std::ios::binary);
        if (!in) return false;
        uint32_t n = read_u32(in);
        s.docs.resize(n);
        for (uint32_t i=0;i<n;i++) {
            s.docs[i].cord_uid = read_string(in);
            s.docs[i].title = read_string(in);
            s.docs[i].json_relpath = read_string(in);
            s.docs[i].doc_len = read_u32(in);
        }
    }

    if (has_barrels(segdir)) return load_segment_barrels(segdir, s);
    return load_segment_legacy(segdir, s);
}

static void enable_cors(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
}

static std::string seg_name(uint32_t id) {
    std::ostringstream ss;
    ss << "seg_" << std::setw(6) << std::setfill('0') << id;
    return ss.str();
}

static void save_manifest(const fs::path& manifest_path, const std::vector<std::string>& segs) {
    std::ofstream out(manifest_path, std::ios::binary);
    write_u32(out, (uint32_t)segs.size());
    for (auto& s : segs) write_string(out, s);
}

static std::vector<std::string> csv_row(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inq = false;
    for (size_t i=0;i<line.size();i++) {
        char c = line[i];
        if (c == '"' ) { inq = !inq; continue; }
        if (!inq && c == ',') { out.push_back(cur); cur.clear(); continue; }
        cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

// NEW: store both url and publish_time per cord_uid
struct MetaInfo {
    std::string url;
    std::string publish_time; // "YYYY-MM-DD" string
};

static void load_metadata_uid_meta(
    const fs::path& metadata_csv,
    std::unordered_map<std::string, MetaInfo>& uid_to_meta
) {
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
    int uid_i = -1, url_i = -1, pub_i = -1;
    for (int i=0;i<(int)cols.size();i++) {
        if (cols[i] == "cord_uid") uid_i = i;
        if (cols[i] == "url") url_i = i;
        if (cols[i] == "publish_time") pub_i = i;
    }

    std::cerr << "[metadata] columns=" << cols.size()
              << " uid_i=" << uid_i
              << " url_i=" << url_i
              << " pub_i=" << pub_i << "\n";

    if (uid_i < 0 || url_i < 0 || pub_i < 0) {
        std::cerr << "[metadata] missing required columns in header\n";
        return;
    }

    std::string line;
    size_t loaded = 0, bad = 0;
    while (std::getline(in, line)) {
        auto r = csv_row(line);
        if ((int)r.size() <= std::max({uid_i, url_i, pub_i})) { bad++; continue; }

        auto uid = r[uid_i];
        auto url = r[url_i];
        auto pub = r[pub_i];

        if (uid.empty()) continue;

        // CORD-19 may contain multiple rows per uid; keep first non-empty values
        auto& entry = uid_to_meta[uid];
        if (entry.url.empty() && !url.empty()) entry.url = url;
        if (entry.publish_time.empty() && !pub.empty()) entry.publish_time = pub;

        loaded++;
    }

    std::cerr << "[metadata] loaded=" << loaded << " bad_rows=" << bad
              << " map_size=" << uid_to_meta.size() << "\n";
}

static void write_barrelized_index_files_single_doc(
    const fs::path& segdir,
    const std::vector<std::string>& id_to_term,
    const std::vector<std::pair<uint32_t,uint32_t>>& fwd
) {
    BarrelParams bp;
    bp.barrel_count = BARREL_COUNT;
    uint32_t tcount = (uint32_t)id_to_term.size();
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
        write_u32(lex[b], 0); // placeholder
    }

    // df is either 1 (term appears in the doc) or 0
    std::vector<uint32_t> tf_by_tid(tcount, 0);
    for (auto& [tid, tfv] : fwd) {
        if (tid < tcount) tf_by_tid[tid] = tfv;
    }

    for (uint32_t tid = 0; tid < tcount; tid++) {
        uint32_t tfv = tf_by_tid[tid];
        if (tfv == 0) continue;

        uint32_t b = barrel_for_term(tid, bp);
        barrel_term_counts[b]++;

        write_string(lex[b], id_to_term[tid]);
        write_u32(lex[b], tid);
        write_u32(lex[b], 1);                 // df
        write_u64(lex[b], offsets[b]);
        write_u32(lex[b], 1);                 // count

        // postings: docId = 0
        write_u32(inv[b], 0);
        write_u32(inv[b], tfv);

        offsets[b] += sizeof(uint32_t) * 2;
    }

    for (uint32_t b = 0; b < bp.barrel_count; b++) {
        lex[b].flush();
        lex[b].close();

        std::ofstream patch(
            lex_barrel_path(segdir, b),
            std::ios::binary | std::ios::in | std::ios::out
        );
        patch.seekp(0, std::ios::beg);
        write_u32(patch, barrel_term_counts[b]);
        patch.flush();
    }
}

struct Engine {
    fs::path index_dir;
    std::vector<std::string> seg_names;
    std::vector<Segment> segments;

    // CHANGED: store both url and publish_time
    std::unordered_map<std::string, MetaInfo> uid_to_meta;

    std::mutex mtx;

    bool reload() {
        std::cerr << "[reload] metadata map size: " << uid_to_meta.size() << "\n";
        std::lock_guard<std::mutex> lock(mtx);

        seg_names = load_manifest(index_dir / "manifest.bin");
        if (seg_names.empty()) {
            // fallback scan
            fs::path segroot = index_dir / "segments";
            if (fs::exists(segroot) && fs::is_directory(segroot)) {
                seg_names.clear();
                for (auto& e : fs::directory_iterator(segroot)) {
                    if (!e.is_directory()) continue;
                    auto name = e.path().filename().string();
                    if (name.rfind("seg_", 0) == 0) seg_names.push_back(name);
                }
                std::sort(seg_names.begin(), seg_names.end());
            }
        }
        if (seg_names.empty()) return false;

        std::vector<Segment> loaded;
        loaded.reserve(seg_names.size());

        for (auto& name : seg_names) {
            Segment s;
            fs::path segdir = index_dir / "segments" / name;
            if (!load_segment(segdir, s)) {
                std::cerr << "Failed to load segment: " << segdir << "\n";
                return false;
            }
            loaded.push_back(std::move(s));
        }

        segments = std::move(loaded);

        // reload metadata -> uid_to_meta
        uid_to_meta.clear();
        load_metadata_uid_meta(index_dir / "metadata.csv", uid_to_meta);

        return true;
    }

    json search(const std::string& query, int k) {
        std::lock_guard<std::mutex> lock(mtx);

        const float k1 = 1.2f;
        const float b  = 0.75f;
        const int K = std::max(1, std::min(k, 100)); // cap to prevent abuse

        auto qtoks = tokenize(query);
        std::vector<std::string> terms;
        for (auto& t : qtoks) {
            if (t.size() < 2) continue;
            if (is_stopword(t)) continue;
            terms.push_back(t);
        }

        json out;
        out["query"] = query;
        out["k"] = K;
        out["segments"] = (int)segments.size();
        out["results"] = json::array();

        if (terms.empty() || segments.empty()) return out;

        struct Hit { float s; uint32_t segId; uint32_t docId; };
        auto cmp = [](const Hit& a, const Hit& b){ return a.s > b.s; };
        std::priority_queue<Hit, std::vector<Hit>, decltype(cmp)> pq(cmp);
        uint64_t total_found = 0;

        for (uint32_t segId=0; segId<(uint32_t)segments.size(); segId++) {
            auto& seg = segments[segId];
            std::unordered_map<uint32_t, float> score;
            score.reserve(20000);

            for (auto& term : terms) {
                auto it = seg.lex.find(term);
                if (it == seg.lex.end()) continue;

                const LexEntry& e = it->second;
                float idf = bm25_idf(seg.N, e.df);

                std::ifstream* invp = nullptr;
                if (seg.use_barrels) {
                    invp = &seg.inv_barrels[e.barrelId];
                } else {
                    invp = &seg.inv;
                }

                invp->clear();
                invp->seekg((std::streamoff)e.offset, std::ios::beg);

                for (uint32_t i=0;i<e.count;i++) {
                    uint32_t docId = read_u32(*invp);
                    uint32_t tf    = read_u32(*invp);

                    float dl = (float)seg.docs[docId].doc_len;
                    float denom = (float)tf + k1 * (1.0f - b + b * (dl / seg.avgdl));
                    float s = idf * ((float)tf * (k1 + 1.0f)) / denom;
                    score[docId] += s;
                }
            }

            for (auto& kv : score) {
                Hit h{kv.second, segId, kv.first};
                if ((int)pq.size() < K) pq.push(h);
                else if (h.s > pq.top().s) { pq.pop(); pq.push(h); }
            }

            total_found += (uint64_t)score.size();
        }

        std::vector<Hit> hits;
        while (!pq.empty()) { hits.push_back(pq.top()); pq.pop(); }
        std::reverse(hits.begin(), hits.end());
        out["found"] = total_found;           // total matches across all segments

        for (auto& h : hits) {
            auto& d = segments[h.segId].docs[h.docId];
            json r;
            r["score"] = h.s;
            r["segment"] = seg_names[h.segId];
            r["docId"] = h.docId;
            r["cord_uid"] = d.cord_uid;
            r["title"] = d.title;
            r["json_relpath"] = d.json_relpath;

            auto it = uid_to_meta.find(d.cord_uid);
            if (it != uid_to_meta.end()) {
                // metadata.csv sometimes has multiple URLs separated by ';'
                std::string url = it->second.url;
                auto semi = url.find(';');
                if (semi != std::string::npos) url = url.substr(0, semi);
                if (!url.empty()) r["url"] = url;

                if (!it->second.publish_time.empty()) {
                    // choose your preferred key name:
                    r["publish_time"] = it->second.publish_time;
                    // r["publish_date"] = it->second.publish_time;
                }
            }

            out["results"].push_back(r);
        }

        return out;
    }
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: api_server <INDEX_DIR> [port]\n"
                  << "Example: api_server D:\\cord19_index 8080\n";
        return 1;
    }

    Engine engine;
    engine.index_dir = fs::path(argv[1]);

    int port = 8080;
    if (argc >= 3) port = std::stoi(argv[2]);

    if (!engine.reload()) {
        std::cerr << "Failed to load index segments from: " << engine.index_dir << "\n";
        return 1;
    }

    httplib::Server svr;

    svr.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
        enable_cors(res);
        res.status = 204;
    });

    svr.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
        enable_cors(res);
        json j;
        j["ok"] = true;
        j["segments"] = (int)engine.segments.size();
        res.set_content(j.dump(2), "application/json");
    });

    svr.Get("/search", [&](const httplib::Request& req, httplib::Response& res) {
        enable_cors(res);

        using clock = std::chrono::steady_clock;
        auto total_t0 = clock::now();

        if (!req.has_param("q")) {
            res.status = 400;
            res.set_content(R"({"error":"missing q param"})", "application/json");
            return;
        }
        std::string q = req.get_param_value("q");
        int k = 10;
        if (req.has_param("k")) k = std::stoi(req.get_param_value("k"));

        auto search_t0 = clock::now();
        auto j = engine.search(q, k);
        auto search_t1 = clock::now();

        double search_ms = std::chrono::duration<double, std::milli>(search_t1 - search_t0).count();
        j["search_time_ms"] = search_ms;

        auto total_t1 = clock::now();
        double total_ms = std::chrono::duration<double, std::milli>(total_t1 - total_t0).count();
        j["total_time_ms"] = total_ms;

        std::cerr << "[search] q=\"" << q << "\" k=" << k
                  << " search=" << search_ms << "ms total=" << total_ms << "ms\n";

        res.set_content(j.dump(2), "application/json");
    });

    svr.Post("/add_document", [&](const httplib::Request& req, httplib::Response& res) {
        enable_cors(res);

        using clock = std::chrono::steady_clock;
        auto t0 = clock::now();

        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"invalid json body"})", "application/json");
            return;
        }

        if (!body.contains("cord_root") || !body.contains("json_relpath") ||
            !body.contains("cord_uid")  || !body.contains("title")) {
            res.status = 400;
            res.set_content(R"({"error":"required: cord_root, json_relpath, cord_uid, title"})", "application/json");
            return;
        }

        fs::path cord_root = body["cord_root"].get<std::string>();
        std::string relpath = body["json_relpath"].get<std::string>();
        std::string cord_uid = body["cord_uid"].get<std::string>();
        std::string title = body["title"].get<std::string>();

        bool ok = false;
        std::string new_seg;

        {
            std::lock_guard<std::mutex> lock(engine.mtx);

            fs::path index_dir = engine.index_dir;
            fs::path manifest = index_dir / "manifest.bin";
            fs::path segments_dir = index_dir / "segments";
            fs::create_directories(segments_dir);

            auto segs = load_manifest(manifest);
            uint32_t new_id = (uint32_t)segs.size() + 1;
            new_seg = seg_name(new_id);

            fs::path segdir = segments_dir / new_seg;
            fs::create_directories(segdir);

            fs::path json_path = cord_root / fs::path(relpath);
            if (!fs::exists(json_path)) {
                res.status = 400;
                json err;
                err["error"] = "JSON not found";
                err["path"] = json_path.string();
                res.set_content(err.dump(2), "application/json");
                return;
            }

            std::string raw = read_file_all(json_path);
            if (raw.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"failed to read json"})", "application/json");
                return;
            }

            json jdoc;
            try { jdoc = json::parse(raw); } catch(...) {
                res.status = 400;
                res.set_content(R"({"error":"failed to parse json"})", "application/json");
                return;
            }

            std::string text = extract_text_from_cord_json(jdoc);
            auto toks = tokenize(text);

            std::unordered_map<std::string, uint32_t> tf;
            uint32_t doc_len = 0;
            for (auto& t : toks) {
                if (t.size() < 2) continue;
                if (is_stopword(t)) continue;
                tf[t] += 1;
                doc_len += 1;
            }
            if (doc_len == 0) {
                res.status = 400;
                res.set_content(R"({"error":"document has no indexable tokens"})", "application/json");
                return;
            }

            // Assign per-segment termIds (same as your current design)
            std::vector<std::string> id_to_term;
            id_to_term.reserve(tf.size());
            std::vector<std::pair<uint32_t,uint32_t>> fwd;
            fwd.reserve(tf.size());

            for (auto& kv : tf) {
                uint32_t tid = (uint32_t)id_to_term.size();
                id_to_term.push_back(kv.first);
                fwd.push_back({tid, kv.second});
            }
            std::sort(fwd.begin(), fwd.end());

            // docs.bin (1 doc)
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

            // BARRELIZED inverted + lexicon
            write_barrelized_index_files_single_doc(segdir, id_to_term, fwd);

            // update manifest
            segs.push_back(new_seg);
            save_manifest(manifest, segs);

            ok = true;
        }

        bool reloaded = ok ? engine.reload() : false;

        auto t1 = clock::now();
        double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        json out;
        out["ok"] = ok;
        out["segment"] = new_seg;
        out["reloaded"] = reloaded;
        out["total_time_ms"] = total_ms;
        res.set_content(out.dump(2), "application/json");
    });

    svr.Post("/reload", [&](const httplib::Request&, httplib::Response& res) {
        enable_cors(res);
        bool ok = engine.reload();
        json j;
        j["reloaded"] = ok;
        j["segments"] = (int)engine.segments.size();
        res.set_content(j.dump(2), "application/json");
    });

    std::cout << "API running on http://127.0.0.1:" << port << "\n";
    std::cout << "Try: /search?q=mycoplasma+pneumonia&k=10\n";
    svr.listen("0.0.0.0", port);
    return 0;
}
