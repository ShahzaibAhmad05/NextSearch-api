#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <fstream>
#include "indexio.hpp"

namespace fs = std::filesystem;

// Choose barrel count. 64 is a reasonable starter for small/medium indexes.
static constexpr uint32_t BARREL_COUNT = 64;

// Stored per-segment so reader knows how termIds map to barrels.
struct BarrelParams {
    uint32_t barrel_count = BARREL_COUNT;
    uint32_t terms_per_barrel = 0; // computed at build time
};

inline fs::path barrels_manifest_path(const fs::path& segdir) {
    return segdir / "barrels.bin";
}

inline void write_barrels_manifest(const fs::path& segdir, const BarrelParams& p) {
    std::ofstream out(barrels_manifest_path(segdir), std::ios::binary);
    write_u32(out, p.barrel_count);
    write_u32(out, p.terms_per_barrel);
}

inline bool read_barrels_manifest(const fs::path& segdir, BarrelParams& p) {
    std::ifstream in(barrels_manifest_path(segdir), std::ios::binary);
    if (!in) return false;
    p.barrel_count = read_u32(in);
    p.terms_per_barrel = read_u32(in);
    return true;
}

// Range partition by termId (wordID). This matches the "range of word IDs" idea.
inline uint32_t barrel_for_term(uint32_t termId, const BarrelParams& p) {
    if (p.terms_per_barrel == 0) return 0;
    uint32_t b = termId / p.terms_per_barrel;
    if (b >= p.barrel_count) b = p.barrel_count - 1; // clamp last barrel
    return b;
}

inline std::string barrel_suffix(uint32_t barrel_id) {
    // 3 digits: 000..999
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%03u", barrel_id);
    return std::string(buf);
}

inline fs::path inv_barrel_path(const fs::path& segdir, uint32_t barrel_id) {
    return segdir / ("inverted_b" + barrel_suffix(barrel_id) + ".bin");
}

inline fs::path lex_barrel_path(const fs::path& segdir, uint32_t barrel_id) {
    return segdir / ("lexicon_b" + barrel_suffix(barrel_id) + ".bin");
}

inline bool has_barrels(const fs::path& segdir) {
    // presence of barrels.bin + first barrel files is enough
    return fs::exists(barrels_manifest_path(segdir)) &&
           fs::exists(inv_barrel_path(segdir, 0)) &&
           fs::exists(lex_barrel_path(segdir, 0));
}
