// What the XP build's libraries import that Windows XP SP3 does not export
// (docs/PORTING.md, R6).
//
// libc++, libunwind and compiler-rt from llvm-mingw's msvcrt release were
// built for Windows 7: libc++'s mutexes are SRW locks, its call_once is
// InitOnceExecuteOnce, its thread-locals are fiber-local storage, its
// locale functions are msvcrt.dll's _l variants (Vista's msvcrt.dll); Dear
// ImGui's Win32 backend links dwmapi.dll, which XP does not have. A program
// importing any of them does not start on XP at all -- the loader refuses
// it before main. So this file defines those imports itself: for each one,
// both the import-table pointer (__imp__Name@N) and the call stub (_Name@N),
// which the linker then takes from here instead of from kernel32's or
// msvcrt's import library. Each looks up the real function once and calls
// it when the system has it (Vista and later), and otherwise does the same
// job with what XP has. DRAGON_XP_THUNKS=fallback forces the XP side on any
// Windows, which is how it is tested away from an XP machine.
//
// tools/xp/check_imports.py reads the linked executable's imports against
// XP SP3's exports and fails on any name this file has not covered: the
// list below grows from that, not from guesses.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include <errno.h>
#include <locale.h>

// Defines one import: the pointer the import table would hold, and the stub
// a direct call goes through.
#define XP_IMPORT(ret, conv, name, decorated, params, args)                       \
    extern "C" ret conv xp_##name params;                                       \
    extern "C" ret conv xp_stub_##name params __asm__(decorated);               \
    extern "C" ret conv xp_stub_##name params { return xp_##name args; }        \
    extern "C" __attribute__((used)) void* xp_imp_##name __asm__("__imp_" decorated) = (void*)&xp_##name;

