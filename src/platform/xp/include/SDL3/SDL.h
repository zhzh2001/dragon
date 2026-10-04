#pragma once

// The Windows XP build's stand-in for SDL3 (docs/PORTING.md, R6).
//
// SDL3 needs Windows 7. Rather than put a platform interface under the whole
// engine, the XP build compiles the same sources against this: the subset of
// SDL3's API the engine calls outside the SDL GPU backend (which the XP build
// does not have) -- one window, its events, the keyboard by scancode, the
// mouse with a raw relative mode, XInput gamepads, logging, files, paths,
// timers and a BMP writer -- with SDL3's names and signatures, implemented on
// Win32 calls XP SP3 has (src/platform/xp/sdl3_xp.cpp). Every other build
// links the real SDL3; nothing here is used there. A call the engine starts
// making that this does not declare fails the XP build, which is the point:
// add it here, on XP's API, or keep it out of the shared code.
//
// The values are SDL3's where code could depend on them (scancodes are USB
// HID usages, as in SDL; button indices; the gamepad axis and button order),
// so a recorded binding means the same in both builds.

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t Uint8;
typedef int16_t Sint16;
typedef uint16_t Uint16;
typedef int32_t Sint32;
typedef uint32_t Uint32;
typedef int64_t Sint64;
typedef uint64_t Uint64;

// ---- stdinc
#define SDL_getenv getenv
#define SDL_atoi atoi
#define SDL_atof atof
#define SDL_sscanf sscanf
#define SDL_free free
#define SDL_malloc malloc

// ---- errors and hints
const char* SDL_GetError(void);
bool SDL_SetError(const char* fmt, ...);
#define SDL_HINT_VIDEO_DRIVER "SDL_VIDEO_DRIVER"
bool SDL_SetHint(const char* name, const char* value);

// ---- init
typedef Uint32 SDL_InitFlags;
#define SDL_INIT_VIDEO 0x00000020u
#define SDL_INIT_GAMEPAD 0x00002000u
bool SDL_Init(SDL_InitFlags flags);
void SDL_Quit(void);

// ---- log
typedef enum SDL_LogPriority {
    SDL_LOG_PRIORITY_INVALID,
    SDL_LOG_PRIORITY_TRACE,
    SDL_LOG_PRIORITY_VERBOSE,
    SDL_LOG_PRIORITY_DEBUG,
    SDL_LOG_PRIORITY_INFO,
    SDL_LOG_PRIORITY_WARN,
    SDL_LOG_PRIORITY_ERROR,
    SDL_LOG_PRIORITY_CRITICAL,
} SDL_LogPriority;
#define SDL_LOG_CATEGORY_APPLICATION 0
typedef void (*SDL_LogOutputFunction)(void* userdata, int category, SDL_LogPriority priority, const char* message);
void SDL_SetLogPriorities(SDL_LogPriority priority);
void SDL_GetLogOutputFunction(SDL_LogOutputFunction* callback, void** userdata);
void SDL_SetLogOutputFunction(SDL_LogOutputFunction callback, void* userdata);
void SDL_Log(const char* fmt, ...);
void SDL_LogDebug(int category, const char* fmt, ...);
void SDL_LogInfo(int category, const char* fmt, ...);
void SDL_LogWarn(int category, const char* fmt, ...);
void SDL_LogError(int category, const char* fmt, ...);

// ---- time
Uint64 SDL_GetTicks(void);
Uint64 SDL_GetTicksNS(void);

// ---- files and paths (UTF-8, as in SDL)
void* SDL_LoadFile(const char* file, size_t* datasize);
bool SDL_SaveFile(const char* file, const void* data, size_t datasize);
typedef enum SDL_PathType { SDL_PATHTYPE_NONE, SDL_PATHTYPE_FILE, SDL_PATHTYPE_DIRECTORY, SDL_PATHTYPE_OTHER } SDL_PathType;
typedef struct SDL_PathInfo {
    SDL_PathType type;
    Uint64 size;
    Sint64 create_time, modify_time, access_time;
} SDL_PathInfo;
bool SDL_GetPathInfo(const char* path, SDL_PathInfo* info);
const char* SDL_GetBasePath(void);
char* SDL_GetPrefPath(const char* org, const char* app);

// ---- properties (only what a window answers)
typedef Uint32 SDL_PropertiesID;
#define SDL_PROP_WINDOW_WIN32_HWND_POINTER "SDL.window.win32.hwnd"
void* SDL_GetPointerProperty(SDL_PropertiesID props, const char* name, void* default_value);

