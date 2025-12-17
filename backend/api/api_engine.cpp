#include "api_engine.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <queue>

#include "api_metadata.hpp"
#include "api_segment.hpp"
#include "indexio.hpp"
#include "textutil.hpp"

namespace cord19 {

static float bm25_idf(uint32_t N, uint32_t df) {
    return std::log((((N - df + 0.5f) / (df + 0.5f)) + 1.0f));
}

bool Engine::reload() {
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

json Engine::search(const std::string& query, int k) {
    std::lock_guard<std::mutex> lock(mtx);

    const float k1 = 1.2f;
    const float b = 0.75f;
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

    struct Hit {
        float s;
        uint32_t segId;
        uint32_t docId;
    };
    auto cmp = [](const Hit& a, const Hit& b) { return a.s > b.s; };
    std::priority_queue<Hit, std::vector<Hit>, decltype(cmp)> pq(cmp);
    uint64_t total_found = 0;

    for (uint32_t segId = 0; segId < (uint32_t)segments.size(); segId++) {
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

            for (uint32_t i = 0; i < e.count; i++) {
                uint32_t docId = read_u32(*invp);
                uint32_t tf = read_u32(*invp);

                float dl = (float)seg.docs[docId].doc_len;
                float denom = (float)tf + k1 * (1.0f - b + b * (dl / seg.avgdl));
                float s = idf * ((float)tf * (k1 + 1.0f)) / denom;
                score[docId] += s;
            }
        }

        for (auto& kv : score) {
            Hit h{kv.second, segId, kv.first};
            if ((int)pq.size() < K)
                pq.push(h);
            else if (h.s > pq.top().s) {
                pq.pop();
                pq.push(h);
            }
        }

        total_found += (uint64_t)score.size();
    }

    std::vector<Hit> hits;
    while (!pq.empty()) {
        hits.push_back(pq.top());
        pq.pop();
    }
    std::reverse(hits.begin(), hits.end());
    out["found"] = total_found; // total matches across all segments

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
                r["publish_time"] = it->second.publish_time;
            }

            // Return ONLY first author in "Surname et al." form
            if (!it->second.author.empty()) {
                r["author"] = it->second.author;
            }
        }

        out["results"].push_back(r);
    }

    return out;
}

} // namespace cord19
