// pcicfg: the PCI devices and their configuration space, from user mode.
//
//   pcicfg.exe OUT.txt [VENDOR]
//   pcicfg.exe OUT.txt set BUS:DEV.FN OFFSET VALUE [BYTES]   (all hex; BYTES 1, 2 or 4)
//   pcicfg.exe OUT.txt port BUS:DEV.FN OFFSET VALUE          (a dword, through CF8h/CFCh)
//
// Reads configuration space through NtSystemDebugControl's
// SysDbgReadBusData (XP's 32-bit kernel still answers it for an
// administrator holding SeDebugPrivilege), walking buses 0-31. Lists every
// function with its command register (bus master, memory and I/O decode),
// status, class and BARs; bridges with their bus numbers and bridge
// control; any PCI Express capability's link (width, speed) and device
// control (max payload, max read request). Every function of VENDOR (hex,
// default 102b, Matrox) and every bridge gets its whole 256 bytes dumped,
// so a register can be read off offline. "set" writes one register through
// SysDbgWriteBusData and reads it back: how the G550's bridge was tuned
// live (docs/PORTING.md, "The G550 on the G41"); a reboot undoes it. The
// HAL refuses some header registers (the cache line size); "port" writes
// those through configuration mechanism 1's ports instead
// (SysDbgWriteIoSpace), with nothing to stop another access interleaving.
// Built with the XP toolchain beside
// tris.exe (build.sh).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

typedef LONG NTSTATUS;
typedef NTSTATUS(NTAPI* NtSystemDebugControlFn)(ULONG, PVOID, ULONG, PVOID, ULONG, PULONG);
const ULONG kSysDbgReadBusData = 18;
const ULONG kSysDbgWriteBusData = 19;
const ULONG kSysDbgReadIoSpace = 14;
const ULONG kSysDbgWriteIoSpace = 15;

struct IoSpace {  // SYSDBG_IO_SPACE
    ULONGLONG address;
    PVOID buffer;
    ULONG request;
    ULONG interface_type;  // Isa
    ULONG bus_number;
    ULONG address_space;  // 1: I/O
};
const ULONG kPciConfiguration = 4;  // BUS_DATA_TYPE

