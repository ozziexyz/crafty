#ifndef FRAMING_H
#define FRAMING_H

#include <array>
#include <cstdint>
#include <string>

constexpr uint32_t MAX_FRAME_SIZE = 1 << 20;

inline std::string encode_frame(const std::string& payload) {
    uint32_t s = payload.size();
    std::string out;
    out.reserve(4 + s);
    out.push_back(static_cast<char>(s >> 24));
    out.push_back(static_cast<char>(s >> 16));
    out.push_back(static_cast<char>(s >> 8));
    out.push_back(static_cast<char>(s));
    out += payload;
    return out;
}

inline uint32_t decode_length(const std::array<unsigned char, 4>& h) {
    return (uint32_t(h[0]) << 24) | (uint32_t(h[1]) << 16) | (uint32_t(h[2]) << 8) | h[3];
}

#endif