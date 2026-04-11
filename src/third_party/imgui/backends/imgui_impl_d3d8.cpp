// dear imgui: Renderer Backend for Direct3D8
// Project-local backend used to render the shared RetroSkill ImGui panel on
// the game's D3D8 device. D3D8 has no SetScissorRect, so draw-command clipping
// is performed on the CPU by clipping triangles to each ImGui clip rectangle.

#include "imgui.h"
#ifndef IMGUI_DISABLE
#include "imgui_impl_d3d8.h"

#include "d3d8/d3d8_renderer.h"
#include "ui/retro_render_backend.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

struct ImGui_ImplD3D8_Data
{
    void* pd3dDevice;

    ImGui_ImplD3D8_Data() { memset((void*)this, 0, sizeof(*this)); }
};

struct D3D8ImGuiVertex
{
    float x, y, z, rhw;
    DWORD col;
    float u, v;
};

struct ClipVertex
{
    float x, y;
    float u, v;
    DWORD col;
};

#ifdef IMGUI_USE_BGRA_PACKED_COLOR
#define IMGUI_COL_TO_D3D8_ARGB(_COL)     (_COL)
#else
#define IMGUI_COL_TO_D3D8_ARGB(_COL)     (((_COL) & 0xFF00FF00) | (((_COL) & 0xFF0000) >> 16) | (((_COL) & 0xFF) << 16))
#endif

typedef ULONG   (__stdcall* pfn_D3D8IUnknown_AddRef)(void* pThis);
typedef ULONG   (__stdcall* pfn_D3D8IUnknown_Release)(void* pThis);
typedef HRESULT (__stdcall* pfn_D3D8Dev_SetTexture)(void* pThis, DWORD Stage, void* pTexture);
typedef HRESULT (__stdcall* pfn_D3D8Dev_SetTextureStageState)(void* pThis, DWORD Stage, DWORD Type, DWORD Value);
typedef HRESULT (__stdcall* pfn_D3D8Dev_SetVertexShader)(void* pThis, DWORD Handle);
typedef HRESULT (__stdcall* pfn_D3D8Dev_SetViewport)(void* pThis, const void* pViewport);
typedef HRESULT (__stdcall* pfn_D3D8Dev_DrawPrimitiveUP)(void* pThis, DWORD PrimitiveType, UINT PrimitiveCount, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride);
typedef HRESULT (__stdcall* pfn_D3D8Tex_LockRect)(void* pThis, UINT Level, void* pLockedRect, const RECT* pRect, DWORD Flags);
typedef HRESULT (__stdcall* pfn_D3D8Tex_UnlockRect)(void* pThis, UINT Level);

static inline void* ImGui_ImplD3D8_VT(void* obj, int index)
{
    DWORD* vt = *(DWORD**)obj;
    return (void*)vt[index];
}

static ImGui_ImplD3D8_Data* ImGui_ImplD3D8_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplD3D8_Data*)ImGui::GetIO().BackendRendererUserData : nullptr;
}

static DWORD ImGui_ImplD3D8_LerpColor(DWORD a, DWORD b, float t)
{
    if (t <= 0.0f)
        return a;
    if (t >= 1.0f)
        return b;

    DWORD out = 0;
    for (int shift = 0; shift <= 24; shift += 8)
    {
        const int ac = (int)((a >> shift) & 0xFF);
        const int bc = (int)((b >> shift) & 0xFF);
        int value = ac + (int)floorf(((float)(bc - ac) * t) + 0.5f);
        if (value < 0) value = 0;
        if (value > 255) value = 255;
        out |= (DWORD)value << shift;
    }
    return out;
}

static ClipVertex ImGui_ImplD3D8_LerpVertex(const ClipVertex& a, const ClipVertex& b, float t)
{
    ClipVertex out = {};
    out.x = a.x + (b.x - a.x) * t;
    out.y = a.y + (b.y - a.y) * t;
    out.u = a.u + (b.u - a.u) * t;
    out.v = a.v + (b.v - a.v) * t;
    out.col = ImGui_ImplD3D8_LerpColor(a.col, b.col, t);
    return out;
}

