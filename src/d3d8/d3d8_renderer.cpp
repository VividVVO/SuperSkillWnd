//
// d3d8_renderer.cpp - D3D8 直接渲染引擎
// 通过 vtable 调用 D3D8 设备方法，实现纹理创建/绘制/状态管理
//

#include "d3d8_renderer.h"
#include "../util/stb_image.h"

#include <cstring>
#include <cmath>
#include <cstdio>

// ============================================================================
// D3D8 vtable 调用辅助宏
// ============================================================================
// 用宏把 vtable 索引 -> 函数指针调用封装起来，减少重复代码
// D3D8 COM 方法都是 __stdcall，第一个隐式参数是 this

typedef HRESULT (__stdcall *pfn_D3D8Dev_SetRenderState)(void* pThis, DWORD State, DWORD Value);
typedef HRESULT (__stdcall *pfn_D3D8Dev_GetRenderState)(void* pThis, DWORD State, DWORD* pValue);
typedef HRESULT (__stdcall *pfn_D3D8Dev_SetTextureStageState)(void* pThis, DWORD Stage, DWORD Type, DWORD Value);
typedef HRESULT (__stdcall *pfn_D3D8Dev_GetTextureStageState)(void* pThis, DWORD Stage, DWORD Type, DWORD* pValue);
typedef HRESULT (__stdcall *pfn_D3D8Dev_SetTexture)(void* pThis, DWORD Stage, void* pTexture);
typedef HRESULT (__stdcall *pfn_D3D8Dev_GetTexture)(void* pThis, DWORD Stage, void** ppTexture);
typedef HRESULT (__stdcall *pfn_D3D8Dev_SetVertexShader)(void* pThis, DWORD Handle);
typedef HRESULT (__stdcall *pfn_D3D8Dev_GetVertexShader)(void* pThis, DWORD* pHandle);
typedef HRESULT (__stdcall *pfn_D3D8Dev_DrawPrimitiveUP)(void* pThis, DWORD PrimitiveType, UINT PrimitiveCount, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride);
typedef HRESULT (__stdcall *pfn_D3D8Dev_CreateTexture)(void* pThis, UINT Width, UINT Height, UINT Levels, DWORD Usage, DWORD Format, DWORD Pool, void** ppTexture);
typedef HRESULT (__stdcall *pfn_D3D8Dev_SetTransform)(void* pThis, DWORD State, const void* pMatrix);
typedef HRESULT (__stdcall *pfn_D3D8Dev_GetTransform)(void* pThis, DWORD State, void* pMatrix);
typedef HRESULT (__stdcall *pfn_D3D8Dev_SetViewport)(void* pThis, const void* pViewport);
typedef HRESULT (__stdcall *pfn_D3D8Dev_GetViewport)(void* pThis, void* pViewport);

// IDirect3DTexture8 vtable call typedefs
typedef ULONG   (__stdcall *pfn_D3D8Tex_Release)(void* pThis);
typedef HRESULT (__stdcall *pfn_D3D8Tex_LockRect)(void* pThis, UINT Level, void* pLockedRect, const RECT* pRect, DWORD Flags);
typedef HRESULT (__stdcall *pfn_D3D8Tex_UnlockRect)(void* pThis, UINT Level);

// 快捷方法：从 vtable 取函数指针
static inline void* VT(void* pObj, int index) {
    DWORD* vt = *(DWORD**)pObj;
    return (void*)vt[index];
}

// ============================================================================
// 日志辅助
// ============================================================================
#include "../core/Common.h"  // WriteLog, WriteLogFmt

static void SSW_Log(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    WriteLog(buf);
}

// ============================================================================
// 纹理管理
// ============================================================================

