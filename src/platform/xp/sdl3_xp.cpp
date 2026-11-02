// The Windows XP build's SDL3 subset (include/SDL3/SDL.h says why it exists).
// Only Win32 calls XP SP3 has; anything newer is looked up at run time.

#include <SDL3/SDL.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <stdio.h>

#include <deque>
#include <string>

namespace {

// ---- UTF-8 <-> UTF-16, which every path crosses

std::wstring widen(const char* s) {
    if (!s || !*s) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? size_t(n - 1) : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    return w;
}

std::string narrow(const wchar_t* w) {
    if (!w || !*w) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? size_t(n - 1) : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

char g_error[512] = "";

bool set_error_from_last(const char* what) {
    SDL_SetError("%s failed (Win32 error %lu)", what, static_cast<unsigned long>(GetLastError()));
    return false;
}

// ---- log

SDL_LogPriority g_priority = SDL_LOG_PRIORITY_INFO;

void default_output(void*, int, SDL_LogPriority priority, const char* message) {
    static const char* const names[] = {"", "TRACE", "VERBOSE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL"};
    const char* name = priority >= 0 && priority <= SDL_LOG_PRIORITY_CRITICAL ? names[priority] : "";
    if (priority == SDL_LOG_PRIORITY_INFO) fprintf(stderr, "%s\n", message);
    else fprintf(stderr, "%s: %s\n", name, message);
    fflush(stderr);
}

SDL_LogOutputFunction g_output = default_output;
void* g_output_data = nullptr;

void log_v(int category, SDL_LogPriority priority, const char* fmt, va_list args) {
    if (priority < g_priority || !g_output) return;
    char message[4096];
    vsnprintf(message, sizeof message, fmt, args);
    g_output(g_output_data, category, priority, message);
}

// ---- time

LARGE_INTEGER g_frequency = {};
LARGE_INTEGER g_start = {};

void init_clock() {
    if (g_frequency.QuadPart) return;
    QueryPerformanceFrequency(&g_frequency);
    QueryPerformanceCounter(&g_start);
}

// ---- the window and its events

struct Window {
    HWND hwnd = nullptr;
    bool relative = false;
};

Window g_window;  // the engine has one
std::deque<SDL_Event> g_events;
SDLXP_WndProcHook g_hook = nullptr;
const wchar_t* const CLASS_NAME = L"DragonXPWindow";

// Windows scan code set 1 (lParam bits 16-23, bit 24 extended) -> HID usage.
SDL_Scancode scancode_from(LPARAM lparam, WPARAM vk) {
    const unsigned code = (unsigned(lparam) >> 16) & 0xFF;
    const bool extended = (lparam >> 24) & 1;
    if (extended) {
        switch (code) {
            case 0x1C: return SDL_SCANCODE_KP_ENTER;
            case 0x1D: return SDL_SCANCODE_RCTRL;
            case 0x35: return SDL_SCANCODE_KP_DIVIDE;
            case 0x37: return SDL_SCANCODE_PRINTSCREEN;
            case 0x38: return SDL_SCANCODE_RALT;
            case 0x47: return SDL_SCANCODE_HOME;
            case 0x48: return SDL_SCANCODE_UP;
            case 0x49: return SDL_SCANCODE_PAGEUP;
            case 0x4B: return SDL_SCANCODE_LEFT;
            case 0x4D: return SDL_SCANCODE_RIGHT;
            case 0x4F: return SDL_SCANCODE_END;
            case 0x50: return SDL_SCANCODE_DOWN;
            case 0x51: return SDL_SCANCODE_PAGEDOWN;
            case 0x52: return SDL_SCANCODE_INSERT;
            case 0x53: return SDL_SCANCODE_DELETE;
            case 0x5B: return SDL_SCANCODE_LGUI;
            case 0x5C: return SDL_SCANCODE_RGUI;
            default: return SDL_SCANCODE_UNKNOWN;
        }
    }
    // Right Shift arrives unextended with its own code; tell it by VK too.
    if (vk == VK_SHIFT) return code == 0x36 ? SDL_SCANCODE_RSHIFT : SDL_SCANCODE_LSHIFT;
    static const Uint8 table[0x59] = {
        0, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5,
        SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_0, SDL_SCANCODE_MINUS,
        SDL_SCANCODE_EQUALS, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_TAB,
        SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R, SDL_SCANCODE_T, SDL_SCANCODE_Y,
        SDL_SCANCODE_U, SDL_SCANCODE_I, SDL_SCANCODE_O, SDL_SCANCODE_P, SDL_SCANCODE_LEFTBRACKET,
        SDL_SCANCODE_RIGHTBRACKET, SDL_SCANCODE_RETURN, SDL_SCANCODE_LCTRL,
        SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_H,
        SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_APOSTROPHE,
        SDL_SCANCODE_GRAVE, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_BACKSLASH,
        SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_B, SDL_SCANCODE_N,
        SDL_SCANCODE_M, SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH, SDL_SCANCODE_RSHIFT,
        SDL_SCANCODE_KP_MULTIPLY, SDL_SCANCODE_LALT, SDL_SCANCODE_SPACE, SDL_SCANCODE_CAPSLOCK,
        SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4, SDL_SCANCODE_F5, SDL_SCANCODE_F6,
        SDL_SCANCODE_F7, SDL_SCANCODE_F8, SDL_SCANCODE_F9, SDL_SCANCODE_F10, SDL_SCANCODE_NUMLOCKCLEAR,
        SDL_SCANCODE_SCROLLLOCK, SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9, SDL_SCANCODE_KP_MINUS,
        SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_PLUS, SDL_SCANCODE_KP_1,
        SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_PERIOD, 0, 0, 0,
        SDL_SCANCODE_F11, SDL_SCANCODE_F12,
    };
    return code < sizeof table ? SDL_Scancode(table[code]) : SDL_SCANCODE_UNKNOWN;
}

void push(const SDL_Event& e) {
    if (g_events.size() < 4096) g_events.push_back(e);
}

void mouse_button(UINT message, WPARAM wparam, LPARAM lparam, HWND hwnd) {
    SDL_Event e = {};
    Uint8 button = SDL_BUTTON_LEFT;
    bool down = true;
    switch (message) {
        case WM_LBUTTONDOWN: button = SDL_BUTTON_LEFT; break;
        case WM_LBUTTONUP: button = SDL_BUTTON_LEFT; down = false; break;
        case WM_RBUTTONDOWN: button = SDL_BUTTON_RIGHT; break;
        case WM_RBUTTONUP: button = SDL_BUTTON_RIGHT; down = false; break;
        case WM_MBUTTONDOWN: button = SDL_BUTTON_MIDDLE; break;
        case WM_MBUTTONUP: button = SDL_BUTTON_MIDDLE; down = false; break;
        case WM_XBUTTONDOWN: button = HIWORD(wparam) == XBUTTON1 ? SDL_BUTTON_X1 : SDL_BUTTON_X2; break;
        case WM_XBUTTONUP: button = HIWORD(wparam) == XBUTTON1 ? SDL_BUTTON_X1 : SDL_BUTTON_X2; down = false; break;
    }
    // Held buttons keep reporting outside the window, as SDL's do.
    if (down) SetCapture(hwnd);
    else if (!(wparam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2))) ReleaseCapture();
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = button;
    e.button.down = down;
    e.button.x = float(short(LOWORD(lparam)));
    e.button.y = float(short(HIWORD(lparam)));
    push(e);
}

// Relative mode: the cursor hidden and held inside the client area; the
// motion comes from raw input (WM_INPUT, XP's) whichever mode is on.
void clip_to_window(HWND hwnd, bool on) {
    if (!on) {
        ClipCursor(nullptr);
        return;
    }
    RECT r;
    GetClientRect(hwnd, &r);
    POINT a = {r.left, r.top}, b = {r.right, r.bottom};
    ClientToScreen(hwnd, &a);
    ClientToScreen(hwnd, &b);
    const LONG cx = (a.x + b.x) / 2, cy = (a.y + b.y) / 2;
    RECT c = {cx - 1, cy - 1, cx + 1, cy + 1};
    ClipCursor(&c);
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (g_hook) {
        if (const intptr_t r = g_hook(hwnd, message, wparam, lparam)) return LRESULT(r);
    }
    SDL_Event e = {};
    switch (message) {
        case WM_CLOSE:
            e.type = SDL_EVENT_QUIT;
            push(e);
            return 0;
        case WM_KILLFOCUS:
            e.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            push(e);
            if (g_window.relative) clip_to_window(hwnd, false);
            break;
        case WM_SETFOCUS:
            if (g_window.relative) clip_to_window(hwnd, true);
            break;
        case WM_SIZE:
        case WM_MOVE:
            if (g_window.relative && GetFocus() == hwnd) clip_to_window(hwnd, true);
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.scancode = scancode_from(lparam, wparam);
            e.key.down = down;
            e.key.repeat = down && ((lparam >> 30) & 1);
            if (e.key.scancode != SDL_SCANCODE_UNKNOWN) push(e);
            // Alt+F4 and the system menu still work; F10 alone does not
            // freeze the loop in menu mode.
            if (message == WM_SYSKEYDOWN && wparam == VK_F4) break;
            if (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) return 0;
            return 0;
        }
        case WM_CHAR: {
            e.type = SDL_EVENT_TEXT_INPUT;
            const wchar_t w[2] = {wchar_t(wparam), 0};
            WideCharToMultiByte(CP_UTF8, 0, w, -1, e.text.text, sizeof e.text.text, nullptr, nullptr);
            push(e);
            return 0;
        }
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_XBUTTONDOWN: case WM_XBUTTONUP:
            mouse_button(message, wparam, lparam, hwnd);
            return message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ? TRUE : 0;
        case WM_MOUSEWHEEL:
            e.type = SDL_EVENT_MOUSE_WHEEL;
            e.wheel.y = float(short(HIWORD(wparam))) / float(WHEEL_DELTA);
            push(e);
            return 0;
        case WM_INPUT: {
            RAWINPUT raw;
            UINT size = sizeof raw;
            if (GetRawInputData(HRAWINPUT(lparam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != UINT(-1) &&
                raw.header.dwType == RIM_TYPEMOUSE && !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                e.type = SDL_EVENT_MOUSE_MOTION;
                e.motion.xrel = float(raw.data.mouse.lLastX);
                e.motion.yrel = float(raw.data.mouse.lLastY);
                POINT p;
                GetCursorPos(&p);
                ScreenToClient(hwnd, &p);
                e.motion.x = float(p.x);
                e.motion.y = float(p.y);
                if (e.motion.xrel != 0.0f || e.motion.yrel != 0.0f) push(e);
            }
            break;
        }
        case WM_SYSCOMMAND:
            // No screensaver or monitor power-off over the game, as SDL3
            // does by default: one that starts over a fullscreen D3D9
            // device takes its focus, and the game waits, lost, until it ends.
            if ((wparam & 0xFFF0) == SC_SCREENSAVE || (wparam & 0xFFF0) == SC_MONITORPOWER) return 0;
            break;
        case WM_SETCURSOR:
            if (g_window.relative && LOWORD(lparam) == HTCLIENT) {
                SetCursor(nullptr);
                return TRUE;
            }
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

// ---- gamepads: XInput, from whichever DLL is there (9.1.0 ships with XP's
// DirectX runtime; 1.3 with the June 2010 redistributable)

struct XInputPad {
    DWORD packet;
    WORD buttons;
    BYTE left_trigger, right_trigger;
    SHORT lx, ly, rx, ry;
};
typedef DWORD(WINAPI* XInputGetStateFn)(DWORD, XInputPad*);
XInputGetStateFn g_xinput_get_state = nullptr;
bool g_xinput_loaded = false;
bool g_connected[4] = {};
DWORD g_last_poll = 0;
XInputPad g_pads[4] = {};

void load_xinput() {
    if (g_xinput_loaded) return;
    g_xinput_loaded = true;
    const char* const names[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll", "xinput1_2.dll", "xinput1_1.dll"};
    for (const char* name : names) {
        if (HMODULE dll = LoadLibraryA(name)) {
            g_xinput_get_state = reinterpret_cast<XInputGetStateFn>(GetProcAddress(dll, "XInputGetState"));
            if (g_xinput_get_state) return;
        }
    }
}

struct Pad {
    DWORD index;
};
Pad g_pad_objects[4] = {{0}, {1}, {2}, {3}};

// Connections are polled at most twice a second: XInputGetState on an empty
// slot is slow enough to show in a frame on old machines.
void poll_gamepads() {
    load_xinput();
    if (!g_xinput_get_state) return;
    const DWORD now = GetTickCount();
    const bool full = now - g_last_poll > 500;
    if (full) g_last_poll = now;
    for (DWORD i = 0; i < 4; ++i) {
        if (!g_connected[i] && !full) continue;
        XInputPad state = {};
        const bool connected = g_xinput_get_state(i, &state) == ERROR_SUCCESS;
        if (connected) g_pads[i] = state;
        if (connected != g_connected[i]) {
            g_connected[i] = connected;
            SDL_Event e = {};
            e.type = connected ? SDL_EVENT_GAMEPAD_ADDED : SDL_EVENT_GAMEPAD_REMOVED;
            e.gdevice.which = i + 1;
            push(e);
        }
    }
}

}  // namespace

extern "C" {

// ---- errors and hints

const char* SDL_GetError(void) { return g_error; }

bool SDL_SetError(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_error, sizeof g_error, fmt, args);
    va_end(args);
    return false;
}

bool SDL_SetHint(const char*, const char*) { return true; }

// ---- init

bool SDL_Init(SDL_InitFlags) {
    init_clock();
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.lpszClassName = CLASS_NAME;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return set_error_from_last("RegisterClassExW");
    return true;
}

void SDL_Quit(void) {
    ClipCursor(nullptr);
    UnregisterClassW(CLASS_NAME, GetModuleHandleW(nullptr));
}

// ---- log

void SDL_SetLogPriorities(SDL_LogPriority priority) { g_priority = priority; }

void SDL_GetLogOutputFunction(SDL_LogOutputFunction* callback, void** userdata) {
    if (callback) *callback = g_output;
    if (userdata) *userdata = g_output_data;
}

void SDL_SetLogOutputFunction(SDL_LogOutputFunction callback, void* userdata) {
    g_output = callback;
    g_output_data = userdata;
}

#define LOG_BODY(priority)          \
    va_list args;                   \
    va_start(args, fmt);            \
    log_v(category, priority, fmt, args); \
    va_end(args)

void SDL_Log(const char* fmt, ...) {
    const int category = SDL_LOG_CATEGORY_APPLICATION;
    LOG_BODY(SDL_LOG_PRIORITY_INFO);
}
void SDL_LogDebug(int category, const char* fmt, ...) { LOG_BODY(SDL_LOG_PRIORITY_DEBUG); }
void SDL_LogInfo(int category, const char* fmt, ...) { LOG_BODY(SDL_LOG_PRIORITY_INFO); }
void SDL_LogWarn(int category, const char* fmt, ...) { LOG_BODY(SDL_LOG_PRIORITY_WARN); }
void SDL_LogError(int category, const char* fmt, ...) { LOG_BODY(SDL_LOG_PRIORITY_ERROR); }

// ---- time

Uint64 SDL_GetTicksNS(void) {
    init_clock();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const Uint64 ticks = Uint64(now.QuadPart - g_start.QuadPart);
    const Uint64 f = Uint64(g_frequency.QuadPart);
    return (ticks / f) * 1000000000ull + (ticks % f) * 1000000000ull / f;
}

Uint64 SDL_GetTicks(void) { return SDL_GetTicksNS() / 1000000ull; }

void SDL_Delay(Uint32 ms) { Sleep(ms); }

// ---- the CPU

int SDL_GetNumLogicalCPUCores(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return int(info.dwNumberOfProcessors);
}

// ---- files and paths

void* SDL_LoadFile(const char* file, size_t* datasize) {
    if (datasize) *datasize = 0;
    HANDLE h = CreateFileW(widen(file).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        SDL_SetError("could not open %s", file);
        return nullptr;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart > 0x7fffffff) {
        CloseHandle(h);
        SDL_SetError("could not size %s", file);
        return nullptr;
    }
    char* data = static_cast<char*>(malloc(size_t(size.QuadPart) + 1));
    DWORD done = 0;
    const bool ok = data && ReadFile(h, data, DWORD(size.QuadPart), &done, nullptr) && done == DWORD(size.QuadPart);
    CloseHandle(h);
    if (!ok) {
        free(data);
        SDL_SetError("could not read %s", file);
        return nullptr;
    }
    data[done] = '\0';  // SDL null-terminates, and text parsers rely on it
    if (datasize) *datasize = done;
    return data;
}

bool SDL_SaveFile(const char* file, const void* data, size_t datasize) {
    HANDLE h = CreateFileW(widen(file).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return SDL_SetError("could not create %s", file);
    DWORD done = 0;
    const bool ok = WriteFile(h, data, DWORD(datasize), &done, nullptr) && done == DWORD(datasize);
    CloseHandle(h);
    return ok ? true : SDL_SetError("could not write %s", file);
}

bool SDL_GetPathInfo(const char* path, SDL_PathInfo* info) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(widen(path).c_str(), GetFileExInfoStandard, &data)) {
        if (info) info->type = SDL_PATHTYPE_NONE;
        return SDL_SetError("no such path: %s", path);
    }
    if (info) {
        *info = SDL_PathInfo{};
        info->type = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? SDL_PATHTYPE_DIRECTORY : SDL_PATHTYPE_FILE;
        info->size = (Uint64(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    }
    return true;
}

const char* SDL_GetBasePath(void) {
    static std::string base;
    if (base.empty()) {
        wchar_t path[MAX_PATH * 2];
        const DWORD n = GetModuleFileNameW(nullptr, path, DWORD(sizeof path / sizeof path[0]));
        if (n == 0 || n >= sizeof path / sizeof path[0]) return nullptr;
        std::wstring w(path, n);
        const size_t slash = w.find_last_of(L"\\/");
        base = narrow(w.substr(0, slash + 1).c_str());
    }
    return base.c_str();
}

char* SDL_GetPrefPath(const char* org, const char* app) {
    wchar_t appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE, nullptr, 0, appdata))) {
        SDL_SetError("no application data folder");
        return nullptr;
    }
    std::wstring path = appdata;
    for (const char* part : {org, app}) {
        if (!part || !*part) continue;
        path += L'\\';
        path += widen(part);
        CreateDirectoryW(path.c_str(), nullptr);
    }
    path += L'\\';
    return _strdup(narrow(path.c_str()).c_str());
}

// ---- properties

void* SDL_GetPointerProperty(SDL_PropertiesID props, const char* name, void* default_value) {
    if (props == 1 && name && strcmp(name, SDL_PROP_WINDOW_WIN32_HWND_POINTER) == 0 && g_window.hwnd) return g_window.hwnd;
    return default_value;
}

// ---- the window

SDL_Window* SDL_CreateWindow(const char* title, int w, int h, SDL_WindowFlags flags) {
    if (g_window.hwnd) {
        SDL_SetError("the XP build has one window");
        return nullptr;
    }
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!(flags & SDL_WINDOW_RESIZABLE)) style &= ~DWORD(WS_THICKFRAME | WS_MAXIMIZEBOX);
    RECT r = {0, 0, w, h};
    AdjustWindowRect(&r, style, FALSE);
    HWND hwnd = CreateWindowExW(0, CLASS_NAME, widen(title).c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
                                r.right - r.left, r.bottom - r.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) {
        set_error_from_last("CreateWindowExW");
        return nullptr;
    }
    g_window.hwnd = hwnd;
    // Raw mouse motion, for the relative mode and the free look.
    RAWINPUTDEVICE mouse = {0x01, 0x02, 0, hwnd};
    RegisterRawInputDevices(&mouse, 1, sizeof mouse);
    if (!(flags & SDL_WINDOW_HIDDEN)) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);
    }
    return reinterpret_cast<SDL_Window*>(&g_window);
}

