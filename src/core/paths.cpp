#include "core/paths.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_system.h>
#ifdef SDL_PLATFORM_ANDROID
#include <SDL3/SDL_timer.h>
#endif

#include "core/log.h"

#include <cstdint>
#include <fstream>

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
    if (!out.empty() && out.back() != '/' && out.back() != '\\') out += '/';
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

#ifdef SDL_PLATFORM_ANDROID
// A release APK carries the data inside it (tools/release/package_android.sh)
// with a manifest, data.txt: a version on the first line, then one path per
// line. The game reads its files with fopen and std::ifstream, which cannot
// open an APK's assets, so they are copied once to the external files
// directory, where a development build's push_assets.sh puts them, and again
// only when the version changes. A development APK has no manifest and this
// does nothing.
void unpack_apk_data(const std::string& external) {
    size_t size = 0;
    char* manifest = static_cast<char*>(SDL_LoadFile("data.txt", &size));  // relative: from the APK
    if (!manifest) return;
    const std::string text(manifest, size);
    SDL_free(manifest);
    const size_t first = text.find('\n');
    const std::string version = text.substr(0, first);
    const std::string stamp = join(external, "data.version");
    size_t stamp_size = 0;
    if (char* old = static_cast<char*>(SDL_LoadFile(stamp.c_str(), &stamp_size))) {
        const bool same = std::string(old, stamp_size) == version;
        SDL_free(old);
        if (same) return;
    }
    const uint64_t started = SDL_GetTicksNS();
    std::vector<char> buffer(1 << 20);
    int files = 0;
    uint64_t bytes = 0;
    size_t at = first == std::string::npos ? text.size() : first + 1;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string rel = text.substr(at, end - at);
        at = end + 1;
        if (rel.empty()) continue;
        const std::string out_path = join(external, rel);
        const size_t slash = out_path.find_last_of('/');
        if (slash != std::string::npos) SDL_CreateDirectory(out_path.substr(0, slash).c_str());
        SDL_IOStream* in = SDL_IOFromFile(rel.c_str(), "rb");
        SDL_IOStream* out = in ? SDL_IOFromFile(out_path.c_str(), "wb") : nullptr;
        bool ok = in && out;
        while (ok) {
            const size_t n = SDL_ReadIO(in, buffer.data(), buffer.size());
            if (n == 0) break;
            ok = SDL_WriteIO(out, buffer.data(), n) == n;
            bytes += n;
        }
        if (in) SDL_CloseIO(in);
        if (out && !SDL_CloseIO(out)) ok = false;
        if (!ok) {
            // No stamp: the next start tries again.
            LOG_ERROR("unpacking %s from the APK failed: %s", rel.c_str(), SDL_GetError());
            return;
        }
        ++files;
    }
    if (SDL_IOStream* out = SDL_IOFromFile(stamp.c_str(), "wb")) {
        SDL_WriteIO(out, version.data(), version.size());
        SDL_CloseIO(out);
    }
    LOG_INFO("unpacked %d files (%.0f MB) of data %s from the APK in %.1f s", files, bytes / 1048576.0,
             version.c_str(), (SDL_GetTicksNS() - started) / 1e9);
}
#endif

}  // namespace

void init() {
    const char* base_c = SDL_GetBasePath();
    std::string base = base_c ? base_c : "";
#ifdef SDL_PLATFORM_ANDROID
    // Android: the data is not read out of the APK but kept beside it, in
    // the app's external files directory, where fopen and std::ifstream read
    // it as on every other platform (docs/porting/MOBILE.md). A development
    // build's data is pushed there (tools/android/push_assets.sh); a release
    // APK's is unpacked there from the APK on first start.
    if (const char* external = SDL_GetAndroidExternalStoragePath()) {
        base = external;
        unpack_apk_data(base);
    }
#endif

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

std::vector<uint8_t> read_bytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const std::streamsize size = file.tellg();
    if (size <= 0) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) return {};
    return bytes;
}

}  // namespace core::paths
