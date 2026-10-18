// memset, memcmp and bcmp for the Pi 1B build (tools/pi/build.sh).
//
// Zig links its own compiler_rt into the executable, and its memset,
// memcmp and bcmp are byte-at-a-time loops, exported weak. They took the
// place of glibc's (ARM-tuned on Raspberry Pi OS) in everything linked
// into the executable: on the Pi 1B memset was 6.7% of start-up and 2.6%
// of a frame, bcmp (the GLES backend's comparison of each uniform push)
// 2.3%. These strong definitions take their place in turn: a word at a
// time, the bytes either side of the words one at a time.
//
// no_builtin keeps the compiler from recognising the loops as memset and
// memcmp and calling them, which here would be these functions.

#include <stddef.h>
#include <stdint.h>

typedef uint32_t word __attribute__((may_alias));
typedef uint32_t unaligned_word __attribute__((aligned(1), may_alias));

__attribute__((no_builtin)) void* memset(void* dest, int c, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char b = (unsigned char)c;
    while (n > 0 && ((uintptr_t)d & 3u) != 0) {
        *d++ = b;
        --n;
    }
    if (n >= 4) {
        const uint32_t w = 0x01010101u * b;
        word* p = (word*)d;
        for (; n >= 32; n -= 32, p += 8) {
            p[0] = w;
            p[1] = w;
            p[2] = w;
            p[3] = w;
            p[4] = w;
            p[5] = w;
            p[6] = w;
            p[7] = w;
        }
        for (; n >= 4; n -= 4) *p++ = w;
        d = (unsigned char*)p;
    }
    while (n > 0) {
        *d++ = b;
        --n;
    }
    return dest;
}

__attribute__((no_builtin)) int memcmp(const void* left, const void* right, size_t n) {
    const unsigned char* a = (const unsigned char*)left;
    const unsigned char* b = (const unsigned char*)right;
    // Equal words skipped whole; the first that differs is compared byte
    // by byte below, which gives memcmp's ordering.
    while (n >= 4 && *(const unaligned_word*)a == *(const unaligned_word*)b) {
        a += 4;
        b += 4;
        n -= 4;
    }
    for (; n > 0; --n, ++a, ++b) {
        if (*a != *b) return (int)*a - (int)*b;
    }
    return 0;
}

__attribute__((no_builtin)) int bcmp(const void* left, const void* right, size_t n) {
    return memcmp(left, right, n);
}
