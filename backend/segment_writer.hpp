#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <filesystem>

#include "indexio.hpp"
#include "barrels.hpp"

namespace fs = std::filesystem;

struct Posting { uint32_t docId; uint32_t tf; };

struct DocMeta {
    std::string cord_uid;
    std::string title;
    std::string json_relpath;
    uint32_t doc_len;
};

class SegmentWriter {
public:
    // term -> termId
    std::unordered_map<std::string, uint32_t> term_to_id;
    std::vector<std::string> id_to_term;

    // forward[docId] = list of (termId, tf)
    std::vector<std::vector<std::pair<uint32_t,uint32_t>>> forward;

    // inverted[termId] = postings
    std::vector<std::vector<Posting>> inverted;

    std::vector<DocMeta> docs;
    uint64_t total_len = 0;

    uint32_t intern_term(const std::string& term) {
        auto it = term_to_id.find(term);
        if (it != term_to_id.end()) return it->second;
        uint32_t id = (uint32_t)id_to_term.size();
        term_to_id.emplace(term, id);
        id_to_term.push_back(term);
        inverted.emplace_back();
        return id;
    }

    void add_document(const DocMeta& meta, const std::vector<std::pair<std::string,uint32_t>>& term_freqs) {
        uint32_t docId = (uint32_t)docs.size();
        docs.push_back(meta);
        total_len += meta.doc_len;

        std::vector<std::pair<uint32_t,uint32_t>> fwd;
        fwd.reserve(term_freqs.size());

        for (auto& [term, tf] : term_freqs) {
            uint32_t tid = intern_term(term);
            fwd.push_back({tid, tf});
            inverted[tid].push_back(Posting{docId, tf});
        }
        std::sort(fwd.begin(), fwd.end());
        forward.push_back(std::move(fwd));
    }

    void write_segment(const fs::path& segdir) {
        fs::create_directories(segdir);

        float avgdl = docs.empty() ? 0.0f : (float)total_len / (float)docs.size();

        // stats.bin
        {
            std::ofstream out(segdir / "stats.bin", std::ios::binary);
            write_u32(out, (uint32_t)docs.size());
            write_f32(out, avgdl);
        }

        // docs.bin
        {
            std::ofstream out(segdir / "docs.bin", std::ios::binary);
            write_u32(out, (uint32_t)docs.size());
            for (auto& d : docs) {
                write_string(out, d.cord_uid);
                write_string(out, d.title);
                write_string(out, d.json_relpath);
                write_u32(out, d.doc_len);
            }
        }

        // forward.bin
        // format: numDocs; for each doc: count; (termId, tf)*count
        {
            std::ofstream out(segdir / "forward.bin", std::ios::binary);
            write_u32(out, (uint32_t)forward.size());
            for (auto& vec : forward) {
                write_u32(out, (uint32_t)vec.size());
                for (auto& [tid, tf] : vec) {
                    write_u32(out, tid);
                    write_u32(out, tf);
                }
            }
        }

        // terms.bin
        {
            std::ofstream out(segdir / "terms.bin", std::ios::binary);
            write_u32(out, (uint32_t)id_to_term.size());
            for (auto& t : id_to_term) write_string(out, t);
        }

        // BARRELIZED inverted + lexicon
        // Per-barrel lexicon entry format:
        //   term(string), termId(u32), df(u32), offset(u64), count(u32)
        {
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

            for (uint32_t b=0; b<bp.barrel_count; b++) {
                inv[b].open(inv_barrel_path(segdir, b), std::ios::binary);
                lex[b].open(lex_barrel_path(segdir, b), std::ios::binary);
                write_u32(lex[b], 0); // placeholder
            }

            for (uint32_t tid=0; tid<tcount; tid++) {
                auto& plist = inverted[tid];
                if (plist.empty()) continue;

                std::sort(plist.begin(), plist.end(),
                          [](const Posting& a, const Posting& b){ return a.docId < b.docId; });

                uint32_t df = (uint32_t)plist.size();
                uint32_t b  = barrel_for_term(tid, bp);

                barrel_term_counts[b]++;

                write_string(lex[b], id_to_term[tid]);
                write_u32(lex[b], tid);
                write_u32(lex[b], df);
                write_u64(lex[b], offsets[b]);
                write_u32(lex[b], df);

                for (auto& p : plist) {
                    write_u32(inv[b], p.docId);
                    write_u32(inv[b], p.tf);
                }
                offsets[b] += (uint64_t)df * (sizeof(uint32_t)*2);
            }

            // patch counts
            for (uint32_t b=0; b<bp.barrel_count; b++) {
                lex[b].flush();
                lex[b].close();
                std::ofstream patch(lex_barrel_path(segdir, b), std::ios::in | std::ios::out | std::ios::binary);
                patch.seekp(0, std::ios::beg);
                write_u32(patch, barrel_term_counts[b]);
                patch.flush();
            }
        }
    }
};