void SDL_DestroyWindow(SDL_Window* window) {
    if (!window || !g_window.hwnd) return;
    ClipCursor(nullptr);
    DestroyWindow(g_window.hwnd);
    g_window = Window{};
}

SDL_PropertiesID SDL_GetWindowProperties(SDL_Window* window) { return window ? 1 : 0; }

// ---- the display (SDL.h says why there are no modes)

const char* SDL_GetCurrentVideoDriver(void) { return "windows"; }
SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window* window) { return window ? 1 : 0; }
const SDL_DisplayMode* SDL_GetDesktopDisplayMode(SDL_DisplayID) {
    SDL_SetError("no display modes in the XP subset");
    return nullptr;
}
SDL_DisplayMode** SDL_GetFullscreenDisplayModes(SDL_DisplayID, int* count) {
    if (count) *count = 0;
    return nullptr;
}
bool SDL_SetWindowFullscreenMode(SDL_Window*, const SDL_DisplayMode*) {
    return SDL_SetError("no display modes in the XP subset");
}
bool SDL_SetWindowFullscreen(SDL_Window*, bool) { return SDL_SetError("no display modes in the XP subset"); }
bool SDL_SyncWindow(SDL_Window* window) { return window != nullptr; }

bool SDL_GetWindowSize(SDL_Window* window, int* w, int* h) {
    RECT r = {};
    if (!window || !GetClientRect(g_window.hwnd, &r)) return SDL_SetError("no window");
    if (w) *w = r.right - r.left;
    if (h) *h = r.bottom - r.top;
    return true;
}

