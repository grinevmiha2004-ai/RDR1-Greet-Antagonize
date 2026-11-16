#include "../src/owner_root.h"
#include <cassert>
int main() {
    // Actor -> layout -> world layout; the actor itself is not a layout.
    auto owner=[](int h){ return h==942?100:h==100?200:0; };
    assert(findOwnerRoot(942,owner,[](int h){return h!=0;})==200);
    assert(findOwnerRoot(942,[](int h){return h;},[](int h){return h!=0;})==0);
    assert(findOwnerRoot(100,[](int h){return h==100?200:100;},[](int h){return h!=0;})==0);
    assert(findOwnerRoot(1,[](int h){return h+1;},[](int){return true;})==0);
}
