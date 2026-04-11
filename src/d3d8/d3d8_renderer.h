#pragma once
//
// d3d8_renderer.h - D3D8 直接渲染引擎
// 在 D3D8 模式下，不用 ImGui，直接在游戏 D3D8 设备上画 overlay
//

#include <windows.h>
#include <vector>
#include <string>
#include <map>

// ============================================================================
// D3D8 IDirect3DDevice8 VTable 索引
// ============================================================================
namespace D3D8VT {
    // 来源：d3d8.h IDirect3DDevice8 vtable (d3d8to9 项目验证)
    // IUnknown: 0=QueryInterface, 1=AddRef, 2=Release
    // 之后按 d3d8.h STDMETHOD 顺序 +3
    constexpr int Reset             = 14;
    constexpr int Present           = 15;
    constexpr int CreateTexture     = 20;
    constexpr int SetRenderTarget   = 31;
    constexpr int GetRenderTarget   = 32;
    constexpr int BeginScene        = 34;
    constexpr int EndScene          = 35;
    constexpr int SetTransform      = 37;
    constexpr int GetTransform      = 38;
    constexpr int SetViewport       = 40;
    constexpr int GetViewport       = 41;
    constexpr int SetRenderState    = 50;
    constexpr int GetRenderState    = 51;
    constexpr int GetTexture        = 60;
    constexpr int SetTexture        = 61;
    constexpr int GetTextureStageState = 62;
    constexpr int SetTextureStageState = 63;
    constexpr int DrawPrimitiveUP   = 72;
    constexpr int DrawIndexedPrimitiveUP = 73;
    constexpr int SetVertexShader   = 76;  // D3D8 用 SetVertexShader(FVF) 代替 SetFVF
    constexpr int GetVertexShader   = 77;
}

// ============================================================================
// D3D8 IDirect3DTexture8 VTable 索引
// ============================================================================
namespace D3D8TexVT {
    constexpr int QueryInterface = 0;
    constexpr int AddRef         = 1;
    constexpr int Release        = 2;
    constexpr int GetDevice      = 3;
    constexpr int SetPrivateData = 4;
    constexpr int GetPrivateData = 5;
    constexpr int FreePrivateData= 6;
    constexpr int SetPriority    = 7;
    constexpr int GetPriority    = 8;
    constexpr int PreLoad        = 9;
    constexpr int GetType        = 10;
    constexpr int SetLOD         = 11;
    constexpr int GetLOD         = 12;
    constexpr int GetLevelCount  = 13;
    constexpr int GetLevelDesc   = 14;
    constexpr int GetSurfaceLevel= 15;
    constexpr int LockRect       = 16;
    constexpr int UnlockRect     = 17;
}

// ============================================================================
// D3D8 常量（与 D3D9 值相同的直接复用数值）
// ============================================================================
namespace D3D8Const {
    // D3DFMT
    constexpr DWORD FMT_A8R8G8B8   = 21;
    constexpr DWORD FMT_INDEX16    = 101;
    constexpr DWORD FMT_INDEX32    = 102;
    // D3DPOOL
    constexpr DWORD POOL_MANAGED   = 1;
    // D3DRS (same values as D3D9)
    constexpr DWORD RS_ZENABLE            = 7;
    constexpr DWORD RS_FILLMODE           = 8;
    constexpr DWORD RS_SHADEMODE          = 9;
    constexpr DWORD RS_ZWRITEENABLE       = 14;
    constexpr DWORD RS_ALPHATESTENABLE    = 15;
    constexpr DWORD RS_SRCBLEND           = 19;
    constexpr DWORD RS_DESTBLEND          = 20;
    constexpr DWORD RS_CULLMODE           = 22;
    constexpr DWORD RS_FOGENABLE          = 28;
    constexpr DWORD RS_ALPHABLENDENABLE   = 27;
    constexpr DWORD RS_COLORWRITEENABLE   = 168;
    constexpr DWORD RS_BLENDOP            = 171;
    constexpr DWORD RS_LIGHTING           = 137;
    constexpr DWORD RS_CLIPPING           = 136;
    constexpr DWORD RS_SCISSORTESTENABLE  = 174;  // D3D9 only; D3D8 backend uses CPU clipping instead.
    // D3DFILL / D3DSHADE
    constexpr DWORD FILL_SOLID            = 3;
    constexpr DWORD SHADE_GOURAUD         = 2;
    // D3DBLEND
    constexpr DWORD BLEND_SRCALPHA        = 5;
    constexpr DWORD BLEND_INVSRCALPHA     = 6;
    // D3DBLENDOP
    constexpr DWORD BLENDOP_ADD           = 1;
    // D3DCULL
    constexpr DWORD CULL_NONE             = 1;
    // D3DTSS (same as D3D9)
    constexpr DWORD TSS_COLOROP           = 1;
    constexpr DWORD TSS_COLORARG1         = 2;
    constexpr DWORD TSS_COLORARG2         = 3;
    constexpr DWORD TSS_ALPHAOP           = 4;
    constexpr DWORD TSS_ALPHAARG1         = 5;
    constexpr DWORD TSS_ALPHAARG2         = 6;
    // D3D8 sampler states 通过 TSS 设置（D3D8 没有独立的 SamplerState API）
    // 在 D3D8 中这些值和 D3D9 的分离 D3DSAMP_xxx 不同
    constexpr DWORD TSS_ADDRESSU          = 13;  // D3DTSS_ADDRESSU in D3D8
    constexpr DWORD TSS_ADDRESSV          = 14;  // D3DTSS_ADDRESSV in D3D8
    constexpr DWORD TSS_MAGFILTER         = 16;  // D3DTSS_MAGFILTER in D3D8
    constexpr DWORD TSS_MINFILTER         = 17;  // D3DTSS_MINFILTER in D3D8
    constexpr DWORD TSS_MIPFILTER         = 18;  // D3DTSS_MIPFILTER in D3D8
    // D3DTOP
    constexpr DWORD TOP_DISABLE           = 1;
    constexpr DWORD TOP_SELECTARG1        = 2;
    constexpr DWORD TOP_MODULATE          = 4;
    // D3DTA
    constexpr DWORD TA_DIFFUSE            = 0;
    constexpr DWORD TA_TEXTURE            = 2; // D3DTA_TEXTURE in D3D8 is actually 0x00000002 with no complement
    // D3DTEXF
    constexpr DWORD TEXF_NONE             = 0;
    constexpr DWORD TEXF_POINT            = 1;
    // D3DTADDRESS
    constexpr DWORD TADDRESS_CLAMP        = 3;
    // D3DPT
    constexpr DWORD PT_TRIANGLELIST       = 4;
    constexpr DWORD PT_TRIANGLESTRIP      = 5;
    // FVF
    constexpr DWORD FVF_XYZRHW            = 0x004;
    constexpr DWORD FVF_DIFFUSE            = 0x040;
    constexpr DWORD FVF_TEX1              = 0x100;
    constexpr DWORD FVF_TLVERTEX          = FVF_XYZRHW | FVF_DIFFUSE | FVF_TEX1; // 0x144
    constexpr DWORD FVF_SOLIDVERTEX       = FVF_XYZRHW | FVF_DIFFUSE;            // 0x044
}