// XP has no per-monitor scaling: a window's points are its pixels.
bool SDL_GetWindowSizeInPixels(SDL_Window* window, int* w, int* h) { return SDL_GetWindowSize(window, w, h); }

bool SDL_SetWindowSize(SDL_Window* window, int w, int h) {
    if (!window) return SDL_SetError("no window");
    const DWORD style = DWORD(GetWindowLongW(g_window.hwnd, GWL_STYLE));
    RECT r = {0, 0, w, h};
    AdjustWindowRect(&r, style, FALSE);
    return SetWindowPos(g_window.hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != 0;
}

bool SDL_RestoreWindow(SDL_Window* window) {
    if (!window) return SDL_SetError("no window");
    ShowWindow(g_window.hwnd, SW_RESTORE);
    return true;
}

bool SDL_SetWindowRelativeMouseMode(SDL_Window* window, bool enabled) {
    if (!window) return SDL_SetError("no window");
    if (g_window.relative == enabled) return true;
    g_window.relative = enabled;
    clip_to_window(g_window.hwnd, enabled && GetFocus() == g_window.hwnd);
    // ShowCursor counts; one step each way keeps it balanced.
    ShowCursor(enabled ? FALSE : TRUE);
    return true;
}

void SDLXP_SetWndProcHook(SDLXP_WndProcHook hook) { g_hook = hook; }

// ---- events

bool SDL_PollEvent(SDL_Event* event) {
    if (g_events.empty()) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        poll_gamepads();
    }
    if (g_events.empty()) return false;
    if (event) *event = g_events.front();
    g_events.pop_front();
    return true;
}