D3D8Texture D3D8_CreateTextureFromRGBA(void* pDevice8, const unsigned char* rgba, int w, int h)
{
    D3D8Texture result = {};
    if (!pDevice8 || !rgba || w <= 0 || h <= 0)
        return result;

    // IDirect3DDevice8::CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture)
    // vtable[20], 注意 D3D8 没有最后的 pSharedHandle 参数
    void* pTexture8 = nullptr;
    auto fn = (pfn_D3D8Dev_CreateTexture)VT(pDevice8, D3D8VT::CreateTexture);
    HRESULT hr = fn(pDevice8, (UINT)w, (UINT)h, 1, 0, D3D8Const::FMT_A8R8G8B8, D3D8Const::POOL_MANAGED, &pTexture8);
    if (FAILED(hr) || !pTexture8)
    {
        SSW_Log("[D3D8] CreateTexture failed %dx%d hr=0x%08X", w, h, (unsigned)hr);
        return result;
    }

    // D3D8 的 D3DLOCKED_RECT 和 D3D9 二进制布局相同
    struct LockedRect { INT Pitch; void* pBits; };
    LockedRect locked = {};

    auto fnLock = (pfn_D3D8Tex_LockRect)VT(pTexture8, D3D8TexVT::LockRect);
    hr = fnLock(pTexture8, 0, &locked, nullptr, 0);
    if (FAILED(hr) || !locked.pBits)
    {
        SSW_Log("[D3D8] LockRect failed hr=0x%08X", (unsigned)hr);
        auto fnRel = (pfn_D3D8Tex_Release)VT(pTexture8, D3D8TexVT::Release);
        fnRel(pTexture8);
        return result;
    }

    // RGBA -> BGRA 通道交换（D3DFMT_A8R8G8B8 实际是 BGRA 内存布局）
    for (int y = 0; y < h; y++)
    {
        unsigned char* dstRow = (unsigned char*)locked.pBits + y * locked.Pitch;
        const unsigned char* srcRow = rgba + y * w * 4;
        for (int x = 0; x < w; x++)
        {
            dstRow[x * 4 + 0] = srcRow[x * 4 + 2]; // B
            dstRow[x * 4 + 1] = srcRow[x * 4 + 1]; // G
            dstRow[x * 4 + 2] = srcRow[x * 4 + 0]; // R
            dstRow[x * 4 + 3] = srcRow[x * 4 + 3]; // A
        }
    }

    auto fnUnlock = (pfn_D3D8Tex_UnlockRect)VT(pTexture8, D3D8TexVT::UnlockRect);
    fnUnlock(pTexture8, 0);

    result.pTexture8 = pTexture8;
    result.width = w;
    result.height = h;
    return result;
}

void D3D8_ReleaseTexture(D3D8Texture& tex)
{
    if (tex.pTexture8)
    {
        auto fnRel = (pfn_D3D8Tex_Release)VT(tex.pTexture8, D3D8TexVT::Release);
        fnRel(tex.pTexture8);
        tex.pTexture8 = nullptr;
    }
    tex.width = 0;
    tex.height = 0;
}

D3D8Texture D3D8_CreateTextureFromPngMemory(void* pDevice8, const unsigned char* fileData, size_t fileSize)
{
    D3D8Texture result = {};
    if (!pDevice8 || !fileData || fileSize == 0)
        return result;

    int w = 0, h = 0, channels = 0;
    unsigned char* rgba = stbi_load_from_memory(fileData, (int)fileSize, &w, &h, &channels, 4);
    if (!rgba)
        return result;

    result = D3D8_CreateTextureFromRGBA(pDevice8, rgba, w, h);
    stbi_image_free(rgba);
    return result;
}

// ============================================================================
// Render state 保存/恢复
// ============================================================================

// 需要保存的 render state 列表
static const DWORD s_savedRS[] = {
    D3D8Const::RS_ZENABLE,
    D3D8Const::RS_FILLMODE,
    D3D8Const::RS_SHADEMODE,
    D3D8Const::RS_ZWRITEENABLE,
    D3D8Const::RS_ALPHATESTENABLE,
    D3D8Const::RS_SRCBLEND,
    D3D8Const::RS_DESTBLEND,
    D3D8Const::RS_CULLMODE,
    D3D8Const::RS_FOGENABLE,
    D3D8Const::RS_ALPHABLENDENABLE,
    D3D8Const::RS_COLORWRITEENABLE,
    D3D8Const::RS_BLENDOP,
    D3D8Const::RS_LIGHTING,
    D3D8Const::RS_CLIPPING,
};
static const int s_savedRS_count = sizeof(s_savedRS) / sizeof(s_savedRS[0]);

