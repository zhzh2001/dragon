# Dear ImGui has no CMake build of its own, so we declare the target here.
# It is our editor: every tuning slider, telemetry plot, and debug panel.
include(FetchContent)

FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.92.6
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
if(WIN32)
  # imgui_impl_dx9 gives every draw command the whole draw list's vertex
  # count, which a GPU ignores and D3D9's software vertex processing (a card
  # without hardware T&L) honours: it transformed the HUD's whole foreground
  # list once per command, 4 ms of a G41 frame. The fetched source is
  # patched to pass each command's real index range.
  set(_imgui_dx9 ${imgui_SOURCE_DIR}/backends/imgui_impl_dx9.cpp)
  file(READ ${_imgui_dx9} _imgui_dx9_src)
  set(_dx9_draw "device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, pcmd->VtxOffset + global_vtx_offset, 0, (UINT)draw_list->VtxBuffer.Size, pcmd->IdxOffset + global_idx_offset, pcmd->ElemCount / 3);")
  set(_dx9_ranged "{ UINT lo = 0xFFFFFFFFu, hi = 0; for (unsigned int i = 0; i < pcmd->ElemCount; ++i) { const UINT v = draw_list->IdxBuffer.Data[pcmd->IdxOffset + i]; if (v < lo) lo = v; if (v > hi) hi = v; } if (lo > hi) { lo = 0; hi = 0; } device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, pcmd->VtxOffset + global_vtx_offset, lo, hi - lo + 1, pcmd->IdxOffset + global_idx_offset, pcmd->ElemCount / 3); }")
  # It also captures and re-applies a D3DSBT_ALL state block every frame,
  # which a software-vertex-processing device makes expensive. The D3D9
  # backend forgets its own state caches after ImGui draws instead
  # (rhi::d3d9::forget_state), so the block goes.
  string(REPLACE "    IDirect3DStateBlock9* state_block = nullptr;\n    if (device->CreateStateBlock(D3DSBT_ALL, &state_block) < 0)\n        return;\n    if (state_block->Capture() < 0)\n    {\n        state_block->Release();\n        return;\n    }\n" "    // No state block: the caller restores its own state; the error paths\n    // below still call Release on this stand-in.\n    struct NoStateBlock { void Release() {} void Apply() {} } no_state_block;\n    NoStateBlock* state_block = &no_state_block;\n" _imgui_dx9_src "${_imgui_dx9_src}")
  # And its vertices are untransformed (D3DFVF_XYZ under an orthographic
  # projection), which a software-vertex-processing device runs through
  # D3D9's whole transform pipeline: the HUD cost 3.5 ms of a G41 frame.
  # Pre-transformed screen positions (D3DFVF_XYZRHW, D3D9's pixel-centre
  # half pixel off) skip it on every device.
  string(REPLACE "    float    pos[3];\n    D3DCOLOR col;" "    float    pos[4];\n    D3DCOLOR col;" _imgui_dx9_src "${_imgui_dx9_src}")
  string(REPLACE "#define D3DFVF_CUSTOMVERTEX (D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1)" "#define D3DFVF_CUSTOMVERTEX (D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1)" _imgui_dx9_src "${_imgui_dx9_src}")
  string(REPLACE "            vtx_dst->pos[0] = vtx_src->pos.x;\n            vtx_dst->pos[1] = vtx_src->pos.y;\n            vtx_dst->pos[2] = 0.0f;" "            vtx_dst->pos[0] = (vtx_src->pos.x - draw_data->DisplayPos.x) * draw_data->FramebufferScale.x - 0.5f;\n            vtx_dst->pos[1] = (vtx_src->pos.y - draw_data->DisplayPos.y) * draw_data->FramebufferScale.y - 0.5f;\n            vtx_dst->pos[2] = 0.0f;\n            vtx_dst->pos[3] = 1.0f;" _imgui_dx9_src "${_imgui_dx9_src}")
  # And on a device whose vertices are processed in software (a card
  # without T&L), the D3D9 runtime reads the vertex and index buffers with
  # the CPU, and D3DPOOL_DEFAULT put them in the card's memory, across the
  # G41's PCI bus: 3,000 HUD vertices were 9.4 ms of a 35 ms frame. Such a
  # device gets them in system memory.
  string(REPLACE "D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFVF_CUSTOMVERTEX, D3DPOOL_DEFAULT" "ImGui_ImplDX9_BufferUsage(device), D3DFVF_CUSTOMVERTEX, ImGui_ImplDX9_BufferPool(device)" _imgui_dx9_src "${_imgui_dx9_src}")
  string(REPLACE "D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, sizeof(ImDrawIdx) == 2 ? D3DFMT_INDEX16 : D3DFMT_INDEX32, D3DPOOL_DEFAULT" "ImGui_ImplDX9_BufferUsage(device), sizeof(ImDrawIdx) == 2 ? D3DFMT_INDEX16 : D3DFMT_INDEX32, ImGui_ImplDX9_BufferPool(device)" _imgui_dx9_src "${_imgui_dx9_src}")
  if(NOT _imgui_dx9_src MATCHES "static D3DPOOL ImGui_ImplDX9_BufferPool")
    string(REPLACE "// DirectX\n#include <d3d9.h>\n" "// DirectX\n#include <d3d9.h>\nstatic bool ImGui_ImplDX9_SoftwareVP(LPDIRECT3DDEVICE9 device) { D3DDEVICE_CREATION_PARAMETERS p = {}; return SUCCEEDED(device->GetCreationParameters(&p)) && (p.BehaviorFlags & (D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MIXED_VERTEXPROCESSING)); }\nstatic DWORD ImGui_ImplDX9_BufferUsage(LPDIRECT3DDEVICE9 device) { return ImGui_ImplDX9_SoftwareVP(device) ? (D3DUSAGE_DYNAMIC | D3DUSAGE_SOFTWAREPROCESSING) : (D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY); }\nstatic D3DPOOL ImGui_ImplDX9_BufferPool(LPDIRECT3DDEVICE9 device) { return ImGui_ImplDX9_SoftwareVP(device) ? D3DPOOL_SYSTEMMEM : D3DPOOL_DEFAULT; }\n" _imgui_dx9_src "${_imgui_dx9_src}")
  endif()
  # And its font atlas is a D3DUSAGE_DYNAMIC texture in D3DPOOL_DEFAULT,
  # which a driver without D3DCAPS2_DYNAMICTEXTURES refuses: the G550's
  # (a DX6-era design) did, and every glyph drew as a solid quad. Such a
  # device gets a managed texture, which locks just the same.
  string(REPLACE "CreateTexture(tex->Width, tex->Height, 1, D3DUSAGE_DYNAMIC, bd->HasRgbaSupport ? D3DFMT_A8B8G8R8 : D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &dx_tex, nullptr);" "CreateTexture(tex->Width, tex->Height, 1, ImGui_ImplDX9_DynamicTextures(bd->pd3dDevice) ? D3DUSAGE_DYNAMIC : 0, bd->HasRgbaSupport ? D3DFMT_A8B8G8R8 : D3DFMT_A8R8G8B8, ImGui_ImplDX9_DynamicTextures(bd->pd3dDevice) ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED, &dx_tex, nullptr);" _imgui_dx9_src "${_imgui_dx9_src}")
  if(NOT _imgui_dx9_src MATCHES "static bool ImGui_ImplDX9_DynamicTextures")
    string(REPLACE "// DirectX\n#include <d3d9.h>\n" "// DirectX\n#include <d3d9.h>\nstatic bool ImGui_ImplDX9_DynamicTextures(LPDIRECT3DDEVICE9 device) { D3DCAPS9 caps = {}; return SUCCEEDED(device->GetDeviceCaps(&caps)) && (caps.Caps2 & D3DCAPS2_DYNAMICTEXTURES); }\n" _imgui_dx9_src "${_imgui_dx9_src}")
  endif()
  if(NOT _imgui_dx9_src MATCHES "ImGui_ImplDX9_DynamicTextures\\(bd->pd3dDevice\\) \\? D3DUSAGE_DYNAMIC")
    message(FATAL_ERROR "imgui_impl_dx9.cpp changed: the managed font texture patch no longer applies")
  endif()
  # And a switch for the Lowest preset's Plain HUD (ui::Hud::set_plain):
  # alpha-tested instead of blended, plates opaque, glyph edges hard --
  # blending the HUD at full resolution was 2.1 ms of a G41 frame.
  if(NOT _imgui_dx9_src MATCHES "bool ImGui_ImplDX9_AlphaTest")
    string(REPLACE "// DirectX\n#include <d3d9.h>\n" "// DirectX\n#include <d3d9.h>\nbool ImGui_ImplDX9_AlphaTest = false;\n" _imgui_dx9_src "${_imgui_dx9_src}")
    string(REPLACE "    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);\n" "    device->SetRenderState(D3DRS_ALPHATESTENABLE, ImGui_ImplDX9_AlphaTest ? TRUE : FALSE);\n    device->SetRenderState(D3DRS_ALPHAREF, 0x40);\n    device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);\n" _imgui_dx9_src "${_imgui_dx9_src}")
    string(REPLACE "    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);\n" "    device->SetRenderState(D3DRS_ALPHABLENDENABLE, ImGui_ImplDX9_AlphaTest ? FALSE : TRUE);\n" _imgui_dx9_src "${_imgui_dx9_src}")
  endif()
  if(NOT _imgui_dx9_src MATCHES "ALPHABLENDENABLE, ImGui_ImplDX9_AlphaTest")
    message(FATAL_ERROR "imgui_impl_dx9.cpp changed: the alpha-test patch no longer applies")
  endif()
  if(NOT _imgui_dx9_src MATCHES "ImGui_ImplDX9_BufferPool\\(device\\)" OR NOT _imgui_dx9_src MATCHES "static D3DPOOL ImGui_ImplDX9_BufferPool")
    message(FATAL_ERROR "imgui_impl_dx9.cpp changed: the system-memory buffer patch no longer applies")
  endif()
  if(NOT _imgui_dx9_src MATCHES "D3DFVF_XYZRHW" OR NOT _imgui_dx9_src MATCHES "pos\\[3\\] = 1.0f")
    message(FATAL_ERROR "imgui_impl_dx9.cpp changed: the pre-transformed vertex patch no longer applies")
  endif()
  string(FIND "${_imgui_dx9_src}" "${_dx9_draw}" _dx9_at)
  if(_dx9_at GREATER -1)
    string(REPLACE "${_dx9_draw}" "${_dx9_ranged}" _imgui_dx9_src "${_imgui_dx9_src}")
    file(WRITE ${_imgui_dx9} "${_imgui_dx9_src}")
  elseif(NOT _imgui_dx9_src MATCHES "UINT lo = 0xFFFFFFFFu")
    message(FATAL_ERROR "imgui_impl_dx9.cpp changed: the per-command vertex range patch no longer applies")
  endif()
  if(_imgui_dx9_src MATCHES "CreateStateBlock\\(D3DSBT_ALL")
    message(FATAL_ERROR "imgui_impl_dx9.cpp changed: the state block patch no longer applies")
  endif()
  file(WRITE ${_imgui_dx9} "${_imgui_dx9_src}")