// ---- gamepads

SDL_Gamepad* SDL_OpenGamepad(SDL_JoystickID id) {
    if (id < 1 || id > 4 || !g_connected[id - 1]) {
        SDL_SetError("no gamepad %u", unsigned(id));
        return nullptr;
    }
    return reinterpret_cast<SDL_Gamepad*>(&g_pad_objects[id - 1]);
}

void SDL_CloseGamepad(SDL_Gamepad*) {}

const char* SDL_GetGamepadName(SDL_Gamepad*) { return "XInput Controller"; }

Sint16 SDL_GetGamepadAxis(SDL_Gamepad* gamepad, SDL_GamepadAxis axis) {
    if (!gamepad) return 0;
    const XInputPad& p = g_pads[reinterpret_cast<Pad*>(gamepad)->index];
    // SDL's Y axes are down-positive; XInput's are up-positive. Triggers are
    // 0..32767 in SDL.
    auto flip = [](SHORT v) { return Sint16(v == -32768 ? 32767 : -v); };
    switch (axis) {
        case SDL_GAMEPAD_AXIS_LEFTX: return p.lx;
        case SDL_GAMEPAD_AXIS_LEFTY: return flip(p.ly);
        case SDL_GAMEPAD_AXIS_RIGHTX: return p.rx;
        case SDL_GAMEPAD_AXIS_RIGHTY: return flip(p.ry);
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return Sint16(p.left_trigger * 32767 / 255);
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return Sint16(p.right_trigger * 32767 / 255);
        default: return 0;
    }
}

