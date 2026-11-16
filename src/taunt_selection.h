#pragma once
#include <array>
// Caller supplies only taunt contexts, in preference order. Greeting contexts
// must never be included merely to obtain a positive availability result.
constexpr int chooseTaunt(const std::array<int,4>& ids, const std::array<int,4>& available) {
    for (int i=0; i<4; ++i)
        if (ids[i]>=0 && available[i]==1) return i;
    return -1;
}