// 需要保存的 TSS stage 0
static const DWORD s_savedTSS0[] = {
    D3D8Const::TSS_COLOROP,
    D3D8Const::TSS_COLORARG1,
    D3D8Const::TSS_COLORARG2,
    D3D8Const::TSS_ALPHAOP,
    D3D8Const::TSS_ALPHAARG1,
    D3D8Const::TSS_ALPHAARG2,
    D3D8Const::TSS_MINFILTER,
    D3D8Const::TSS_MAGFILTER,
    D3D8Const::TSS_MIPFILTER,
    D3D8Const::TSS_ADDRESSU,
    D3D8Const::TSS_ADDRESSV,
};
static const int s_savedTSS0_count = sizeof(s_savedTSS0) / sizeof(s_savedTSS0[0]);

void D3D8_SaveRenderState(void* pDevice8, D3D8SavedState& saved)
{
    memset(&saved, 0, sizeof(saved));

    auto fnGetRS = (pfn_D3D8Dev_GetRenderState)VT(pDevice8, D3D8VT::GetRenderState);
    auto fnGetTSS = (pfn_D3D8Dev_GetTextureStageState)VT(pDevice8, D3D8VT::GetTextureStageState);
    auto fnGetTex = (pfn_D3D8Dev_GetTexture)VT(pDevice8, D3D8VT::GetTexture);
    auto fnGetVS = (pfn_D3D8Dev_GetVertexShader)VT(pDevice8, D3D8VT::GetVertexShader);
    auto fnGetViewport = (pfn_D3D8Dev_GetViewport)VT(pDevice8, D3D8VT::GetViewport);
    auto fnGetTransform = (pfn_D3D8Dev_GetTransform)VT(pDevice8, D3D8VT::GetTransform);

    // 保存 render states
    for (int i = 0; i < s_savedRS_count && i < 16; i++)
        fnGetRS(pDevice8, s_savedRS[i], &saved.rs[i]);

    // 保存 TSS stage 0
    for (int i = 0; i < s_savedTSS0_count && i < 16; i++)
        fnGetTSS(pDevice8, 0, s_savedTSS0[i], &saved.tss0[i]);

    // 保存 TSS stage 1 (COLOROP, ALPHAOP)
    fnGetTSS(pDevice8, 1, D3D8Const::TSS_COLOROP, &saved.tss1[0]);
    fnGetTSS(pDevice8, 1, D3D8Const::TSS_ALPHAOP, &saved.tss1[1]);

    // 保存 texture stage 0
    saved.savedTexture0 = nullptr;
    fnGetTex(pDevice8, 0, &saved.savedTexture0);

    // 保存 vertex shader / FVF (D3D8 GetVertexShader vtable[77])
    saved.savedVS = 0;
    fnGetVS(pDevice8, &saved.savedVS);

    // 保存 viewport
    fnGetViewport(pDevice8, &saved.savedViewport);

    // 保存 transforms: D3DTS_VIEW=2, D3DTS_PROJECTION=3, D3DTS_WORLD=256
    fnGetTransform(pDevice8, 2, &saved.savedView);
    fnGetTransform(pDevice8, 3, &saved.savedProjection);
    fnGetTransform(pDevice8, 256, &saved.savedWorld);
}