bool SDL_GetGamepadButton(SDL_Gamepad* gamepad, SDL_GamepadButton button) {
    if (!gamepad) return false;
    const WORD b = g_pads[reinterpret_cast<Pad*>(gamepad)->index].buttons;
    switch (button) {
        case SDL_GAMEPAD_BUTTON_SOUTH: return b & 0x1000;           // A
        case SDL_GAMEPAD_BUTTON_EAST: return b & 0x2000;            // B
        case SDL_GAMEPAD_BUTTON_WEST: return b & 0x4000;            // X
        case SDL_GAMEPAD_BUTTON_NORTH: return b & 0x8000;           // Y
        case SDL_GAMEPAD_BUTTON_BACK: return b & 0x0020;
        case SDL_GAMEPAD_BUTTON_START: return b & 0x0010;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: return b & 0x0040;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return b & 0x0080;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return b & 0x0100;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return b & 0x0200;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return b & 0x0001;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return b & 0x0002;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return b & 0x0004;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return b & 0x0008;
        default: return false;
    }
}

// ---- surfaces

SDL_Surface* SDL_CreateSurfaceFrom(int width, int height, SDL_PixelFormat format, void* pixels, int pitch) {
    if (format != SDL_PIXELFORMAT_RGBA32 || !pixels || width <= 0 || height <= 0) {
        SDL_SetError("unsupported surface");
        return nullptr;
    }
    auto* s = static_cast<SDL_Surface*>(calloc(1, sizeof(SDL_Surface)));
    if (!s) return nullptr;
    s->format = format;
    s->w = width;
    s->h = height;
    s->pitch = pitch;
    s->pixels = pixels;
    return s;
}

