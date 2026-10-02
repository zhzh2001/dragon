#pragma once

// Where the game's files are. Shipped data (assets/, shaders/) is found
// beside the executable -- in a macOS bundle that is Contents/Resources -- and
// a development build falls back to the source tree it was built from, so
// `./build/dragon` still hot-reloads the files being edited. Anything the game
// writes (records, saved tuning, a saved course) goes to the per-user
// directory instead, because a packaged copy's data may be read-only and is
// replaced wholesale by the next version.

#include <string>
#include <string_view>

namespace core::paths {

// Resolve the roots. Call once, before anything asks for a path; safe to call
// before SDL_Init.
void init();

// A shipped, read-only file: <assets>/<rel>.
std::string asset(std::string_view rel);
const std::string& asset_root();
const std::string& shader_root();

// A writable per-user file: <pref dir>/<rel>. The directory exists once
// init() has run (SDL creates it); empty if the platform has none, in which
// case writes fall back to the asset root, as every build did before.
std::string user(std::string_view rel);

// A file the player can override: the user's copy when one exists, else the
// shipped one. Saves go to user(rel), so the first save shadows the shipped
// file without touching it.
std::string user_or_asset(std::string_view rel);

// A path from the command line (--model, --models): as given when it exists,
// else relative to the data root's parent, so the `assets/<name>.glb` spelling
// every doc uses works from any working directory and inside a package.
std::string resolve_cli(const std::string& path);

}  // namespace core::paths