void D3D8_SetOverlayRenderState(void* pDevice8)
{
    auto fnSetRS = (pfn_D3D8Dev_SetRenderState)VT(pDevice8, D3D8VT::SetRenderState);
    auto fnSetTSS = (pfn_D3D8Dev_SetTextureStageState)VT(pDevice8, D3D8VT::SetTextureStageState);

    // Alpha blend
    fnSetRS(pDevice8, D3D8Const::RS_FILLMODE, D3D8Const::FILL_SOLID);
    fnSetRS(pDevice8, D3D8Const::RS_SHADEMODE, D3D8Const::SHADE_GOURAUD);
    fnSetRS(pDevice8, D3D8Const::RS_ALPHABLENDENABLE, TRUE);
    fnSetRS(pDevice8, D3D8Const::RS_SRCBLEND, D3D8Const::BLEND_SRCALPHA);
    fnSetRS(pDevice8, D3D8Const::RS_DESTBLEND, D3D8Const::BLEND_INVSRCALPHA);
    fnSetRS(pDevice8, D3D8Const::RS_BLENDOP, D3D8Const::BLENDOP_ADD);
    fnSetRS(pDevice8, D3D8Const::RS_COLORWRITEENABLE, 0x0000000F);

    // 关闭不需要的功能
    fnSetRS(pDevice8, D3D8Const::RS_ALPHATESTENABLE, FALSE);
    fnSetRS(pDevice8, D3D8Const::RS_LIGHTING, FALSE);
    fnSetRS(pDevice8, D3D8Const::RS_ZENABLE, FALSE);
    fnSetRS(pDevice8, D3D8Const::RS_ZWRITEENABLE, FALSE);
    fnSetRS(pDevice8, D3D8Const::RS_CULLMODE, D3D8Const::CULL_NONE);
    fnSetRS(pDevice8, D3D8Const::RS_FOGENABLE, FALSE);
    fnSetRS(pDevice8, D3D8Const::RS_CLIPPING, TRUE);

    // Texture stage 0 - 纹理和顶点色混合
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_COLOROP, D3D8Const::TOP_MODULATE);
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_COLORARG1, D3D8Const::TA_TEXTURE);
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_COLORARG2, D3D8Const::TA_DIFFUSE);
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_ALPHAOP, D3D8Const::TOP_MODULATE);
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_ALPHAARG1, D3D8Const::TA_TEXTURE);
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_ALPHAARG2, D3D8Const::TA_DIFFUSE);

    // Texture stage 1 - 禁用
    fnSetTSS(pDevice8, 1, D3D8Const::TSS_COLOROP, D3D8Const::TOP_DISABLE);
    fnSetTSS(pDevice8, 1, D3D8Const::TSS_ALPHAOP, D3D8Const::TOP_DISABLE);

    // D3D8 sampler 设置通过 TSS
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_MINFILTER, D3D8Const::TEXF_POINT);
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_MAGFILTER, D3D8Const::TEXF_POINT);
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_MIPFILTER, D3D8Const::TEXF_NONE);
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_ADDRESSU, D3D8Const::TADDRESS_CLAMP);
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_ADDRESSV, D3D8Const::TADDRESS_CLAMP);
}

void D3D8_RestoreRenderState(void* pDevice8, const D3D8SavedState& saved)
{
    auto fnSetRS = (pfn_D3D8Dev_SetRenderState)VT(pDevice8, D3D8VT::SetRenderState);
    auto fnSetTSS = (pfn_D3D8Dev_SetTextureStageState)VT(pDevice8, D3D8VT::SetTextureStageState);
    auto fnSetTex = (pfn_D3D8Dev_SetTexture)VT(pDevice8, D3D8VT::SetTexture);
    auto fnSetVS = (pfn_D3D8Dev_SetVertexShader)VT(pDevice8, D3D8VT::SetVertexShader);
    auto fnSetViewport = (pfn_D3D8Dev_SetViewport)VT(pDevice8, D3D8VT::SetViewport);
    auto fnSetTransform = (pfn_D3D8Dev_SetTransform)VT(pDevice8, D3D8VT::SetTransform);

    // 恢复 render states
    for (int i = 0; i < s_savedRS_count && i < 16; i++)
        fnSetRS(pDevice8, s_savedRS[i], saved.rs[i]);

    // 恢复 TSS stage 0
    for (int i = 0; i < s_savedTSS0_count && i < 16; i++)
        fnSetTSS(pDevice8, 0, s_savedTSS0[i], saved.tss0[i]);

    // 恢复 TSS stage 1
    fnSetTSS(pDevice8, 1, D3D8Const::TSS_COLOROP, saved.tss1[0]);
    fnSetTSS(pDevice8, 1, D3D8Const::TSS_ALPHAOP, saved.tss1[1]);

    // 恢复 texture
    fnSetTex(pDevice8, 0, saved.savedTexture0);
    // 如果保存时 GetTexture AddRef 了，这里需要 Release
    if (saved.savedTexture0)
    {
        auto fnRel = (pfn_D3D8Tex_Release)VT(saved.savedTexture0, D3D8TexVT::Release);
        fnRel(saved.savedTexture0);
    }

    // 恢复 vertex shader / FVF
    fnSetVS(pDevice8, saved.savedVS);

    // 恢复 viewport
    fnSetViewport(pDevice8, &saved.savedViewport);

    // 恢复 transforms: D3DTS_VIEW=2, D3DTS_PROJECTION=3, D3DTS_WORLD=256
    fnSetTransform(pDevice8, 2, &saved.savedView);
    fnSetTransform(pDevice8, 3, &saved.savedProjection);
    fnSetTransform(pDevice8, 256, &saved.savedWorld);
}

