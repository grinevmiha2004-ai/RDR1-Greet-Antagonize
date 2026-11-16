#pragma once
#include <array>
// Follow actual ownership, never the GET_LAYOUT_FROM_OBJECT cast.
// Refuse cycles or an unexpectedly deep graph.
template<class Owner, class Valid>
int findOwnerRoot(int start, Owner ownerOf, Valid valid) {
    std::array<int,32> seen{};
    int current=start;
    for (unsigned depth=0;depth<seen.size();++depth) {
        seen[depth]=current;
        int owner=ownerOf(current);
        if (!valid(owner)) return current;
        for (unsigned i=0;i<=depth;++i) if (seen[i]==owner) return 0;
        current=owner;
    }
    return 0;
}
