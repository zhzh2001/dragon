// sampler: where a running 32-bit process spends its CPU, by module.
//
//   sampler.exe NAME.exe SECONDS OUT.txt
//
// Waits for a process of that name, finds its busiest thread over half a
// second, then suspends it a thousand times a second for SECONDS and reads
// its instruction pointer. Prints the share of samples in each module and,
// for the busiest modules, the hottest 16-byte buckets as RVAs, with the
// nearest export and the module's CodeView record (PDB name, GUID, age), so
// the RVAs can be named offline against Microsoft's public symbols
// (llvm-symbolizer --obj=DLL with the PDB beside it). Built with the XP
// toolchain beside swvp.exe; runs on XP, 7 and 10, from the same user (an
// SSH session on Windows 10 can sample a desktop process).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

FILE* g_out = nullptr;
void say(const char* fmt, ...) {
    char line[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof line, fmt, args);
    va_end(args);
    fputs(line, stdout);
    if (g_out) fputs(line, g_out);
}

struct Module {
    std::string name;
    DWORD base = 0, size = 0;
    std::vector<std::pair<DWORD, std::string>> exports;  // rva, name, sorted
    std::string codeview;
};

bool read(HANDLE p, DWORD addr, void* out, size_t n) {
    SIZE_T got = 0;
    return ReadProcessMemory(p, reinterpret_cast<LPCVOID>(uintptr_t(addr)), out, n, &got) && got == n;
}

// The export names and the CodeView record, read out of the target's memory.
void read_headers(HANDLE p, Module& m) {
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS32 nt;
    if (!read(p, m.base, &dos, sizeof dos) || !read(p, m.base + dos.e_lfanew, &nt, sizeof nt)) return;
    const IMAGE_DATA_DIRECTORY& ed = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    IMAGE_EXPORT_DIRECTORY ex;
    if (ed.VirtualAddress && read(p, m.base + ed.VirtualAddress, &ex, sizeof ex)) {
        std::vector<DWORD> names(ex.NumberOfNames), funcs(ex.NumberOfFunctions);
        std::vector<WORD> ords(ex.NumberOfNames);
        read(p, m.base + ex.AddressOfNames, names.data(), names.size() * 4);
        read(p, m.base + ex.AddressOfFunctions, funcs.data(), funcs.size() * 4);
        read(p, m.base + ex.AddressOfNameOrdinals, ords.data(), ords.size() * 2);
        for (size_t i = 0; i < names.size(); ++i) {
            char buf[128] = {};
            read(p, m.base + names[i], buf, sizeof buf - 1);
            if (ords[i] < funcs.size()) m.exports.emplace_back(funcs[ords[i]], buf);
        }
        std::sort(m.exports.begin(), m.exports.end());
    }
    const IMAGE_DATA_DIRECTORY& dd = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    for (DWORD off = 0; dd.VirtualAddress && off + sizeof(IMAGE_DEBUG_DIRECTORY) <= dd.Size;
         off += sizeof(IMAGE_DEBUG_DIRECTORY)) {
        IMAGE_DEBUG_DIRECTORY d;
        if (!read(p, m.base + dd.VirtualAddress + off, &d, sizeof d) || d.Type != IMAGE_DEBUG_TYPE_CODEVIEW) continue;
        struct {
            DWORD sig;
            GUID guid;
            DWORD age;
            char pdb[260];
        } cv = {};
        if (!read(p, m.base + d.AddressOfRawData, &cv, std::min<DWORD>(d.SizeOfData, sizeof cv)) || cv.sig != 0x53445352)
            continue;  // 'RSDS'
        char guid[64];
        snprintf(guid, sizeof guid, "%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%lX", cv.guid.Data1, cv.guid.Data2,
                 cv.guid.Data3, cv.guid.Data4[0], cv.guid.Data4[1], cv.guid.Data4[2], cv.guid.Data4[3], cv.guid.Data4[4],
                 cv.guid.Data4[5], cv.guid.Data4[6], cv.guid.Data4[7], cv.age);
        cv.pdb[259] = 0;
        m.codeview = std::string(cv.pdb) + " " + guid;
    }
}

std::vector<Module> modules(DWORD pid, HANDLE p) {
    std::vector<Module> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | 0x10 /* TH32CS_SNAPMODULE32 */, pid);
    if (snap == INVALID_HANDLE_VALUE) snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);  // XP
    MODULEENTRY32 me = {sizeof me};
    for (BOOL ok = Module32First(snap, &me); ok; ok = Module32Next(snap, &me)) {
        Module m;
        m.name = me.szModule;
        m.base = DWORD(uintptr_t(me.modBaseAddr));
        m.size = me.modBaseSize;
        read_headers(p, m);
        out.push_back(m);
    }
    CloseHandle(snap);
    return out;
}