// ============================================================================
// 绘制原语
// ============================================================================

void D3D8_DrawTexturedQuadUV(void* pDevice8, D3D8Texture* tex, float x, float y, float w, float h,
                              float u0, float v0, float u1, float v1, DWORD color)
{
    if (!pDevice8 || !tex || !tex->pTexture8 || w <= 0.0f || h <= 0.0f)
        return;

    D3D8TexturedVertex verts[4] = {
        { x,     y,     0.5f, 1.0f, color, u0, v0 },
        { x + w, y,     0.5f, 1.0f, color, u1, v0 },
        { x,     y + h, 0.5f, 1.0f, color, u0, v1 },
        { x + w, y + h, 0.5f, 1.0f, color, u1, v1 },
    };

    auto fnSetVS = (pfn_D3D8Dev_SetVertexShader)VT(pDevice8, D3D8VT::SetVertexShader);
    auto fnSetTex = (pfn_D3D8Dev_SetTexture)VT(pDevice8, D3D8VT::SetTexture);
    auto fnDraw = (pfn_D3D8Dev_DrawPrimitiveUP)VT(pDevice8, D3D8VT::DrawPrimitiveUP);

    fnSetVS(pDevice8, D3D8Const::FVF_TLVERTEX);
    fnSetTex(pDevice8, 0, tex->pTexture8);
    fnDraw(pDevice8, D3D8Const::PT_TRIANGLESTRIP, 2, verts, sizeof(D3D8TexturedVertex));
}

void D3D8_DrawTexturedQuad(void* pDevice8, D3D8Texture* tex, float x, float y, float w, float h, DWORD color)
{
    D3D8_DrawTexturedQuadUV(pDevice8, tex, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, color);
}

void D3D8_DrawSolidQuad(void* pDevice8, float x, float y, float w, float h, DWORD color)
{
    if (!pDevice8 || w <= 0.0f || h <= 0.0f)
        return;

    D3D8SolidVertex verts[4] = {
        { x,     y,     0.5f, 1.0f, color },
        { x + w, y,     0.5f, 1.0f, color },
        { x,     y + h, 0.5f, 1.0f, color },
        { x + w, y + h, 0.5f, 1.0f, color },
    };

    auto fnSetVS = (pfn_D3D8Dev_SetVertexShader)VT(pDevice8, D3D8VT::SetVertexShader);
    auto fnSetTex = (pfn_D3D8Dev_SetTexture)VT(pDevice8, D3D8VT::SetTexture);
    auto fnDraw = (pfn_D3D8Dev_DrawPrimitiveUP)VT(pDevice8, D3D8VT::DrawPrimitiveUP);

    // 纯色模式：设置 TSS stage 0 为只用 diffuse
    auto fnSetTSS = (pfn_D3D8Dev_SetTextureStageState)VT(pDevice8, D3D8VT::SetTextureStageState);
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_COLOROP, D3D8Const::TOP_SELECTARG1);
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_COLORARG1, D3D8Const::TA_DIFFUSE);
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_ALPHAOP, D3D8Const::TOP_SELECTARG1);
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_ALPHAARG1, D3D8Const::TA_DIFFUSE);

    fnSetVS(pDevice8, D3D8Const::FVF_SOLIDVERTEX);
    fnSetTex(pDevice8, 0, nullptr);
    fnDraw(pDevice8, D3D8Const::PT_TRIANGLESTRIP, 2, verts, sizeof(D3D8SolidVertex));

    // 恢复 MODULATE 模式供后续纹理绘制使用
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_COLOROP, D3D8Const::TOP_MODULATE);
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_COLORARG1, D3D8Const::TA_TEXTURE);
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_COLORARG2, D3D8Const::TA_DIFFUSE);
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_ALPHAOP, D3D8Const::TOP_MODULATE);
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_ALPHAARG1, D3D8Const::TA_TEXTURE);
    fnSetTSS(pDevice8, 0, D3D8Const::TSS_ALPHAARG2, D3D8Const::TA_DIFFUSE);
}