// ---- the window
typedef struct SDL_Window SDL_Window;
typedef Uint64 SDL_WindowFlags;
#define SDL_WINDOW_HIDDEN 0x0000000000000008ull
#define SDL_WINDOW_RESIZABLE 0x0000000000000020ull
#define SDL_WINDOW_HIGH_PIXEL_DENSITY 0x0000000000002000ull
#define SDL_WINDOW_VULKAN 0x0000000010000000ull
SDL_Window* SDL_CreateWindow(const char* title, int w, int h, SDL_WindowFlags flags);
void SDL_DestroyWindow(SDL_Window* window);
SDL_PropertiesID SDL_GetWindowProperties(SDL_Window* window);
bool SDL_GetWindowSize(SDL_Window* window, int* w, int* h);
bool SDL_GetWindowSizeInPixels(SDL_Window* window, int* w, int* h);
bool SDL_SetWindowSize(SDL_Window* window, int w, int h);
bool SDL_RestoreWindow(SDL_Window* window);
bool SDL_SetWindowRelativeMouseMode(SDL_Window* window, bool enabled);

// The XP build's one extension: a hook called first in the window
// procedure, for Dear ImGui's Win32 backend (editor/imgui_layer.cpp). A
// non-zero return is the message's result and stops it there.
typedef intptr_t (*SDLXP_WndProcHook)(void* hwnd, unsigned message, uintptr_t wparam, intptr_t lparam);
void SDLXP_SetWndProcHook(SDLXP_WndProcHook hook);

// ---- keyboard: USB HID usages, as SDL's scancodes are
typedef enum SDL_Scancode {
    SDL_SCANCODE_UNKNOWN = 0,
    SDL_SCANCODE_A = 4, SDL_SCANCODE_B, SDL_SCANCODE_C, SDL_SCANCODE_D, SDL_SCANCODE_E, SDL_SCANCODE_F,
    SDL_SCANCODE_G, SDL_SCANCODE_H, SDL_SCANCODE_I, SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L,
    SDL_SCANCODE_M, SDL_SCANCODE_N, SDL_SCANCODE_O, SDL_SCANCODE_P, SDL_SCANCODE_Q, SDL_SCANCODE_R,
    SDL_SCANCODE_S, SDL_SCANCODE_T, SDL_SCANCODE_U, SDL_SCANCODE_V, SDL_SCANCODE_W, SDL_SCANCODE_X,
    SDL_SCANCODE_Y, SDL_SCANCODE_Z,
    SDL_SCANCODE_1 = 30, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5,
    SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_0,
    SDL_SCANCODE_RETURN = 40, SDL_SCANCODE_ESCAPE = 41, SDL_SCANCODE_BACKSPACE = 42, SDL_SCANCODE_TAB = 43,
    SDL_SCANCODE_SPACE = 44, SDL_SCANCODE_MINUS = 45, SDL_SCANCODE_EQUALS = 46, SDL_SCANCODE_LEFTBRACKET = 47,
    SDL_SCANCODE_RIGHTBRACKET = 48, SDL_SCANCODE_BACKSLASH = 49, SDL_SCANCODE_SEMICOLON = 51,
    SDL_SCANCODE_APOSTROPHE = 52, SDL_SCANCODE_GRAVE = 53, SDL_SCANCODE_COMMA = 54, SDL_SCANCODE_PERIOD = 55,
    SDL_SCANCODE_SLASH = 56, SDL_SCANCODE_CAPSLOCK = 57,
    SDL_SCANCODE_F1 = 58, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4, SDL_SCANCODE_F5, SDL_SCANCODE_F6,
    SDL_SCANCODE_F7, SDL_SCANCODE_F8, SDL_SCANCODE_F9, SDL_SCANCODE_F10, SDL_SCANCODE_F11, SDL_SCANCODE_F12,
    SDL_SCANCODE_PRINTSCREEN = 70, SDL_SCANCODE_SCROLLLOCK = 71, SDL_SCANCODE_PAUSE = 72,
    SDL_SCANCODE_INSERT = 73, SDL_SCANCODE_HOME = 74, SDL_SCANCODE_PAGEUP = 75, SDL_SCANCODE_DELETE = 76,
    SDL_SCANCODE_END = 77, SDL_SCANCODE_PAGEDOWN = 78, SDL_SCANCODE_RIGHT = 79, SDL_SCANCODE_LEFT = 80,
    SDL_SCANCODE_DOWN = 81, SDL_SCANCODE_UP = 82, SDL_SCANCODE_NUMLOCKCLEAR = 83,
    SDL_SCANCODE_KP_DIVIDE = 84, SDL_SCANCODE_KP_MULTIPLY = 85, SDL_SCANCODE_KP_MINUS = 86,
    SDL_SCANCODE_KP_PLUS = 87, SDL_SCANCODE_KP_ENTER = 88,
    SDL_SCANCODE_KP_1 = 89, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5,
    SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9, SDL_SCANCODE_KP_0,
    SDL_SCANCODE_KP_PERIOD = 99,
    SDL_SCANCODE_LCTRL = 224, SDL_SCANCODE_LSHIFT = 225, SDL_SCANCODE_LALT = 226, SDL_SCANCODE_LGUI = 227,
    SDL_SCANCODE_RCTRL = 228, SDL_SCANCODE_RSHIFT = 229, SDL_SCANCODE_RALT = 230, SDL_SCANCODE_RGUI = 231,
    SDL_SCANCODE_COUNT = 512
} SDL_Scancode;

