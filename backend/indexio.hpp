#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// Binary write helpers
inline void write_u32(std::ofstream& out, uint32_t v) { out.write((char*)&v, sizeof(v)); }
inline void write_u64(std::ofstream& out, uint64_t v) { out.write((char*)&v, sizeof(v)); }
inline void write_f32(std::ofstream& out, float v) { out.write((char*)&v, sizeof(v)); }

// Binary read helpers
inline uint32_t read_u32(std::ifstream& in) { uint32_t v; in.read((char*)&v, sizeof(v)); return v; }
inline uint64_t read_u64(std::ifstream& in) { uint64_t v; in.read((char*)&v, sizeof(v)); return v; }
inline float read_f32(std::ifstream& in) { float v; in.read((char*)&v, sizeof(v)); return v; }

// Write length-prefixed string
inline void write_string(std::ofstream& out, const std::string& s) {
    write_u32(out, (uint32_t)s.size());
    out.write(s.data(), s.size());
}

// Read length-prefixed string
inline std::string read_string(std::ifstream& in) {
    uint32_t n = read_u32(in);
    std::string s(n, '\0');
    in.read(&s[0], n);
    return s;
}