// ============================================================================
// D3D8 文字渲染（GDI 绘制到内存 -> D3D8 纹理）
// ============================================================================

static HDC      s_d3d8TextDC = nullptr;
static HFONT    s_d3d8TextFont = nullptr;
static bool     s_d3d8DWriteInited = false;

bool D3D8_DWriteInit()
{
    if (s_d3d8DWriteInited)
        return true;

    s_d3d8TextDC = CreateCompatibleDC(nullptr);
    if (!s_d3d8TextDC)
        return false;

    SetBkMode(s_d3d8TextDC, TRANSPARENT);
    SetTextAlign(s_d3d8TextDC, TA_TOP | TA_LEFT);

    s_d3d8DWriteInited = true;
    return true;
}

void D3D8_DWriteShutdown()
{
    if (s_d3d8TextFont)
    {
        DeleteObject(s_d3d8TextFont);
        s_d3d8TextFont = nullptr;
    }
    if (s_d3d8TextDC)
    {
        DeleteDC(s_d3d8TextDC);
        s_d3d8TextDC = nullptr;
    }
    s_d3d8DWriteInited = false;
}

static HFONT D3D8_EnsureFont(int pixelHeight)
{
    // 简化实现：缓存一个字体，字号变化时重新创建
    static int s_cachedHeight = 0;
    if (s_d3d8TextFont && s_cachedHeight == pixelHeight)
        return s_d3d8TextFont;

    if (s_d3d8TextFont)
        DeleteObject(s_d3d8TextFont);

    s_d3d8TextFont = CreateFontW(
        -pixelHeight, 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"\x5B8B\x4F53"  // 宋体
    );
    s_cachedHeight = pixelHeight;
    return s_d3d8TextFont;
}

// 将文字渲染成 ARGB bitmap，然后创建 D3D8 纹理
static D3D8Texture D3D8_RenderTextToTexture(void* pDevice8, const char* text, float fontSize, DWORD color)
{
    D3D8Texture result = {};
    if (!s_d3d8DWriteInited || !s_d3d8TextDC || !pDevice8 || !text || !text[0])
        return result;

    int pixelHeight = (int)(fontSize > 0.0f ? fontSize : 12.0f);
    HFONT font = D3D8_EnsureFont(pixelHeight);
    if (!font)
        return result;

    // UTF-8 -> wchar_t
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (wlen <= 0)
        return result;

    std::vector<wchar_t> wtext((size_t)wlen);
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext.data(), wlen);

    int textLen = wlen - 1; // 不含 null
    if (textLen <= 0)
        return result;

    HGDIOBJ oldFont = SelectObject(s_d3d8TextDC, font);

    // 测量文字尺寸
    SIZE textSize = {};
    GetTextExtentPoint32W(s_d3d8TextDC, wtext.data(), textLen, &textSize);

    int texW = textSize.cx + 2; // 留一点余量
    int texH = textSize.cy + 2;
    if (texW <= 0 || texH <= 0)
    {
        SelectObject(s_d3d8TextDC, oldFont);
        return result;
    }

    // 创建 DIB 用于 GDI 绘制
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = texW;
    bmi.bmiHeader.biHeight = -texH; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBitmapBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(s_d3d8TextDC, &bmi, DIB_RGB_COLORS, &pBitmapBits, nullptr, 0);
    if (!hBitmap || !pBitmapBits)
    {
        SelectObject(s_d3d8TextDC, oldFont);
        return result;
    }

    HGDIOBJ oldBitmap = SelectObject(s_d3d8TextDC, hBitmap);

    // 清空背景为黑色
    memset(pBitmapBits, 0, (size_t)texW * (size_t)texH * 4);

    // 用白色绘制文字（之后通过 alpha 合成着色）
    SetTextColor(s_d3d8TextDC, RGB(255, 255, 255));
    TextOutW(s_d3d8TextDC, 1, 1, wtext.data(), textLen);

    GdiFlush();

    // 将 GDI bitmap 转为带 alpha 的 ARGB
    unsigned char colorR = (unsigned char)((color >> 16) & 0xFF);
    unsigned char colorG = (unsigned char)((color >> 8) & 0xFF);
    unsigned char colorB = (unsigned char)(color & 0xFF);

    std::vector<unsigned char> argbPixels((size_t)texW * (size_t)texH * 4);
    const unsigned char* src = (const unsigned char*)pBitmapBits;

    for (int y = 0; y < texH; y++)
    {
        for (int x = 0; x < texW; x++)
        {
            size_t idx = ((size_t)y * (size_t)texW + (size_t)x) * 4;
            // GDI 输出是 BGRA，文字像素亮度作为 alpha
            unsigned char luminance = src[idx + 0]; // B channel (white text -> all channels same)
            // 也检查 G 和 R，取最大值
            unsigned char g = src[idx + 1];
            unsigned char r = src[idx + 2];
            if (g > luminance) luminance = g;
            if (r > luminance) luminance = r;

            // 输出 RGBA (后续 CreateTextureFromRGBA 会做 RGBA->BGRA 交换)
            argbPixels[idx + 0] = colorR;
            argbPixels[idx + 1] = colorG;
            argbPixels[idx + 2] = colorB;
            argbPixels[idx + 3] = luminance;
        }
    }

    SelectObject(s_d3d8TextDC, oldBitmap);
    SelectObject(s_d3d8TextDC, oldFont);
    DeleteObject(hBitmap);

    result = D3D8_CreateTextureFromRGBA(pDevice8, argbPixels.data(), texW, texH);
    return result;
}

