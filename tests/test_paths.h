#pragma once

// Where the tests may write, and where they find the source tree. Both were
// POSIX-only literals (/tmp, and __FILE__'s directory) until the suites ran on
// Windows (docs/PORTING.md, P2): there /tmp does not exist, and a binary
// cross-compiled on the Mac carries the Mac's path in __FILE__.

#include <cstdlib>
#include <filesystem>
#include <string>

// A scratch file in the OS's temporary directory.
inline std::string test_temp_path(const char* name) {
    return (std::filesystem::temp_directory_path() / (std::string("dragon_test_") + name)).string();
}

// The source tree: DRAGON_SOURCE_ROOT when set (a cross-compiled suite run
// elsewhere), else derived from this file's own path at build time.
inline std::filesystem::path test_source_root() {
    if (const char* root = std::getenv("DRAGON_SOURCE_ROOT")) return std::filesystem::path(root);
    return std::filesystem::path(__FILE__).parent_path().parent_path();
}