static void ImGui_ImplD3D8_AppendOutputVertex(const ClipVertex& src, std::vector<D3D8ImGuiVertex>& out)
{
    D3D8ImGuiVertex dst = {};
    dst.x = src.x - 0.5f;
    dst.y = src.y - 0.5f;
    dst.z = 0.5f;
    dst.rhw = 1.0f;
    dst.col = src.col;
    dst.u = src.u;
    dst.v = src.v;
    out.push_back(dst);
}

enum D3D8ClipEdge
{
    D3D8Clip_Left,
    D3D8Clip_Right,
    D3D8Clip_Top,
    D3D8Clip_Bottom
};

static bool ImGui_ImplD3D8_IsInsideEdge(const ClipVertex& v, D3D8ClipEdge edge, const ImVec4& clip)
{
    switch (edge)
    {
    case D3D8Clip_Left:   return v.x >= clip.x;
    case D3D8Clip_Right:  return v.x <= clip.z;
    case D3D8Clip_Top:    return v.y >= clip.y;
    case D3D8Clip_Bottom: return v.y <= clip.w;
    default:              return true;
    }
}

static ClipVertex ImGui_ImplD3D8_IntersectEdge(const ClipVertex& a, const ClipVertex& b, D3D8ClipEdge edge, const ImVec4& clip)
{
    float t = 0.0f;
    if (edge == D3D8Clip_Left || edge == D3D8Clip_Right)
    {
        const float boundary = (edge == D3D8Clip_Left) ? clip.x : clip.z;
        const float denom = b.x - a.x;
        t = (fabsf(denom) > 0.00001f) ? ((boundary - a.x) / denom) : 0.0f;
    }
    else
    {
        const float boundary = (edge == D3D8Clip_Top) ? clip.y : clip.w;
        const float denom = b.y - a.y;
        t = (fabsf(denom) > 0.00001f) ? ((boundary - a.y) / denom) : 0.0f;
    }

    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return ImGui_ImplD3D8_LerpVertex(a, b, t);
}

static void ImGui_ImplD3D8_ClipPolygonToEdge(
    const std::vector<ClipVertex>& input,
    std::vector<ClipVertex>& output,
    D3D8ClipEdge edge,
    const ImVec4& clip)
{
    output.clear();
    if (input.empty())
        return;

    ClipVertex prev = input.back();
    bool prev_inside = ImGui_ImplD3D8_IsInsideEdge(prev, edge, clip);
    for (size_t i = 0; i < input.size(); ++i)
    {
        const ClipVertex cur = input[i];
        const bool cur_inside = ImGui_ImplD3D8_IsInsideEdge(cur, edge, clip);

        if (cur_inside != prev_inside)
            output.push_back(ImGui_ImplD3D8_IntersectEdge(prev, cur, edge, clip));
        if (cur_inside)
            output.push_back(cur);

        prev = cur;
        prev_inside = cur_inside;
    }
}

static void ImGui_ImplD3D8_AppendClippedTriangle(
    const ClipVertex tri[3],
    const ImVec4& clip,
    std::vector<D3D8ImGuiVertex>& out)
{
    std::vector<ClipVertex> a;
    std::vector<ClipVertex> b;
    a.reserve(8);
    b.reserve(8);
    a.push_back(tri[0]);
    a.push_back(tri[1]);
    a.push_back(tri[2]);

    ImGui_ImplD3D8_ClipPolygonToEdge(a, b, D3D8Clip_Left, clip);
    ImGui_ImplD3D8_ClipPolygonToEdge(b, a, D3D8Clip_Right, clip);
    ImGui_ImplD3D8_ClipPolygonToEdge(a, b, D3D8Clip_Top, clip);
    ImGui_ImplD3D8_ClipPolygonToEdge(b, a, D3D8Clip_Bottom, clip);

    if (a.size() < 3)
        return;

    for (size_t i = 1; i + 1 < a.size(); ++i)
    {
        ImGui_ImplD3D8_AppendOutputVertex(a[0], out);
        ImGui_ImplD3D8_AppendOutputVertex(a[i], out);
        ImGui_ImplD3D8_AppendOutputVertex(a[i + 1], out);
    }
}

