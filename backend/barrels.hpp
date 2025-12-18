#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <fstream>
#include "indexio.hpp"

namespace fs = std::filesystem;

// Barrel count setting
static constexpr uint32_t BARREL_COUNT = 64;

// Barrel config stored per segment
struct BarrelParams {
    uint32_t barrel_count = BARREL_COUNT;
    uint32_t terms_per_barrel = 0;
};

// Path for barrels manifest file
inline fs::path barrels_manifest_path(const fs::path& segdir) {
    return segdir / "barrels.bin";
}

// Write barrel config to disk
inline void write_barrels_manifest(const fs::path& segdir, const BarrelParams& p) {
    std::ofstream out(barrels_manifest_path(segdir), std::ios::binary);
    write_u32(out, p.barrel_count);
    write_u32(out, p.terms_per_barrel);
}

// Read barrel config from disk
inline bool read_barrels_manifest(const fs::path& segdir, BarrelParams& p) {
    std::ifstream in(barrels_manifest_path(segdir), std::ios::binary);
    if (!in) return false;
    p.barrel_count = read_u32(in);
    p.terms_per_barrel = read_u32(in);
    return true;
}

// Map termId to barrel id
inline uint32_t barrel_for_term(uint32_t termId, const BarrelParams& p) {
    if (p.terms_per_barrel == 0) return 0;
    uint32_t b = termId / p.terms_per_barrel;
    if (b >= p.barrel_count) b = p.barrel_count - 1;
    return b;
}

// Create fixed-width barrel id suffix
inline std::string barrel_suffix(uint32_t barrel_id) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%03u", barrel_id);
    return std::string(buf);
}

// Path for one inverted barrel file
inline fs::path inv_barrel_path(const fs::path& segdir, uint32_t barrel_id) {
    return segdir / ("inverted_b" + barrel_suffix(barrel_id) + ".bin");
}

// Path for one lexicon barrel file
inline fs::path lex_barrel_path(const fs::path& segdir, uint32_t barrel_id) {
    return segdir / ("lexicon_b" + barrel_suffix(barrel_id) + ".bin");
}

// Quick check if barrel files exist
inline bool has_barrels(const fs::path& segdir) {
    return fs::exists(barrels_manifest_path(segdir)) &&
           fs::exists(inv_barrel_path(segdir, 0)) &&
           fs::exists(lex_barrel_path(segdir, 0));
}