namespace {

bool force_fallback() {
    static int forced = -1;
    if (forced < 0) {
        char value[16] = "";
        forced = GetEnvironmentVariableA("DRAGON_XP_THUNKS", value, sizeof value) && strcmp(value, "fallback") == 0;
    }
    return forced != 0;
}

FARPROC system_function(const char* dll, const char* name) {
    if (force_fallback()) return nullptr;
    HMODULE module = GetModuleHandleA(dll);
    if (!module) module = LoadLibraryA(dll);
    return module ? GetProcAddress(module, name) : nullptr;
}

// The system's own `name`, typed as our replacement for it, or null.
#define REAL(name, dll) \
    static auto real = reinterpret_cast<decltype(&xp_##name)>(system_function(dll, #name))

// ---- locks and condition variables. The system's set is used only when all
// of it is there (TryAcquireSRWLockExclusive is Windows 7's): a lock taken
// by one implementation and released by the other would be corrupted.

struct SrwSet {
    void(WINAPI* acquire_exclusive)(void*);
    void(WINAPI* acquire_shared)(void*);
    void(WINAPI* release_exclusive)(void*);
    void(WINAPI* release_shared)(void*);
    BOOLEAN(WINAPI* try_acquire_exclusive)(void*);
    BOOL(WINAPI* sleep_condition)(void*, void*, DWORD, ULONG);
    void(WINAPI* wake_all)(void*);
    void(WINAPI* wake_one)(void*);
    bool usable;
};

const SrwSet& srw() {
    static SrwSet set = [] {
        SrwSet s = {};
        s.acquire_exclusive = reinterpret_cast<void(WINAPI*)(void*)>(system_function("kernel32.dll", "AcquireSRWLockExclusive"));
        s.acquire_shared = reinterpret_cast<void(WINAPI*)(void*)>(system_function("kernel32.dll", "AcquireSRWLockShared"));
        s.release_exclusive = reinterpret_cast<void(WINAPI*)(void*)>(system_function("kernel32.dll", "ReleaseSRWLockExclusive"));
        s.release_shared = reinterpret_cast<void(WINAPI*)(void*)>(system_function("kernel32.dll", "ReleaseSRWLockShared"));
        s.try_acquire_exclusive = reinterpret_cast<BOOLEAN(WINAPI*)(void*)>(system_function("kernel32.dll", "TryAcquireSRWLockExclusive"));
        s.sleep_condition = reinterpret_cast<BOOL(WINAPI*)(void*, void*, DWORD, ULONG)>(system_function("kernel32.dll", "SleepConditionVariableSRW"));
        s.wake_all = reinterpret_cast<void(WINAPI*)(void*)>(system_function("kernel32.dll", "WakeAllConditionVariable"));
        s.wake_one = reinterpret_cast<void(WINAPI*)(void*)>(system_function("kernel32.dll", "WakeConditionVariable"));
        s.usable = s.acquire_exclusive && s.acquire_shared && s.release_exclusive && s.release_shared &&
                   s.try_acquire_exclusive && s.sleep_condition && s.wake_all && s.wake_one;
        return s;
    }();
    return set;
}

// The XP lock: the SRW lock's pointer-sized word as a spin lock, shared
// taken as exclusive. Correct, unfair, and fine for a game that takes locks
// in libc++'s internals and nowhere hot.
void spin_acquire(void* lock) {
    volatile LONG* word = static_cast<volatile LONG*>(lock);
    for (unsigned spins = 0; InterlockedCompareExchange(word, 1, 0) != 0; ++spins) {
        if (spins < 64) YieldProcessor();
        else if (spins < 128) SwitchToThread();
        else Sleep(1);
    }
}

}  // namespace

extern "C" {

void WINAPI xp_AcquireSRWLockExclusive(void* lock) {
    if (srw().usable) srw().acquire_exclusive(lock);
    else spin_acquire(lock);
}
void WINAPI xp_AcquireSRWLockShared(void* lock) {
    if (srw().usable) srw().acquire_shared(lock);
    else spin_acquire(lock);
}
void WINAPI xp_ReleaseSRWLockExclusive(void* lock) {
    if (srw().usable) srw().release_exclusive(lock);
    else InterlockedExchange(static_cast<volatile LONG*>(lock), 0);
}
void WINAPI xp_ReleaseSRWLockShared(void* lock) {
    if (srw().usable) srw().release_shared(lock);
    else InterlockedExchange(static_cast<volatile LONG*>(lock), 0);
}
BOOLEAN WINAPI xp_TryAcquireSRWLockExclusive(void* lock) {
    if (srw().usable) return srw().try_acquire_exclusive(lock);
    return InterlockedCompareExchange(static_cast<volatile LONG*>(lock), 1, 0) == 0;
}

// The XP condition variable wakes spuriously, which the contract allows
// (every waiter re-checks its predicate): it gives the lock up for a
// millisecond and reports a timeout once the wait has run out.
BOOL WINAPI xp_SleepConditionVariableSRW(void* condition, void* lock, DWORD ms, ULONG flags) {
    if (srw().usable) return srw().sleep_condition(condition, lock, ms, flags);
    const DWORD started = GetTickCount();
    xp_ReleaseSRWLockExclusive(lock);
    Sleep(ms == 0 ? 0 : 1);
    xp_AcquireSRWLockExclusive(lock);
    if (ms != INFINITE && GetTickCount() - started >= ms) {
        SetLastError(ERROR_TIMEOUT);
        return FALSE;
    }
    return TRUE;
}
void WINAPI xp_WakeAllConditionVariable(void* condition) {
    if (srw().usable) srw().wake_all(condition);
}
void WINAPI xp_WakeConditionVariable(void* condition) {
    if (srw().usable) srw().wake_one(condition);
}

// call_once: 0 not run, 1 running, 2 done, in the INIT_ONCE's word.
typedef BOOL(WINAPI* InitOnceFn)(void* once, void* parameter, void** context);
BOOL WINAPI xp_InitOnceExecuteOnce(void* once, InitOnceFn fn, void* parameter, void** context) {
    REAL(InitOnceExecuteOnce, "kernel32.dll");
    if (real) return real(once, fn, parameter, context);
    volatile LONG* state = static_cast<volatile LONG*>(once);
    for (;;) {
        const LONG seen = InterlockedCompareExchange(state, 1, 0);
        if (seen == 2) return TRUE;
        if (seen == 0) {
            if (fn(once, parameter, context)) {
                InterlockedExchange(state, 2);
                return TRUE;
            }
            InterlockedExchange(state, 0);
            return FALSE;
        }
        Sleep(0);
    }
}

// Fiber-local storage as thread-local storage. The cleanup callback is not
// run, so a thread's thread_local objects leak when it exits on XP; the game
// does not end threads that own any.
DWORD WINAPI xp_FlsAlloc(void* callback) {
    REAL(FlsAlloc, "kernel32.dll");
    return real ? real(callback) : TlsAlloc();
}
void* WINAPI xp_FlsGetValue(DWORD index) {
    REAL(FlsGetValue, "kernel32.dll");
    return real ? real(index) : TlsGetValue(index);
}
BOOL WINAPI xp_FlsSetValue(DWORD index, void* value) {
    REAL(FlsSetValue, "kernel32.dll");
    return real ? real(index, value) : TlsSetValue(index, value);
}

// Locale names are Vista's; XP has LCIDs. The empty name is the invariant
// locale, anything else the user's.
int WINAPI xp_GetLocaleInfoEx(const wchar_t* name, LCTYPE type, wchar_t* data, int size) {
    REAL(GetLocaleInfoEx, "kernel32.dll");
    if (real) return real(name, type, data, size);
    const LCID lcid = name && !*name ? MAKELCID(MAKELANGID(LANG_INVARIANT, SUBLANG_NEUTRAL), SORT_DEFAULT) : LOCALE_USER_DEFAULT;
    return GetLocaleInfoW(lcid, type, data, size);
}

// A thread's id from its handle: ntdll's thread basic information on XP.
// libc++'s std::thread::hardware_concurrency asks for the processors in
// every group (Windows 7). XP has one group, so GetSystemInfo's count is it.
DWORD WINAPI xp_GetActiveProcessorCount(WORD group) {
    REAL(GetActiveProcessorCount, "kernel32.dll");
    if (real) return real(group);
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors;
}

DWORD WINAPI xp_GetThreadId(HANDLE thread) {
    REAL(GetThreadId, "kernel32.dll");
    if (real) return real(thread);
    if (thread == GetCurrentThread()) return GetCurrentThreadId();
    struct BasicInformation {
        LONG exit_status;
        void* teb;
        void* process;
        void* thread;
        ULONG_PTR affinity;
        LONG priority, base_priority;
    } info = {};
    typedef LONG(WINAPI* QueryFn)(HANDLE, int, void*, ULONG, ULONG*);
    static QueryFn query = reinterpret_cast<QueryFn>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread"));
    if (!query || query(thread, 0, &info, sizeof info, nullptr) != 0) return 0;
    return DWORD(reinterpret_cast<ULONG_PTR>(info.thread));
}

// Windows 7 moved psapi's EnumProcessModules into kernel32; XP has psapi.dll.
BOOL WINAPI xp_K32EnumProcessModules(HANDLE process, HMODULE* modules, DWORD bytes, DWORD* needed) {
    REAL(K32EnumProcessModules, "kernel32.dll");
    if (real) return real(process, modules, bytes, needed);
    static FARPROC psapi = [] {
        HMODULE dll = LoadLibraryA("psapi.dll");
        return dll ? GetProcAddress(dll, "EnumProcessModules") : nullptr;
    }();
    if (psapi) return reinterpret_cast<BOOL(WINAPI*)(HANDLE, HMODULE*, DWORD, DWORD*)>(psapi)(process, modules, bytes, needed);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

// ---- msvcrt.dll's locale-taking functions (Vista's msvcrt.dll). The game
// runs in the "C" locale throughout, so the locale argument is the C
// locale's, and the plain functions XP has give the same answers.

int __cdecl xp__iswalpha_l(wint_t c, void*) { return iswalpha(c); }
int __cdecl xp__iswcntrl_l(wint_t c, void*) { return iswcntrl(c); }
int __cdecl xp__iswdigit_l(wint_t c, void*) { return iswdigit(c); }
int __cdecl xp__iswlower_l(wint_t c, void*) { return iswlower(c); }
int __cdecl xp__iswprint_l(wint_t c, void*) { return iswprint(c); }
int __cdecl xp__iswpunct_l(wint_t c, void*) { return iswpunct(c); }
int __cdecl xp__iswspace_l(wint_t c, void*) { return iswspace(c); }
int __cdecl xp__iswupper_l(wint_t c, void*) { return iswupper(c); }
int __cdecl xp__iswxdigit_l(wint_t c, void*) { return iswxdigit(c); }
int __cdecl xp__mbtowc_l(wchar_t* out, const char* in, size_t n, void*) { return mbtowc(out, in, n); }
int __cdecl xp__strcoll_l(const char* a, const char* b, void*) { return strcoll(a, b); }
double __cdecl xp__strtod_l(const char* s, char** end, void*) { return strtod(s, end); }
size_t __cdecl xp__strxfrm_l(char* out, const char* in, size_t n, void*) { return strxfrm(out, in, n); }
int __cdecl xp__tolower_l(int c, void*) { return tolower(c); }
int __cdecl xp__toupper_l(int c, void*) { return toupper(c); }
wint_t __cdecl xp__towlower_l(wint_t c, void*) { return towlower(c); }
wint_t __cdecl xp__towupper_l(wint_t c, void*) { return towupper(c); }
int __cdecl xp__wcscoll_l(const wchar_t* a, const wchar_t* b, void*) { return wcscoll(a, b); }
size_t __cdecl xp__wcsxfrm_l(wchar_t* out, const wchar_t* in, size_t n, void*) { return wcsxfrm(out, in, n); }

// The secure-CRT wcrtomb (Vista's msvcrt.dll), through the ANSI code page.
int __cdecl xp_wcrtomb_s(size_t* written, char* out, size_t size, wchar_t c, mbstate_t*) {
    char buffer[8];
    BOOL lossy = FALSE;
    const int n = WideCharToMultiByte(CP_ACP, 0, &c, 1, buffer, sizeof buffer, nullptr, &lossy);
    if (n <= 0 || lossy) {
        if (written) *written = size_t(-1);
        return EILSEQ;
    }
    if (out) {
        if (size < size_t(n)) return ERANGE;
        memcpy(out, buffer, size_t(n));
    }
    if (written) *written = size_t(n);
    return 0;
}

// ---- dwmapi.dll, which XP does not have: no composition there, so Dear
// ImGui's alpha-compositing path finds it off and does nothing.

HRESULT WINAPI xp_DwmIsCompositionEnabled(BOOL* enabled) {
    REAL(DwmIsCompositionEnabled, "dwmapi.dll");
    if (real) return real(enabled);
    if (enabled) *enabled = FALSE;
    return S_OK;
}
HRESULT WINAPI xp_DwmGetColorizationColor(DWORD* colour, BOOL* opaque) {
    REAL(DwmGetColorizationColor, "dwmapi.dll");
    return real ? real(colour, opaque) : E_NOTIMPL;
}
HRESULT WINAPI xp_DwmEnableBlurBehindWindow(HWND window, const void* blur) {
    REAL(DwmEnableBlurBehindWindow, "dwmapi.dll");
    return real ? real(window, blur) : E_NOTIMPL;
}

}  // extern "C"

// The imports themselves: the name the linker resolves, decorated as the
// i686 import libraries spell it (stdcall: _Name@bytes; cdecl: _name).
XP_IMPORT(void, WINAPI, AcquireSRWLockExclusive, "_AcquireSRWLockExclusive@4", (void* l), (l))
XP_IMPORT(void, WINAPI, AcquireSRWLockShared, "_AcquireSRWLockShared@4", (void* l), (l))
XP_IMPORT(void, WINAPI, ReleaseSRWLockExclusive, "_ReleaseSRWLockExclusive@4", (void* l), (l))
XP_IMPORT(void, WINAPI, ReleaseSRWLockShared, "_ReleaseSRWLockShared@4", (void* l), (l))
XP_IMPORT(BOOLEAN, WINAPI, TryAcquireSRWLockExclusive, "_TryAcquireSRWLockExclusive@4", (void* l), (l))
XP_IMPORT(BOOL, WINAPI, SleepConditionVariableSRW, "_SleepConditionVariableSRW@16",
          (void* c, void* l, DWORD ms, ULONG f), (c, l, ms, f))
XP_IMPORT(void, WINAPI, WakeAllConditionVariable, "_WakeAllConditionVariable@4", (void* c), (c))
XP_IMPORT(void, WINAPI, WakeConditionVariable, "_WakeConditionVariable@4", (void* c), (c))
XP_IMPORT(BOOL, WINAPI, InitOnceExecuteOnce, "_InitOnceExecuteOnce@16",
          (void* o, InitOnceFn f, void* p, void** c), (o, f, p, c))
XP_IMPORT(DWORD, WINAPI, FlsAlloc, "_FlsAlloc@4", (void* cb), (cb))
XP_IMPORT(void*, WINAPI, FlsGetValue, "_FlsGetValue@4", (DWORD i), (i))
XP_IMPORT(BOOL, WINAPI, FlsSetValue, "_FlsSetValue@8", (DWORD i, void* v), (i, v))
XP_IMPORT(int, WINAPI, GetLocaleInfoEx, "_GetLocaleInfoEx@16", (const wchar_t* n, LCTYPE t, wchar_t* d, int s),
          (n, t, d, s))
XP_IMPORT(DWORD, WINAPI, GetThreadId, "_GetThreadId@4", (HANDLE t), (t))
XP_IMPORT(DWORD, WINAPI, GetActiveProcessorCount, "_GetActiveProcessorCount@4", (WORD g), (g))
XP_IMPORT(BOOL, WINAPI, K32EnumProcessModules, "_K32EnumProcessModules@16",
          (HANDLE p, HMODULE* m, DWORD b, DWORD* n), (p, m, b, n))
XP_IMPORT(HRESULT, WINAPI, DwmIsCompositionEnabled, "_DwmIsCompositionEnabled@4", (BOOL* e), (e))
XP_IMPORT(HRESULT, WINAPI, DwmGetColorizationColor, "_DwmGetColorizationColor@8", (DWORD* c, BOOL* o), (c, o))
XP_IMPORT(HRESULT, WINAPI, DwmEnableBlurBehindWindow, "_DwmEnableBlurBehindWindow@8", (HWND w, const void* b), (w, b))

#define XP_CRT(ret, name, params, args) XP_IMPORT(ret, __cdecl, _##name, "__" #name, params, args)
XP_CRT(int, iswalpha_l, (wint_t c, void* l), (c, l))
XP_CRT(int, iswcntrl_l, (wint_t c, void* l), (c, l))
XP_CRT(int, iswdigit_l, (wint_t c, void* l), (c, l))
XP_CRT(int, iswlower_l, (wint_t c, void* l), (c, l))
XP_CRT(int, iswprint_l, (wint_t c, void* l), (c, l))
XP_CRT(int, iswpunct_l, (wint_t c, void* l), (c, l))
XP_CRT(int, iswspace_l, (wint_t c, void* l), (c, l))
XP_CRT(int, iswupper_l, (wint_t c, void* l), (c, l))
XP_CRT(int, iswxdigit_l, (wint_t c, void* l), (c, l))
XP_CRT(int, mbtowc_l, (wchar_t* o, const char* i, size_t n, void* l), (o, i, n, l))
XP_CRT(int, strcoll_l, (const char* a, const char* b, void* l), (a, b, l))
XP_CRT(double, strtod_l, (const char* s, char** e, void* l), (s, e, l))
XP_CRT(size_t, strxfrm_l, (char* o, const char* i, size_t n, void* l), (o, i, n, l))
XP_CRT(int, tolower_l, (int c, void* l), (c, l))
XP_CRT(int, toupper_l, (int c, void* l), (c, l))
XP_CRT(wint_t, towlower_l, (wint_t c, void* l), (c, l))
XP_CRT(wint_t, towupper_l, (wint_t c, void* l), (c, l))
XP_CRT(int, wcscoll_l, (const wchar_t* a, const wchar_t* b, void* l), (a, b, l))
XP_CRT(size_t, wcsxfrm_l, (wchar_t* o, const wchar_t* i, size_t n, void* l), (o, i, n, l))
XP_IMPORT(int, __cdecl, wcrtomb_s, "_wcrtomb_s", (size_t* w, char* o, size_t s, wchar_t c, mbstate_t* m), (w, o, s, c, m))
