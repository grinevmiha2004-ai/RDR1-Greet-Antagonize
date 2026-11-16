#include "../src/taunt_selection.h"
// Actual v0.5 user log: generic=424/unavailable, base absent,
// criminal=423/available, law=425/available. Previously discarded both.
static_assert(chooseTaunt({424,-1,423,425},{0,-1,1,1}) == 2);
static_assert(chooseTaunt({424,-1,423,425},{0,-1,0,1}) == 3);
static_assert(chooseTaunt({424,-1,423,425},{1,-1,1,1}) == 0);
static_assert(chooseTaunt({424,-1,423,425},{0,1,0,0}) == -1);
static_assert(chooseTaunt({424,-1,423,425},{0,-1,0,0}) == -1);
int main() {}
