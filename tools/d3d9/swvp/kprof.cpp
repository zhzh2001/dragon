// kprof: where the whole machine's time goes, kernel included.
//
//   kprof.exe DELAY SECONDS OUT.txt [COMMAND LINE...]
//
// sampler.exe reads one thread's instruction pointer from user mode, so a
// thread inside a system call shows only as KiFastSystemCall: on the G550
// that was 97% of the dragon's samples, and the driver's own work stayed
// out of sight. This uses the profiling objects XP's kernrate uses
// (NtCreateProfile on a range, NtStartProfile): the kernel counts, on every
// profile interrupt on every processor, which bucket of which range the
// interrupted instruction was in. One range per loaded kernel module (the
// display driver and its miniport included, session space or not) in
// 16-byte buckets, plus the whole of kernel and user space in coarse ones
// so nothing falls outside. The command, if given, is started first; the
// profile runs from DELAY seconds after it for SECONDS. Prints the share of
// samples in each module and the hottest buckets as module RVAs with the
// nearest export, read from the module's file, so they can be found in a
// disassembly. Needs an administrator (SeSystemProfilePrivilege). Built
// with the XP toolchain beside tris.exe (build.sh).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

typedef LONG NTSTATUS;
typedef NTSTATUS(NTAPI* NtCreateProfileFn)(PHANDLE, HANDLE, PVOID, ULONG, ULONG, PULONG, ULONG, int, ULONG_PTR);
typedef NTSTATUS(NTAPI* NtProfileFn)(HANDLE);
typedef NTSTATUS(NTAPI* NtSetIntervalProfileFn)(ULONG, int);
typedef NTSTATUS(NTAPI* NtQueryIntervalProfileFn)(int, PULONG);
typedef NTSTATUS(NTAPI* NtQuerySystemInformationFn)(ULONG, PVOID, ULONG, PULONG);

const int kProfileTime = 0;                 // KPROFILE_SOURCE ProfileTime
const ULONG kSystemModuleInformation = 11;  // SYSTEM_INFORMATION_CLASS
const ULONG kInterval = 2500;               // 100 ns units: 4,000 samples a second a processor

struct ModuleInfo {  // RTL_PROCESS_MODULE_INFORMATION, 32-bit
    HANDLE section;
    PVOID mapped_base;
    PVOID image_base;
    ULONG image_size;
    ULONG flags;
    USHORT load_order, init_order, load_count, name_offset;
    char path[256];
};

FILE* g_out = nullptr;
void say(const char* fmt, ...) {
    char line[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof line, fmt, args);
    va_end(args);
    fputs(line, stdout);
    if (g_out) {
        fputs(line, g_out);
        fflush(g_out);
    }
}

bool enable_privilege(const char* name) {
    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    const bool ok = LookupPrivilegeValueA(nullptr, name, &tp.Privileges[0].Luid) &&
                    AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return ok;
}

// A module's exports and CodeView record, read from its file (a kernel
// module cannot be read from user space where it is loaded).
struct Image {
    std::vector<std::pair<DWORD, std::string>> exports;  // rva, name, sorted
    std::string codeview;
    DWORD timestamp = 0;
};

