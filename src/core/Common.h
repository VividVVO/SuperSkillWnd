#pragma once
//
// Common.h — 公共头文件、工具函数、日志
//
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "d3d9.lib")

// ============================================================================
// 安全内存检查
// ============================================================================
inline bool SafeIsBadReadPtr(const void* ptr, size_t size)
{
    if (!ptr) return true;
    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == 0) return true;
    if (mbi.State != MEM_COMMIT) return true;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return true;
    uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    if ((uintptr_t)ptr + size > regionEnd) return true;
    return false;
}

// ============================================================================
// 日志
// ============================================================================
#define LOG_FILE "C:\\SuperSkillWnd.log"

inline void WriteLog(const char* msg)
{
    FILE* f = fopen(LOG_FILE, "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}

inline void WriteLogFmt(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    WriteLog(buf);
}

// ============================================================================
// CWnd坐标读取
// ============================================================================
inline int CWnd_GetX(uintptr_t cwnd)
{
    if (!cwnd || SafeIsBadReadPtr((void*)(cwnd + 0x18), 4)) return 0;
    uintptr_t com = *(uintptr_t*)(cwnd + 0x18);
    if (!com || SafeIsBadReadPtr((void*)(com + 0x54), 4)) return 0;
    return *(int*)(com + 0x54);
}

inline int CWnd_GetY(uintptr_t cwnd)
{
    if (!cwnd || SafeIsBadReadPtr((void*)(cwnd + 0x18), 4)) return 0;
    uintptr_t com = *(uintptr_t*)(cwnd + 0x18);
    if (!com || SafeIsBadReadPtr((void*)(com + 0x58), 4)) return 0;
    return *(int*)(com + 0x58);
}

inline void CWnd_SetHomePos(uintptr_t cwnd, int x, int y)
{
    if (!cwnd || SafeIsBadReadPtr((void*)(cwnd + 2756), 4)) return;
    *(int*)(cwnd + 2756) = x;
    *(int*)(cwnd + 2760) = y;
}

// 直接写COM surface的屏幕坐标（游戏通常从home自动计算，但自建CWnd可能不走那条路）
inline void CWnd_SetComPos(uintptr_t cwnd, int x, int y)
{
    if (!cwnd || SafeIsBadReadPtr((void*)(cwnd + 0x18), 4)) return;
    uintptr_t com = *(uintptr_t*)(cwnd + 0x18);
    if (!com || SafeIsBadReadPtr((void*)(com + 0x54), 4)) return;
    *(int*)(com + 0x54) = x;
    *(int*)(com + 0x58) = y;
}

// 原生绘制管线使用的渲染坐标字段，证据来自 sub_B9B800 / sub_B9DF60
inline int CWnd_GetRenderX(uintptr_t cwnd)
{
    if (!cwnd || SafeIsBadReadPtr((void*)(cwnd + 0x44), 4)) return 0;
    return *(int*)(cwnd + 0x44);
}

inline int CWnd_GetRenderY(uintptr_t cwnd)
{
    if (!cwnd || SafeIsBadReadPtr((void*)(cwnd + 0x48), 4)) return 0;
    return *(int*)(cwnd + 0x48);
}

inline int CWnd_GetWidth(uintptr_t cwnd)
{
    if (!cwnd || SafeIsBadReadPtr((void*)(cwnd + 0x28), 4)) return 0;
    return *(int*)(cwnd + 0x28);
}

inline int CWnd_GetHeight(uintptr_t cwnd)
{
    if (!cwnd || SafeIsBadReadPtr((void*)(cwnd + 0x2C), 4)) return 0;
    return *(int*)(cwnd + 0x2C);
}

inline void CWnd_SetRenderPos(uintptr_t cwnd, int x, int y)
{
    if (!cwnd || SafeIsBadReadPtr((void*)(cwnd + 0x44), 8)) return;
    *(int*)(cwnd + 0x44) = x;
    *(int*)(cwnd + 0x48) = y;
}

inline int CWnd_GetHomeX(uintptr_t cwnd)
{
    if (!cwnd || SafeIsBadReadPtr((void*)(cwnd + 2756), 4)) return 0;
    return *(int*)(cwnd + 2756);
}

inline int CWnd_GetHomeY(uintptr_t cwnd)
{
    if (!cwnd || SafeIsBadReadPtr((void*)(cwnd + 2760), 4)) return 0;
    return *(int*)(cwnd + 2760);
}

// ============================================================================
// D3D9纹理绘制
// ============================================================================
struct TexturedVertex { float x, y, z, rhw; DWORD color; float u, v; };
#define D3DFVF_TLVERTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

inline void DrawTexturedQuad(IDirect3DDevice9* dev, IDirect3DTexture9* tex,
                             float x, float y, float w, float h, DWORD color = 0xFFFFFFFF)
{
    if (!dev || !tex) return;

    TexturedVertex verts[4] = {
        { x,     y,     0.5f, 1.0f, color, 0.0f, 0.0f },
        { x + w, y,     0.5f, 1.0f, color, 1.0f, 0.0f },
        { x,     y + h, 0.5f, 1.0f, color, 0.0f, 1.0f },
        { x + w, y + h, 0.5f, 1.0f, color, 1.0f, 1.0f },
    };

    IDirect3DStateBlock9* pSB = nullptr;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &pSB))) return;
    pSB->Capture();

    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(nullptr);
    dev->SetTexture(0, tex);
    dev->SetTexture(1, nullptr);
    dev->SetFVF(D3DFVF_TLVERTEX);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0000000F);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(TexturedVertex));

    pSB->Apply();
    pSB->Release();
    dev->SetTexture(0, nullptr);
}