// ============================================================================
// 顶点结构（与 D3D9 版本二进制兼容）
// ============================================================================
struct D3D8TexturedVertex {
    float x, y, z, rhw;
    DWORD color;
    float u, v;
};

struct D3D8SolidVertex {
    float x, y, z, rhw;
    DWORD color;
};

// ============================================================================
// D3D8 纹理包装
// ============================================================================
struct D3D8Texture {
    void* pTexture8 = nullptr;  // IDirect3DTexture8*
    int width  = 0;
    int height = 0;
};

// ============================================================================
// D3D8 vtable 调用辅助（从 void* pDevice8 取 vtable 后调用）
// ============================================================================
inline DWORD* D3D8_GetVTable(void* pObj) {
    return *(DWORD**)pObj;
}

// ============================================================================
// D3D8 渲染引擎 API
// ============================================================================

// 纹理管理
D3D8Texture D3D8_CreateTextureFromRGBA(void* pDevice8, const unsigned char* rgba, int w, int h);
void D3D8_ReleaseTexture(D3D8Texture& tex);

// 从 stb_image 内存 PNG 创建纹理
D3D8Texture D3D8_CreateTextureFromPngMemory(void* pDevice8, const unsigned char* fileData, size_t fileSize);

// 绘制原语
void D3D8_DrawTexturedQuad(void* pDevice8, D3D8Texture* tex, float x, float y, float w, float h, DWORD color = 0xFFFFFFFF);
void D3D8_DrawTexturedQuadUV(void* pDevice8, D3D8Texture* tex, float x, float y, float w, float h,
                              float u0, float v0, float u1, float v1, DWORD color = 0xFFFFFFFF);
void D3D8_DrawSolidQuad(void* pDevice8, float x, float y, float w, float h, DWORD color);

// Render state 保存/恢复
// D3D8 D3DVIEWPORT8 布局（和 D3D9 D3DVIEWPORT9 二进制兼容）
struct D3D8Viewport {
    DWORD X, Y, Width, Height;
    float MinZ, MaxZ;
};

// D3D8 D3DMATRIX（4x4 float，和 D3D9 D3DMATRIX 二进制兼容）
struct D3D8Matrix {
    float m[4][4];
};

struct D3D8SavedState {
    DWORD rs[16];   // 保存的 render state 值
    DWORD tss0[16]; // stage 0 的 TSS
    DWORD tss1[4];  // stage 1 的 TSS (只保存 COLOROP/ALPHAOP)
    void* savedTexture0;
    DWORD savedVS;
    D3D8Viewport savedViewport;
    D3D8Matrix savedWorld;       // D3DTS_WORLD = 256
    D3D8Matrix savedView;        // D3DTS_VIEW = 2
    D3D8Matrix savedProjection;  // D3DTS_PROJECTION = 3
};

void D3D8_SaveRenderState(void* pDevice8, D3D8SavedState& saved);
void D3D8_SetOverlayRenderState(void* pDevice8);
void D3D8_RestoreRenderState(void* pDevice8, const D3D8SavedState& saved);

// ============================================================================
// D3D8 文字渲染（DWrite -> 内存 bitmap -> D3D8 纹理 -> DrawPrimitiveUP）
// ============================================================================
struct D3D8TextCache {
    std::map<std::string, D3D8Texture> cache;
};

// 初始化/关闭 DWrite (复用现有 DWrite COM 初始化)
bool D3D8_DWriteInit();
void D3D8_DWriteShutdown();

// 渲染文字到 D3D8 纹理（缓存）
D3D8Texture* D3D8_GetTextTexture(void* pDevice8, D3D8TextCache& cache, const char* text, float fontSize, DWORD color);

// 绘制文字
void D3D8_DrawText(void* pDevice8, D3D8TextCache& cache, float x, float y, DWORD color, const char* text, float fontSize);

// 测量文字宽高
void D3D8_MeasureText(const char* text, float fontSize, float* outW, float* outH);

// 清空文字纹理缓存（设备丢失时调用）
void D3D8_ClearTextCache(D3D8TextCache& cache);