struct BusData {  // SYSDBG_BUS_DATA
    ULONG address;
    PVOID buffer;
    ULONG request;
    ULONG bus_data_type;
    ULONG bus_number;
    ULONG slot_number;
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

NtSystemDebugControlFn g_control = nullptr;
NTSTATUS g_last = 0;

bool read_config(ULONG bus, ULONG dev, ULONG fn, ULONG offset, void* out, ULONG n) {
    BusData d = {offset, out, n, kPciConfiguration, bus, dev | (fn << 5)};
    ULONG got = 0;
    g_last = g_control(kSysDbgReadBusData, &d, sizeof d, nullptr, 0, &got);
    return g_last >= 0;
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

}  // namespace

int main(int argc, char** argv) {
    g_out = fopen(argc > 1 ? argv[1] : "pcicfg.txt", "w");
    const unsigned vendor_dump = argc > 2 ? unsigned(strtoul(argv[2], nullptr, 16)) : 0x102B;
    say("SeDebugPrivilege %s\n", enable_privilege("SeDebugPrivilege") ? "enabled" : "NOT enabled");
    g_control = reinterpret_cast<NtSystemDebugControlFn>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtSystemDebugControl"));
    if (!g_control) {
        say("no NtSystemDebugControl\n");
        return 1;
    }
    if (argc > 5 && !std::strcmp(argv[2], "port")) {
        unsigned bus = 0, dev = 0, fn = 0;
        if (sscanf(argv[3], "%x:%x.%x", &bus, &dev, &fn) != 3) {
            say("bad address %s\n", argv[3]);
            return 2;
        }
        const ULONG offset = strtoul(argv[4], nullptr, 16) & 0xFC, value = strtoul(argv[5], nullptr, 16);
        auto io = [&](ULONG command, USHORT port, ULONG* data) {
            IoSpace s = {port, data, 4, 1, 0, 1};
            ULONG got = 0;
            return g_control(command, &s, sizeof s, nullptr, 0, &got);
        };
        ULONG addr = 0x80000000u | bus << 16 | dev << 11 | fn << 8 | offset, before = 0, after = 0, v = value;
        NTSTATUS st = io(kSysDbgWriteIoSpace, 0xCF8, &addr);
        if (st >= 0) st = io(kSysDbgReadIoSpace, 0xCFC, &before);
        if (st >= 0) st = io(kSysDbgWriteIoSpace, 0xCFC, &v);
        if (st >= 0) st = io(kSysDbgReadIoSpace, 0xCFC, &after);
        say("%02x:%02x.%x +%02lx by port: %08lx -> wrote %08lx (status 0x%08lx) -> %08lx\n", bus, dev, fn, offset, before,
            value, st, after);
        if (g_out) fclose(g_out);
        return st < 0 ? 1 : 0;
    }
    if (argc > 5 && !std::strcmp(argv[2], "set")) {
        unsigned bus = 0, dev = 0, fn = 0;
        if (sscanf(argv[3], "%x:%x.%x", &bus, &dev, &fn) != 3) {
            say("bad address %s\n", argv[3]);
            return 2;
        }
        const ULONG offset = strtoul(argv[4], nullptr, 16), value = strtoul(argv[5], nullptr, 16);
        const ULONG bytes = argc > 6 ? strtoul(argv[6], nullptr, 16) : 1;
        ULONG before = 0, after = 0;
        read_config(bus, dev, fn, offset, &before, bytes);
        ULONG v = value;
        BusData d = {offset, &v, bytes, kPciConfiguration, bus, dev | (fn << 5)};
        ULONG got = 0;
        const NTSTATUS st = g_control(kSysDbgWriteBusData, &d, sizeof d, nullptr, 0, &got);
        read_config(bus, dev, fn, offset, &after, bytes);
        say("%02x:%02x.%x +%02lx (%lu bytes): %08lx -> wrote %08lx (status 0x%08lx) -> %08lx\n", bus, dev, fn, offset, bytes,
            before, value, st, after);
        if (g_out) fclose(g_out);
        return st < 0 ? 1 : 0;
    }
    DWORD id = 0;
    if (!read_config(0, 0, 0, 0, &id, 4)) {
        say("SysDbgReadBusData refused: 0x%08lx\n", g_last);
        return 1;
    }
    for (ULONG bus = 0; bus < 32; ++bus) {
        for (ULONG dev = 0; dev < 32; ++dev) {
            for (ULONG fn = 0; fn < 8; ++fn) {
                BYTE c[256] = {};
                if (!read_config(bus, dev, fn, 0, c, 4)) continue;
                const WORD ven = WORD(c[0] | c[1] << 8), did = WORD(c[2] | c[3] << 8);
                if (ven == 0xFFFF || ven == 0) {
                    if (fn == 0) break;
                    continue;
                }
                read_config(bus, dev, fn, 0, c, 256);
                auto w16 = [&](int o) { return unsigned(c[o] | c[o + 1] << 8); };
                auto w32 = [&](int o) { return unsigned(c[o] | c[o + 1] << 8 | c[o + 2] << 16 | unsigned(c[o + 3]) << 24); };
                const unsigned cmd = w16(4), status = w16(6), header = c[14] & 0x7F;
                say("\n%02lx:%02lx.%lx %04x:%04x class %02x%02x%02x rev %02x header %u  cmd %04x [%s%s%s%s] status %04x\n",
                    bus, dev, fn, ven, did, c[11], c[10], c[9], c[8], header, cmd, (cmd & 4) ? "BUSMASTER " : "busmaster-off ",
                    (cmd & 2) ? "mem " : "", (cmd & 1) ? "io " : "", (cmd & 0x400) ? "intx-off" : "", status);
                const int bars = header == 0 ? 6 : header == 1 ? 2 : 0;
                for (int b = 0; b < bars; ++b)
                    if (w32(0x10 + b * 4)) say("  BAR%d %08x\n", b, w32(0x10 + b * 4));
                if (header == 0) say("  subsystem %04x:%04x\n", w16(0x2C), w16(0x2E));
                if (header == 1)
                    say("  buses primary %u secondary %u subordinate %u, bridge control %04x, mem %04x-%04x, pref %04x-%04x\n",
                        c[0x18], c[0x19], c[0x1A], w16(0x3E), w16(0x20), w16(0x22), w16(0x24), w16(0x26));
                // The capability list.
                if (status & 0x10) {
                    int at = c[header == 2 ? 0x14 : 0x34] & 0xFC, guard = 0;
                    while (at >= 0x40 && guard++ < 48) {
                        const BYTE cap = c[at];
                        say("  cap %02x at %02x", cap, at);
                        if (cap == 0x10) {  // PCI Express
                            const unsigned pcie = w16(at + 2), devctl = w16(at + 8), lnkcap = w32(at + 12),
                                           lnkctl = w16(at + 16), lnkst = w16(at + 18);
                            say(" PCIe type %u: devctl %04x (max payload %u, max read %u), link cap x%u gen%u, "
                                "link ctl %04x, link status x%u gen%u",
                                (pcie >> 4) & 15, devctl, 128u << ((devctl >> 5) & 7), 128u << ((devctl >> 12) & 7),
                                (lnkcap >> 4) & 63, lnkcap & 15, lnkctl, (lnkst >> 4) & 63, lnkst & 15);
                        } else if (cap == 0x02) {  // AGP
                            say(" AGP status %08x command %08x", w32(at + 4), w32(at + 8));
                        } else if (cap == 0x01) {
                            say(" power management, control %04x", w16(at + 4));
                        }
                        say("\n");
                        at = c[at + 1] & 0xFC;
                    }
                }
                if (ven == vendor_dump || header == 1) {
                    for (int row = 0; row < 256; row += 16) {
                        say("  %02x:", row);
                        for (int i = 0; i < 16; ++i) say(" %02x", c[row + i]);
                        say("\n");
                    }
                }
                if (fn == 0 && !(c[14] & 0x80)) break;  // single function
            }
        }
    }
    if (g_out) fclose(g_out);
    return 0;
}
