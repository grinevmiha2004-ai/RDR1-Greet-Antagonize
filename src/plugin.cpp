#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <type_traits>
#include <cstdarg>
#include <cstdio>
#include <vector>
#include "speech_hash.h"

namespace {
using Keyboard = void(*)(DWORD, WORD, BYTE, BOOL, BOOL, BOOL, BOOL);
struct Hook {
    void (*scriptRegister)(HMODULE, void(*)());
    void (*scriptUnregister)(HMODULE);
    void (*wait)(std::uint64_t);
    void (*keyboardRegister)(Keyboard);
    void (*keyboardUnregister)(Keyboard);
    void (*nativeInit)(std::uint32_t);
    void (*nativePush)(std::uint64_t);
    std::uint64_t* (*nativeCall)();
    void (*print)(int, const char*, ...);
} hook{};
volatile LONG pending = 0;
DWORD key = 'F';
float radius = 8.0f;
float taskSeconds = 3.0f;
ULONGLONG cooldown = 4000;
HMODULE self;
bool registered = false;
HANDLE logFile = INVALID_HANDLE_VALUE;
bool taskMode = false;
char context[96] = "PLAYER_TAUNT_GENERIC";
void log(const char* format, ...) {
    if (logFile == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t{};
    GetLocalTime(&t);
    char line[1024]{};
    int prefix = std::snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u.%03u [%lu] ",
        t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,GetCurrentThreadId());
    va_list args;
    va_start(args, format);
    std::vsnprintf(line+prefix, sizeof(line)-prefix-3, format, args);
    va_end(args);
    size_t length = std::strlen(line);
    line[length++]='\r'; line[length++]='\n';
    DWORD written = 0;
    WriteFile(logFile, line, static_cast<DWORD>(length), &written, nullptr);
}
void openLog() {
    wchar_t path[MAX_PATH]{};
    DWORD size = GetModuleFileNameW(self, path, MAX_PATH);
    if (size && size < MAX_PATH) {
        wchar_t* dot = wcsrchr(path, L'.');
        if (dot && (dot-path)+5 < MAX_PATH) {
            wcscpy(dot, L".log");
            logFile = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }
    }
    if (logFile == INVALID_HANDLE_VALUE) {
        DWORD n = GetTempPathW(MAX_PATH, path);
        if (n && n+13 < MAX_PATH) {
            wcscat(path, L"TauntKey.log");
            logFile = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }
    }
    log("ATTACH TauntKey v0.5 diagnostics; pid=%lu", GetCurrentProcessId());
}

template<class T> bool resolve(HMODULE mod, T& out, const char* name) {
    out = reinterpret_cast<T>(GetProcAddress(mod, name));
    if (!out) log("BIND FAILED: %s error=%lu", name, GetLastError());
    return out != nullptr;
}
bool bind() {
    HMODULE mod = GetModuleHandleW(L"RedHook.dll");
    if (!mod) { log("BIND FAILED: RedHook.dll not loaded"); return false; }
    // Exact exports from the user-supplied RedHook 0.8, Windows x64 ABI.
    return resolve(mod, hook.scriptRegister, "?ScriptRegister@@YAXPEAUHINSTANCE__@@P6AXXZ@Z") &&
        resolve(mod, hook.scriptUnregister, "?ScriptUnregister@@YAXPEAUHINSTANCE__@@@Z") &&
        resolve(mod, hook.wait, "?ScriptWait@@YAX_K@Z") &&
        resolve(mod, hook.keyboardRegister, "?KeyboardHandlerRegister@@YAXP6AXKGEHHHH@Z@Z") &&
        resolve(mod, hook.keyboardUnregister, "?KeyboardHandlerUnregister@@YAXP6AXKGEHHHH@Z@Z") &&
        resolve(mod, hook.nativeInit, "?NativeInit@@YAXI@Z") &&
        resolve(mod, hook.nativePush, "?NativePush64@@YAX_K@Z") &&
        resolve(mod, hook.nativeCall, "?NativeCall@@YAPEA_KXZ") &&
        resolve(mod, hook.print, "?Print@@YAXW4LogType@@PEBDZZ");
}
template<class T> void push(T value) {
    static_assert(sizeof(T) <= 8 && std::is_trivially_copyable_v<T>);
    std::uint64_t slot = 0;
    std::memcpy(&slot, &value, sizeof(value));
    hook.nativePush(slot);
}
template<std::uint32_t Hash, class Result = void, class... Args>
Result call(Args... args) {
    hook.nativeInit(Hash);
    (push(args), ...);
    auto result = hook.nativeCall();
    if constexpr (!std::is_void_v<Result>) {
        Result value{};
        if (result) std::memcpy(&value, result, sizeof(value));
        return value;
    }
}
// Read-only inspection of context IDs; only for the supplied RDR executable build.
// Never call internal addresses or write game memory.
template<class T> bool readMemory(std::uintptr_t address, T& value) {
    SIZE_T count = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
        &value, sizeof(value), &count) && count == sizeof(value);
}
bool contextTable(std::uintptr_t& table, int& count) {
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!readMemory(base,dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 0 || dos.e_lfanew > 4096 ||
        !readMemory(base+dos.e_lfanew,nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.TimeDateStamp != 0x6711591d || nt.OptionalHeader.SizeOfImage != 0x5a5dd600) {
        log("CONTEXT inspection disabled: executable build differs from supplied EXE"); return false;
    }
    const unsigned char expected[24] = {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0x41,0x10,0x48,0x8b,0xd9,0x8b,0x70};
    unsigned char actual[24]{};
    if (!readMemory(base+0xe26650,actual) || std::memcmp(actual,expected,sizeof(actual)) != 0) {
        log("CONTEXT inspection disabled: handler signature mismatch"); return false;
    }
    if (!readMemory(base+0x2ad4180,count) || !readMemory(base+0x2c26da8,table) ||
        count <= 0 || count > 8192 || !table) {
        log("CONTEXT table unavailable count=%d",count); return false;
    }
    log("CONTEXT table entries=%d", count);
    return true;
}
int selectTaunt(int player) {
    std::uintptr_t table = 0; int count = 0;
    if (!contextTable(table,count)) return -1;
    std::vector<unsigned char> entries(static_cast<size_t>(count)*0x9c);
    SIZE_T copied = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(table), entries.data(),
        entries.size(), &copied) || copied != entries.size()) {
        log("CONTEXT table bulk read failed"); return -1;
    }
    const int model = call<0x0B28E9EC,int>(player);
    const char* names[] = {"PLAYER_TAUNT_GENERIC", "PLAYER_TAUNT", "PLAYER_TAUNT_LAW", "PLAYER_TAUNT_CRIMINAL", "PLAYER_GREET", "PLAYER_GREET_VIRTUAL"};
    const char* chosen = nullptr;
    int chosenId = -1;
    for (unsigned n = 0; n < sizeof(names)/sizeof(names[0]); ++n) {
        const std::uint32_t hash = speechNameHash(names[n]);
        int id = -1;
        for (int i = 0; i < count; ++i) {
            std::uint32_t stored = 0;
            std::memcpy(&stored, entries.data()+static_cast<size_t>(i)*0x9c+0x94, sizeof(stored));
            if (stored == hash) { id = i; break; }
        }
        int playable = id < 0 ? -1 : call<0xD02757C1,int>(model,id);
        log("CONTEXT name=%s hash=%08X id=%d model=%d modelHasPhrase=%d",names[n],hash,id,model,playable);
        // Prefer generic taunts; law/criminal and greeting contexts are diagnostic only.
        if (n < 2 && playable == 1 && !chosen) { chosen = names[n]; chosenId = id; }
    }
    if (!chosen) { log("SKIP no generic taunt context with phrases for player model; no blind speech request"); return -1; }
    std::strcpy(context, chosen);
    return chosenId;
}
struct Vec3 { float x, y, z; };
static_assert(sizeof(void*) == 8 && sizeof(Vec3) == 12);
bool valid(int actor) { return call<0xBA6C3E92, bool>(actor); }
bool alive(int actor) { return valid(actor) && call<0x2F232639, bool>(actor); }
Vec3 position(int actor) {
    Vec3 v{};
    call<0x99BD9D6F>(actor, &v);
    return v;
}
int nearest(int player) {
    int layout = call<0x51D6DA2C, int>(player); // GET_LAYOUT_FROM_OBJECT
    log("SEARCH layout=%d", layout);
    if (!call<0xD7E7187B, bool>(layout)) { log("SKIP invalid layout"); return 0; }
    int iterator = call<0xD8A12B74, int>(layout);
    log("SEARCH iterator=%d", iterator);
    if (!call<0x5A9CC0B0, bool>(iterator)) { log("SKIP invalid iterator"); return 0; }
    call<0xBE553F84>(iterator, 15); // OBJECT_TYPE_Actor from supplied SDK
    call<0xF35C5859>(iterator);    // ITERATE_EVERYWHERE
    const Vec3 origin = position(player);
    float bestDistance = radius * radius;
    int best = 0;
    int object = call<0xE96A0318, int>(iterator);
    int count = 0;
    while (call<0xD7E7187B, bool>(object) && count++ < 8192) {
        int actor = call<0x34F0AD96, int>(object);
        if (actor != player && alive(actor) && call<0x882C84DC, bool>(actor) &&
            !call<0xB27E91E7, bool>(actor)) {
            const Vec3 p = position(actor);
            const float dx = p.x-origin.x, dy = p.y-origin.y, dz = p.z-origin.z;
            const float distance = dx*dx + dy*dy + dz*dz;
            if (distance < bestDistance) { best = actor; bestDistance = distance; log("CANDIDATE actor=%d distanceSquared=%.2f", actor, distance); }
        }
        object = call<0xD88DC865, int>(iterator);
    }
    call<0xE284A10C>(iterator);
    if (count > 8192) {
        log("SKIP actor scan limit reached");
        return 0;
    }
    log("SEARCH complete scanned=%d selected=%d distanceSquared=%.2f", count, best, bestDistance);
    return best;
}
void keyboard(DWORD pressed, WORD, BYTE, BOOL, BOOL alt, BOOL wasDown, BOOL up) {
    if (pressed == key && !alt && !wasDown && !up)
        InterlockedExchange(&pending, 1);
}
bool foreground() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}
void script() {
    log("SCRIPT START key=%lu radius=%.1f mode=%s context=%s", key, radius,
        taskMode ? "Task" : "Speech", context);
    ULONGLONG next = 0, beat = 0, observeStart = 0, observeNext = 0;
    int observedPlayer = 0;
    bool wasDown = false;
    while (true) {
        ULONGLONG now = GetTickCount64();
        bool down = foreground() && (GetAsyncKeyState(static_cast<int>(key)) & 0x8000);
        bool polled = down && !wasDown;
        wasDown = down;
        bool callback = InterlockedExchange(&pending, 0) != 0;
        if (now >= beat) {
            log("HEARTBEAT script running foreground=%d", foreground());
            beat = now + 10000;
        }
        // Polling is the only activation source. Delayed callbacks are diagnostic only.
        if (polled) {
            log("KEY key=%lu polling=%d callback=%d", key, polled, callback);
            if (!foreground()) log("SKIP game window is not foreground");
            else if (now < next) log("SKIP cooldown remaining=%llu ms", next-now);
            else if (call<0x57246C02, bool>()) log("SKIP game paused");
            else if (call<0xA61FA36B, bool>()) log("SKIP cutscene playing");
            else {
                int player = call<0xE8CFDD53, int>(-1);
                log("PLAYER actor=%d", player);
                if (!alive(player)) log("SKIP player invalid or dead");
                else if (call<0x2B74A6D6, bool>(player)) log("SKIP player already speaking");
                else {
                    bool requested = false;
                    const ULONGLONG started = GetTickCount64();
                    if (taskMode) {
                        int target = nearest(player);
                        if (!alive(target)) log("SKIP task mode: no living NPC within radius");
                        else {
                            log("REQUEST task player=%d target=%d parameter=%.1f", player,target,taskSeconds);
                            call<0x030FB1FA>(player, target, taskSeconds);
                            requested = true;
                        }
                    } else {
                        int contextId = selectTaunt(player);
                        if (contextId >= 0) {
                            // Native context path sets the engine's context flags, unlike
                            // SAY_SINGLE_LINE_STRING. Backup context IDs use -1 (none).
                            log("REQUEST context player=%d name=%s id=%d hash=31BAF169", player,context,contextId);
                            call<0x31BAF169>(player, contextId, -1, true, false, 1, -1, -1, 0, false);
                            requested = true;
                        }
                    }
                    log("ATTEMPT completed elapsed=%llu ms requested=%d", GetTickCount64()-started, requested);
                    // Reserve cooldown even when task lookup fails.
                    next = GetTickCount64() + cooldown;
                    if (requested) {
                        observedPlayer = player;
                        observeStart = GetTickCount64();
                        observeNext = observeStart + 250;
                    }
                }
            }
        }
        if (observedPlayer && now >= observeNext) {
            if (!valid(observedPlayer)) {
                log("OBSERVE player no longer valid"); observedPlayer = 0;
            } else {
                bool speech = call<0x2B74A6D6, bool>(observedPlayer);
                log("OBSERVE elapsed=%llu ms anySpeechPlaying=%d", now-observeStart, speech);
                observeNext = now + 250;
                if (now-observeStart >= 2000) observedPlayer = 0;
            }
        }
        hook.wait(0);
    }
}
void config() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(self, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    wchar_t* dot = wcsrchr(path, L'.');
    if (!dot || (dot-path) + 5 >= MAX_PATH) return;
    wcscpy(dot, L".ini");
    taskMode = GetPrivateProfileIntW(L"TauntKey", L"TaskMode", 0, path) != 0;
    // Named contexts are intentionally restricted to identifiers found in supplied RDR.exe.
    if (GetPrivateProfileIntW(L"TauntKey", L"GenericContext", 1, path) == 0)
        std::strcpy(context, "PLAYER_TAUNT");
    const UINT configuredKey = GetPrivateProfileIntW(L"TauntKey", L"Key", 'F', path);
    if (configuredKey > 0 && configuredKey < 256) key = configuredKey;
    const UINT range = GetPrivateProfileIntW(L"TauntKey", L"RadiusMetres", 8, path);
    if (range >= 1 && range <= 20) radius = static_cast<float>(range);
    const UINT duration = GetPrivateProfileIntW(L"TauntKey", L"TaskSeconds", 3, path);
    if (duration >= 1 && duration <= 5) taskSeconds = static_cast<float>(duration);
    const UINT delay = GetPrivateProfileIntW(L"TauntKey", L"CooldownMs", 4000, path);
    cooldown = delay >= 1000 && delay <= 30000 ? delay : 4000;
    if (cooldown < static_cast<ULONGLONG>(taskSeconds * 1000))
        cooldown = static_cast<ULONGLONG>(taskSeconds * 1000);
}
}
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        self = module;
        openLog();
        if (!bind()) {
            OutputDebugStringA("TauntKey: RedHook 0.8 exports unavailable. Plugin not loaded.\n");
            log("ATTACH FAILED; plugin is not registered");
            if (logFile != INVALID_HANDLE_VALUE) { CloseHandle(logFile); logFile = INVALID_HANDLE_VALUE; }
            return FALSE;
        }
        log("BIND OK");
        config();
        log("CONFIG key=%lu cooldown=%llu radius=%.1f", key,cooldown,radius);
        hook.keyboardRegister(keyboard);
        hook.scriptRegister(module, script);
        registered = true;
        log("REGISTERED; waiting for script fiber");
    } else if (reason == DLL_PROCESS_DETACH && registered) {
        log("DETACH");
        hook.keyboardUnregister(keyboard);
        hook.scriptUnregister(module);
        if (logFile != INVALID_HANDLE_VALUE) { CloseHandle(logFile); logFile = INVALID_HANDLE_VALUE; }
    }
    return TRUE;
}