static void ImGui_ImplD3D8_SetTextureBlendMode(void* device, void* texture)
{
    auto fnSetTSS = (pfn_D3D8Dev_SetTextureStageState)ImGui_ImplD3D8_VT(device, D3D8VT::SetTextureStageState);
    if (texture)
    {
        fnSetTSS(device, 0, D3D8Const::TSS_COLOROP, D3D8Const::TOP_MODULATE);
        fnSetTSS(device, 0, D3D8Const::TSS_COLORARG1, D3D8Const::TA_TEXTURE);
        fnSetTSS(device, 0, D3D8Const::TSS_COLORARG2, D3D8Const::TA_DIFFUSE);
        fnSetTSS(device, 0, D3D8Const::TSS_ALPHAOP, D3D8Const::TOP_MODULATE);
        fnSetTSS(device, 0, D3D8Const::TSS_ALPHAARG1, D3D8Const::TA_TEXTURE);
        fnSetTSS(device, 0, D3D8Const::TSS_ALPHAARG2, D3D8Const::TA_DIFFUSE);
    }
    else
    {
        fnSetTSS(device, 0, D3D8Const::TSS_COLOROP, D3D8Const::TOP_SELECTARG1);
        fnSetTSS(device, 0, D3D8Const::TSS_COLORARG1, D3D8Const::TA_DIFFUSE);
        fnSetTSS(device, 0, D3D8Const::TSS_ALPHAOP, D3D8Const::TOP_SELECTARG1);
        fnSetTSS(device, 0, D3D8Const::TSS_ALPHAARG1, D3D8Const::TA_DIFFUSE);
    }
}

static void ImGui_ImplD3D8_SetupRenderState(ImDrawData* draw_data)
{
    ImGui_ImplD3D8_Data* bd = ImGui_ImplD3D8_GetBackendData();
    if (!bd || !bd->pd3dDevice)
        return;

    void* device = bd->pd3dDevice;
    D3D8Viewport vp = {};
    vp.X = 0;
    vp.Y = 0;
    vp.Width = (DWORD)draw_data->DisplaySize.x;
    vp.Height = (DWORD)draw_data->DisplaySize.y;
    vp.MinZ = 0.0f;
    vp.MaxZ = 1.0f;

    auto fnSetViewport = (pfn_D3D8Dev_SetViewport)ImGui_ImplD3D8_VT(device, D3D8VT::SetViewport);
    auto fnSetVS = (pfn_D3D8Dev_SetVertexShader)ImGui_ImplD3D8_VT(device, D3D8VT::SetVertexShader);

    fnSetViewport(device, &vp);
    D3D8_SetOverlayRenderState(device);
    fnSetVS(device, D3D8Const::FVF_TLVERTEX);
}

static void ImGui_ImplD3D8_CopyTextureRegion(const ImTextureData* tex, int src_x, int src_y, unsigned char* dst, int dst_pitch, int w, int h)
{
    if (!tex || !dst || w <= 0 || h <= 0)
        return;

    if (tex->Format == ImTextureFormat_Alpha8)
    {
        for (int y = 0; y < h; ++y)
        {
            const unsigned char* src = tex->Pixels + (size_t)(src_x + (src_y + y) * tex->Width);
            unsigned char* row = dst + y * dst_pitch;
            for (int x = 0; x < w; ++x)
            {
                row[x * 4 + 0] = 255;
                row[x * 4 + 1] = 255;
                row[x * 4 + 2] = 255;
                row[x * 4 + 3] = src[x];
            }
        }
        return;
    }

    for (int y = 0; y < h; ++y)
    {
        const unsigned char* src = tex->Pixels + (size_t)(src_x + (src_y + y) * tex->Width) * 4u;
        unsigned char* row = dst + y * dst_pitch;
        for (int x = 0; x < w; ++x)
        {
            row[x * 4 + 0] = src[x * 4 + 2];
            row[x * 4 + 1] = src[x * 4 + 1];
            row[x * 4 + 2] = src[x * 4 + 0];
            row[x * 4 + 3] = src[x * 4 + 3];
        }
    }
}

