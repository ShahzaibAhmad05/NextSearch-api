#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <algorithm>

#include "indexio.hpp"
#include "barrels.hpp"

namespace fs = std::filesystem;

struct Posting { uint32_t docId; uint32_t tf; };

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: lexicon <SEGMENT_DIR>\n";
        return 1;
    }

    fs::path seg = fs::path(argv[1]);
    fs::path fwd_path  = seg / "forward.bin";
    fs::path term_path = seg / "terms.bin";     // created by forwardindex.cpp

    if (!fs::exists(fwd_path) || !fs::exists(term_path)) {
        std::cerr << "Missing forward.bin or terms.bin in: " << seg << "\n";
        return 1;
    }

    // Load terms list: termId -> term
    std::vector<std::string> terms;
    {
        std::ifstream in(term_path, std::ios::binary);
        if (!in) {
            std::cerr << "Failed to open: " << term_path << "\n";
            return 1;
        }
        uint32_t n = read_u32(in);
        terms.resize(n);
        for (uint32_t i=0;i<n;i++) terms[i] = read_string(in);
    }

    // Read forward index, build inverted postings
    std::vector<std::vector<Posting>> inverted(terms.size());

    {
        std::ifstream in(fwd_path, std::ios::binary);
        if (!in) {
            std::cerr << "Failed to open: " << fwd_path << "\n";
            return 1;
        }

        uint32_t numDocs = read_u32(in);

        for (uint32_t docId=0; docId<numDocs; docId++) {
            uint32_t cnt = read_u32(in);
            for (uint32_t i=0;i<cnt;i++) {
                uint32_t termId = read_u32(in);
                uint32_t tf     = read_u32(in);
                if (termId >= inverted.size()) continue;
                inverted[termId].push_back(Posting{docId, tf});
            }
        }
    }

    // Write BARRELIZED inverted + lexicon.
    // Per-barrel lexicon entry format:
    //   term(string), termId(u32), df(u32), offset(u64), count(u32)
    {
        BarrelParams bp;
        bp.barrel_count = BARREL_COUNT;
        uint32_t tcount = (uint32_t)terms.size();
        bp.terms_per_barrel = (tcount + bp.barrel_count - 1) / bp.barrel_count;
        if (bp.terms_per_barrel == 0) bp.terms_per_barrel = 1;

        write_barrels_manifest(seg, bp);

        std::vector<std::ofstream> inv(bp.barrel_count);
        std::vector<std::ofstream> lex(bp.barrel_count);
        std::vector<uint64_t> offsets(bp.barrel_count, 0);
        std::vector<uint32_t> barrel_term_counts(bp.barrel_count, 0);

        for (uint32_t b=0; b<bp.barrel_count; b++) {
            inv[b].open(inv_barrel_path(seg, b), std::ios::binary);
            lex[b].open(lex_barrel_path(seg, b), std::ios::binary);
            if (!inv[b] || !lex[b]) {
                std::cerr << "Failed to open barrel files in: " << seg << "\n";
                return 1;
            }
            write_u32(lex[b], 0); // placeholder for number of lex entries in this barrel
        }

        for (uint32_t tid=0; tid<tcount; tid++) {
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

        // Patch barrel term counts at start of each lexicon_bXXX.bin
        for (uint32_t b=0; b<bp.barrel_count; b++) {
            lex[b].flush();
            lex[b].close();

            std::ofstream patch(
                lex_barrel_path(seg, b),
                std::ios::in | std::ios::out | std::ios::binary
            );
            if (!patch) {
                std::cerr << "Failed to patch lexicon barrel: " << lex_barrel_path(seg, b) << "\n";
                return 1;
            }
            patch.seekp(0, std::ios::beg);
            write_u32(patch, barrel_term_counts[b]);
            patch.flush();
        }
    }

    std::cerr << "Built BARRELIZED lexicon+inverted in: " << seg << "\n";
    return 0;
}