Image read_image(const std::string& path) {
    Image img;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return img;
    std::vector<BYTE> b;
    fseek(f, 0, SEEK_END);
    b.resize(size_t(ftell(f)));
    fseek(f, 0, SEEK_SET);
    if (fread(b.data(), 1, b.size(), f) != b.size()) b.clear();
    fclose(f);
    if (b.size() < sizeof(IMAGE_DOS_HEADER)) return img;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(b.data());
    if (size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > b.size()) return img;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(b.data() + dos->e_lfanew);
    img.timestamp = nt->FileHeader.TimeDateStamp;
    const auto* sec = IMAGE_FIRST_SECTION(nt);
    auto at = [&](DWORD rva, size_t n) -> const BYTE* {
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            const DWORD va = sec[i].VirtualAddress, size = std::max(sec[i].Misc.VirtualSize, sec[i].SizeOfRawData);
            if (rva >= va && rva < va + size) {
                const size_t off = size_t(sec[i].PointerToRawData) + (rva - va);
                return off + n <= b.size() ? b.data() + off : nullptr;
            }
        }
        return nullptr;
    };
    const IMAGE_DATA_DIRECTORY& ed = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (const auto* ex = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(at(ed.VirtualAddress, sizeof(IMAGE_EXPORT_DIRECTORY)));
        ed.VirtualAddress && ex) {
        const auto* names = reinterpret_cast<const DWORD*>(at(ex->AddressOfNames, ex->NumberOfNames * 4));
        const auto* funcs = reinterpret_cast<const DWORD*>(at(ex->AddressOfFunctions, ex->NumberOfFunctions * 4));
        const auto* ords = reinterpret_cast<const WORD*>(at(ex->AddressOfNameOrdinals, ex->NumberOfNames * 2));
        for (DWORD i = 0; names && funcs && ords && i < ex->NumberOfNames; ++i) {
            const char* name = reinterpret_cast<const char*>(at(names[i], 1));
            if (name && ords[i] < ex->NumberOfFunctions) img.exports.emplace_back(funcs[ords[i]], name);
        }
        std::sort(img.exports.begin(), img.exports.end());
    }
    const IMAGE_DATA_DIRECTORY& dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    for (DWORD off = 0; dd.VirtualAddress && off + sizeof(IMAGE_DEBUG_DIRECTORY) <= dd.Size;
         off += sizeof(IMAGE_DEBUG_DIRECTORY)) {
        const auto* d = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(at(dd.VirtualAddress + off, sizeof(IMAGE_DEBUG_DIRECTORY)));
        if (!d || d->Type != IMAGE_DEBUG_TYPE_CODEVIEW || d->PointerToRawData + 24 > b.size()) continue;
        const BYTE* cv = b.data() + d->PointerToRawData;
        if (std::memcmp(cv, "RSDS", 4) == 0) {
            const GUID& g = *reinterpret_cast<const GUID*>(cv + 4);
            char buf[400];
            snprintf(buf, sizeof buf, "%s %08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%lu",
                     reinterpret_cast<const char*>(cv + 24), g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1],
                     g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7],
                     *reinterpret_cast<const DWORD*>(cv + 20));
            img.codeview = buf;
        } else if (std::memcmp(cv, "NB10", 4) == 0) {
            char buf[400];
            snprintf(buf, sizeof buf, "%s %08lX%lu", reinterpret_cast<const char*>(cv + 16),
                     *reinterpret_cast<const DWORD*>(cv + 8), *reinterpret_cast<const DWORD*>(cv + 12));
            img.codeview = buf;
        }
    }
    return img;
}

// "\SystemRoot\System32\x.sys", "\WINDOWS\system32\x", "\??\C:\x" -> a Win32 path.
std::string win32_path(const char* nt) {
    char windir[MAX_PATH];
    GetWindowsDirectoryA(windir, sizeof windir);
    std::string p = nt;
    if (!_strnicmp(nt, "\\SystemRoot\\", 12)) return std::string(windir) + "\\" + (nt + 12);
    if (!_strnicmp(nt, "\\??\\", 4)) return nt + 4;
    if (nt[0] == '\\') return std::string(windir, 2) + p;  // "C:" + "\WINDOWS\..."
    return std::string(windir) + "\\system32\\drivers\\" + p;
}