static bool ImGui_ImplD3D8_CreateTexture(ImTextureData* tex)
{
    ImGui_ImplD3D8_Data* bd = ImGui_ImplD3D8_GetBackendData();
    if (!bd || !bd->pd3dDevice || !tex || !tex->Pixels || tex->Width <= 0 || tex->Height <= 0)
        return false;

    void* backend_tex = nullptr;
    if (tex->Format == ImTextureFormat_RGBA32)
    {
        const RetroDeviceRef deviceRef = { bd->pd3dDevice, RetroRenderBackend_D3D8 };
        if (!RetroCreateBackendTextureFromRgba(deviceRef, tex->Pixels, tex->Width, tex->Height, &backend_tex))
            return false;
    }
    else if (tex->Format == ImTextureFormat_Alpha8)
    {
        std::vector<unsigned char> rgba;
        rgba.resize((size_t)tex->Width * (size_t)tex->Height * 4u);
        for (int i = 0; i < tex->Width * tex->Height; ++i)
        {
            rgba[(size_t)i * 4u + 0u] = 255;
            rgba[(size_t)i * 4u + 1u] = 255;
            rgba[(size_t)i * 4u + 2u] = 255;
            rgba[(size_t)i * 4u + 3u] = tex->Pixels[i];
        }

        const RetroDeviceRef deviceRef = { bd->pd3dDevice, RetroRenderBackend_D3D8 };
        if (!RetroCreateBackendTextureFromRgba(deviceRef, rgba.data(), tex->Width, tex->Height, &backend_tex))
            return false;
    }
    else
    {
        return false;
    }

    tex->SetTexID((ImTextureID)(intptr_t)backend_tex);
    tex->SetStatus(ImTextureStatus_OK);
    return true;
}

static void ImGui_ImplD3D8_DestroyTexture(ImTextureData* tex)
{
    if (!tex)
        return;

    void* texture = (void*)(intptr_t)tex->TexID;
    if (texture && tex->TexID != ImTextureID_Invalid)
        RetroReleaseBackendTexture(texture, RetroRenderBackend_D3D8);
    tex->SetTexID(ImTextureID_Invalid);
}

void ImGui_ImplD3D8_UpdateTexture(ImTextureData* tex)
{
    if (!tex)
        return;

    if (tex->Status == ImTextureStatus_WantCreate)
    {
        IM_ASSERT(tex->TexID == ImTextureID_Invalid);
        ImGui_ImplD3D8_CreateTexture(tex);
    }
    else if (tex->Status == ImTextureStatus_WantUpdates)
    {
        void* backend_tex = (void*)(intptr_t)tex->TexID;
        if (!backend_tex)
        {
            ImGui_ImplD3D8_CreateTexture(tex);
            return;
        }

        RECT update_rect = {
            (LONG)tex->UpdateRect.x,
            (LONG)tex->UpdateRect.y,
            (LONG)(tex->UpdateRect.x + tex->UpdateRect.w),
            (LONG)(tex->UpdateRect.y + tex->UpdateRect.h)
        };

        struct LockedRect { INT Pitch; void* pBits; };
        LockedRect locked = {};
        auto fnLock = (pfn_D3D8Tex_LockRect)ImGui_ImplD3D8_VT(backend_tex, D3D8TexVT::LockRect);
        auto fnUnlock = (pfn_D3D8Tex_UnlockRect)ImGui_ImplD3D8_VT(backend_tex, D3D8TexVT::UnlockRect);
        if (fnLock(backend_tex, 0, &locked, &update_rect, 0) >= 0 && locked.pBits)
        {
            for (ImTextureRect& r : tex->Updates)
            {
                unsigned char* dst = (unsigned char*)locked.pBits +
                    (int)(r.y - update_rect.top) * locked.Pitch +
                    (int)(r.x - update_rect.left) * 4;
                ImGui_ImplD3D8_CopyTextureRegion(tex, r.x, r.y, dst, locked.Pitch, r.w, r.h);
            }
            fnUnlock(backend_tex, 0);
            tex->SetStatus(ImTextureStatus_OK);
        }
        else
        {
            ImGui_ImplD3D8_DestroyTexture(tex);
            ImGui_ImplD3D8_CreateTexture(tex);
        }
    }
    else if (tex->Status == ImTextureStatus_WantDestroy)
    {
        ImGui_ImplD3D8_DestroyTexture(tex);
        tex->SetStatus(ImTextureStatus_Destroyed);
    }
}