ULONGLONG thread_cpu(HANDLE t) {
    FILETIME c, e, k, u;
    if (!GetThreadTimes(t, &c, &e, &k, &u)) return 0;
    return (ULONGLONG(k.dwHighDateTime) << 32 | k.dwLowDateTime) + (ULONGLONG(u.dwHighDateTime) << 32 | u.dwLowDateTime);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: sampler NAME.exe SECONDS OUT.txt\n");
        return 2;
    }
    g_out = fopen(argv[3], "w");
    const double seconds = atof(argv[2]);
    DWORD pid = 0;
    for (int wait = 0; !pid && wait < 600; ++wait) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 pe = {sizeof pe};
        for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
            if (!_stricmp(pe.szExeFile, argv[1])) pid = pe.th32ProcessID;
        CloseHandle(snap);
        if (!pid) Sleep(100);
    }
    if (!pid) {
        say("no %s\n", argv[1]);
        return 1;
    }
    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    // Its threads, and the busiest one over half a second.
    std::vector<std::pair<DWORD, HANDLE>> threads;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = {sizeof te};
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (t) threads.emplace_back(te.th32ThreadID, t);
    }
    CloseHandle(snap);
    std::vector<ULONGLONG> before;
    for (auto& t : threads) before.push_back(thread_cpu(t.second));
    Sleep(500);
    size_t busiest = 0;
    ULONGLONG most = 0;
    for (size_t i = 0; i < threads.size(); ++i) {
        const ULONGLONG used = thread_cpu(threads[i].second) - before[i];
        if (used >= most) {
            most = used;
            busiest = i;
        }
    }
    if (threads.empty()) {
        say("no threads\n");
        return 1;
    }
    HANDLE th = threads[busiest].second;
    say("pid %lu, %u threads, sampling thread %lu (%.0f%% of a core over 0.5 s)\n", pid, unsigned(threads.size()),
        threads[busiest].first, most / 50000.0);

    std::vector<Module> mods = modules(pid, proc);
    std::map<DWORD, unsigned> hits;  // instruction pointer, count
    unsigned samples = 0;
    timeBeginPeriod(1);
    const DWORD end = GetTickCount() + DWORD(seconds * 1000);
    const ULONGLONG cpu0 = thread_cpu(th);
    const DWORD t0 = GetTickCount();
    while (GetTickCount() < end) {
        if (SuspendThread(th) != DWORD(-1)) {
            CONTEXT c = {};
            c.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(th, &c)) {
                ++hits[c.Eip];
                ++samples;
            }
            ResumeThread(th);
        }
        Sleep(1);
    }
    timeEndPeriod(1);
    const double cpu = (thread_cpu(th) - cpu0) / 1e7, wall = (GetTickCount() - t0) / 1000.0;
    say("%u samples over %.1f s; the thread used %.1f s of CPU (%.0f%%)\n", samples, wall, cpu, 100 * cpu / wall);

    // By module.
    std::map<int, unsigned> per_module;
    std::map<int, std::map<DWORD, unsigned>> buckets;
    for (auto& h : hits) {
        int found = -1;
        for (size_t i = 0; i < mods.size(); ++i)
            if (h.first >= mods[i].base && h.first < mods[i].base + mods[i].size) found = int(i);
        per_module[found] += h.second;
        if (found >= 0) buckets[found][(h.first - mods[found].base) & ~15u] += h.second;
    }
    std::vector<std::pair<unsigned, int>> order;
    for (auto& pm : per_module) order.emplace_back(pm.second, pm.first);
    std::sort(order.rbegin(), order.rend());
    for (auto& o : order)
        say("%6.1f%%  %s\n", 100.0 * o.first / samples, o.second < 0 ? "(no module)" : mods[o.second].name.c_str());
    for (size_t k = 0; k < order.size() && k < 4; ++k) {
        if (order[k].second < 0) continue;
        const Module& m = mods[order[k].second];
        say("\n%s base %08lx size %lx  pdb %s\n", m.name.c_str(), m.base, m.size, m.codeview.c_str());
        std::vector<std::pair<unsigned, DWORD>> top;
        for (auto& b : buckets[order[k].second]) top.emplace_back(b.second, b.first);
        std::sort(top.rbegin(), top.rend());
        for (size_t i = 0; i < top.size() && i < 25; ++i) {
            const DWORD rva = top[i].second;
            std::string near_export = "?";
            for (auto& e : m.exports)
                if (e.first <= rva) {
                    char buf[200];
                    snprintf(buf, sizeof buf, "%s+0x%lx", e.second.c_str(), rva - e.first);
                    near_export = buf;
                }
            say("  %5.1f%%  rva %08lx  %s\n", 100.0 * top[i].first / samples, rva, near_export.c_str());
        }
    }
    if (g_out) fclose(g_out);
    return 0;
}
