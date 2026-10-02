#include "core/paths.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include "core/log.h"

namespace core::paths {
namespace {

std::string g_assets;
std::string g_shaders;
std::string g_user;

bool is_dir(const std::string& path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

bool is_file(const std::string& path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

std::string join(const std::string& root, std::string_view rel) {
    std::string out = root;
    if (!out.empty() && out.back() != '/') out += '/';
    out.append(rel);
    return out;
}

// The first candidate root that exists as a directory, or the last one, so a
// broken install still names a path in its errors rather than an empty one.
std::string pick(std::initializer_list<std::string> candidates) {
    std::string last;
    for (const std::string& c : candidates) {
        if (c.empty()) continue;
        if (is_dir(c)) return c;
        last = c;
    }
    return last;
}

}  // namespace

void init() {
    const char* base_c = SDL_GetBasePath();
    const std::string base = base_c ? base_c : "";

#ifdef ASSET_ROOT
    const std::string dev_assets = ASSET_ROOT;
#else
    const std::string dev_assets;
#endif
#ifdef SHADER_ROOT
    const std::string dev_shaders = SHADER_ROOT;
#else
    const std::string dev_shaders;
#endif
    // Beside the executable first: a package carries its data there, and a
    // development build has none there, so it falls through to the tree.
    g_assets = pick({base.empty() ? std::string() : join(base, "assets"), dev_assets});
    g_shaders = pick({base.empty() ? std::string() : join(base, "shaders"), dev_shaders});

    if (char* pref = SDL_GetPrefPath("Paleshell", "Dragon")) {
        g_user = pref;
        SDL_free(pref);
    } else {
        LOG_WARN("no per-user directory (%s); saving beside the assets", SDL_GetError());
        g_user = g_assets;
    }
    LOG_INFO("data: %s", g_assets.c_str());
    LOG_INFO("shaders: %s", g_shaders.c_str());
    LOG_INFO("user files: %s", g_user.c_str());
}

std::string asset(std::string_view rel) { return join(g_assets, rel); }
const std::string& asset_root() { return g_assets; }
const std::string& shader_root() { return g_shaders; }
std::string user(std::string_view rel) { return join(g_user, rel); }

std::string user_or_asset(std::string_view rel) {
    const std::string mine = user(rel);
    return is_file(mine) ? mine : asset(rel);
}

std::string resolve_cli(const std::string& path) {
    if (path.empty() || path.front() == '/' || is_file(path)) return path;
    std::string parent = g_assets;
    while (!parent.empty() && parent.back() == '/') parent.pop_back();
    const size_t slash = parent.find_last_of('/');
    if (slash == std::string::npos) return path;
    const std::string candidate = join(parent.substr(0, slash), path);
    return is_file(candidate) ? candidate : path;
}

}  // namespace core::paths