bool ImGui_ImplD3D8_Init(void* device)
{
    ImGuiIO& io = ImGui::GetIO();
    IMGUI_CHECKVERSION();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");
    if (!device)
        return false;

    ImGui_ImplD3D8_Data* bd = IM_NEW(ImGui_ImplD3D8_Data)();
    io.BackendRendererUserData = (void*)bd;
    io.BackendRendererName = "imgui_impl_d3d8";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Renderer_TextureMaxWidth = platform_io.Renderer_TextureMaxHeight = 4096;

    bd->pd3dDevice = device;
    auto fnAddRef = (pfn_D3D8IUnknown_AddRef)ImGui_ImplD3D8_VT(device, D3D8TexVT::AddRef);
    fnAddRef(device);
    return true;
}

void ImGui_ImplD3D8_Shutdown()
{
    ImGui_ImplD3D8_Data* bd = ImGui_ImplD3D8_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");

    ImGuiIO& io = ImGui::GetIO();
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

    ImGui_ImplD3D8_InvalidateDeviceObjects();
    if (bd->pd3dDevice)
    {
        auto fnRelease = (pfn_D3D8IUnknown_Release)ImGui_ImplD3D8_VT(bd->pd3dDevice, D3D8TexVT::Release);
        fnRelease(bd->pd3dDevice);
    }

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    platform_io.ClearRendererHandlers();
    IM_DELETE(bd);
}

bool ImGui_ImplD3D8_CreateDeviceObjects()
{
    ImGui_ImplD3D8_Data* bd = ImGui_ImplD3D8_GetBackendData();
    return bd && bd->pd3dDevice;
}

void ImGui_ImplD3D8_InvalidateDeviceObjects()
{
    ImGui_ImplD3D8_Data* bd = ImGui_ImplD3D8_GetBackendData();
    if (!bd || !bd->pd3dDevice)
        return;

    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
        if (tex->RefCount == 1)
        {
            tex->SetStatus(ImTextureStatus_WantDestroy);
            ImGui_ImplD3D8_UpdateTexture(tex);
        }
}

void ImGui_ImplD3D8_NewFrame()
{
    ImGui_ImplD3D8_Data* bd = ImGui_ImplD3D8_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplD3D8_Init()?");
    IM_UNUSED(bd);
}