void D3D8_MeasureText(const char* text, float fontSize, float* outW, float* outH)
{
    if (outW) *outW = 0.0f;
    if (outH) *outH = 0.0f;
    if (!text || !text[0])
        return;

    if (!s_d3d8DWriteInited || !s_d3d8TextDC)
    {
        // fallback 估算
        int len = (int)strlen(text);
        if (outW) *outW = (float)(len * (int)(fontSize * 0.6f));
        if (outH) *outH = fontSize;
        return;
    }

    int pixelHeight = (int)(fontSize > 0.0f ? fontSize : 12.0f);
    HFONT font = D3D8_EnsureFont(pixelHeight);
    if (!font) return;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (wlen <= 0) return;
    std::vector<wchar_t> wtext((size_t)wlen);
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext.data(), wlen);

    HGDIOBJ oldFont = SelectObject(s_d3d8TextDC, font);
    SIZE textSize = {};
    GetTextExtentPoint32W(s_d3d8TextDC, wtext.data(), wlen - 1, &textSize);
    SelectObject(s_d3d8TextDC, oldFont);

    if (outW) *outW = (float)textSize.cx;
    if (outH) *outH = (float)textSize.cy;
}

D3D8Texture* D3D8_GetTextTexture(void* pDevice8, D3D8TextCache& cache, const char* text, float fontSize, DWORD color)
{
    if (!text || !text[0])
        return nullptr;

    // 构建缓存 key: text + fontSize + color
    char key[512];
    snprintf(key, sizeof(key), "%s|%.0f|%08X", text, fontSize, (unsigned)color);
    std::string keyStr(key);

    auto it = cache.cache.find(keyStr);
    if (it != cache.cache.end())
    {
        if (it->second.pTexture8)
            return &it->second;
        // 之前创建失败了，不再重试
        return nullptr;
    }

    D3D8Texture tex = D3D8_RenderTextToTexture(pDevice8, text, fontSize, color);
    cache.cache[keyStr] = tex;

    if (tex.pTexture8)
        return &cache.cache[keyStr];
    return nullptr;
}

void D3D8_DrawText(void* pDevice8, D3D8TextCache& cache, float x, float y, DWORD color, const char* text, float fontSize)
{
    D3D8Texture* tex = D3D8_GetTextTexture(pDevice8, cache, text, fontSize, color);
    if (tex && tex->pTexture8)
    {
        D3D8_DrawTexturedQuad(pDevice8, tex, x, y, (float)tex->width, (float)tex->height, 0xFFFFFFFF);
    }
}

void D3D8_ClearTextCache(D3D8TextCache& cache)
{
    for (auto& pair : cache.cache)
        D3D8_ReleaseTexture(pair.second);
    cache.cache.clear();
}