// A 32-bit bottom-up BMP with an alpha channel, the layout SDL writes for
// RGBA32: what tools/golden reads either way.
bool SDL_SaveBMP(SDL_Surface* surface, const char* file) {
    if (!surface) return SDL_SetError("no surface");
    const int w = surface->w, h = surface->h;
    const DWORD row = DWORD(w) * 4, image = row * DWORD(h);
    BITMAPFILEHEADER fh = {};
    BITMAPINFOHEADER ih = {};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + image;
    ih.biSize = sizeof ih;
    ih.biWidth = w;
    ih.biHeight = h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = image;
    std::string out(fh.bfSize, '\0');
    memcpy(&out[0], &fh, sizeof fh);
    memcpy(&out[sizeof fh], &ih, sizeof ih);
    for (int y = 0; y < h; ++y) {
        const Uint8* src = static_cast<const Uint8*>(surface->pixels) + size_t(h - 1 - y) * size_t(surface->pitch);
        Uint8* dst = reinterpret_cast<Uint8*>(&out[fh.bfOffBits + size_t(y) * row]);
        for (int x = 0; x < w; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }
    return SDL_SaveFile(file, out.data(), out.size());
}

void SDL_DestroySurface(SDL_Surface* surface) { free(surface); }

}  // extern "C"
