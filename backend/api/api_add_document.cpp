#include "api_add_document.hpp"

#include <chrono>
#include <filesystem>
#include <algorithm>
#include <string>
#include <unordered_map>

#include "api_http.hpp"
#include "api_segment.hpp"
#include "cordjson.hpp"
#include "indexio.hpp"
#include "textutil.hpp"

namespace cord19 {

void handle_add_document(Engine& engine, const httplib::Request& req, httplib::Response& res) {
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

    if (!body.contains("cord_root") || !body.contains("json_relpath") || !body.contains("cord_uid") ||
        !body.contains("title")) {
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
        try {
            jdoc = json::parse(raw);
        } catch (...) {
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
        std::vector<std::pair<uint32_t, uint32_t>> fwd;
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
}

} // namespace cord19
