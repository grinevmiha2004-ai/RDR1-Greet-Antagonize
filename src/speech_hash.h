#pragma once
#include <cstdint>
// RDR.exe RVA 0xC7570: case/slash normalization and Jenkins accumulation,
// without the final mix. Context table +0x94 stores this intermediate value.
constexpr std::uint32_t speechNameHash(const char* name) {
    std::uint32_t h = 0;
    for (; *name; ++name) {
        unsigned char c = static_cast<unsigned char>(*name);
        if (c >= 'A' && c <= 'Z') c += 'a'-'A';
        if (c == '\\') c = '/';
        h += c; h += h << 10; h ^= h >> 6;
    }
    return h;
}
