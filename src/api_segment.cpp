#include "api_segment.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "indexio.hpp"

namespace cord19 {

// Load segment list from manifest.bin
std::vector<std::string> load_manifest(const fs::path& manifest_path) {
    std::vector<std::string> segs;
    if (!fs::exists(manifest_path)) return segs;
    std::ifstream in(manifest_path, std::ios::binary);
    if (!in) return segs;

    uint32_t n = read_u32(in);
    segs.resize(n);

    // Read all segment names
    for (uint32_t i = 0; i < n; i++) segs[i] = read_string(in);
    return segs;
}

// Save segment list to manifest.bin
void save_manifest(const fs::path& manifest_path, const std::vector<std::string>& segs) {
    std::ofstream out(manifest_path, std::ios::binary);
    write_u32(out, (uint32_t)segs.size());

    // Write all segment names
    for (auto& s : segs) write_string(out, s);
}

// Create a zero-padded segment folder name
std::string seg_name(uint32_t id) {
    std::ostringstream ss;
    ss << "seg_" << std::setw(6) << std::setfill('0') << id;
    return ss.str();
}

// Load segment using legacy (single inverted.bin + lexicon.bin) format
static bool load_segment_legacy(const fs::path& segdir, Segment& s) {
    std::ifstream in(segdir / "lexicon.bin", std::ios::binary);
    if (!in) return false;

    uint32_t tcount = read_u32(in);
    s.lex.reserve(tcount * 2 + 1);

    // Read lexicon entries
    for (uint32_t i = 0; i < tcount; i++) {
        std::string term = read_string(in);
        LexEntry e;
        e.termId = read_u32(in);
        e.df = read_u32(in);
        e.offset = read_u64(in);
        e.count = read_u32(in);
        s.lex.emplace(std::move(term), e);
    }

    // Open legacy inverted file stream
    s.inv.open(segdir / "inverted.bin", std::ios::binary);
    s.use_barrels = false;
    return (bool)s.inv;
}

// Load segment using barrelized inverted index format
static bool load_segment_barrels(const fs::path& segdir, Segment& s) {
    s.use_barrels = true;
    if (!read_barrels_manifest(segdir, s.barrel_params)) return false;

    // Open all inverted barrel files
    s.inv_barrels.resize(s.barrel_params.barrel_count);
    for (uint32_t b = 0; b < s.barrel_params.barrel_count; b++) {
        s.inv_barrels[b].open(inv_barrel_path(segdir, b), std::ios::binary);
        if (!s.inv_barrels[b]) return false;
    }

    // Load lexicon from all lex barrels
    s.lex.clear();
    for (uint32_t b = 0; b < s.barrel_params.barrel_count; b++) {
        std::ifstream in(lex_barrel_path(segdir, b), std::ios::binary);
        if (!in) return false;

        uint32_t tcount = read_u32(in);

        // Read lexicon entries for this barrel
        for (uint32_t i = 0; i < tcount; i++) {
            std::string term = read_string(in);
            LexEntry e;
            e.termId = read_u32(in);
            e.df = read_u32(in);
            e.offset = read_u64(in);
            e.count = read_u32(in);
            e.barrelId = b;
            s.lex.emplace(std::move(term), e);
        }
    }
    return true;
}

// Load segment stats, docs, and lexicon/index files
bool load_segment(const fs::path& segdir, Segment& s) {
    s = Segment{};
    s.dir = segdir;

    // Load stats.bin (N and avgdl)
    {
        std::ifstream in(segdir / "stats.bin", std::ios::binary);
        if (!in) return false;
        s.N = read_u32(in);
        s.avgdl = read_f32(in);
    }

    // Load docs.bin document metadata
    {
        std::ifstream in(segdir / "docs.bin", std::ios::binary);
        if (!in) return false;
        uint32_t n = read_u32(in);
        s.docs.resize(n);

        // Read per-doc fields (only cord_uid and doc_len are used)
        for (uint32_t i = 0; i < n; i++) {
            s.docs[i].cord_uid = read_string(in);
            read_string(in);  // Skip title (available in metadata.csv)
            read_string(in);  // Skip json_relpath (available in metadata.csv)
            s.docs[i].doc_len = read_u32(in);
        }
    }

    // Pick barrel or legacy loader based on segment files
    if (has_barrels(segdir)) return load_segment_barrels(segdir, s);
    return load_segment_legacy(segdir, s);
}

// Write barrelized inverted + lexicon files for a single document segment
void write_barrelized_index_files_single_doc(
    const fs::path& segdir,
    const std::vector<std::string>& id_to_term,
    const std::vector<std::pair<uint32_t, uint32_t>>& fwd
) {
    // Setup barrel parameters for term distribution
    BarrelParams bp;
    bp.barrel_count = BARREL_COUNT;
    uint32_t tcount = (uint32_t)id_to_term.size();
    bp.terms_per_barrel = (tcount + bp.barrel_count - 1) / bp.barrel_count;
    if (bp.terms_per_barrel == 0) bp.terms_per_barrel = 1;

    // Write barrels manifest file
    write_barrels_manifest(segdir, bp);

    // Create output streams for each barrel
    std::vector<std::ofstream> inv(bp.barrel_count);
    std::vector<std::ofstream> lex(bp.barrel_count);
    std::vector<uint64_t> offsets(bp.barrel_count, 0);
    std::vector<uint32_t> barrel_term_counts(bp.barrel_count, 0);

    // Open barrel files and write lex placeholders
    for (uint32_t b = 0; b < bp.barrel_count; b++) {
        inv[b].open(inv_barrel_path(segdir, b), std::ios::binary);
        lex[b].open(lex_barrel_path(segdir, b), std::ios::binary);
        write_u32(lex[b], 0); // placeholder
    }

    // Build quick tf lookup by termId
    std::vector<uint32_t> tf_by_tid(tcount, 0);
    for (auto& [tid, tfv] : fwd) {
        if (tid < tcount) tf_by_tid[tid] = tfv;
    }

    // Write lexicon and postings into the correct barrel
    for (uint32_t tid = 0; tid < tcount; tid++) {
        uint32_t tfv = tf_by_tid[tid];
        if (tfv == 0) continue;

        uint32_t b = barrel_for_term(tid, bp);
        barrel_term_counts[b]++;

        write_string(lex[b], id_to_term[tid]);
        write_u32(lex[b], tid);
        write_u32(lex[b], 1); // df
        write_u64(lex[b], offsets[b]);
        write_u32(lex[b], 1); // count

        // Write single posting (docId=0, tf=tfv)
        write_u32(inv[b], 0);
        write_u32(inv[b], tfv);

        offsets[b] += sizeof(uint32_t) * 2;
    }

    // Patch final term counts into each lex barrel header
    for (uint32_t b = 0; b < bp.barrel_count; b++) {
        lex[b].flush();
        lex[b].close();

        std::ofstream patch(lex_barrel_path(segdir, b),
                            std::ios::binary | std::ios::in | std::ios::out);
        patch.seekp(0, std::ios::beg);
        write_u32(patch, barrel_term_counts[b]);
        patch.flush();
    }
}

} // namespace cord19