struct Range {
    std::string name, path;
    DWORD base = 0, size = 0;
    ULONG bucket_shift = 4;
    std::vector<ULONG> counts;
    HANDLE profile = nullptr;
    unsigned long long hits = 0;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "kprof.exe DELAY SECONDS OUT.txt [COMMAND LINE...]\n");
        return 2;
    }
    const double delay = std::atof(argv[1]), seconds = std::atof(argv[2]);
    g_out = fopen(argv[3], "w");
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    auto create = reinterpret_cast<NtCreateProfileFn>(GetProcAddress(ntdll, "NtCreateProfile"));
    auto start = reinterpret_cast<NtProfileFn>(GetProcAddress(ntdll, "NtStartProfile"));
    auto stop = reinterpret_cast<NtProfileFn>(GetProcAddress(ntdll, "NtStopProfile"));
    auto set_interval = reinterpret_cast<NtSetIntervalProfileFn>(GetProcAddress(ntdll, "NtSetIntervalProfile"));
    auto query_interval = reinterpret_cast<NtQueryIntervalProfileFn>(GetProcAddress(ntdll, "NtQueryIntervalProfile"));
    auto query_system = reinterpret_cast<NtQuerySystemInformationFn>(GetProcAddress(ntdll, "NtQuerySystemInformation"));
    if (!create || !start || !stop || !set_interval || !query_system) {
        say("ntdll lacks the profile calls\n");
        return 1;
    }
    say("SeSystemProfilePrivilege %s\n", enable_privilege("SeSystemProfilePrivilege") ? "enabled" : "NOT enabled");

    // The loaded kernel modules.
    std::vector<BYTE> buf(1 << 16);
    ULONG got = 0;
    NTSTATUS st;
    while ((st = query_system(kSystemModuleInformation, buf.data(), ULONG(buf.size()), &got)) == LONG(0xC0000004))
        buf.resize(buf.size() * 2);
    if (st < 0) {
        say("NtQuerySystemInformation(modules) 0x%08lx\n", st);
        return 1;
    }
    const ULONG count = *reinterpret_cast<ULONG*>(buf.data());
    const auto* mods = reinterpret_cast<const ModuleInfo*>(buf.data() + 4);
    std::vector<Range> ranges;
    for (ULONG i = 0; i < count; ++i) {
        Range r;
        r.name = mods[i].path + mods[i].name_offset;
        r.path = win32_path(mods[i].path);
        r.base = DWORD(uintptr_t(mods[i].image_base));
        r.size = mods[i].image_size;
        r.bucket_shift = r.size > (8u << 20) ? 6 : 4;
        ranges.push_back(r);
    }
    // Everything else: kernel space and user space (every process) in
    // coarse buckets.
    {
        Range k;
        k.name = "(kernel space)";
        k.base = 0x80000000u;
        k.size = 0x7FFF0000u;
        k.bucket_shift = 16;
        ranges.push_back(k);
        Range u;
        u.name = "(user space)";
        u.base = 0x00010000u;
        u.size = 0x7FFE0000u;
        u.bucket_shift = 16;
        ranges.push_back(u);
    }

    PROCESS_INFORMATION pi = {};
    if (argc > 4) {
        std::string cmd;
        for (int i = 4; i < argc; ++i) {
            if (i > 4) cmd += ' ';
            const bool quote = std::strchr(argv[i], ' ') != nullptr;
            cmd += quote ? "\"" + std::string(argv[i]) + "\"" : argv[i];
        }
        STARTUPINFOA si = {};
        si.cb = sizeof si;
        std::vector<char> line(cmd.begin(), cmd.end());
        line.push_back(0);
        if (!CreateProcessA(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            say("CreateProcess(%s) failed %lu\n", cmd.c_str(), GetLastError());
            return 1;
        }
        say("started %s, pid %lu\n", cmd.c_str(), pi.dwProcessId);
    }
    Sleep(DWORD(delay * 1000));

    st = set_interval(kInterval, kProfileTime);
    ULONG interval = 0;
    if (query_interval) query_interval(kProfileTime, &interval);
    say("profile interval %lu (set 0x%08lx), %lu modules\n", interval, st, count);
    DWORD_PTR process_mask = 0, system_mask = 0;
    GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask);
    int failed = 0;
    for (Range& r : ranges) {
        r.counts.assign((r.size >> r.bucket_shift) + 1, 0);
        st = create(&r.profile, nullptr, reinterpret_cast<PVOID>(uintptr_t(r.base)), r.size, r.bucket_shift,
                    r.counts.data(), ULONG(r.counts.size() * 4), kProfileTime, system_mask);
        if (st < 0) {
            if (failed++ < 5) say("NtCreateProfile(%s) 0x%08lx\n", r.name.c_str(), st);
            r.profile = nullptr;
        }
    }
    for (Range& r : ranges)
        if (r.profile && (st = start(r.profile)) < 0) say("NtStartProfile(%s) 0x%08lx\n", r.name.c_str(), st);
    const DWORD t0 = GetTickCount();
    Sleep(DWORD(seconds * 1000));
    for (Range& r : ranges)
        if (r.profile) stop(r.profile);
    say("profiled %.1f s\n", (GetTickCount() - t0) / 1000.0);

    // Module totals. The coarse ranges overlap the modules, so the share of
    // kernel time outside any module is the kernel range less the modules.
    unsigned long long kernel = 0, user = 0, in_modules = 0;
    for (Range& r : ranges) {
        for (ULONG c : r.counts) r.hits += c;
        if (r.name == "(kernel space)") kernel = r.hits;
        else if (r.name == "(user space)") user = r.hits;
        else if (r.base >= 0x80000000u) in_modules += r.hits;
    }
    const double total = double(kernel + user);
    say("samples: %llu kernel (%.1f%%), %llu user (%.1f%%), kernel outside modules %llu\n", kernel,
        100.0 * kernel / std::max(1.0, total), user, 100.0 * user / std::max(1.0, total),
        kernel > in_modules ? kernel - in_modules : 0);
    std::vector<Range*> order;
    for (Range& r : ranges)
        if (r.hits && r.name[0] != '(') order.push_back(&r);
    std::sort(order.begin(), order.end(), [](const Range* a, const Range* b) { return a->hits > b->hits; });
    say("\n%-22s %10s %7s  %s\n", "module", "samples", "share", "base, size, path");
    for (const Range* r : order)
        say("%-22s %10llu %6.2f%%  %08lx %07lx %s\n", r->name.c_str(), r->hits, 100.0 * r->hits / std::max(1.0, total),
            r->base, r->size, r->path.c_str());

    // The hottest buckets of the busiest modules.
    for (size_t m = 0; m < order.size() && m < 8; ++m) {
        const Range& r = *order[m];
        if (r.hits * 200 < total) break;  // under 0.5%
        const Image img = read_image(r.path);
        say("\n%s: %llu samples, file timestamp %08lx, %s\n", r.name.c_str(), r.hits, img.timestamp,
            img.codeview.empty() ? "no CodeView record" : img.codeview.c_str());
        std::vector<std::pair<ULONG, DWORD>> hot;
        for (size_t i = 0; i < r.counts.size(); ++i)
            if (r.counts[i]) hot.emplace_back(r.counts[i], DWORD(i << r.bucket_shift));
        std::sort(hot.rbegin(), hot.rend());
        for (size_t i = 0; i < hot.size() && i < 40; ++i) {
            const DWORD rva = hot[i].second;
            const char* near_name = "";
            DWORD near_rva = 0;
            auto it = std::upper_bound(img.exports.begin(), img.exports.end(), std::make_pair(rva + 1, std::string()));
            if (it != img.exports.begin()) {
                --it;
                near_name = it->second.c_str();
                near_rva = it->first;
            }
            say("  rva %06lx %8lu %6.2f%%  %s%s+0x%lx\n", rva, hot[i].first, 100.0 * hot[i].first / std::max(1.0, total),
                near_name, *near_name ? "" : "(no export)", rva - near_rva);
        }
    }
    if (pi.hProcess) {
        WaitForSingleObject(pi.hProcess, 600000);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        say("\nchild exited %lu\n", code);
    }
    if (g_out) fclose(g_out);
    return 0;
}