void ImGui_ImplD3D8_RenderDrawData(ImDrawData* draw_data)
{
    if (!draw_data || draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
        return;

    ImGui_ImplD3D8_Data* bd = ImGui_ImplD3D8_GetBackendData();
    if (!bd || !bd->pd3dDevice)
        return;

    if (draw_data->Textures != nullptr)
        for (ImTextureData* tex : *draw_data->Textures)
            if (tex->Status != ImTextureStatus_OK)
                ImGui_ImplD3D8_UpdateTexture(tex);

    void* device = bd->pd3dDevice;
    D3D8SavedState saved = {};
    D3D8_SaveRenderState(device, saved);
    ImGui_ImplD3D8_SetupRenderState(draw_data);

    auto fnSetTex = (pfn_D3D8Dev_SetTexture)ImGui_ImplD3D8_VT(device, D3D8VT::SetTexture);
    auto fnSetVS = (pfn_D3D8Dev_SetVertexShader)ImGui_ImplD3D8_VT(device, D3D8VT::SetVertexShader);
    auto fnDraw = (pfn_D3D8Dev_DrawPrimitiveUP)ImGui_ImplD3D8_VT(device, D3D8VT::DrawPrimitiveUP);

    const ImVec2 clip_off = draw_data->DisplayPos;
    std::vector<D3D8ImGuiVertex> clipped_vertices;

    for (const ImDrawList* draw_list : draw_data->CmdLists)
    {
        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; ++cmd_i)
        {
            const ImDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr)
            {
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    ImGui_ImplD3D8_SetupRenderState(draw_data);
                else
                    pcmd->UserCallback(draw_list, pcmd);
                continue;
            }

            ImVec4 clip_rect(
                pcmd->ClipRect.x - clip_off.x,
                pcmd->ClipRect.y - clip_off.y,
                pcmd->ClipRect.z - clip_off.x,
                pcmd->ClipRect.w - clip_off.y);
            if (clip_rect.x < 0.0f) clip_rect.x = 0.0f;
            if (clip_rect.y < 0.0f) clip_rect.y = 0.0f;
            if (clip_rect.z > draw_data->DisplaySize.x) clip_rect.z = draw_data->DisplaySize.x;
            if (clip_rect.w > draw_data->DisplaySize.y) clip_rect.w = draw_data->DisplaySize.y;
            if (clip_rect.z <= clip_rect.x || clip_rect.w <= clip_rect.y)
                continue;

            clipped_vertices.clear();
            clipped_vertices.reserve((size_t)pcmd->ElemCount * 3u);

            const ImDrawIdx* idx_buffer = draw_list->IdxBuffer.Data + pcmd->IdxOffset;
            for (unsigned int elem = 0; elem + 2 < pcmd->ElemCount; elem += 3)
            {
                const unsigned int i0 = (unsigned int)idx_buffer[elem + 0] + pcmd->VtxOffset;
                const unsigned int i1 = (unsigned int)idx_buffer[elem + 1] + pcmd->VtxOffset;
                const unsigned int i2 = (unsigned int)idx_buffer[elem + 2] + pcmd->VtxOffset;
                if (i0 >= (unsigned int)draw_list->VtxBuffer.Size ||
                    i1 >= (unsigned int)draw_list->VtxBuffer.Size ||
                    i2 >= (unsigned int)draw_list->VtxBuffer.Size)
                    continue;

                const ImDrawVert& v0 = draw_list->VtxBuffer.Data[i0];
                const ImDrawVert& v1 = draw_list->VtxBuffer.Data[i1];
                const ImDrawVert& v2 = draw_list->VtxBuffer.Data[i2];
                ClipVertex tri[3] = {
                    { v0.pos.x - clip_off.x, v0.pos.y - clip_off.y, v0.uv.x, v0.uv.y, IMGUI_COL_TO_D3D8_ARGB(v0.col) },
                    { v1.pos.x - clip_off.x, v1.pos.y - clip_off.y, v1.uv.x, v1.uv.y, IMGUI_COL_TO_D3D8_ARGB(v1.col) },
                    { v2.pos.x - clip_off.x, v2.pos.y - clip_off.y, v2.uv.x, v2.uv.y, IMGUI_COL_TO_D3D8_ARGB(v2.col) },
                };
                ImGui_ImplD3D8_AppendClippedTriangle(tri, clip_rect, clipped_vertices);
            }

            if (clipped_vertices.empty())
                continue;

            void* texture = (void*)(intptr_t)pcmd->GetTexID();
            if (pcmd->GetTexID() == ImTextureID_Invalid)
                texture = nullptr;
            fnSetVS(device, D3D8Const::FVF_TLVERTEX);
            fnSetTex(device, 0, texture);
            ImGui_ImplD3D8_SetTextureBlendMode(device, texture);
            fnDraw(device, D3D8Const::PT_TRIANGLELIST, (UINT)(clipped_vertices.size() / 3u), clipped_vertices.data(), sizeof(D3D8ImGuiVertex));
        }
    }

    D3D8_RestoreRenderState(device, saved);
}

#endif