// ---- mouse
#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT 3
#define SDL_BUTTON_X1 4
#define SDL_BUTTON_X2 5
#define SDL_BUTTON_MASK(X) (1u << ((X) - 1))

// ---- gamepads (XInput)
typedef Uint32 SDL_JoystickID;
typedef struct SDL_Gamepad SDL_Gamepad;
typedef enum SDL_GamepadAxis {
    SDL_GAMEPAD_AXIS_INVALID = -1,
    SDL_GAMEPAD_AXIS_LEFTX,
    SDL_GAMEPAD_AXIS_LEFTY,
    SDL_GAMEPAD_AXIS_RIGHTX,
    SDL_GAMEPAD_AXIS_RIGHTY,
    SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
    SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
    SDL_GAMEPAD_AXIS_COUNT
} SDL_GamepadAxis;
typedef enum SDL_GamepadButton {
    SDL_GAMEPAD_BUTTON_INVALID = -1,
    SDL_GAMEPAD_BUTTON_SOUTH,
    SDL_GAMEPAD_BUTTON_EAST,
    SDL_GAMEPAD_BUTTON_WEST,
    SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_BACK,
    SDL_GAMEPAD_BUTTON_GUIDE,
    SDL_GAMEPAD_BUTTON_START,
    SDL_GAMEPAD_BUTTON_LEFT_STICK,
    SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
    SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    SDL_GAMEPAD_BUTTON_DPAD_UP,
    SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    SDL_GAMEPAD_BUTTON_COUNT
} SDL_GamepadButton;
SDL_Gamepad* SDL_OpenGamepad(SDL_JoystickID instance_id);
void SDL_CloseGamepad(SDL_Gamepad* gamepad);
const char* SDL_GetGamepadName(SDL_Gamepad* gamepad);
Sint16 SDL_GetGamepadAxis(SDL_Gamepad* gamepad, SDL_GamepadAxis axis);
bool SDL_GetGamepadButton(SDL_Gamepad* gamepad, SDL_GamepadButton button);

// ---- events
typedef enum SDL_EventType {
    SDL_EVENT_FIRST = 0,
    SDL_EVENT_QUIT = 0x100,
    SDL_EVENT_WINDOW_FOCUS_LOST = 0x20F,
    SDL_EVENT_KEY_DOWN = 0x300,
    SDL_EVENT_KEY_UP,
    SDL_EVENT_TEXT_EDITING,
    SDL_EVENT_TEXT_INPUT,
    SDL_EVENT_MOUSE_MOTION = 0x400,
    SDL_EVENT_MOUSE_BUTTON_DOWN,
    SDL_EVENT_MOUSE_BUTTON_UP,
    SDL_EVENT_MOUSE_WHEEL,
    SDL_EVENT_GAMEPAD_ADDED = 0x653,
    SDL_EVENT_GAMEPAD_REMOVED,
} SDL_EventType;
typedef struct SDL_KeyboardEvent {
    Uint32 type;
    SDL_Scancode scancode;
    bool down;
    bool repeat;
} SDL_KeyboardEvent;
typedef struct SDL_MouseMotionEvent {
    Uint32 type;
    float x, y;
    float xrel, yrel;
} SDL_MouseMotionEvent;
typedef struct SDL_MouseButtonEvent {
    Uint32 type;
    Uint8 button;
    bool down;
    float x, y;
} SDL_MouseButtonEvent;
typedef struct SDL_MouseWheelEvent {
    Uint32 type;
    float x, y;
} SDL_MouseWheelEvent;
typedef struct SDL_GamepadDeviceEvent {
    Uint32 type;
    SDL_JoystickID which;
} SDL_GamepadDeviceEvent;
typedef struct SDL_TextInputEvent {
    Uint32 type;
    char text[8];
} SDL_TextInputEvent;
typedef union SDL_Event {
    Uint32 type;
    SDL_KeyboardEvent key;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
    SDL_GamepadDeviceEvent gdevice;
    SDL_TextInputEvent text;
    Uint8 padding[64];
} SDL_Event;
bool SDL_PollEvent(SDL_Event* event);

// ---- surfaces: enough to save a screenshot
typedef enum SDL_PixelFormat { SDL_PIXELFORMAT_UNKNOWN = 0, SDL_PIXELFORMAT_RGBA32 = 0x16762004u } SDL_PixelFormat;
typedef struct SDL_Surface {
    SDL_PixelFormat format;
    int w, h, pitch;
    void* pixels;
} SDL_Surface;
SDL_Surface* SDL_CreateSurfaceFrom(int width, int height, SDL_PixelFormat format, void* pixels, int pitch);
bool SDL_SaveBMP(SDL_Surface* surface, const char* file);
void SDL_DestroySurface(SDL_Surface* surface);

#ifdef __cplusplus
}
#endif
