// dear imgui: Renderer Backend for Direct3D8
// This backend is project-local and mirrors the DX9 backend behavior on an
// IDirect3DDevice8 exposed as void* to avoid taking a d3d8.h dependency.

#pragma once
#include "imgui.h"
#ifndef IMGUI_DISABLE

IMGUI_IMPL_API bool     ImGui_ImplD3D8_Init(void* device);
IMGUI_IMPL_API void     ImGui_ImplD3D8_Shutdown();
IMGUI_IMPL_API void     ImGui_ImplD3D8_NewFrame();
IMGUI_IMPL_API void     ImGui_ImplD3D8_RenderDrawData(ImDrawData* draw_data);

IMGUI_IMPL_API bool     ImGui_ImplD3D8_CreateDeviceObjects();
IMGUI_IMPL_API void     ImGui_ImplD3D8_InvalidateDeviceObjects();
IMGUI_IMPL_API void     ImGui_ImplD3D8_UpdateTexture(ImTextureData* tex);

#endif
