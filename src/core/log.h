#pragma once

#include <SDL3/SDL_log.h>

// Thin wrappers over SDL_Log so every subsystem reports the same way and we
// can retarget output (file, in-game console) in one place later.
#define LOG_INFO(...)  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define LOG_WARN(...)  SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define LOG_ERROR(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define LOG_DEBUG(...) SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)

// Log an SDL failure together with SDL_GetError(), then evaluate to false so it
// can tail a failed init step: `if (!thing) return sdl_fail("thing");`
#define SDL_FAIL(what) (LOG_ERROR("%s failed: %s", what, SDL_GetError()), false)
