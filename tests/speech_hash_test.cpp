#include "../src/speech_hash.h"
// Constants computed from the supplied EXE's normalization table and C7570 code.
// These intentionally differ from standard finalized JOAAT values.
static_assert(speechNameHash("PLAYER_TAUNT_GENERIC") == 0xc7be8aa9);
static_assert(speechNameHash("PLAYER_TAUNT") == 0xd2bec080);
static_assert(speechNameHash("PLAYER_GREET") == 0xd12d8adc);
static_assert(speechNameHash("PLAYER_GREET_VIRTUAL") == 0x70f183e0);
static_assert(speechNameHash("player_greet") == speechNameHash("PLAYER_GREET"));
static_assert(speechNameHash("A\\B") == speechNameHash("a/b"));
static_assert(speechNameHash("") == 0);
int main() {}
