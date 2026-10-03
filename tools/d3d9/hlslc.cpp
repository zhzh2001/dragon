// hlslc: compile HLSL to Direct3D 9 shader bytecode with d3dcompiler_47.
//
//   hlslc.exe PROFILE IN.hlsl OUT.bin [PROFILE IN.hlsl OUT.bin ...]
//
// The last compiler Microsoft ships that still targets vs_3_0/ps_3_0 and the
// SM2 profiles is d3dcompiler_47's D3DCompile. It is a Windows DLL, so this
// tiny program wraps it: tools/d3d9/bake_d3d9.py runs it natively on Windows,
// or under Wine on the Mac with Microsoft's DLL beside it
// (~/.local/opt/d3dcompiler, docs/PORTING.md, R3). Exit status 1 if any
// compile failed; the compiler's messages go to stderr.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <vector>

namespace {

// d3dcompiler's ID3DBlob, by vtable, so this needs no d3dcompiler.h.
struct Blob {
    struct Vtbl {
        void* query_interface;
        void* add_ref;
        ULONG(STDMETHODCALLTYPE* release)(Blob*);
        void*(STDMETHODCALLTYPE* get_buffer_pointer)(Blob*);
        SIZE_T(STDMETHODCALLTYPE* get_buffer_size)(Blob*);
    } * vtbl;
};

typedef HRESULT(WINAPI* D3DCompileFn)(const void* src, SIZE_T size, const char* name, const void* defines,
                                      void* include, const char* entry, const char* target, UINT flags1,
                                      UINT flags2, Blob** code, Blob** errors);

// D3DCOMPILE_OPTIMIZATION_LEVEL3: the cards this is for have no cycles to spare.
const UINT OPTIMIZATION_LEVEL3 = 1u << 15;

std::vector<char> read_file(const char* path) {
    std::vector<char> data;
    FILE* f = std::fopen(path, "rb");
    if (!f) return data;
    char buffer[65536];
    size_t n;
    while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) data.insert(data.end(), buffer, buffer + n);
    std::fclose(f);
    return data;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4 || (argc - 1) % 3 != 0) {
        std::fprintf(stderr, "usage: hlslc PROFILE IN.hlsl OUT.bin [...]\n");
        return 2;
    }
    HMODULE dll = LoadLibraryA("d3dcompiler_47.dll");
    D3DCompileFn compile = dll ? reinterpret_cast<D3DCompileFn>(GetProcAddress(dll, "D3DCompile")) : nullptr;
    if (!compile) {
        std::fprintf(stderr, "hlslc: d3dcompiler_47.dll not found\n");
        return 2;
    }
    int failures = 0;
    for (int i = 1; i + 2 < argc; i += 3) {
        const char* profile = argv[i];
        const char* in = argv[i + 1];
        const char* out = argv[i + 2];
        const std::vector<char> src = read_file(in);
        if (src.empty()) {
            std::fprintf(stderr, "%s: cannot read\n", in);
            ++failures;
            continue;
        }
        Blob* code = nullptr;
        Blob* errors = nullptr;
        const HRESULT hr = compile(src.data(), src.size(), in, nullptr, nullptr, "main", profile,
                                   OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (errors) {
            std::fwrite(errors->vtbl->get_buffer_pointer(errors), 1, errors->vtbl->get_buffer_size(errors),
                        stderr);
            errors->vtbl->release(errors);
        }
        if (FAILED(hr) || !code) {
            std::fprintf(stderr, "%s: %s failed (0x%08lx)\n", in, profile, static_cast<unsigned long>(hr));
            ++failures;
            continue;
        }
        FILE* f = std::fopen(out, "wb");
        if (f) {
            std::fwrite(code->vtbl->get_buffer_pointer(code), 1, code->vtbl->get_buffer_size(code), f);
            std::fclose(f);
        } else {
            std::fprintf(stderr, "%s: cannot write\n", out);
            ++failures;
        }
        code->vtbl->release(code);
    }
    return failures ? 1 : 0;
}