endif()
if(DRAGON_XP)
  # The XP build: Win32 for the platform side (fed from the SDL3 subset's
  # window procedure hook) and D3D9 to draw.
  target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
                               ${imgui_SOURCE_DIR}/backends/imgui_impl_dx9.cpp)
  target_link_libraries(imgui PUBLIC d3d9 gdi32 dwmapi)
  target_compile_features(imgui PUBLIC cxx_std_20)
  return()
endif()
target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
                             ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp)
target_link_libraries(imgui PUBLIC SDL3::SDL3)
if(WIN32)
  # The D3D9 backend's UI (editor/imgui_layer.cpp).
  target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_dx9.cpp
                               ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
  # imgui_impl_dx11 compiles its two shaders at runtime through D3DCompile
  # (d3dcompiler_47.dll, in every Windows since 8.1 and 7's platform update).
  target_link_libraries(imgui PUBLIC d3d9 d3d11 d3dcompiler)
  # It asks for vs_4_0 and ps_4_0, which a feature level 9 device refuses
  # ("Invalid shader version provided"), so the UI drew nothing on the X550
  # at 9_2. The level_9_1 profiles build the same two shaders and run on
  # every feature level, so the fetched source is patched to ask for those.
  set(_imgui_dx11 ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
  file(READ ${_imgui_dx11} _imgui_dx11_src)
  string(REPLACE "\"vs_4_0\"" "\"vs_4_0_level_9_1\"" _imgui_dx11_src "${_imgui_dx11_src}")
  string(REPLACE "\"ps_4_0\"" "\"ps_4_0_level_9_1\"" _imgui_dx11_src "${_imgui_dx11_src}")
  # Its font sampler clamps MaxLOD to 0, which feature level 9 refuses
  # ("MaxLOD must be FLT_MAX"); the font atlas has one level, so the
  # clamp did nothing anyway.
  string(REPLACE "desc.MaxLOD = 0.f;" "desc.MaxLOD = D3D11_FLOAT32_MAX;" _imgui_dx11_src "${_imgui_dx11_src}")
  file(WRITE ${_imgui_dx11} "${_imgui_dx11_src}")
endif()
target_compile_features(imgui PUBLIC cxx_std_20)
