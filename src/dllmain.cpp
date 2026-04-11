//
// SuperSkillWnd — dllmain.cpp  v14.9
//   按钮 = 原生UI系统 sub_66A770，复刻 BtMacro 创建模式
//   窗口 = ImGui DX9 叠加层（复用 retro-skill-panel-dx9-imgui-main 模块）
//   协议 = 保留现有按钮/toggle/定位逻辑，停用 native child 主路线
//   消息 = Hook SkillWndEx 真实消息分发 sub_9DDB30
//   生命周期 = Hook sub_9E14D0，跟随 SkillWnd 关闭/销毁清状态
//   D3D9 Present Hook 仅用于获取设备+纹理加载
//

#include "core/Common.h"
#include "core/GameAddresses.h"
#include "skill/SkillData.h"
#include "skill/skill_overlay_bridge.h"
#include "hook/InlineHook.h"
#include "hook/win32_input_spoof.h"
#include "resource.h"
#include "ui/super_imgui_overlay.h"
#include "ui/super_imgui_overlay_d3d8.h"
#include "ui/retro_skill_text_dwrite.h"
#include "d3d8/d3d8_renderer.h"
#include <cwchar>
#include <intrin.h>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "util/stb_image.h"

// ============================================================================
// 全局状态
// ============================================================================
static HWND    g_GameHwnd       = nullptr;
static WNDPROC g_OriginalWndProc = nullptr;
static HMODULE g_hModule        = nullptr;
static bool    g_LastOverlaySuppressMouse = false;
static bool    g_MouseSuppressFallbackActive = false;

// SkillWndEx this 指针（sub_9E17D0 中捕获）
static volatile uintptr_t g_SkillWndThis = 0;

// 超级面板状态
static volatile bool g_SuperExpanded = false;
static volatile bool g_Ready         = false;

// D3D 模式检测：运行时判断游戏用 D3D8 还是 D3D9
static bool g_IsD3D8Mode = false;

// D3D9（面板纹理绘制用，后续迁移到原生后可移除）
static IDirect3DDevice9* g_pDevice = nullptr;
static bool g_TexturesLoaded       = false;
static IDirect3DTexture9* g_texPanelBg = nullptr;
static IDirect3DTexture9* g_texBtnNormal = nullptr;
static IDirect3DTexture9* g_texBtnHover = nullptr;
static IDirect3DTexture9* g_texBtnPressed = nullptr;
static IDirect3DTexture9* g_texBtnDisabled = nullptr;
static IDirect3DTexture9* g_texCursorNormal = nullptr;
static IDirect3DTexture9* g_texCursorHoverA = nullptr;
static IDirect3DTexture9* g_texCursorHoverB = nullptr;
static IDirect3DTexture9* g_texCursorPressed = nullptr;
static volatile LONG g_PresentBtnDrawLogBudget = 32;
static volatile LONG g_BtnD3DDrawLogBudget = 40;
static volatile LONG g_SkillBtnD3DDrawLogBudget = 40;
static volatile LONG g_SuperBtnClipLogBudget = 40;
static volatile LONG g_PostUiTimingTestLogBudget = 48;
static volatile LONG g_PresentCursorDrawLogBudget = 24;

// 技能管理器
static SkillManager g_SkillMgr;

// ============================================================================
// 原生按钮对象（由 sub_66A770 创建）
// ============================================================================
static uintptr_t g_SuperBtnObj = 0;   // 按钮COM对象指针（resultBuf[1]）
static uintptr_t g_SuperBtnSkinDonorObj = 0; // 实例级换肤 donor 按钮（离屏保活）
static uintptr_t g_SuperBtnCompareObj = 0;   // A/B验证：保留原版BtMacro实例，供SuperBtn借用draw object
static uintptr_t g_SuperBtnStateDonorObj[5] = {};
static bool g_SuperBtnStateDonorPatched[5] = {};
static bool g_SuperBtnSelfStatePatched[5] = {};
static DWORD g_SuperBtnStateDonorRetryTick[5] = {};
static bool g_SuperBtnForcedStableNormalMode = false;
static void* g_SuperBtnCachedDrawObj = nullptr; // v16.3: 缓存 SuperBtn state=0 的 resolve 结果，避免被游戏引擎资源树重建破坏
static bool g_NativeBtnCreated = false;
static DWORD g_SuperBtnLastDrawTick = 0;  // v17.7: 上次 SkillWnd draw 成功绘制按钮的 tick
static const DWORD SUPERBTN_REDRAW_INTERVAL_MS = 50; // v17.7: 超过此间隔未 draw 则在 metric 中补画

// v17.7b: sub_529640 矩形写入 hook (0x52972E)
// 当 edi 是 donor/compare 按钮时跳过矩形写入，防止 -4096 坐标覆盖 SuperBtn
static const DWORD ADDR_52972E = 0x0052972E; // hook 点：矩形写入前
static const DWORD ADDR_529736 = 0x00529736; // 正常继续点（被覆盖指令之后）
static const DWORD ADDR_52974F = 0x0052974F; // 跳过写入点（函数尾部恢复栈+ret）

// ============================================================================
// 原生子窗口（route-B：外置持有的轻量 native child）
// ============================================================================
static uintptr_t g_SuperCWnd      = 0;   // 子窗口对象指针
static bool g_NativeWndCreated    = false;
static DWORD* g_CustomVTable1     = nullptr;
static bool g_SuperChildHooksReady = false;
static bool g_SuperUsesSkillWndSecondSlot = false;
static const bool ENABLE_IMGUI_OVERLAY_PANEL = true;
static const char* IMGUI_PANEL_ASSET_PATH = "";

// ============================================================================
// 布局常量
// ============================================================================
static const int PANEL_W = 174;
static const int PANEL_H = 299;
static const int PANEL_LEFT_GAP = 0;    // 用户实测：overlay 面板整体再向右 21px
static const int SUPER_CHILD_OFFSET_X = 336;  // 日志已证实当前偏移单次生效；用户实测现状为“左偏约666”，因此改为反向校正
static const int SUPER_CHILD_OFFSET_Y = 77;   // 轻量 child 标题/内容框相对锚点仍偏上，先按实测校正
static const int SUPER_CHILD_VT_DELTA_X = 0;  // v10.1 后实测 finalX ≈ skillVtX - PANEL_W
static const int SUPER_CHILD_VT_DELTA_Y = 2;  // v10.1 后实测 finalY ≈ skillVtY + 2
static const int BTN_X_OFFSET = -216;   // 用户实测：在当前基础上再向左 2px
static const int BTN_Y_OFFSET = -44;    // 用户实测：在当前基础上再向上 3px
static const int BTN_COMPARE_DEBUG_DX = 60;
static const int BTN_METRIC_FALLBACK_X = 10;
static const int BTN_METRIC_FALLBACK_Y = 255;
static const int PREFER_VT_DELTA = 80;
static const int ROUTEB_CHILD_ALLOC_SIZE = 0xAE8;
static const int SUPER_CHILD_DONOR_MODE = 1; // 9DDB30 的 a2 实际是“伪指针型 ctrlID”；3001 -> a2-750 的有效模式是 1..4，不是 2251
static const DWORD SKILLWND_GONE_DEBOUNCE_MS = 800;
static const bool ENABLE_NATIVE_BUTTON_INSTANCE_SKIN = true;
static const bool ENABLE_SUPERBTN_D3D_BUTTON_MODE = true; // v18.0: D3D button with clipped visible rects
static const bool ENABLE_POST_B9F6E0_NATIVE_TIMING_TEST = false; // 关闭 v18.1 的 post-B9F6E0 纯色块实验，回到可见按钮基线
static const bool ENABLE_SUPERBTN_STATE_DRAWOBJ_OVERRIDE = false; // 稳定模式：停用 ExBtMacro 叶子draw object override，避免 hover 命中坏资源链
static const bool ENABLE_SUPERBTN_DRAWOBJ_AB_FALLBACK = false; // dump 已证实 compare 按钮只适合诊断，不再参与正式显示
static const bool ENABLE_SUPERBTN_NATIVE_DONOR_DRAWOBJ = false; // v18.0: D3D-clipped button skips donor state chain
static const bool ENABLE_SUPERBTN_SELF_DRAWOBJ_PATCH = false; // 自 patch 会命中共享 draw object，导致原版宏按钮一起被改
static const bool ENABLE_SUPERBTN_RUNTIME_WRAPPER_PATCH = false; // v17.4b: 停用错误的 +0x3C wrapper patch
static const bool ENABLE_NATIVE_BUTTON_SKIN_REMAP = false; // 先关闭按钮换图实验，恢复渲染稳定性
static const bool ENABLE_DEBUG_VISIBLE_COMPARE_BUTTON = false;
static const bool ENABLE_PRESENT_CLICK_POLL = false; // v7.2: 关闭Present轮询，避免与WndProc双触发
static const bool ENABLE_PRESENT_PANEL_DRAW = false; // v8.0: 正式停用 Present 面板直绘，走 SkillWnd draw hook
static const bool ENABLE_PRESENT_SUPERBTN_DRAW = false; // v17.6: Present 会压鼠标，回退
static const bool ENABLE_TOGGLE_FOCUS_SYNC = false; // v10.3: 先关闭focus伪同步实验，日志已证实它会干扰拖动/层级
static const bool ENABLE_MOVE_FOCUS_SYNC = false;   // v10.3: MoveFocusSync 会在拖动时大量触发，先停掉以消除抽搐
static const bool ENABLE_PRESENT_NATIVE_CHILD_UPDATE = false; // v10.4+: native child 不再每帧在 Present 中搬运，先消除抽搐/视口漂移
static const bool ENABLE_REFRESH_NATIVE_CHILD_UPDATE = false; // v10.6: native child 不再在 refresh hook 中高频搬运，优先消除拖动抽搐
static const char* SAVE_STATE_PATH = "G:\\code\\c++\\SuperSkillWnd\\skill\\save_state.json";
static const char* BUILD_MARKER = "v20.3-2026-04-11-second-child-safety";
static const wchar_t* SUPER_BTN_RES_PATH = L"UI/UIWindow2.img/Skill/main/BtMacro";
static const wchar_t* SUPER_BTN_RES_PATH_ALT = L"/UIWindow2.img/Skill/main/BtMacro";
static int g_PanelDrawX = -9999;
static int g_PanelDrawY = -9999;
static DWORD g_LastToggleTick = 0;
static DWORD g_LastNativeMsgToggleTick = 0;
static DWORD g_LastFallbackHitLogTick = 0;
static DWORD g_LastSkillWndSeenTick = 0;
static DWORD g_LastNativeMsgSkipTick = 0;
static DWORD g_LastFocusSyncTick = 0;

struct CpuButtonBitmap1555
{
    int width = 0;
    int height = 0;
    bool loaded = false;
    std::vector<unsigned short> pixels;
    std::vector<unsigned char> alpha;
};

static CpuButtonBitmap1555 g_SuperBtnCpuNormal;
static CpuButtonBitmap1555 g_SuperBtnCpuHover;
static CpuButtonBitmap1555 g_SuperBtnCpuPressed;
static CpuButtonBitmap1555 g_SuperBtnCpuDisabled;

struct SuperBtnVisiblePiece
{
    RECT screen = {};
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
};

static std::vector<SuperBtnVisiblePiece> g_SuperBtnVisiblePieces;
static DWORD g_SuperBtnD3DVisualState = 0;
static bool g_SuperBtnD3DPressed = false;
static bool g_SuperBtnD3DHover = false;
static DWORD g_SuperBtnD3DHoverStartTick = 0;

static void UpdateSuperCWnd();
static void SetSuperWndVisible(uintptr_t wndObj, int showVal);
static void __fastcall SuperCWndDraw(uintptr_t thisPtr, void* edxUnused, int* clipRegion);
static bool GetSkillWndAnchorPos(uintptr_t skillWndThis, int* outX, int* outY);
static bool GetSkillWndComPos(uintptr_t skillWndThis, int* outX, int* outY);
static void MarkSuperWndDirty(uintptr_t wndObj, const char* logTag);
static void ForwardGameMouseOffscreenNow();
static void UpdateGameMouseSuppressionFallback(bool suppressMouse);
static void RefreshGameCursorImmediately();
static void ClearGameVariant(VARIANTARG* pv);
static void ReleaseUiObj(void* obj);
static bool ResolveNativeImage(const unsigned short* pathWide, void** outImage, const char* logTag);
static bool IsPointInRectPad(int mx, int my, int x, int y, int w, int h, int pad);
static bool GetExpectedButtonRectCom(int* outX, int* outY, int* outW, int* outH);
static bool GetExpectedButtonRectVt(int* outX, int* outY, int* outW, int* outH);
static void LogSuperButtonGeometry(const char* tag);
static void DrawPostB9F6E0NativeTimingTest();
static void DrawSuperButtonCursorInPresent(IDirect3DDevice9* pDevice);
static bool RectHasArea(const RECT& rc)
{
    return rc.right > rc.left && rc.bottom > rc.top;
}

static RECT MakeRectXYWH(int x, int y, int w, int h)
{
    RECT rc = { x, y, x + w, y + h };
    return rc;
}

static bool GetCWndScreenRectByObj(uintptr_t wndObj, RECT* outRect)
{
    if (!wndObj || !outRect)
        return false;

    int w = CWnd_GetWidth(wndObj);
    int h = CWnd_GetHeight(wndObj);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
        return false;

    int x = CWnd_GetRenderX(wndObj);
    int y = CWnd_GetRenderY(wndObj);
    if (x < -10000 || x > 10000 || y < -10000 || y > 10000) {
        x = CWnd_GetX(wndObj);
        y = CWnd_GetY(wndObj);
    }
    if (x < -10000 || x > 10000 || y < -10000 || y > 10000)
        return false;

    *outRect = MakeRectXYWH(x, y, w, h);
    return true;
}

static bool GetSkillWndScreenRectForD3D(RECT* outRect)
{
    if (!outRect || !g_SkillWndThis)
        return false;

    int x = 0;
    int y = 0;
    if (!GetSkillWndAnchorPos(g_SkillWndThis, &x, &y)) {
        if (!GetSkillWndComPos(g_SkillWndThis, &x, &y))
            return false;
    }

    int w = CWnd_GetWidth(g_SkillWndThis);
    int h = CWnd_GetHeight(g_SkillWndThis);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
        return false;

    *outRect = MakeRectXYWH(x, y, w, h);
    return true;
}

static bool GetSuperButtonBaseRectForD3D(RECT* outRect)
{
    if (!outRect)
        return false;

    int x = 0, y = 0, w = 0, h = 0;
    if (!GetExpectedButtonRectVt(&x, &y, &w, &h) &&
        !GetExpectedButtonRectCom(&x, &y, &w, &h)) {
        return false;
    }

    *outRect = MakeRectXYWH(x, y, w, h);
    return true;
}

static void DrawPostB9F6E0NativeTimingTest()
{
    if (!ENABLE_POST_B9F6E0_NATIVE_TIMING_TEST || !g_pDevice)
        return;

    RECT rc = {};
    if (!GetSuperButtonBaseRectForD3D(&rc)) {
        if (GetSkillWndScreenRectForD3D(&rc)) {
            rc.left += 10;
            rc.top += 10;
            rc.right = rc.left + 220;
            rc.bottom = rc.top + 72;
        } else {
            rc = MakeRectXYWH(200, 200, 220, 72);
        }
    } else {
        rc.left -= 16;
        rc.top -= 16;
        rc.right += 150;
        rc.bottom += 32;
    }

    const DWORD color = (GetTickCount() / 250) & 1 ? 0xE0FF2060 : 0xE020D0FF;
    bool ok = false;
    __try {
        DrawSolidQuad(
            g_pDevice,
            (float)rc.left,
            (float)rc.top,
            (float)(rc.right - rc.left),
            (float)(rc.bottom - rc.top),
            color);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }

    LONG after = InterlockedDecrement(&g_PostUiTimingTestLogBudget);
    if (after >= 0) {
        WriteLogFmt("[PostUiTimingTest] ok=%d rect=(%ld,%ld,%ld,%ld) color=0x%08X btn=0x%08X dev=0x%08X",
            ok ? 1 : 0,
            rc.left, rc.top, rc.right, rc.bottom,
            color,
            (DWORD)g_SuperBtnObj,
            (DWORD)(uintptr_t)g_pDevice);
    }
}

static int GetCWndZOrderValue(uintptr_t wndObj)
{
    if (!wndObj || SafeIsBadReadPtr((void*)(wndObj + CWND_OFF_ZORDER * 4), 4))
        return 0;
    return *(int*)(wndObj + CWND_OFF_ZORDER * 4);
}

static bool GetCWndManTopLevelVector(uintptr_t* outVec, int* outCount, int maxCount)
{
    if (!outVec || !outCount || maxCount <= 0)
        return false;
    *outVec = 0;
    *outCount = 0;

    if (SafeIsBadReadPtr((void*)ADDR_CWndMan, 4))
        return false;
    uintptr_t wndMan = *(uintptr_t*)ADDR_CWndMan;
    if (!wndMan || SafeIsBadReadPtr((void*)(wndMan + CWNDMAN_TOPLEVEL_OFF), 4))
        return false;

    // CWndMan+0x4A74 is a vector data pointer, not an inline array.
    uintptr_t vec = *(uintptr_t*)(wndMan + CWNDMAN_TOPLEVEL_OFF);
    if (!vec || vec < 4 || SafeIsBadReadPtr((void*)(vec - 4), 4))
        return false;

    int count = *(int*)(vec - 4);
    if (count <= 0 || count > 4096)
        return false;
    if (count > maxCount)
        count = maxCount;

    *outVec = vec;
    *outCount = count;
    return true;
}

static bool IsIgnoredSuperBtnOccluder(uintptr_t wndObj)
{
    if (!wndObj)
        return true;
    if (wndObj == g_SkillWndThis || wndObj == g_SuperBtnObj || wndObj == g_SuperCWnd ||
        wndObj == g_SuperBtnSkinDonorObj || wndObj == g_SuperBtnCompareObj)
        return true;
    for (int i = 0; i < 5; ++i) {
        if (wndObj == g_SuperBtnStateDonorObj[i])
            return true;
    }
    return false;
}

static void AppendRectIfValid(std::vector<RECT>& rects, const RECT& rc)
{
    if (RectHasArea(rc))
        rects.push_back(rc);
}

static void SubtractRectFromPiece(const RECT& src, const RECT& cut, std::vector<RECT>& out)
{
    RECT inter = {};
    if (!IntersectRect(&inter, &src, &cut)) {
        out.push_back(src);
        return;
    }

    RECT top = { src.left, src.top, src.right, inter.top };
    RECT bottom = { src.left, inter.bottom, src.right, src.bottom };
    RECT left = { src.left, inter.top, inter.left, inter.bottom };
    RECT right = { inter.right, inter.top, src.right, inter.bottom };

    AppendRectIfValid(out, top);
    AppendRectIfValid(out, bottom);
    AppendRectIfValid(out, left);
    AppendRectIfValid(out, right);
}

static void CollectSuperBtnOccluderRects(const RECT& baseRect, std::vector<RECT>& outRects)
{
    uintptr_t topVec = 0;
    int topCount = 0;
    if (!GetCWndManTopLevelVector(&topVec, &topCount, 512))
        return;

    for (int i = 0; i < topCount; ++i) {
        uintptr_t slotAddr = topVec + i * 4;
        if (SafeIsBadReadPtr((void*)slotAddr, 4))
            break;
        uintptr_t wndObj = *(DWORD*)slotAddr;
        if (IsIgnoredSuperBtnOccluder(wndObj))
            continue;

        RECT wndRect = {};
        RECT inter = {};
        if (!GetCWndScreenRectByObj(wndObj, &wndRect))
            continue;
        if (!IntersectRect(&inter, &baseRect, &wndRect))
            continue;

        bool duplicate = false;
        for (size_t k = 0; k < outRects.size(); ++k) {
            if (EqualRect(&outRects[k], &wndRect)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            outRects.push_back(wndRect);
    }

}

static void ResetSuperBtnD3DInteractionState()
{
    g_SuperBtnD3DVisualState = 0;
    g_SuperBtnD3DPressed = false;
    g_SuperBtnD3DHover = false;
    g_SuperBtnD3DHoverStartTick = 0;
    g_SuperBtnVisiblePieces.clear();
}

static bool UpdateSuperBtnVisiblePieces(const char* reason)
{
    g_SuperBtnVisiblePieces.clear();
    if (!ENABLE_SUPERBTN_D3D_BUTTON_MODE || !g_Ready || !g_NativeBtnCreated || !g_SkillWndThis)
        return false;

    RECT baseRect = {};
    if (!GetSuperButtonBaseRectForD3D(&baseRect))
        return false;

    std::vector<RECT> pieces;
    RECT skillRect = {};
    RECT clippedBase = baseRect;
    if (GetSkillWndScreenRectForD3D(&skillRect)) {
        if (!IntersectRect(&clippedBase, &baseRect, &skillRect))
            return false;
    }
    pieces.push_back(clippedBase);

    std::vector<RECT> occluders;
    CollectSuperBtnOccluderRects(baseRect, occluders);
    for (size_t i = 0; i < occluders.size() && !pieces.empty(); ++i) {
        std::vector<RECT> nextPieces;
        nextPieces.reserve(pieces.size() * 2 + 4);
        for (size_t j = 0; j < pieces.size(); ++j)
            SubtractRectFromPiece(pieces[j], occluders[i], nextPieces);
        pieces.swap(nextPieces);
        if (pieces.size() > 32)
            pieces.resize(32);
    }

    const float baseW = (float)(baseRect.right - baseRect.left);
    const float baseH = (float)(baseRect.bottom - baseRect.top);
    if (baseW <= 0.0f || baseH <= 0.0f)
        return false;

    for (size_t i = 0; i < pieces.size(); ++i) {
        const RECT& rc = pieces[i];
        if (!RectHasArea(rc))
            continue;
        SuperBtnVisiblePiece piece = {};
        piece.screen = rc;
        piece.u0 = (float)(rc.left - baseRect.left) / baseW;
        piece.v0 = (float)(rc.top - baseRect.top) / baseH;
        piece.u1 = (float)(rc.right - baseRect.left) / baseW;
        piece.v1 = (float)(rc.bottom - baseRect.top) / baseH;
        g_SuperBtnVisiblePieces.push_back(piece);
    }

    LONG after = InterlockedDecrement(&g_SuperBtnClipLogBudget);
    if (after >= 0) {
        WriteLogFmt("[BtnClip] reason=%s base=(%ld,%ld,%ld,%ld) skill=%s(%ld,%ld,%ld,%ld) occluders=%d pieces=%d state=%u",
            reason ? reason : "-",
            baseRect.left, baseRect.top, baseRect.right, baseRect.bottom,
            RectHasArea(skillRect) ? "Y" : "N",
            skillRect.left, skillRect.top, skillRect.right, skillRect.bottom,
            (int)occluders.size(),
            (int)g_SuperBtnVisiblePieces.size(),
            (unsigned)g_SuperBtnD3DVisualState);
    }

    return !g_SuperBtnVisiblePieces.empty();
}

static bool IsPointInVisibleSuperBtnPieces(int mx, int my)
{
    if (!UpdateSuperBtnVisiblePieces("hit"))
        return false;

    for (size_t i = 0; i < g_SuperBtnVisiblePieces.size(); ++i) {
        const RECT& rc = g_SuperBtnVisiblePieces[i].screen;
        if (mx >= rc.left && mx < rc.right && my >= rc.top && my < rc.bottom)
            return true;
    }
    return false;
}

static void ToggleSuperWnd(const char* srcTag = "unknown");

static bool HandleSuperBtnD3DWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    UNREFERENCED_PARAMETER(h);
    UNREFERENCED_PARAMETER(w);

    if (ENABLE_POST_B9F6E0_NATIVE_TIMING_TEST)
        return false;

    if (!ENABLE_SUPERBTN_D3D_BUTTON_MODE || !g_Ready || !g_NativeBtnCreated || !g_SkillWndThis)
        return false;

    switch (m) {
    case WM_CAPTURECHANGED:
    case WM_KILLFOCUS:
        ResetSuperBtnD3DInteractionState();
        return false;
    case WM_ACTIVATEAPP:
        if (!w)
            ResetSuperBtnD3DInteractionState();
        return false;
    default:
        break;
    }

    if (m != WM_MOUSEMOVE && m != WM_LBUTTONDOWN && m != WM_LBUTTONUP)
        return false;

    const int mx = (short)LOWORD(l);
    const int my = (short)HIWORD(l);
    const bool hit = IsPointInVisibleSuperBtnPieces(mx, my);

    if (m == WM_MOUSEMOVE) {
        const bool wasHover = g_SuperBtnD3DHover;
        g_SuperBtnD3DHover = hit;
        if (g_SuperBtnD3DHover && !wasHover)
            g_SuperBtnD3DHoverStartTick = GetTickCount();
        else if (!g_SuperBtnD3DHover)
            g_SuperBtnD3DHoverStartTick = 0;
        g_SuperBtnD3DVisualState = g_SuperBtnD3DPressed ? 1u : (hit ? 3u : 0u);
        if ((hit || g_SuperBtnD3DPressed) && !Win32InputSpoofIsInstalled())
            ForwardGameMouseOffscreenNow();
        return hit || g_SuperBtnD3DPressed;
    }

    if (m == WM_LBUTTONDOWN) {
        if (!hit)
            return false;
        g_SuperBtnD3DPressed = true;
        g_SuperBtnD3DHover = true;
        if (!g_SuperBtnD3DHoverStartTick)
            g_SuperBtnD3DHoverStartTick = GetTickCount();
        g_SuperBtnD3DVisualState = 1;
        return true;
    }

    if (m == WM_LBUTTONUP) {
        if (!g_SuperBtnD3DPressed)
            return false;
        g_SuperBtnD3DPressed = false;
        g_SuperBtnD3DHover = hit;
        if (g_SuperBtnD3DHover && !g_SuperBtnD3DHoverStartTick)
            g_SuperBtnD3DHoverStartTick = GetTickCount();
        else if (!g_SuperBtnD3DHover)
            g_SuperBtnD3DHoverStartTick = 0;
        g_SuperBtnD3DVisualState = hit ? 3u : 0u;
        if (hit) {
            g_LastNativeMsgToggleTick = GetTickCount();
            ToggleSuperWnd("d3d_btn");
        }
        return true;
    }

    return false;
}

static bool ShouldSuppressGameMouseForSuperBtnD3D()
{
    if (!ENABLE_SUPERBTN_D3D_BUTTON_MODE || !g_Ready || !g_NativeBtnCreated || !g_SkillWndThis)
        return false;
    return g_SuperBtnD3DPressed || g_SuperBtnD3DHover;
}

static void LogNativeButtonCoreFields(uintptr_t btnObj, const char* tag);
static int __fastcall hkButtonDrawCurrentState(uintptr_t thisPtr, void* edxUnused, int x, int y, int a4);
static DWORD GetCurrentSuperBtnState();
static bool EnsureSuperBtnCpuBitmapLoaded(DWORD state);
static const CpuButtonBitmap1555* GetSuperBtnCpuBitmapForState(DWORD state);
static bool PatchSuperBtnDonorDrawObjectsFromResources();
static bool ForceSuperButtonAllStatesToNormalDonor(uintptr_t btnObj);
static bool PatchSuperBtnOwnDrawObjectsFromResources(uintptr_t btnObj, const char* reason = nullptr);
 void ResetSuperBtnD3DInteractionState();
static bool UpdateSuperBtnVisiblePieces(const char* reason = nullptr);
static bool IsPointInVisibleSuperBtnPieces(int mx, int my);
static bool HandleSuperBtnD3DWndProc(HWND h, UINT m, WPARAM w, LPARAM l);
static bool ShouldSuppressGameMouseForSuperBtnD3D();
static void ToggleSuperWnd(const char* srcTag);

// SkillWnd坐标：优先走 this+4 的vtable(0x30/0x34)，与原sub_9E17D0一致
static bool GetSkillWndAnchorPos(uintptr_t skillWndThis, int* outX, int* outY)
{
    if (!skillWndThis || !outX || !outY) return false;

    uintptr_t thisForVT = skillWndThis + 4;
    if (SafeIsBadReadPtr((void*)thisForVT, 4)) return false;

    DWORD vt = *(DWORD*)thisForVT;
    if (!vt) return false;
    if (SafeIsBadReadPtr((void*)(vt + 0x30), 8)) return false;

    DWORD fnGetX = *(DWORD*)(vt + 0x30);
    DWORD fnGetY = *(DWORD*)(vt + 0x34);
    if (!fnGetX || !fnGetY) return false;

    int x = 0, y = 0;
    DWORD ecxVal = (DWORD)thisForVT;

    __try {
        __asm {
            mov ecx, [ecxVal]
            call [fnGetX]
            mov [x], eax
        }
        __asm {
            mov ecx, [ecxVal]
            call [fnGetY]
            mov [y], eax
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    if (x < -10000 || x > 10000 || y < -10000 || y > 10000) return false;

    *outX = x;
    *outY = y;
    return true;
}

static bool GetUiObjPosByVtablePlus4(uintptr_t objThis, int* outX, int* outY)
{
    if (!objThis || !outX || !outY) return false;

    uintptr_t thisForVT = objThis + 4;
    if (SafeIsBadReadPtr((void*)thisForVT, 4)) return false;

    DWORD vt = *(DWORD*)thisForVT;
    if (!vt) return false;
    if (SafeIsBadReadPtr((void*)(vt + 0x30), 8)) return false;

    DWORD fnGetX = *(DWORD*)(vt + 0x30);
    DWORD fnGetY = *(DWORD*)(vt + 0x34);
    if (!fnGetX || !fnGetY) return false;

    int x = 0, y = 0;
    DWORD ecxVal = (DWORD)thisForVT;
    __try {
        __asm {
            mov ecx, [ecxVal]
            call [fnGetX]
            mov [x], eax
        }
        __asm {
            mov ecx, [ecxVal]
            call [fnGetY]
            mov [y], eax
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    if (x < -10000 || x > 10000 || y < -10000 || y > 10000) return false;
    *outX = x;
    *outY = y;
    return true;
}

static bool GetSkillWndComPos(uintptr_t skillWndThis, int* outX, int* outY)
{
    if (!skillWndThis || !outX || !outY) return false;
    int x = CWnd_GetX(skillWndThis);
    int y = CWnd_GetY(skillWndThis);
    if (x < -10000 || x > 10000 || y < -10000 || y > 10000) return false;
    *outX = x;
    *outY = y;
    return true;
}

static bool GetButtonScreenRectByObj(uintptr_t btnObj, int* outX, int* outY, int* outW, int* outH)
{
    if (!btnObj || !outX || !outY || !outW || !outH)
        return false;

    int x = 0;
    int y = 0;
    if (!GetUiObjPosByVtablePlus4(btnObj, &x, &y)) {
        x = CWnd_GetX(btnObj);
        y = CWnd_GetY(btnObj);
    }
    if (x < -9000 || y < -9000 || x > 10000 || y > 10000)
        return false;

    int w = 50;
    int h = 16;
    __try {
        if (!SafeIsBadReadPtr((void*)(btnObj + 0x1C), 8)) {
            int tw = *(int*)(btnObj + 0x1C);
            int th = *(int*)(btnObj + 0x20);
            if (tw >= 24 && tw <= 256) w = tw;
            if (th >= 12 && th <= 64) h = th;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    *outX = x;
    *outY = y;
    *outW = w;
    *outH = h;
    return true;
}

static void RefreshGameCursorImmediately()
{
    if (!g_GameHwnd || !g_OriginalWndProc)
        return;

    POINT pt = {};
    if (!::GetCursorPos(&pt))
        return;
    if (!::ScreenToClient(g_GameHwnd, &pt))
        return;

    const LPARAM mouseLp = MAKELPARAM((short)pt.x, (short)pt.y);
    CallWindowProc(g_OriginalWndProc, g_GameHwnd, WM_MOUSEMOVE, 0, mouseLp);
    CallWindowProc(g_OriginalWndProc, g_GameHwnd, WM_SETCURSOR, (WPARAM)g_GameHwnd, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
}

static void ForwardGameMouseOffscreenNow()
{
    if (!g_GameHwnd || !g_OriginalWndProc)
        return;

    CallWindowProc(g_OriginalWndProc, g_GameHwnd, WM_MOUSEMOVE, 0, Win32InputSpoofMakeOffscreenMouseLParam());
    CallWindowProc(g_OriginalWndProc, g_GameHwnd, WM_SETCURSOR, (WPARAM)g_GameHwnd, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
}

static void UpdateGameMouseSuppressionFallback(bool suppressMouse)
{
    if (Win32InputSpoofIsInstalled())
    {
        g_MouseSuppressFallbackActive = false;
        return;
    }

    if (suppressMouse)
    {
        ForwardGameMouseOffscreenNow();
        SetCursor(nullptr);
        if (!g_MouseSuppressFallbackActive)
            WriteLog("[MouseSuppressFallback] ON");
        g_MouseSuppressFallbackActive = true;
        return;
    }

    if (g_MouseSuppressFallbackActive)
    {
        WriteLog("[MouseSuppressFallback] OFF");
        g_MouseSuppressFallbackActive = false;
        RefreshGameCursorImmediately();
    }
}

typedef int (__cdecl *tCloneVariant)(VARIANTARG* pvarg, VARIANTARG* pvargSrc);
typedef int (__thiscall *tQueryDrawIface)(void** outIface, void* obj);
typedef int (__thiscall *tAssignUiSlot)(DWORD* slot, void* obj);
typedef void* (__thiscall *tRefreshButtonState)(DWORD* thisPtr, int** stateIndexOrNull);
typedef LONG (__thiscall *tMoveNativeButton)(DWORD* thisPtr, int x, int y);
typedef DWORD* (__thiscall *tGetSurface)(uintptr_t thisPtr, DWORD** outSurface);
typedef int (__thiscall *tSurfaceDrawImage)(DWORD* surface, int x, int y, int imageObj, DWORD* alphaVar);
typedef LONG (__thiscall *tMoveNativeWnd)(uintptr_t thisPtr, int x, int y);
typedef int (__thiscall *tResizeNativeWnd)(uintptr_t thisPtr, int x, int y, int width, int height, int a6, int a7, int a8);
typedef unsigned int** (__thiscall *tMakeGameWString)(unsigned int** outStr, const unsigned short* wideText);
typedef void** (__thiscall *tButtonResolveCurrentDrawObj)(uintptr_t thisPtr, void** outObj);
typedef int (__thiscall *tButtonDrawCurrentState)(uintptr_t thisPtr, int x, int y, int a4);
typedef int (__thiscall *tButtonMetricCurrent)(uintptr_t thisPtr);
static tMakeGameWString oMakeGameWStringHooked = nullptr;
static tButtonResolveCurrentDrawObj oButtonResolveCurrentDrawObj = nullptr;
static tButtonDrawCurrentState oButtonDrawCurrentState = nullptr;
static tButtonMetricCurrent oButtonMetric507DF0 = nullptr;
static tButtonMetricCurrent oButtonMetric507ED0 = nullptr;
static tRefreshButtonState oButtonRefreshState5095A0 = nullptr;
static void* oPostB9F6E0DrawContinue = nullptr;
static volatile LONG g_SuperBtnCreateTraceScope = 0;
static volatile LONG g_SuperBtnCreateTraceBudget = 0;
static DWORD g_SuperBtnCreateTraceUntilTick = 0;
static volatile LONG g_SuperBtnCreateRemapSeen = 0;
static volatile LONG g_BtnResolveLogBudget = 64;
static volatile LONG g_BtnDrawLogBudget = 500;
static volatile LONG g_BtnMetricLogBudget = 64;
static LONG g_SuperBtnMetricOverrideX[5] = { LONG_MIN, LONG_MIN, LONG_MIN, LONG_MIN, LONG_MIN };
static LONG g_SuperBtnMetricOverrideY[5] = { LONG_MIN, LONG_MIN, LONG_MIN, LONG_MIN, LONG_MIN };

static bool IsReasonableButtonMetric(int value)
{
    return value > -512 && value < 4096;
}

static bool TryGetSuperBtnMetricOverrideValue(DWORD state, bool isY, int* outValue)
{
    if (outValue) *outValue = 0;
    if (state >= 5)
        return false;

    LONG* table = isY ? g_SuperBtnMetricOverrideY : g_SuperBtnMetricOverrideX;
    LONG cached = InterlockedCompareExchange(&table[state], LONG_MIN, LONG_MIN);
    if (cached != LONG_MIN && IsReasonableButtonMetric((int)cached)) {
        if (outValue) *outValue = (int)cached;
        return true;
    }

    LONG fallback = InterlockedCompareExchange(&table[0], LONG_MIN, LONG_MIN);
    if (fallback != LONG_MIN && IsReasonableButtonMetric((int)fallback)) {
        if (outValue) *outValue = (int)fallback;
        return true;
    }

    return false;
}

static void SeedSuperBtnMetricOverridesIfEmpty(int x, int y, const char* logTag)
{
    for (int i = 0; i < 5; ++i) {
        if (InterlockedCompareExchange(&g_SuperBtnMetricOverrideX[i], LONG_MIN, LONG_MIN) == LONG_MIN) {
            InterlockedExchange(&g_SuperBtnMetricOverrideX[i], x);
        }
        if (InterlockedCompareExchange(&g_SuperBtnMetricOverrideY[i], LONG_MIN, LONG_MIN) == LONG_MIN) {
            InterlockedExchange(&g_SuperBtnMetricOverrideY[i], y);
        }
    }

    WriteLogFmt("[%s] seed metric fallback=(%d,%d)",
        logTag ? logTag : "BtnMetricSeed",
        x, y);
}

static bool WideContainsNoExcept(const wchar_t* text, const wchar_t* token)
{
    if (!text || !token || !*token) return false;
    __try {
        return wcsstr(text, token) != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool WideEqualsNoExcept(const wchar_t* text, const wchar_t* token)
{
    if (!text || !token) return false;
    __try {
        return wcscmp(text, token) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool IsButtonStateToken(const wchar_t* text)
{
    if (!text) return false;
    return WideEqualsNoExcept(text, L"normal") ||
           WideEqualsNoExcept(text, L"mouseOver") ||
           WideEqualsNoExcept(text, L"pressed") ||
           WideEqualsNoExcept(text, L"disabled") ||
           WideEqualsNoExcept(text, L"checked");
}

static bool IsSuperBtnTraceWindowActive()
{
    if (InterlockedCompareExchange(&g_SuperBtnCreateTraceScope, 0, 0) > 0)
        return true;
    DWORD until = g_SuperBtnCreateTraceUntilTick;
    return until != 0 && GetTickCount() <= until;
}

static void SafePreviewWideText(const unsigned short* text, wchar_t* outBuf, size_t outCap)
{
    if (!outBuf || outCap == 0)
        return;
    outBuf[0] = 0;
    if (!text)
        return;

    __try {
        size_t pos = 0;
        bool sawPrintable = false;
        for (; pos + 1 < outCap; ++pos) {
            unsigned short ch = text[pos];
            if (ch == 0)
                break;
            if (ch < 0x20 && ch != L'/' && ch != L'\\' && ch != L'.' && ch != L'_' && ch != L'-')
                break;
            if ((ch >= 0x20 && ch <= 0x7E) || ch >= 0x80) {
                outBuf[pos] = (wchar_t)ch;
                sawPrintable = true;
            } else {
                break;
            }
        }
        outBuf[pos] = 0;
        if (sawPrintable && pos > 0)
            return;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    __try {
        _snwprintf_s(outBuf, outCap, _TRUNCATE,
            L"<hex:%04X,%04X,%04X,%04X>",
            text[0], text[1], text[2], text[3]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        _snwprintf_s(outBuf, outCap, _TRUNCATE, L"<badptr:%08X>", (DWORD)(uintptr_t)text);
    }
}

static const unsigned short* ReplaceWidePathNeedle(
    const wchar_t* src,
    const wchar_t* needle,
    const wchar_t* repl)
{
    if (!src || !needle || !repl || !*needle)
        return nullptr;

    const wchar_t* hit = wcsstr(src, needle);
    if (!hit)
        return nullptr;

    static wchar_t s_replaceBuf[260];
    const size_t prefixLen = (size_t)(hit - src);
    const size_t needleLen = wcslen(needle);
    const size_t replLen = wcslen(repl);
    const wchar_t* suffix = hit + needleLen;
    const size_t suffixLen = wcslen(suffix);
    if (prefixLen + replLen + suffixLen + 1 >= _countof(s_replaceBuf))
        return nullptr;

    if (prefixLen > 0)
        wmemcpy(s_replaceBuf, src, prefixLen);
    wmemcpy(s_replaceBuf + prefixLen, repl, replLen);
    if (suffixLen > 0)
        wmemcpy(s_replaceBuf + prefixLen + replLen, suffix, suffixLen);
    s_replaceBuf[prefixLen + replLen + suffixLen] = 0;
    return reinterpret_cast<const unsigned short*>(s_replaceBuf);
}

static const unsigned short* RemapSuperButtonStatePathIfNeeded(const unsigned short* wideText)
{
    if (!wideText)
        return nullptr;

    if (InterlockedCompareExchange(&g_SuperBtnCreateTraceScope, 0, 0) <= 0)
        return nullptr;

    const wchar_t* text = reinterpret_cast<const wchar_t*>(wideText);
    if (WideEqualsNoExcept(text, L"UI/UIWindow2.img/SkillEx/main/BtMacro")) {
        InterlockedExchange(&g_SuperBtnCreateRemapSeen, 1);
        return reinterpret_cast<const unsigned short*>(SUPER_BTN_RES_PATH);
    }
    if (WideEqualsNoExcept(text, L"/UI/UIWindow2.img/SkillEx/main/BtMacro")) {
        InterlockedExchange(&g_SuperBtnCreateRemapSeen, 1);
        return reinterpret_cast<const unsigned short*>(SUPER_BTN_RES_PATH_ALT);
    }
    if (WideEqualsNoExcept(text, L"checked")) {
        InterlockedExchange(&g_SuperBtnCreateRemapSeen, 1);
        return reinterpret_cast<const unsigned short*>(L"pressed");
    }

    return nullptr;
}

static unsigned int** __fastcall hkMakeGameWString(unsigned int** outStrLikeThis, void* /*edxUnused*/, const unsigned short* wideText)
{
    const unsigned short* resolvedPath = wideText;
    const unsigned short* remappedPath = RemapSuperButtonStatePathIfNeeded(wideText);
    if (remappedPath)
        resolvedPath = remappedPath;

    if (remappedPath && wideText) {
        WriteLogFmt("[BtnSkinRemap] 402F60 src=%S dst=%S",
            reinterpret_cast<const wchar_t*>(wideText),
            reinterpret_cast<const wchar_t*>(resolvedPath));
    } else if (IsSuperBtnTraceWindowActive() &&
               InterlockedCompareExchange(&g_SuperBtnCreateTraceBudget, 0, 0) > 0 &&
               wideText) {
        LONG after = InterlockedDecrement(&g_SuperBtnCreateTraceBudget);
        if (after >= 0) {
            wchar_t preview[160];
            SafePreviewWideText(wideText, preview, _countof(preview));
            WriteLogFmt("[BtnTrace402F60] caller=0x%08X ptr=0x%08X text=%S",
                (DWORD)(uintptr_t)_ReturnAddress(),
                (DWORD)(uintptr_t)wideText,
                preview);
        }
    }

    if (!oMakeGameWStringHooked)
        return outStrLikeThis;
    return oMakeGameWStringHooked(outStrLikeThis, resolvedPath);
}

static const char* GetTrackedButtonTag(uintptr_t btnObj)
{
    if (!btnObj)
        return nullptr;
    if (btnObj == g_SuperBtnObj)
        return "SuperBtn";
    if (btnObj == g_SuperBtnSkinDonorObj)
        return "BtMacroCmp";
    for (int i = 0; i < 5; ++i) {
        if (btnObj == g_SuperBtnStateDonorObj[i])
            return "BtMacroCmp";
    }
    return nullptr;
}

static const unsigned short* GetSuperBtnStateLeafPath(DWORD state)
{
    switch (state) {
    case 0: return reinterpret_cast<const unsigned short*>(L"UI/UIWindow2.img/Skill/main/ExBtMacro/normal/0");
    case 1: return reinterpret_cast<const unsigned short*>(L"UI/UIWindow2.img/Skill/main/ExBtMacro/pressed/0");
    case 2: return reinterpret_cast<const unsigned short*>(L"UI/UIWindow2.img/Skill/main/ExBtMacro/disabled/0");
    case 3: return reinterpret_cast<const unsigned short*>(L"UI/UIWindow2.img/Skill/main/ExBtMacro/mouseOver/0");
    case 4: return reinterpret_cast<const unsigned short*>(L"UI/UIWindow2.img/Skill/main/ExBtMacro/checked/0");
    default: return nullptr;
    }
}

static DWORD NormalizeSuperBtnStateIndex(DWORD state)
{
    return (state <= 4) ? state : 0;
}

static DWORD NormalizeSuperBtnVisualState(DWORD state)
{
    state = NormalizeSuperBtnStateIndex(state);
    // v17.5: hover(3) -> normal(0)，因为 state=3 的 donor draw object 结构
    // 与 0/1/2/4 不同（entryList 里 scanned=0），patch 始终失败。
    // 先保证按钮稳定不消失，后续再尝试修复 hover donor 结构匹配。
    if (state == 3)
        return 0;
    // checked(4) -> pressed(1)
    if (state == 4)
        return 1;
    return state;
}

static DWORD GetDonorResolveState(DWORD donorStateIndex)
{
    return (donorStateIndex == 4) ? 1 : donorStateIndex;
}

static bool EnsureSuperBtnDonorStatePatched(DWORD superState, const char* reason = nullptr)
{
    if (g_SuperBtnForcedStableNormalMode)
        return true;

    DWORD idx = NormalizeSuperBtnVisualState(superState);
    if (idx > 4)
        return false;
    if (g_SuperBtnStateDonorPatched[idx])
        return true;
    if (!ENABLE_SUPERBTN_NATIVE_DONOR_DRAWOBJ)
        return false;
    if (!g_SuperBtnStateDonorObj[idx])
        return false;

    DWORD now = GetTickCount();
    if (g_SuperBtnStateDonorRetryTick[idx] && (now - g_SuperBtnStateDonorRetryTick[idx]) < 150)
        return false;
    g_SuperBtnStateDonorRetryTick[idx] = now;

    bool ok = PatchSuperBtnDonorDrawObjectsFromResources();
    WriteLogFmt("[BtnDonorRetry] state=%u reason=%s ok=%d patched=%d donor=0x%08X",
        idx,
        reason ? reason : "-",
        ok ? 1 : 0,
        g_SuperBtnStateDonorPatched[idx] ? 1 : 0,
        (DWORD)g_SuperBtnStateDonorObj[idx]);
    return g_SuperBtnStateDonorPatched[idx];
}

static uintptr_t SelectSuperBtnDonorForState(DWORD superState, DWORD* outDonorStateIndex = nullptr)
{
    DWORD idx = NormalizeSuperBtnVisualState(superState);

    auto donorReady = [&](DWORD s) -> bool {
        return s <= 4 && g_SuperBtnStateDonorObj[s] && g_SuperBtnStateDonorPatched[s];
    };

    if (!donorReady(idx)) {
        EnsureSuperBtnDonorStatePatched(idx, "select");
    }

    if (!donorReady(idx)) {
        if (idx == 3 && donorReady(0)) {
            idx = 0;
        } else if (idx == 4 && donorReady(1)) {
            idx = 1;
        } else if (donorReady(0)) {
            idx = 0;
        } else if (donorReady(1)) {
            idx = 1;
        } else {
            for (DWORD i = 0; i < 5; ++i) {
                if (donorReady(i)) {
                    idx = i;
                    break;
                }
            }
        }
    }

    if (outDonorStateIndex)
        *outDonorStateIndex = idx;

    uintptr_t donor = (idx <= 4) ? g_SuperBtnStateDonorObj[idx] : 0;
    if (!donor)
        donor = g_SuperBtnSkinDonorObj;
    return donor;
}

static bool NormalizeSuperBtnLeafDrawObjFromVisibleModel(void* replacementObj, DWORD state, DWORD keySource)
{
    if (!replacementObj || !g_SuperBtnCompareObj || !oButtonResolveCurrentDrawObj)
        return false;

    void* modelObj = nullptr;
    bool ok = false;
    __try {
        if (!SafeIsBadReadPtr((void*)(g_SuperBtnCompareObj + 0x34), 4))
            *(DWORD*)(g_SuperBtnCompareObj + 0x34) = state;
        if (!SafeIsBadReadPtr((void*)(g_SuperBtnCompareObj + 0x38), 4))
            *(DWORD*)(g_SuperBtnCompareObj + 0x38) = keySource;

        oButtonResolveCurrentDrawObj(g_SuperBtnCompareObj, &modelObj);
        if (modelObj &&
            !SafeIsBadReadPtr(modelObj, 0x50) &&
            !SafeIsBadReadPtr(replacementObj, 0x50)) {
            // dump 对比里最稳定的差异就是这几项 wrapper 字段：
            // +0x08 / +0x1C / +0x40。先借原版可见模型，把 Ex 叶子对象补成同类外壳。
            *(DWORD*)((uintptr_t)replacementObj + 0x08) = *(DWORD*)((uintptr_t)modelObj + 0x08);
            *(DWORD*)((uintptr_t)replacementObj + 0x1C) = *(DWORD*)((uintptr_t)modelObj + 0x1C);
            *(DWORD*)((uintptr_t)replacementObj + 0x40) = *(DWORD*)((uintptr_t)modelObj + 0x40);
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }

    static LONG s_leafNormalizeLogBudget = 24;
    LONG after = InterlockedDecrement(&s_leafNormalizeLogBudget);
    if (after >= 0) {
        DWORD repl08 = 0, repl1C = 0, repl40 = 0, model08 = 0, model1C = 0, model40 = 0;
        __try {
            if (!SafeIsBadReadPtr(replacementObj, 0x44)) {
                repl08 = *(DWORD*)((uintptr_t)replacementObj + 0x08);
                repl1C = *(DWORD*)((uintptr_t)replacementObj + 0x1C);
                repl40 = *(DWORD*)((uintptr_t)replacementObj + 0x40);
            }
            if (modelObj && !SafeIsBadReadPtr(modelObj, 0x44)) {
                model08 = *(DWORD*)((uintptr_t)modelObj + 0x08);
                model1C = *(DWORD*)((uintptr_t)modelObj + 0x1C);
                model40 = *(DWORD*)((uintptr_t)modelObj + 0x40);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        WriteLogFmt("[BtnLeafNormalize] state=%u ok=%d repl=0x%08X model=0x%08X repl[08/1C/40]=[%08X,%08X,%08X] model[08/1C/40]=[%08X,%08X,%08X]",
            state,
            ok ? 1 : 0,
            (DWORD)(uintptr_t)replacementObj,
            (DWORD)(uintptr_t)modelObj,
            repl08, repl1C, repl40,
            model08, model1C, model40);
    }

    ReleaseUiObj(modelObj);
    return ok;
}

static void** __fastcall hkButtonResolveCurrentDrawObj(uintptr_t thisPtr, void* /*edxUnused*/, void** outObj)
{
    void** result = outObj;
    const char* tag = GetTrackedButtonTag(thisPtr);
    uintptr_t resolveThis = thisPtr;
    DWORD superState = 0xFFFFFFFF;
    DWORD superKeySource = 0;
    DWORD donorStateIndex = 0;
    bool usedLeafOverride = false;
    const unsigned short* leafOverridePath = nullptr;

    if (tag && strcmp(tag, "SuperBtn") == 0 && ENABLE_SUPERBTN_STATE_DRAWOBJ_OVERRIDE) {
        __try {
            if (!SafeIsBadReadPtr((void*)(thisPtr + 0x34), 4))
                superState = *(DWORD*)(thisPtr + 0x34);
            if (!SafeIsBadReadPtr((void*)(thisPtr + 0x38), 4))
                superKeySource = *(DWORD*)(thisPtr + 0x38);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            superState = 0xFFFFFFFF;
            superKeySource = 0;
        }
        if (superState <= 4)
            superState = NormalizeSuperBtnVisualState(superState);

        leafOverridePath = GetSuperBtnStateLeafPath(superState);
        if (!leafOverridePath && superState == 4) {
            leafOverridePath = reinterpret_cast<const unsigned short*>(L"UI/UIWindow2.img/Skill/main/ExBtMacro/pressed/0");
        }

        if (leafOverridePath && outObj) {
            void* replacementObj = nullptr;
            if (ResolveNativeImage(leafOverridePath, &replacementObj, "BtnLeafOverride") && replacementObj) {
                if (oButtonResolveCurrentDrawObj)
                    result = oButtonResolveCurrentDrawObj(thisPtr, outObj);
                if (outObj && !SafeIsBadReadPtr(outObj, 4) && *outObj && *outObj != replacementObj) {
                    ReleaseUiObj(*outObj);
                }
                NormalizeSuperBtnLeafDrawObjFromVisibleModel(replacementObj, superState, superKeySource);
                *outObj = replacementObj;
                result = outObj;
                usedLeafOverride = true;
            }
        }
    }

    if (!usedLeafOverride &&
        tag && strcmp(tag, "SuperBtn") == 0 &&
        !g_SuperBtnForcedStableNormalMode &&
        ENABLE_SUPERBTN_NATIVE_DONOR_DRAWOBJ &&
        oButtonResolveCurrentDrawObj) {
        __try {
            if (superState == 0xFFFFFFFF && !SafeIsBadReadPtr((void*)(thisPtr + 0x34), 4))
                superState = *(DWORD*)(thisPtr + 0x34);
            if (!SafeIsBadReadPtr((void*)(thisPtr + 0x38), 4))
                superKeySource = *(DWORD*)(thisPtr + 0x38);
            if (superState <= 4)
                superState = NormalizeSuperBtnVisualState(superState);

            resolveThis = SelectSuperBtnDonorForState(superState, &donorStateIndex);
            if (resolveThis) {
                const DWORD resolveState = GetDonorResolveState(donorStateIndex);
                if (!SafeIsBadReadPtr((void*)(resolveThis + 0x34), 4))
                    *(DWORD*)(resolveThis + 0x34) = resolveState;
                if (!SafeIsBadReadPtr((void*)(resolveThis + 0x38), 4))
                    *(DWORD*)(resolveThis + 0x38) = superKeySource;
            } else {
                resolveThis = thisPtr;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            resolveThis = thisPtr;
        }
    }


    if (!usedLeafOverride &&
        tag && strcmp(tag, "SuperBtn") == 0 && ENABLE_SUPERBTN_DRAWOBJ_AB_FALLBACK && g_SuperBtnCompareObj) {
        __try {
            if (superState == 0xFFFFFFFF && !SafeIsBadReadPtr((void*)(thisPtr + 0x34), 4))
                superState = *(DWORD*)(thisPtr + 0x34);
            if (!SafeIsBadReadPtr((void*)(thisPtr + 0x38), 4))
                superKeySource = *(DWORD*)(thisPtr + 0x38);
            if (superState <= 4 && !SafeIsBadReadPtr((void*)(g_SuperBtnCompareObj + 0x34), 4))
                *(DWORD*)(g_SuperBtnCompareObj + 0x34) = superState;
            if (!SafeIsBadReadPtr((void*)(g_SuperBtnCompareObj + 0x38), 4))
                *(DWORD*)(g_SuperBtnCompareObj + 0x38) = superKeySource;
            resolveThis = g_SuperBtnCompareObj;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            resolveThis = thisPtr;
        }
    }

    // v16.3: stableNormal 模式 + 有缓存 drawObj 时，直接返回缓存对象
    // 完全绕过原始 resolve，避免游戏引擎资源树重建导致 resolve 结果变化
    if (!usedLeafOverride &&
        tag && strcmp(tag, "SuperBtn") == 0 &&
        g_SuperBtnForcedStableNormalMode &&
        g_SuperBtnCachedDrawObj &&
        outObj) {
        // AddRef cached drawObj (caller 会 Release)
        __try {
            (*(void (__stdcall **)(void*))(*reinterpret_cast<DWORD*>(g_SuperBtnCachedDrawObj) + 4))(g_SuperBtnCachedDrawObj);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        *outObj = g_SuperBtnCachedDrawObj;
        result = outObj;
        // 跳过原始 resolve
        goto resolve_done;
    }

    // v16: stableNormal 模式下但没有缓存时的 fallback：临时钉 state=0
    DWORD savedStateForStable = 0xFFFFFFFF;
    bool didPinStateForStable = false;
    if (!usedLeafOverride &&
        tag && strcmp(tag, "SuperBtn") == 0 &&
        g_SuperBtnForcedStableNormalMode &&
        resolveThis == thisPtr) {
        __try {
            if (!SafeIsBadReadPtr((void*)(thisPtr + 0x34), 4)) {
                savedStateForStable = *(DWORD*)(thisPtr + 0x34);
                if (savedStateForStable != 0) {
                    *(DWORD*)(thisPtr + 0x34) = 0;
                    didPinStateForStable = true;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    if (!usedLeafOverride && oButtonResolveCurrentDrawObj)
        result = oButtonResolveCurrentDrawObj(resolveThis, outObj);

    if (didPinStateForStable) {
        __try {
            if (!SafeIsBadReadPtr((void*)(thisPtr + 0x34), 4))
                *(DWORD*)(thisPtr + 0x34) = savedStateForStable;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

resolve_done:

    if (!tag)
        return result;

    LONG after = InterlockedDecrement(&g_BtnResolveLogBudget);
    if (after < 0)
        return result;

    DWORD stateIndex = 0xFFFFFFFF;
    DWORD keySource = 0;
    uintptr_t stateSlot = 0;
    uintptr_t resolvedObj = 0;
    __try {
        if (!SafeIsBadReadPtr((void*)(resolveThis + 0x34), 4))
            stateIndex = *(DWORD*)(resolveThis + 0x34);
        if (!SafeIsBadReadPtr((void*)(resolveThis + 0x38), 4))
            keySource = *(DWORD*)(resolveThis + 0x38);
        if (stateIndex <= 4 && !SafeIsBadReadPtr((void*)(resolveThis + 0x78 + stateIndex * 4), 4))
            stateSlot = *(DWORD*)(resolveThis + 0x78 + stateIndex * 4);
        if (outObj && !SafeIsBadReadPtr(outObj, 4))
            resolvedObj = (uintptr_t)(*outObj);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    WriteLogFmt("[BtnResolveDraw] tag=%s this=0x%08X resolveThis=0x%08X leaf=%s state=%u keySrc=0x%08X slot=0x%08X out=0x%08X caller=0x%08X",
        tag,
        (DWORD)thisPtr,
        (DWORD)resolveThis,
        usedLeafOverride && leafOverridePath ? reinterpret_cast<const wchar_t*>(leafOverridePath) : L"-",
        stateIndex,
        keySource,
        (DWORD)stateSlot,
        (DWORD)resolvedObj,
        (DWORD)(uintptr_t)_ReturnAddress());
    return result;
}

static int QueryDrawObjectExtent(void* drawObj, int vtblOffset, int* outValue)
{
    if (outValue) *outValue = 0;
    if (!drawObj)
        return E_POINTER;
    __try {
        DWORD vt = *(DWORD*)drawObj;
        if (!vt || SafeIsBadReadPtr((void*)(vt + vtblOffset), 4))
            return E_POINTER;
        typedef int (__stdcall *tGetExtent)(void* self, int* outValue);
        tGetExtent fn = *(tGetExtent*)(vt + vtblOffset);
        if (!fn)
            return E_POINTER;
        int tmp = 0;
        int hr = fn(drawObj, &tmp);
        if (outValue)
            *outValue = tmp;
        return hr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

static int GetSuperBtnResIdForState(DWORD state)
{
    switch (state) {
    case 0: return IDR_BTN_NORMAL;
    case 1: return IDR_BTN_PRESSED;
    case 2: return IDR_BTN_DISABLED;
    case 3: return IDR_BTN_HOVER;
    case 4: return IDR_BTN_PRESSED;
    default: return IDR_BTN_NORMAL;
    }
}

static CpuButtonBitmap1555* GetSuperBtnCpuBitmapSlot(DWORD state)
{
    switch (state) {
    case 0: return &g_SuperBtnCpuNormal;
    case 1: return &g_SuperBtnCpuPressed;
    case 2: return &g_SuperBtnCpuDisabled;
    case 3: return &g_SuperBtnCpuHover;
    case 4: return &g_SuperBtnCpuPressed;
    default: return &g_SuperBtnCpuNormal;
    }
}

static unsigned short PackBgra4444(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    const unsigned short bb = (unsigned short)(b >> 4);
    const unsigned short gg = (unsigned short)(g >> 4);
    const unsigned short rr = (unsigned short)(r >> 4);
    const unsigned short aa = (unsigned short)(a >> 4);
    return (unsigned short)(bb | (gg << 4) | (rr << 8) | (aa << 12));
}

static bool LoadCpuButtonBitmap1555FromResource(int resID, CpuButtonBitmap1555* outBmp)
{
    if (!outBmp)
        return false;
    if (outBmp->loaded)
        return !outBmp->pixels.empty();

    outBmp->loaded = true;
    outBmp->width = 0;
    outBmp->height = 0;
    outBmp->pixels.clear();
    outBmp->alpha.clear();

    HRSRC hRes = FindResourceA(g_hModule, MAKEINTRESOURCEA(resID), RT_RCDATA);
    if (!hRes) {
        WriteLogFmt("[BtnCpuBmp] FindResource(%d) failed", resID);
        return false;
    }

    HGLOBAL hMem = LoadResource(g_hModule, hRes);
    DWORD sz = SizeofResource(g_hModule, hRes);
    if (!hMem || !sz) {
        WriteLogFmt("[BtnCpuBmp] LoadResource(%d) failed", resID);
        return false;
    }

    void* pData = LockResource(hMem);
    if (!pData)
        return false;

    int w = 0;
    int h = 0;
    int ch = 0;
    unsigned char* rgba = stbi_load_from_memory((const unsigned char*)pData, (int)sz, &w, &h, &ch, 4);
    if (!rgba || w <= 0 || h <= 0) {
        WriteLogFmt("[BtnCpuBmp] stbi_load(%d) failed", resID);
        if (rgba)
            stbi_image_free(rgba);
        return false;
    }

    outBmp->width = w;
    outBmp->height = h;
    outBmp->pixels.resize((size_t)w * (size_t)h, 0);
    outBmp->alpha.resize((size_t)w * (size_t)h, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char* src = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4;
            outBmp->pixels[(size_t)y * (size_t)w + (size_t)x] =
                PackBgra4444(src[0], src[1], src[2], src[3]);
            outBmp->alpha[(size_t)y * (size_t)w + (size_t)x] = src[3];
        }
    }

    stbi_image_free(rgba);
    WriteLogFmt("[BtnCpuBmp] loaded res=%d size=%dx%d", resID, w, h);
    return true;
}

static bool EnsureSuperBtnCpuBitmapLoaded(DWORD state)
{
    CpuButtonBitmap1555* slot = GetSuperBtnCpuBitmapSlot(state);
    if (!slot)
        return false;
    return LoadCpuButtonBitmap1555FromResource(GetSuperBtnResIdForState(state), slot);
}

static const CpuButtonBitmap1555* GetSuperBtnCpuBitmapForState(DWORD state)
{
    if (!EnsureSuperBtnCpuBitmapLoaded(state))
        return nullptr;
    CpuButtonBitmap1555* slot = GetSuperBtnCpuBitmapSlot(state);
    if (!slot || slot->pixels.empty())
        return nullptr;
    return slot;
}

static bool PatchCanvasObjWithCpuBitmap1555(void* canvasObj, const CpuButtonBitmap1555* bmp, const char* logTag)
{
    if (!canvasObj || !bmp || bmp->pixels.empty())
        return false;

    bool ok = false;
    int w = 0;
    int h = 0;
    int pitchBytes = 0;
    uintptr_t pixelBaseAddr = 0;
    const char* failReason = "none";
    __try {
        w = *(int*)((uintptr_t)canvasObj + 0x40);
        h = *(int*)((uintptr_t)canvasObj + 0x44);
        pitchBytes = *(int*)((uintptr_t)canvasObj + 0x68);
        unsigned short* pixelBase = *(unsigned short**)((uintptr_t)canvasObj + 0x6C);
        pixelBaseAddr = (uintptr_t)pixelBase;
        if (!pixelBase || w <= 0 || h <= 0 || pitchBytes <= 0) {
            failReason = "bad_fields";
            __leave;
        }
        if (w != bmp->width || h != bmp->height) {
            failReason = "size_mismatch";
            __leave;
        }
        if (pitchBytes < (w * (int)sizeof(unsigned short)) || pitchBytes > 4096)
            pitchBytes = w * (int)sizeof(unsigned short);

        for (int y = 0; y < h; ++y) {
            unsigned char* dst = (unsigned char*)pixelBase + (size_t)y * (size_t)pitchBytes;
            const unsigned short* src = bmp->pixels.data() + (size_t)y * (size_t)w;
            memcpy(dst, src, (size_t)w * sizeof(unsigned short));
        }
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
        failReason = "exception";
    }

    static LONG s_patchCanvasLogBudget = 32;
    LONG after = InterlockedDecrement(&s_patchCanvasLogBudget);
    if (after >= 0) {
        WriteLogFmt("[%s] canvas=0x%08X ok=%d wh=%dx%d pitchBytes=%d pixel=0x%08X fail=%s",
            logTag ? logTag : "BtnCanvasPatch",
            (DWORD)(uintptr_t)canvasObj,
            ok ? 1 : 0,
            w, h, pitchBytes,
            (DWORD)pixelBaseAddr,
            failReason);
    }
    return ok;
}

static int PatchDrawObjWithCpuBitmap1555(void* drawObj, DWORD state, const char* logTag)
{
    const CpuButtonBitmap1555* bmp = GetSuperBtnCpuBitmapForState(state);
    if (!drawObj || !bmp)
        return 0;

    uintptr_t entryList = 0;
    __try {
        if (!SafeIsBadReadPtr(drawObj, 0x44))
            entryList = *(uintptr_t*)((uintptr_t)drawObj + 0x40);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        entryList = 0;
    }
    if (!entryList)
        return 0;

    int patched = 0;
    int scanned = 0;
    uintptr_t lastWrapperObj = 0;
    uintptr_t matchedWrapperObj = 0;
    for (int i = 0; i < 64; ++i) {
        uintptr_t entry = entryList + (size_t)i * 0x14;
        if (SafeIsBadReadPtr((void*)entry, 0x14))
            break;

        uintptr_t wrapperObj = *(uintptr_t*)entry;
        DWORD entryKind = *(DWORD*)(entry + 0x0C);
        DWORD entryFlag = *(DWORD*)(entry + 0x10);
        if (!wrapperObj)
            break;
        if (entryKind != 0x10 || (entryFlag != 0 && entryFlag != 1))
            break;
        ++scanned;
        lastWrapperObj = wrapperObj;

        __try {
            if (!SafeIsBadReadPtr((void*)(wrapperObj + 0x44), 8)) {
                const int w = *(int*)(wrapperObj + 0x40);
                const int h = *(int*)(wrapperObj + 0x44);
                if (w == bmp->width && h == bmp->height) {
                    matchedWrapperObj = wrapperObj;
                    break;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    if (matchedWrapperObj) {
        if (PatchCanvasObjWithCpuBitmap1555((void*)matchedWrapperObj, bmp, logTag))
            ++patched;
    } else if (lastWrapperObj) {
        if (PatchCanvasObjWithCpuBitmap1555((void*)lastWrapperObj, bmp, logTag))
            ++patched;
    } else if (PatchCanvasObjWithCpuBitmap1555((void*)entryList, bmp, logTag)) {
        // hover/mouseOver 这类状态在 dump 里可能不是 entry array，而是直接指向单层 wrapper。
        ++patched;
    }

    static LONG s_patchDrawObjLogBudget = 24;
    LONG after = InterlockedDecrement(&s_patchDrawObjLogBudget);
    if (after >= 0) {
        WriteLogFmt("[%s] state=%u drawObj=0x%08X entryList=0x%08X scanned=%d last=0x%08X patched=%d",
            logTag ? logTag : "BtnLeafPatch",
            state,
            (DWORD)(uintptr_t)drawObj,
            (DWORD)entryList,
            scanned,
            (DWORD)(matchedWrapperObj ? matchedWrapperObj : lastWrapperObj),
            patched);
    }

    return patched;
}

static bool PatchSuperBtnDonorDrawObjectsFromResources()
{
    if (!ENABLE_SUPERBTN_NATIVE_DONOR_DRAWOBJ || !oButtonResolveCurrentDrawObj)
        return false;

    for (int i = 0; i < 5; ++i) {
        g_SuperBtnStateDonorPatched[i] = false;
    }

    int patchedTotal = 0;
    for (DWORD state = 0; state <= 4; ++state) {
        uintptr_t donorObj = g_SuperBtnStateDonorObj[state];
        if (!donorObj)
            continue;

        DWORD savedState = 0;
        DWORD savedKeySrc = 0;
        __try {
            if (!SafeIsBadReadPtr((void*)(donorObj + 0x34), 4))
                savedState = *(DWORD*)(donorObj + 0x34);
            if (!SafeIsBadReadPtr((void*)(donorObj + 0x38), 4))
                savedKeySrc = *(DWORD*)(donorObj + 0x38);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        void* drawObj = nullptr;
        __try {
            if (!SafeIsBadReadPtr((void*)(donorObj + 0x34), 4))
                *(DWORD*)(donorObj + 0x34) = GetDonorResolveState(state);
            if (!SafeIsBadReadPtr((void*)(donorObj + 0x38), 4))
                *(DWORD*)(donorObj + 0x38) = 0;
            oButtonResolveCurrentDrawObj(donorObj, &drawObj);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            drawObj = nullptr;
        }

        const int patched = PatchDrawObjWithCpuBitmap1555(drawObj, state, "BtnDonorLeafPatch");
        g_SuperBtnStateDonorPatched[state] = patched > 0;
        patchedTotal += patched;

        // v16.3: state=0 且 patch 成功时，缓存 drawObj 供 resolve hook 直接返回
        if (state == 0 && patched > 0 && drawObj) {
            if (g_SuperBtnCachedDrawObj && g_SuperBtnCachedDrawObj != drawObj)
                ReleaseUiObj(g_SuperBtnCachedDrawObj);
            g_SuperBtnCachedDrawObj = drawObj;
            // AddRef 以防游戏引擎回收
            __try {
                (*(void (__stdcall **)(void*))(*reinterpret_cast<DWORD*>(drawObj) + 4))(drawObj);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            WriteLogFmt("[BtnCachedDrawObj] cached=0x%08X state=0", (DWORD)(uintptr_t)drawObj);
        } else {
            ReleaseUiObj(drawObj);
        }

        __try {
            if (!SafeIsBadReadPtr((void*)(donorObj + 0x34), 4))
                *(DWORD*)(donorObj + 0x34) = savedState;
            if (!SafeIsBadReadPtr((void*)(donorObj + 0x38), 4))
                *(DWORD*)(donorObj + 0x38) = savedKeySrc;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        WriteLogFmt("[BtnDonorLeafPatch] donorState=%u donor=0x%08X patched=%d",
            state,
            (DWORD)donorObj,
            patched);
    }

    WriteLogFmt("[BtnDonorLeafPatch] donor=0x%08X patchedTotal=%d",
        (DWORD)g_SuperBtnSkinDonorObj,
        patchedTotal);
    g_SuperBtnForcedStableNormalMode = false;
    return patchedTotal > 0;
}

static bool PatchSuperBtnOwnDrawObjectsFromResources(uintptr_t btnObj, const char* reason)
{
    if (!ENABLE_SUPERBTN_SELF_DRAWOBJ_PATCH || !btnObj || !oButtonResolveCurrentDrawObj)
        return false;

    bool anyPatched = false;
    DWORD savedState = 0;
    DWORD savedKeySrc = 0;
    __try {
        if (!SafeIsBadReadPtr((void*)(btnObj + 0x34), 4))
            savedState = *(DWORD*)(btnObj + 0x34);
        if (!SafeIsBadReadPtr((void*)(btnObj + 0x38), 4))
            savedKeySrc = *(DWORD*)(btnObj + 0x38);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    for (DWORD state = 0; state <= 4; ++state) {
        void* drawObj = nullptr;
        int patched = 0;
        __try {
            if (!SafeIsBadReadPtr((void*)(btnObj + 0x34), 4))
                *(DWORD*)(btnObj + 0x34) = state;
            if (!SafeIsBadReadPtr((void*)(btnObj + 0x38), 4))
                *(DWORD*)(btnObj + 0x38) = 0;
            oButtonResolveCurrentDrawObj(btnObj, &drawObj);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            drawObj = nullptr;
        }

        patched = PatchDrawObjWithCpuBitmap1555(drawObj, state, "BtnSelfLeafPatch");
        g_SuperBtnSelfStatePatched[state] = patched > 0;
        if (patched > 0)
            anyPatched = true;

        WriteLogFmt("[BtnSelfLeafPatch] reason=%s state=%u btn=0x%08X patched=%d",
            reason ? reason : "-",
            state,
            (DWORD)btnObj,
            patched);

        ReleaseUiObj(drawObj);
    }

    __try {
        if (!SafeIsBadReadPtr((void*)(btnObj + 0x34), 4))
            *(DWORD*)(btnObj + 0x34) = savedState;
        if (!SafeIsBadReadPtr((void*)(btnObj + 0x38), 4))
            *(DWORD*)(btnObj + 0x38) = savedKeySrc;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    WriteLogFmt("[BtnSelfPatch] reason=%s btn=0x%08X patched=%d states=[%d,%d,%d,%d,%d]",
        reason ? reason : "-",
        (DWORD)btnObj,
        anyPatched ? 1 : 0,
        g_SuperBtnSelfStatePatched[0] ? 1 : 0,
        g_SuperBtnSelfStatePatched[1] ? 1 : 0,
        g_SuperBtnSelfStatePatched[2] ? 1 : 0,
        g_SuperBtnSelfStatePatched[3] ? 1 : 0,
        g_SuperBtnSelfStatePatched[4] ? 1 : 0);
    return anyPatched;
}

static bool PatchSuperBtnCurrentWrapperFromResources(uintptr_t btnObj, const char* reason)
{
    if (!ENABLE_SUPERBTN_RUNTIME_WRAPPER_PATCH || !btnObj)
        return false;

    DWORD state = 0;
    void* wrapperObj = nullptr;
    __try {
        if (!SafeIsBadReadPtr((void*)(btnObj + 0x34), 4))
            state = *(DWORD*)(btnObj + 0x34);
        if (!SafeIsBadReadPtr((void*)(btnObj + 0x3C), 4))
            wrapperObj = *(void**)(btnObj + 0x3C);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        wrapperObj = nullptr;
    }

    state = NormalizeSuperBtnVisualState(state);
    const int patched = PatchDrawObjWithCpuBitmap1555(wrapperObj, state, "BtnWrapperLeafPatch");
    WriteLogFmt("[BtnWrapperPatch] reason=%s state=%u btn=0x%08X wrapper=0x%08X patched=%d",
        reason ? reason : "-",
        state,
        (DWORD)btnObj,
        (DWORD)(uintptr_t)wrapperObj,
        patched);
    return patched > 0;
}

static IDirect3DTexture9* GetSuperBtnTextureForState(DWORD state)
{
    switch (state) {
    case 0: return g_texBtnNormal;
    case 1: return g_texBtnPressed;
    case 2: return g_texBtnDisabled ? g_texBtnDisabled : g_texBtnPressed;
    case 3: return g_texBtnHover ? g_texBtnHover : g_texBtnNormal;
    case 4: return g_texBtnPressed ? g_texBtnPressed : g_texBtnNormal;
    default: return g_texBtnNormal;
    }
}

static IDirect3DTexture9* GetSuperCursorTextureForCurrentState()
{
    if (g_SuperBtnD3DPressed)
        return g_texCursorPressed ? g_texCursorPressed : g_texCursorNormal;

    if (g_SuperBtnD3DHover) {
        const DWORD nowTick = GetTickCount();
        const DWORD hoverStartTick = g_SuperBtnD3DHoverStartTick ? g_SuperBtnD3DHoverStartTick : nowTick;
        const DWORD hoverElapsed = nowTick - hoverStartTick;
        if (hoverElapsed < 500)
            return g_texCursorHoverA ? g_texCursorHoverA : g_texCursorNormal;

        const DWORD loopFrame = ((hoverElapsed - 500) / 500) & 1;
        return loopFrame ? (g_texCursorHoverA ? g_texCursorHoverA : g_texCursorNormal)
                         : (g_texCursorHoverB ? g_texCursorHoverB : g_texCursorNormal);
    }

    return g_texCursorNormal;
}

static float GetSuperCursorFixedYOffset(IDirect3DTexture9* currentTex)
{
    // User-confirmed baseline:
    // 1. whole custom cursor should sit 4px higher than the raw mouse point
    // 2. only the second/taller frame (mouse.normal.1) should move 2px downward
    float y = -4.0f;
    if (currentTex && currentTex == g_texCursorHoverA)
        y += 2.0f;
    return y;
}

static void DrawSuperButtonCursorInPresent(IDirect3DDevice9* pDevice)
{
    if (!pDevice || !g_GameHwnd)
        return;
    if (!ShouldSuppressGameMouseForSuperBtnD3D())
        return;
    if (SuperImGuiOverlayShouldSuppressGameMouse())
        return;

    IDirect3DTexture9* tex = GetSuperCursorTextureForCurrentState();
    if (!tex)
        return;

    POINT pt = {};
    if (!GetCursorPos(&pt) || !ScreenToClient(g_GameHwnd, &pt))
        return;

    D3DSURFACE_DESC desc = {};
    if (FAILED(tex->GetLevelDesc(0, &desc)))
        return;

    const float drawX = floorf((float)pt.x);
    const float extraYOffset = GetSuperCursorFixedYOffset(tex);
    const float drawY = floorf((float)pt.y + extraYOffset);
    DrawTexturedQuad(pDevice, tex, drawX, drawY, (float)desc.Width, (float)desc.Height);

    LONG after = InterlockedDecrement(&g_PresentCursorDrawLogBudget);
    if (after >= 0) {
        WriteLogFmt("[PresentCursorDraw] hover=%d pressed=%d pos=(%d,%d) draw=(%.1f,%.1f) tex=0x%08X size=%ux%u extraY=%.1f",
            g_SuperBtnD3DHover ? 1 : 0,
            g_SuperBtnD3DPressed ? 1 : 0,
            pt.x, pt.y,
            drawX, drawY,
            (DWORD)(uintptr_t)tex,
            (unsigned)desc.Width,
            (unsigned)desc.Height,
            extraYOffset);
    }
}

static bool DrawSuperButtonStateOnNativeSurface(uintptr_t thisPtr, DWORD state, int drawX, int drawY)
{
    if (!thisPtr || state > 4)
        return false;

    const unsigned short* leafPath = GetSuperBtnStateLeafPath(state);
    if (!leafPath && state == 4) {
        leafPath = reinterpret_cast<const unsigned short*>(L"UI/UIWindow2.img/Skill/main/ExBtMacro/pressed/0");
    }
    if (!leafPath)
        return false;

    DWORD* surface = nullptr;
    const char* surfaceSrc = "none";
    void* imageObj = nullptr;
    VARIANTARG alphaVar = {};
    alphaVar.vt = VT_I4;
    alphaVar.lVal = 255;

    bool ok = false;
    __try {
        if (g_SkillWndThis) {
            ((tGetSurface)ADDR_435A50)(g_SkillWndThis, &surface);
            if (surface)
                surfaceSrc = "skillwnd";
        }
        if (!surface && !SafeIsBadReadPtr((void*)(thisPtr + 0x18), 4)) {
            surface = reinterpret_cast<DWORD*>(*(DWORD*)(thisPtr + 0x18));
            if (surface)
                surfaceSrc = "btncom";
        }

        if (surface && ResolveNativeImage(leafPath, &imageObj, "BtnSurfaceOverride") && imageObj) {
            ((tSurfaceDrawImage)ADDR_401C90)(surface, drawX, drawY, (int)imageObj, reinterpret_cast<DWORD*>(&alphaVar));
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }

    static LONG s_btnSurfaceDrawLogBudget = 40;
    LONG after = InterlockedDecrement(&s_btnSurfaceDrawLogBudget);
    if (after >= 0) {
        WriteLogFmt("[BtnSurfaceDraw] state=%u xy=(%d,%d) ok=%d surface=0x%08X src=%s image=0x%08X path=%S",
            state,
            drawX, drawY,
            ok ? 1 : 0,
            (DWORD)(uintptr_t)surface,
            surfaceSrc,
            (DWORD)(uintptr_t)imageObj,
            reinterpret_cast<const wchar_t*>(leafPath));
    }

    ReleaseUiObj(imageObj);
    ClearGameVariant(&alphaVar);
    return ok;
}

static bool DrawSuperButtonTextureInNativeDraw(uintptr_t thisPtr, DWORD state)
{
    if (!thisPtr || state > 4 || !g_pDevice || !g_TexturesLoaded)
        return false;

    IDirect3DTexture9* tex = GetSuperBtnTextureForState(state);
    if (!tex)
        return false;

    int screenX = 0;
    int screenY = 0;
    int screenW = 0;
    int screenH = 0;
    if (!GetButtonScreenRectByObj(thisPtr, &screenX, &screenY, &screenW, &screenH))
        return false;

    bool ok = false;
    __try {
        DrawTexturedQuad(g_pDevice, tex, (float)screenX, (float)screenY, (float)screenW, (float)screenH);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }

    LONG after = InterlockedDecrement(&g_BtnD3DDrawLogBudget);
    if (after >= 0) {
        WriteLogFmt("[BtnD3DDraw] state=%u rect=(%d,%d,%d,%d) ok=%d tex=0x%08X btn=0x%08X",
            state,
            screenX, screenY, screenW, screenH,
            ok ? 1 : 0,
            (DWORD)(uintptr_t)tex,
            (DWORD)thisPtr);
    }

    return ok;
}

static bool DrawSuperButtonTextureInSkillWndDraw(uintptr_t skillWndThis)
{
    if (ENABLE_POST_B9F6E0_NATIVE_TIMING_TEST)
        return false;

    if (ENABLE_SUPERBTN_D3D_BUTTON_MODE) {
        UNREFERENCED_PARAMETER(skillWndThis);
        return false;
    }

    // v17.5: 在 SkillWnd draw handler 尾部手动调 507020 绘制按钮。
    // 关键修复：调用 oButtonDrawCurrentState (trampoline) 会绕过 hkButtonDrawCurrentState，
    // 因此必须在此处手动做状态归一化 + donor slot 借用，否则 hover/pressed 状态下
    // slot 里的 draw object 不稳定，507020 解析失败导致按钮消失。
    if (!skillWndThis || !g_NativeBtnCreated || !g_SuperBtnObj || !oButtonDrawCurrentState)
        return false;

    int screenX = 0, screenY = 0, screenW = 0, screenH = 0;
    if (!GetButtonScreenRectByObj(g_SuperBtnObj, &screenX, &screenY, &screenW, &screenH))
        return false;

    // ---- 状态归一化 + donor 准备（复刻 hkButtonDrawCurrentState 中 SuperBtn 逻辑）----
    DWORD origState = 0xFFFFFFFF;
    DWORD normalizedState = 0;
    DWORD savedStateSlot = 0;
    DWORD savedStateValue = 0;
    DWORD borrowedSlotIndex = 0xFFFFFFFF;
    bool borrowedDonorSlots = false;
    bool statePatched = false;

    __try {
        if (!SafeIsBadReadPtr((void*)(g_SuperBtnObj + 0x34), 4))
            origState = *(DWORD*)(g_SuperBtnObj + 0x34);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        origState = 0xFFFFFFFF;
    }

    if (origState < 5) {
        normalizedState = NormalizeSuperBtnVisualState(origState);

        // donor state patching
        if (ENABLE_SUPERBTN_NATIVE_DONOR_DRAWOBJ)
            EnsureSuperBtnDonorStatePatched(normalizedState, "skillwnd_draw");

        // 临时将 state 写成 normalized（让 507020 读到 normal 而不是 hover）
        if (normalizedState != origState) {
            __try {
                if (!SafeIsBadReadPtr((void*)(g_SuperBtnObj + 0x34), 4)) {
                    *(DWORD*)(g_SuperBtnObj + 0x34) = normalizedState;
                    statePatched = true;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // donor slot 借用（非稳定模式时需要）
        if (!g_SuperBtnForcedStableNormalMode && ENABLE_SUPERBTN_NATIVE_DONOR_DRAWOBJ) {
            __try {
                DWORD donorStateIndex = 0;
                uintptr_t donorObj = SelectSuperBtnDonorForState(normalizedState, &donorStateIndex);
                const DWORD donorResolveState = GetDonorResolveState(donorStateIndex);
                if (donorObj &&
                    !SafeIsBadReadPtr((void*)(g_SuperBtnObj + 0x34), 4) &&
                    !SafeIsBadReadPtr((void*)(g_SuperBtnObj + 0x78 + normalizedState * 4), 4) &&
                    !SafeIsBadReadPtr((void*)(donorObj + 0x78 + donorResolveState * 4), 4)) {
                    savedStateValue = *(DWORD*)(g_SuperBtnObj + 0x34);
                    savedStateSlot = *(DWORD*)(g_SuperBtnObj + 0x78 + normalizedState * 4);
                    if (donorResolveState != normalizedState)
                        *(DWORD*)(g_SuperBtnObj + 0x34) = donorResolveState;
                    *(DWORD*)(g_SuperBtnObj + 0x78 + normalizedState * 4) = *(DWORD*)(donorObj + 0x78 + donorResolveState * 4);
                    borrowedSlotIndex = normalizedState;
                    borrowedDonorSlots = true;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                borrowedDonorSlots = false;
            }
        }
    }

    // ---- 调用原生绘制 ----
    bool ok = false;
    __try {
        oButtonDrawCurrentState(g_SuperBtnObj, screenX, screenY, 0);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }

    // ---- 恢复状态 ----
    if (borrowedDonorSlots) {
        __try {
            if (!SafeIsBadReadPtr((void*)(g_SuperBtnObj + 0x34), 4))
                *(DWORD*)(g_SuperBtnObj + 0x34) = savedStateValue;
            if (borrowedSlotIndex < 5)
                *(DWORD*)(g_SuperBtnObj + 0x78 + borrowedSlotIndex * 4) = savedStateSlot;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    } else if (statePatched) {
        // 只做了 state 归一化但没借 slot，也要恢复原始 state
        __try {
            if (!SafeIsBadReadPtr((void*)(g_SuperBtnObj + 0x34), 4))
                *(DWORD*)(g_SuperBtnObj + 0x34) = origState;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // D3D fallback
    if (!ok && g_pDevice && g_TexturesLoaded) {
        DWORD state = GetCurrentSuperBtnState();
        IDirect3DTexture9* tex = GetSuperBtnTextureForState(state);
        if (tex) {
            __try {
                DrawTexturedQuad(g_pDevice, tex, (float)screenX, (float)screenY, (float)screenW, (float)screenH);
                ok = true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    // v17.7: 记录上次成功 draw tick
    if (ok)
        g_SuperBtnLastDrawTick = GetTickCount();

    static LONG s_logBudget = 80;
    LONG after = InterlockedDecrement(&s_logBudget);
    if (after >= 0) {
        WriteLogFmt("[SkillBtnDraw] ok=%d origState=%u norm=%u borrowed=%d rect=(%d,%d,%d,%d) btn=0x%08X",
            ok ? 1 : 0, origState, normalizedState, borrowedDonorSlots ? 1 : 0,
            screenX, screenY, screenW, screenH,
            (DWORD)g_SuperBtnObj);
    }

    return ok;
}

static DWORD GetCurrentSuperBtnState()
{
    if (!g_SuperBtnObj)
        return 0;

    __try {
        if (!SafeIsBadReadPtr((void*)(g_SuperBtnObj + 0x34), 4)) {
            DWORD state = *(DWORD*)(g_SuperBtnObj + 0x34);
            if (state <= 4)
                return state;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    return 0;
}

static void DrawSuperButtonTextureInPresent(IDirect3DDevice9* pDevice)
{
    if (ENABLE_POST_B9F6E0_NATIVE_TIMING_TEST)
        return;

    if (!pDevice || !g_NativeBtnCreated || !g_SuperBtnObj)
        return;

    if (ENABLE_SUPERBTN_D3D_BUTTON_MODE) {
        if (!g_TexturesLoaded)
            return;
        if (!UpdateSuperBtnVisiblePieces("present_draw"))
            return;

        IDirect3DTexture9* tex = GetSuperBtnTextureForState(g_SuperBtnD3DVisualState);
        if (!tex)
            return;

        bool ok = false;
        for (size_t i = 0; i < g_SuperBtnVisiblePieces.size(); ++i) {
            const SuperBtnVisiblePiece& piece = g_SuperBtnVisiblePieces[i];
            const RECT& rc = piece.screen;
            const int w = rc.right - rc.left;
            const int h = rc.bottom - rc.top;
            if (w <= 0 || h <= 0)
                continue;

            __try {
                DrawTexturedQuadUV(
                    pDevice,
                    tex,
                    (float)rc.left,
                    (float)rc.top,
                    (float)w,
                    (float)h,
                    piece.u0,
                    piece.v0,
                    piece.u1,
                    piece.v1);
                ok = true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }

        if (ok)
            g_SuperBtnLastDrawTick = GetTickCount();

        LONG after = InterlockedDecrement(&g_PresentBtnDrawLogBudget);
        if (after >= 0) {
            WriteLogFmt("[PresentBtnDraw] ok=%d state=%u pieces=%d tex=0x%08X btn=0x%08X",
                ok ? 1 : 0,
                (unsigned)g_SuperBtnD3DVisualState,
                (int)g_SuperBtnVisiblePieces.size(),
                (DWORD)(uintptr_t)tex,
                (DWORD)g_SuperBtnObj);
        }
        return;
    }

    if (!ENABLE_PRESENT_SUPERBTN_DRAW)
        return;

    DWORD state = GetCurrentSuperBtnState();
    state = NormalizeSuperBtnVisualState(state);
    IDirect3DTexture9* tex = GetSuperBtnTextureForState(state);
    int screenX = 0;
    int screenY = 0;
    int screenW = 0;
    int screenH = 0;
    if (!tex || !GetButtonScreenRectByObj(g_SuperBtnObj, &screenX, &screenY, &screenW, &screenH))
        return;

    DrawTexturedQuad(pDevice, tex, (float)screenX, (float)screenY, (float)screenW, (float)screenH);

    LONG after = InterlockedDecrement(&g_PresentBtnDrawLogBudget);
    if (after >= 0) {
        WriteLogFmt("[PresentBtnDraw] state=%u rect=(%d,%d,%d,%d) tex=0x%08X btn=0x%08X",
            state,
            screenX, screenY, screenW, screenH,
            (DWORD)(uintptr_t)tex,
            (DWORD)g_SuperBtnObj);
    }
}

static int __fastcall hkButtonDrawCurrentState(uintptr_t thisPtr, void* /*edxUnused*/, int x, int y, int a4)
{
    const char* tag = GetTrackedButtonTag(thisPtr);
    void* drawObj = nullptr;
    int width = 0;
    int height = 0;
    int hrW = 0;
    int hrH = 0;
    int callX = x;
    int callY = y;
    DWORD state = 0xFFFFFFFF;

    if (tag) {
        __try {
            if (!SafeIsBadReadPtr((void*)(thisPtr + 0x34), 4))
                state = *(DWORD*)(thisPtr + 0x34);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            state = 0xFFFFFFFF;
        }
    }

    if (tag && strcmp(tag, "SuperBtn") == 0 && state < 5) {
        state = NormalizeSuperBtnVisualState(state);
        if (ENABLE_SUPERBTN_NATIVE_DONOR_DRAWOBJ)
            EnsureSuperBtnDonorStatePatched(state, "draw");
        if (ENABLE_SUPERBTN_RUNTIME_WRAPPER_PATCH)
            PatchSuperBtnCurrentWrapperFromResources(thisPtr, "draw");
        // v16.5: 不再用 metric override 值覆盖 draw 坐标。
        // metric 返回的是 surfaceExtent - drawObjExtent（位置差值），不是绝对绘制坐标。
        // draw 的 x, y 应由游戏引擎根据按钮 move 位置自然传入。
    }

    if (tag && oButtonResolveCurrentDrawObj) {
        __try {
            oButtonResolveCurrentDrawObj(thisPtr, &drawObj);
            hrW = QueryDrawObjectExtent(drawObj, 0x40, &width);
            hrH = QueryDrawObjectExtent(drawObj, 0x48, &height);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            drawObj = nullptr;
            hrW = hrH = GetExceptionCode();
            width = height = 0;
        }
    }

    DWORD savedStateSlot = 0;
    DWORD savedStateValue = state;
    DWORD borrowedSlotIndex = 0xFFFFFFFF;
    DWORD borrowedResolveState = 0xFFFFFFFF;
    bool borrowedDonorSlots = false;
    if (tag && strcmp(tag, "SuperBtn") == 0 &&
        !g_SuperBtnForcedStableNormalMode &&
        ENABLE_SUPERBTN_NATIVE_DONOR_DRAWOBJ &&
        state < 5) {
        __try {
            DWORD donorStateIndex = 0;
            uintptr_t donorObj = SelectSuperBtnDonorForState(state, &donorStateIndex);
            const DWORD donorResolveState = GetDonorResolveState(donorStateIndex);
            if (donorObj &&
                !SafeIsBadReadPtr((void*)(thisPtr + 0x34), 4) &&
                !SafeIsBadReadPtr((void*)(thisPtr + 0x78 + state * 4), 4) &&
                !SafeIsBadReadPtr((void*)(donorObj + 0x78 + donorResolveState * 4), 4)) {
                savedStateValue = *(DWORD*)(thisPtr + 0x34);
                savedStateSlot = *(DWORD*)(thisPtr + 0x78 + state * 4);
                if (donorResolveState != state)
                    *(DWORD*)(thisPtr + 0x34) = donorResolveState;
                *(DWORD*)(thisPtr + 0x78 + state * 4) = *(DWORD*)(donorObj + 0x78 + donorResolveState * 4);
                borrowedSlotIndex = state;
                borrowedResolveState = donorResolveState;
                borrowedDonorSlots = true;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            borrowedDonorSlots = false;
        }
    }

    // v16.5: 诊断日志 - 记录 SuperBtn draw 的原始坐标
    if (tag && strcmp(tag, "SuperBtn") == 0) {
        static LONG g_SuperBtnDrawDiagBudget = 200;
        LONG diagAfter = InterlockedDecrement(&g_SuperBtnDrawDiagBudget);
        if (diagAfter >= 0) {
            WriteLogFmt("[BtnDrawDiag] origXY=(%d,%d) callXY=(%d,%d) state=%u a4=%d",
                x, y, callX, callY, state, a4);
        }
    }

    int ret = 0;
    if (oButtonDrawCurrentState)
        ret = oButtonDrawCurrentState(thisPtr, callX, callY, a4);

    if (borrowedDonorSlots) {
        __try {
            if (!SafeIsBadReadPtr((void*)(thisPtr + 0x34), 4))
                *(DWORD*)(thisPtr + 0x34) = savedStateValue;
            if (borrowedSlotIndex < 5)
                *(DWORD*)(thisPtr + 0x78 + borrowedSlotIndex * 4) = savedStateSlot;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    if (tag && strcmp(tag, "SuperBtn") == 0 && state < 5) {
        // v17.3: 不在按钮局部 draw 阶段叠 D3D 图，那个时机会被后续 UI 合成盖掉。
        // 改到 SkillWnd 整体 draw 后段再画，保持低于软件鼠标、又晚于按钮内部状态绘制。
    }

    // SuperBtn 额外叠一层 DLL 内嵌 PNG 纹理，绘制时机仍在原生按钮 draw 链里，
    // 这样层级低于软件鼠标，但又完全绕过 ExBtMacro 那条已证实损坏的资源树。

    if (drawObj) {
        __try {
            (*(void (__stdcall **)(void*))(*reinterpret_cast<DWORD*>(drawObj) + 8))(drawObj);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    if (!tag)
        return ret;

    LONG after = InterlockedDecrement(&g_BtnDrawLogBudget);
    if (after >= 0) {
        DWORD btnW = 0, btnH = 0;
        __try {
            if (!SafeIsBadReadPtr((void*)(thisPtr + 0x1C), 4))
                btnW = *(DWORD*)(thisPtr + 0x1C);
            if (!SafeIsBadReadPtr((void*)(thisPtr + 0x20), 4))
                btnH = *(DWORD*)(thisPtr + 0x20);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        WriteLogFmt("[BtnDrawState] tag=%s this=0x%08X state=%u draw=0x%08X wh=%dx%d hr=(0x%08X,0x%08X) btnWh=%ux%u xy=(%d,%d) a4=%d ret=0x%08X borrowed=%d borrowState=%u",
            tag,
            (DWORD)thisPtr,
            state,
            (DWORD)(uintptr_t)drawObj,
            width,
            height,
            hrW,
            hrH,
            btnW,
            btnH,
            callX,
            callY,
            a4,
            ret,
            borrowedDonorSlots ? 1 : 0,
            borrowedResolveState);
    }

    return ret;
}

static int LogTrackedButtonMetric(const char* metricTag, uintptr_t thisPtr, int ret)
{
    const char* tag = GetTrackedButtonTag(thisPtr);
    if (!tag)
        return ret;

    LONG after = InterlockedDecrement(&g_BtnMetricLogBudget);
    if (after < 0)
        return ret;

    uintptr_t surface = 0;
    uintptr_t drawObj = 0;
    DWORD state = 0xFFFFFFFF;
    DWORD btnW = 0, btnH = 0;
    __try {
        if (!SafeIsBadReadPtr((void*)(thisPtr + 0x18), 4))
            surface = *(DWORD*)(thisPtr + 0x18);
        if (!SafeIsBadReadPtr((void*)(thisPtr + 0x34), 4))
            state = *(DWORD*)(thisPtr + 0x34);
        if (!SafeIsBadReadPtr((void*)(thisPtr + 0x1C), 4))
            btnW = *(DWORD*)(thisPtr + 0x1C);
        if (!SafeIsBadReadPtr((void*)(thisPtr + 0x20), 4))
            btnH = *(DWORD*)(thisPtr + 0x20);
        if (oButtonResolveCurrentDrawObj) {
            void* tmp = nullptr;
            oButtonResolveCurrentDrawObj(thisPtr, &tmp);
            drawObj = (uintptr_t)tmp;
            if (tmp) {
                (*(void (__stdcall **)(void*))(*reinterpret_cast<DWORD*>(tmp) + 8))(tmp);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    WriteLogFmt("[BtnMetric] fn=%s tag=%s this=0x%08X state=%u ret=%d btnWh=%ux%u surf=0x%08X draw=0x%08X caller=0x%08X",
        metricTag ? metricTag : "?",
        tag,
        (DWORD)thisPtr,
        state,
        ret,
        btnW,
        btnH,
        (DWORD)surface,
        (DWORD)drawObj,
        (DWORD)(uintptr_t)_ReturnAddress());
    return ret;
}

static int __fastcall hkButtonMetric507DF0(uintptr_t thisPtr, void* /*edxUnused*/)
{
    int ret = 0;
    if (oButtonMetric507DF0)
        ret = oButtonMetric507DF0(thisPtr);
    const char* tag = GetTrackedButtonTag(thisPtr);
    DWORD state = 0xFFFFFFFF;
    __try {
        if (!SafeIsBadReadPtr((void*)(thisPtr + 0x34), 4))
            state = *(DWORD*)(thisPtr + 0x34);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    if (tag && state < 5) {
        if (strcmp(tag, "SuperBtn") == 0) {
            int overrideValue = 0;
            if (TryGetSuperBtnMetricOverrideValue(state, false, &overrideValue)) {
                static LONG s_metricLogBudget = 200;
                LONG mAfter = InterlockedDecrement(&s_metricLogBudget);
                if (mAfter >= 0)
                    WriteLogFmt("[BtnMetricOverride] fn=507DF0 state=%u old=%d new=%d", state, ret, overrideValue);
                ret = overrideValue;
            }
            // v17.7: 如果 SkillWnd draw 很久没触发，在 metric 调用时主动补画一次
            // metric 在 UI tree refresh 循环内被调用，此时 D3D 状态可用
            DWORD now = GetTickCount();
            DWORD lastDraw = g_SuperBtnLastDrawTick;
            if (lastDraw != 0 && (now - lastDraw) > SUPERBTN_REDRAW_INTERVAL_MS &&
                g_NativeBtnCreated && g_SuperBtnObj && g_SkillWndThis && oButtonDrawCurrentState) {
                // 避免在同一间隔内重复补画
                if (InterlockedCompareExchange((LONG*)&g_SuperBtnLastDrawTick, now, lastDraw) == (LONG)lastDraw) {
                    DrawSuperButtonTextureInSkillWndDraw(g_SkillWndThis);
                }
            }
        }
    }

    return LogTrackedButtonMetric("507DF0", thisPtr, ret);
}

static int __fastcall hkButtonMetric507ED0(uintptr_t thisPtr, void* /*edxUnused*/)
{
    int ret = 0;
    if (oButtonMetric507ED0)
        ret = oButtonMetric507ED0(thisPtr);
    const char* tag = GetTrackedButtonTag(thisPtr);
    DWORD state = 0xFFFFFFFF;
    __try {
        if (!SafeIsBadReadPtr((void*)(thisPtr + 0x34), 4))
            state = *(DWORD*)(thisPtr + 0x34);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    if (tag && state < 5) {
        if (strcmp(tag, "SuperBtn") == 0) {
            int overrideValue = 0;
            if (TryGetSuperBtnMetricOverrideValue(state, true, &overrideValue)) {
                static LONG s_metricLogBudgetED0 = 200;
                LONG mAfter = InterlockedDecrement(&s_metricLogBudgetED0);
                if (mAfter >= 0)
                    WriteLogFmt("[BtnMetricOverride] fn=507ED0 state=%u old=%d new=%d", state, ret, overrideValue);
                ret = overrideValue;
            }
        }
    }

    return LogTrackedButtonMetric("507ED0", thisPtr, ret);
}

// v16.1: hook 5095A0 (button state refresh)
// 当 SuperBtn + stableNormal 模式时，阻止游戏引擎切换到非 normal 状态。
// 5095A0 内部不只设 state，还会更新 surface、flags、metrics 等字段，
// 仅仅在 506EE0 钉 state=0 不够——5095A0 的其他副作用仍会导致按钮"消失"。
// 所以直接在 5095A0 入口拦截：SuperBtn + stableNormal + newState != 0 → 跳过。
// ============================================================================
// v17.7b: sub_529640 矩形写入保护 hook
// hook 点 0x52972E，在写入 a2[0~3] 矩形之前检查 edi 是否是 donor/compare 按钮
// 如果是则跳过矩形写入，防止 -4096 坐标干扰 SuperBtn
// ============================================================================
static int __cdecl IsDonorOrCompareButton(uintptr_t btnObj)
{
    if (!btnObj) return 0;
    if (btnObj == g_SuperBtnSkinDonorObj) return 1;
    if (btnObj == g_SuperBtnCompareObj) return 1;
    for (int i = 0; i < 5; ++i) {
        if (btnObj == g_SuperBtnStateDonorObj[i]) return 1;
    }
    return 0;
}

__declspec(naked) static void hkRectWrite52972E()
{
    __asm {
        // edi = this (按钮对象)，此时在 sub_529640 函数尾部
        // 被覆盖的原指令：
        //   52972E: mov eax, [esp+34h]  (4 bytes: 8B 44 24 34)
        //   529732: mov ecx, [esp+0Ch]  (4 bytes: 8B 4C 24 0C)
        // 总共 8 字节，我们覆盖了前 5 字节放 jmp，后 3 字节 nop

        // 保存 eax/ecx，调用 C 函数检查 edi 是否是 donor/compare
        push eax
        push ecx
        push edi
        call IsDonorOrCompareButton
        add esp, 4
        test eax, eax
        pop ecx
        pop eax
        jnz skip_write

        // 正常路径：执行被覆盖的原指令，然后继续到 529736
        mov eax, [esp+0x34]   // 原 52972E: mov eax, [esp+30h+arg_0]
        mov ecx, [esp+0x0C]   // 原 529732: mov ecx, [esp+30h+var_24]
        jmp dword ptr [ADDR_529736]

    skip_write:
        // donor/compare 路径：跳过矩形写入，直接到函数尾部
        mov eax, [esp+0x34]   // 仍需设置 eax = a2（返回值）
        jmp dword ptr [ADDR_52974F]
    }
}

static void* __fastcall hkButtonRefreshState5095A0(DWORD* thisPtr, void* /*edxUnused*/, int** a2)
{
    uintptr_t btn = (uintptr_t)thisPtr;
    const char* tag = GetTrackedButtonTag(btn);

    if (tag && strcmp(tag, "SuperBtn") == 0 &&
        g_SuperBtnForcedStableNormalMode) {
        // v16.2: stableNormal 模式下，所有 5095A0 调用对 SuperBtn 都拦截。
        // a2=0 (恢复normal) 也拦截，因为 5095A0 内部会更新 surface/flags/metrics，
        // 即使目标是 state=0，这些副作用仍会破坏我们已经 patch 好的按钮状态。
        // 我们自己的调用（ForceSuperButtonAllStatesToNormalDonor）在 stableNormal=true 之前执行，不受影响。
        return nullptr;
    }

    void* ret = nullptr;
    if (oButtonRefreshState5095A0)
        ret = oButtonRefreshState5095A0(thisPtr, a2);

    if (tag && strcmp(tag, "SuperBtn") == 0 && ENABLE_SUPERBTN_RUNTIME_WRAPPER_PATCH) {
        PatchSuperBtnCurrentWrapperFromResources(btn, "5095A0");
    }

    if (tag && strcmp(tag, "SuperBtn") == 0 && ENABLE_SUPERBTN_SELF_DRAWOBJ_PATCH) {
        PatchSuperBtnOwnDrawObjectsFromResources(btn, "5095A0");
    }
    return ret;
}

static void ReleaseUiObj(void* obj)
{
    if (!obj) return;
    __try {
        (*(void (__stdcall **)(void*))(*reinterpret_cast<DWORD*>(obj) + 8))(obj);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void ClearGameVariant(VARIANTARG* pv)
{
    if (!pv) return;
    __try {
        if (pv->vt == VT_BSTR) {
            LONG lVal = pv->lVal;
            pv->vt = VT_EMPTY;
            if (lVal && !SafeIsBadReadPtr((void*)ADDR_F671CC_PTR, 4)) {
                DWORD fnPtr = *(DWORD*)ADDR_F671CC_PTR;
                if (fnPtr) {
                    typedef void (__stdcall *tReleaseGameString)(LONG);
                    ((tReleaseGameString)fnPtr)(lVal - 4);
                }
            }
            return;
        }
        VariantClear(pv);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static bool ResolveNativeImage(const unsigned short* pathWide, void** outImage, const char* logTag)
{
    static int s_resolveLogCount = 0;
    if (outImage) *outImage = nullptr;
    if (!pathWide || !outImage) return false;

    if (SafeIsBadReadPtr((void*)ADDR_F6A84C, 4)) {
        if (s_resolveLogCount < 40) {
            WriteLogFmt("[%s] FAIL: ADDR_F6A84C unreadable", logTag ? logTag : "NativeImg");
            s_resolveLogCount++;
        }
        return false;
    }

    uintptr_t resRoot = *(uintptr_t*)ADDR_F6A84C;
    if (!resRoot) {
        if (s_resolveLogCount < 40) {
            WriteLogFmt("[%s] FAIL: resRoot null", logTag ? logTag : "NativeImg");
            s_resolveLogCount++;
        }
        return false;
    }

    VARIANTARG* pvargSrc = reinterpret_cast<VARIANTARG*>(ADDR_pvargSrc);
    if (SafeIsBadReadPtr(pvargSrc, sizeof(VARIANTARG))) {
        if (s_resolveLogCount < 40) {
            WriteLogFmt("[%s] FAIL: pvargSrc unreadable", logTag ? logTag : "NativeImg");
            s_resolveLogCount++;
        }
        return false;
    }

    VARIANTARG vA = {};
    VARIANTARG vB = {};
    VARIANTARG resVar = {};
    VariantInit(&vA);
    VariantInit(&vB);
    VariantInit(&resVar);
    bool ok = false;
    const char* stage = "begin";
    DWORD rawObj = 0;
    DWORD drawObj = 0;
    int hr = 0;

    __try {
        stage = "cloneA";
        ((tCloneVariant)ADDR_4016D0)(&vA, pvargSrc);
        stage = "cloneB";
        ((tCloneVariant)ADDR_4016D0)(&vB, pvargSrc);

        stage = "resolveChainAsm";
        DWORD resRoot32 = (DWORD)resRoot;
        DWORD pathWide32 = (DWORD)pathWide;
        DWORD fn402F60 = ADDR_402F60;
        DWORD fn404D90 = ADDR_404D90;
        DWORD fn401990 = ADDR_401990;
        DWORD fn40CA00 = ADDR_40CA00;
        __asm {
            push 0
            push 0
            lea eax, [vA]
            push eax
            lea ecx, [vB]
            push ecx
            push ecx
            mov ecx, esp
            push pathWide32
            call fn402F60

            mov ecx, resRoot32
            lea edx, [resVar]
            push edx
            call fn404D90

            mov ecx, eax
            call fn401990
            mov rawObj, eax

            push eax
            lea ecx, [drawObj]
            mov dword ptr [ecx], 0
            call fn40CA00
            mov hr, eax
        }

        if (!rawObj) {
            if (s_resolveLogCount < 40) {
                WriteLogFmt("[%s] FAIL: sub_401990 null path=%S", logTag ? logTag : "NativeImg", pathWide);
                s_resolveLogCount++;
            }
            __leave;
        }

        stage = "queryDrawIface";
        if (hr < 0 || !drawObj) {
            if (s_resolveLogCount < 40) {
                WriteLogFmt("[%s] FAIL: sub_40CA00 hr=0x%08X path=%S", logTag ? logTag : "NativeImg", hr, pathWide);
                s_resolveLogCount++;
            }
            __leave;
        }

        *outImage = reinterpret_cast<void*>(drawObj);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (s_resolveLogCount < 40) {
            WriteLogFmt("[%s] EXCEPTION stage=%s path=%S", logTag ? logTag : "NativeImg", stage, pathWide);
            s_resolveLogCount++;
        }
    }

    ClearGameVariant(&vB);
    ClearGameVariant(&vA);
    ClearGameVariant(&resVar);
    return ok;
}

static bool AssignNativeResourceToSlot(uintptr_t btnObj, int slotIndex, const unsigned short* fullPath, const char* logTag)
{
    static int s_assignSlotLogCount = 0;
    if (!btnObj || !fullPath) return false;

    if (SafeIsBadReadPtr((void*)ADDR_F6A84C, 4)) {
        if (s_assignSlotLogCount < 40) {
            WriteLogFmt("[%s] FAIL: ADDR_F6A84C unreadable", logTag ? logTag : "BtnSlot");
            s_assignSlotLogCount++;
        }
        return false;
    }

    DWORD* slotPtr = reinterpret_cast<DWORD*>(btnObj + slotIndex * sizeof(DWORD));
    if (SafeIsBadReadPtr(slotPtr, sizeof(DWORD))) {
        WriteLogFmt("[%s] FAIL: slot unreadable idx=%d obj=0x%08X",
            logTag ? logTag : "BtnSlot",
            slotIndex,
            (DWORD)btnObj);
        return false;
    }

    uintptr_t resRoot = *(uintptr_t*)ADDR_F6A84C;
    if (!resRoot) {
        if (s_assignSlotLogCount < 40) {
            WriteLogFmt("[%s] FAIL: resRoot null", logTag ? logTag : "BtnSlot");
            s_assignSlotLogCount++;
        }
        return false;
    }

    VARIANTARG* pvargSrc = reinterpret_cast<VARIANTARG*>(ADDR_pvargSrc);
    if (SafeIsBadReadPtr(pvargSrc, sizeof(VARIANTARG))) {
        if (s_assignSlotLogCount < 40) {
            WriteLogFmt("[%s] FAIL: pvargSrc unreadable", logTag ? logTag : "BtnSlot");
            s_assignSlotLogCount++;
        }
        return false;
    }

    VARIANTARG vA = {};
    VARIANTARG vB = {};
    VARIANTARG resVar = {};
    VariantInit(&vA);
    VariantInit(&vB);
    VariantInit(&resVar);
    bool ok = false;
    const char* stage = "begin";
    DWORD rawObj = 0;
    int hr = 0;

    __try {
        stage = "cloneA";
        ((tCloneVariant)ADDR_4016D0)(&vA, pvargSrc);
        stage = "cloneB";
        ((tCloneVariant)ADDR_4016D0)(&vB, pvargSrc);

        stage = "resolveRawAsm";
        DWORD resRoot32 = (DWORD)resRoot;
        DWORD pathWide32 = (DWORD)fullPath;
        DWORD fn402F60 = ADDR_402F60;
        DWORD fn404D90 = ADDR_404D90;
        DWORD fn401990 = ADDR_401990;
        __asm {
            push 0
            push 0
            lea eax, [vA]
            push eax
            lea ecx, [vB]
            push ecx
            push ecx
            mov ecx, esp
            push pathWide32
            call fn402F60

            mov ecx, resRoot32
            lea edx, [resVar]
            push edx
            call fn404D90

            mov ecx, eax
            call fn401990
            mov rawObj, eax
        }

        if (!rawObj) {
            if (s_assignSlotLogCount < 40) {
                WriteLogFmt("[%s] FAIL: sub_401990 null path=%S",
                    logTag ? logTag : "BtnSlot",
                    reinterpret_cast<const wchar_t*>(fullPath));
                s_assignSlotLogCount++;
            }
            __leave;
        }

        stage = "assignSlot";
        hr = ((tAssignUiSlot)ADDR_4027F0)(slotPtr, reinterpret_cast<void*>(rawObj));
        if (hr < 0 && hr != -2147467262) {
            if (s_assignSlotLogCount < 40) {
                WriteLogFmt("[%s] FAIL: assign hr=0x%08X idx=%d path=%S",
                    logTag ? logTag : "BtnSlot",
                    hr,
                    slotIndex,
                    reinterpret_cast<const wchar_t*>(fullPath));
                s_assignSlotLogCount++;
            }
            __leave;
        }

        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (s_assignSlotLogCount < 40) {
            WriteLogFmt("[%s] EXCEPTION stage=%s path=%S",
                logTag ? logTag : "BtnSlot",
                stage,
                reinterpret_cast<const wchar_t*>(fullPath));
            s_assignSlotLogCount++;
        }
    }

    ClearGameVariant(&vB);
    ClearGameVariant(&vA);
    ClearGameVariant(&resVar);
    if (ok) {
        WriteLogFmt("[%s] OK: idx=%d path=%S",
            logTag ? logTag : "BtnSlot",
            slotIndex,
            reinterpret_cast<const wchar_t*>(fullPath));
    }
    return ok;
}

static bool AssignExistingUiObjectToSlot(uintptr_t btnObj, int slotIndex, void* existingObj, const char* logTag)
{
    if (!btnObj || !existingObj)
        return false;

    DWORD* slotPtr = reinterpret_cast<DWORD*>(btnObj + slotIndex * sizeof(DWORD));
    if (SafeIsBadReadPtr(slotPtr, sizeof(DWORD))) {
        WriteLogFmt("[%s] FAIL: slot unreadable idx=%d obj=0x%08X",
            logTag ? logTag : "BtnSlotExisting",
            slotIndex,
            (DWORD)btnObj);
        return false;
    }

    int hr = 0;
    __try {
        hr = ((tAssignUiSlot)ADDR_4027F0)(slotPtr, existingObj);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLogFmt("[%s] EXCEPTION assign idx=%d dst=0x%08X src=0x%08X",
            logTag ? logTag : "BtnSlotExisting",
            slotIndex,
            (DWORD)btnObj,
            (DWORD)(uintptr_t)existingObj);
        return false;
    }

    if (hr < 0 && hr != -2147467262) {
        WriteLogFmt("[%s] FAIL: assign hr=0x%08X idx=%d dst=0x%08X src=0x%08X",
            logTag ? logTag : "BtnSlotExisting",
            hr,
            slotIndex,
            (DWORD)btnObj,
            (DWORD)(uintptr_t)existingObj);
        return false;
    }

    WriteLogFmt("[%s] OK: idx=%d dst=0x%08X src=0x%08X",
        logTag ? logTag : "BtnSlotExisting",
        slotIndex,
        (DWORD)btnObj,
        (DWORD)(uintptr_t)existingObj);
    return true;
}

static bool CreateNativeButtonInstance(
    uintptr_t skillWndThis,
    const unsigned short* resPath,
    DWORD ctrlID,
    int btnXOffset,
    int btnYOffset,
    bool enableTrace,
    DWORD* outObj)
{
    if (outObj) *outObj = 0;
    if (!skillWndThis || !resPath)
        return false;

    const uintptr_t ctrlContainer = skillWndThis + 0xBEC;
    if (SafeIsBadReadPtr((void*)ctrlContainer, 12))
        return false;

    DWORD resultBuf[4] = {0};
    const DWORD fnCreate = ADDR_66A770;
    const DWORD resPath32 = (DWORD)(uintptr_t)resPath;
    const uintptr_t ecxVal = ctrlContainer;

    if (enableTrace) {
        g_SuperBtnCreateTraceUntilTick = GetTickCount() + 2000;
        InterlockedExchange(&g_SuperBtnCreateTraceBudget, 64);
        InterlockedExchange(&g_SuperBtnCreateTraceScope, 1);
    }

    bool ok = false;
    __try {
        __asm {
            push 0xFF
            push 0
            push [btnYOffset]
            push [btnXOffset]
            push [ctrlID]
            push [resPath32]
            lea eax, [resultBuf]
            push eax
            mov ecx, [ecxVal]
            call [fnCreate]
        }
        ok = (resultBuf[1] != 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }

    if (enableTrace)
        InterlockedExchange(&g_SuperBtnCreateTraceScope, 0);

    if (ok && outObj)
        *outObj = resultBuf[1];
    return ok;
}

static void MoveNativeButtonRaw(uintptr_t btnObj, int x, int y, const char* logTag)
{
    if (!btnObj)
        return;

    __try {
        ((tMoveNativeButton)ADDR_50AEB0)(reinterpret_cast<DWORD*>(btnObj), x, y);
        WriteLogFmt("[%s] move btn=0x%08X pos=(%d,%d)",
            logTag ? logTag : "BtnMoveRaw",
            (DWORD)btnObj,
            x, y);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLogFmt("[%s] EXCEPTION btn=0x%08X code=0x%08X",
            logTag ? logTag : "BtnMoveRaw",
            (DWORD)btnObj,
            GetExceptionCode());
    }
}

static bool PatchSuperButtonStateImagesFromDonor(uintptr_t btnObj, uintptr_t donorObj)
{
    if (!btnObj || !donorObj)
        return false;

    const void* donorNormal   = !SafeIsBadReadPtr((void*)(donorObj + 30 * 4), 4) ? *(void**)(donorObj + 30 * 4) : nullptr;
    const void* donorPressed  = !SafeIsBadReadPtr((void*)(donorObj + 31 * 4), 4) ? *(void**)(donorObj + 31 * 4) : nullptr;
    const void* donorDisabled = !SafeIsBadReadPtr((void*)(donorObj + 32 * 4), 4) ? *(void**)(donorObj + 32 * 4) : nullptr;
    const void* donorHover    = !SafeIsBadReadPtr((void*)(donorObj + 33 * 4), 4) ? *(void**)(donorObj + 33 * 4) : nullptr;
    const void* donorChecked  = !SafeIsBadReadPtr((void*)(donorObj + 34 * 4), 4) ? *(void**)(donorObj + 34 * 4) : nullptr;

    WriteLogFmt("[BtnDonor] obj=0x%08X donor=0x%08X slots=[%08X,%08X,%08X,%08X,%08X]",
        (DWORD)btnObj,
        (DWORD)donorObj,
        (DWORD)(uintptr_t)donorNormal,
        (DWORD)(uintptr_t)donorPressed,
        (DWORD)(uintptr_t)donorDisabled,
        (DWORD)(uintptr_t)donorHover,
        (DWORD)(uintptr_t)donorChecked);

    int patchedCount = 0;
    patchedCount += AssignExistingUiObjectToSlot(btnObj, 30, const_cast<void*>(donorNormal), "BtnDonorNormal") ? 1 : 0;
    patchedCount += AssignExistingUiObjectToSlot(btnObj, 31, const_cast<void*>(donorPressed), "BtnDonorPressed") ? 1 : 0;
    patchedCount += AssignExistingUiObjectToSlot(btnObj, 32, const_cast<void*>(donorDisabled), "BtnDonorDisabled") ? 1 : 0;
    patchedCount += AssignExistingUiObjectToSlot(btnObj, 33, const_cast<void*>(donorHover), "BtnDonorHover") ? 1 : 0;
    if (!AssignExistingUiObjectToSlot(btnObj, 34, const_cast<void*>(donorChecked), "BtnDonorChecked")) {
        patchedCount += AssignExistingUiObjectToSlot(btnObj, 34, const_cast<void*>(donorPressed), "BtnDonorCheckedFallback") ? 1 : 0;
    } else {
        patchedCount += 1;
    }

    if (patchedCount > 0) {
        __try {
            ((tRefreshButtonState)ADDR_5095A0)(reinterpret_cast<DWORD*>(btnObj), nullptr);
            WriteLogFmt("[BtnDonor] state refresh OK obj=0x%08X patched=%d", (DWORD)btnObj, patchedCount);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            WriteLogFmt("[BtnDonor] state refresh EXCEPTION obj=0x%08X patched=%d", (DWORD)btnObj, patchedCount);
        }
    }

    return patchedCount > 0;
}

static bool ForceSuperButtonAllStatesToNormalDonor(uintptr_t btnObj)
{
    if (!btnObj)
        return false;

    uintptr_t donorObj = g_SuperBtnStateDonorObj[0] ? g_SuperBtnStateDonorObj[0] : g_SuperBtnSkinDonorObj;
    if (!donorObj)
        return false;

    const void* donorNormal = !SafeIsBadReadPtr((void*)(donorObj + 30 * 4), 4)
        ? *(void**)(donorObj + 30 * 4)
        : nullptr;
    if (!donorNormal)
        return false;

    int patchedCount = 0;
    patchedCount += AssignExistingUiObjectToSlot(btnObj, 30, const_cast<void*>(donorNormal), "BtnForceNormal0") ? 1 : 0;
    patchedCount += AssignExistingUiObjectToSlot(btnObj, 31, const_cast<void*>(donorNormal), "BtnForceNormal1") ? 1 : 0;
    patchedCount += AssignExistingUiObjectToSlot(btnObj, 32, const_cast<void*>(donorNormal), "BtnForceNormal2") ? 1 : 0;
    patchedCount += AssignExistingUiObjectToSlot(btnObj, 33, const_cast<void*>(donorNormal), "BtnForceNormal3") ? 1 : 0;
    patchedCount += AssignExistingUiObjectToSlot(btnObj, 34, const_cast<void*>(donorNormal), "BtnForceNormal4") ? 1 : 0;

    if (patchedCount > 0) {
        __try {
            if (!SafeIsBadReadPtr((void*)(btnObj + 0x34), 4))
                *(DWORD*)(btnObj + 0x34) = 0;
            if (!SafeIsBadReadPtr((void*)(btnObj + 0x38), 4))
                *(DWORD*)(btnObj + 0x38) = 0;
            ((tRefreshButtonState)ADDR_5095A0)(reinterpret_cast<DWORD*>(btnObj), nullptr);
            g_SuperBtnForcedStableNormalMode = true;
            g_SuperBtnStateDonorPatched[0] = true;
            WriteLogFmt("[BtnForceNormal] state refresh OK obj=0x%08X donor=0x%08X patched=%d",
                (DWORD)btnObj,
                (DWORD)donorObj,
                patchedCount);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            WriteLogFmt("[BtnForceNormal] state refresh EXCEPTION obj=0x%08X donor=0x%08X patched=%d code=0x%08X",
                (DWORD)btnObj,
                (DWORD)donorObj,
                patchedCount,
                GetExceptionCode());
        }
    }

    return patchedCount > 0;
}

static void PatchSuperButtonStateImages(uintptr_t btnObj)
{
    if (!btnObj) return;

    static const unsigned short* kNormal   = reinterpret_cast<const unsigned short*>(L"UI/UIWindow2.img/Skill/main/ExBtMacro/normal");
    static const unsigned short* kPressed  = reinterpret_cast<const unsigned short*>(L"UI/UIWindow2.img/Skill/main/ExBtMacro/pressed");
    static const unsigned short* kDisabled = reinterpret_cast<const unsigned short*>(L"UI/UIWindow2.img/Skill/main/ExBtMacro/disabled");
    static const unsigned short* kHover    = reinterpret_cast<const unsigned short*>(L"UI/UIWindow2.img/Skill/main/ExBtMacro/mouseOver");
    static const unsigned short* kChecked  = reinterpret_cast<const unsigned short*>(L"UI/UIWindow2.img/Skill/main/ExBtMacro/checked");

    int patchedCount = 0;
    patchedCount += AssignNativeResourceToSlot(btnObj, 30, kNormal, "BtnSlotNormal") ? 1 : 0;
    patchedCount += AssignNativeResourceToSlot(btnObj, 31, kPressed, "BtnSlotPressed") ? 1 : 0;
    patchedCount += AssignNativeResourceToSlot(btnObj, 32, kDisabled, "BtnSlotDisabled") ? 1 : 0;
    patchedCount += AssignNativeResourceToSlot(btnObj, 33, kHover, "BtnSlotHover") ? 1 : 0;
    if (!AssignNativeResourceToSlot(btnObj, 34, kChecked, "BtnSlotChecked")) {
        patchedCount += AssignNativeResourceToSlot(btnObj, 34, kPressed, "BtnSlotCheckedFallback") ? 1 : 0;
    } else {
        patchedCount += 1;
    }

    if (patchedCount > 0) {
        __try {
            ((tRefreshButtonState)ADDR_5095A0)(reinterpret_cast<DWORD*>(btnObj), nullptr);
            WriteLogFmt("[NativeBtnPatch] state refresh OK obj=0x%08X", (DWORD)btnObj);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            WriteLogFmt("[NativeBtnPatch] state refresh EXCEPTION obj=0x%08X", (DWORD)btnObj);
        }
    }

    WriteLogFmt("[NativeBtnPatch] obj=0x%08X patched=%d", (DWORD)btnObj, patchedCount);
}

static bool DrawNativePanelOnSurface(DWORD* surface, int drawX, int drawY, const char* logTag, bool emitLog)
{
    if (!surface) return false;

    // 参照 donor 52AA90 的原生 draw：每次在画内容前先对 surface 做一次矩形清空。
    // v10.6 的 0xFF 清底会把透明区域冲成灰底；这里先保守试透明清底，
    // 目标是保留 SkillEx 背景自身的透明洞，而不是露出灰/黑底。
    __try {
        DWORD vt = *(DWORD*)surface;
        if (vt && !SafeIsBadReadPtr((void*)(vt + 140), 4)) {
            typedef int (__stdcall *tSurfaceClearRect)(DWORD*, int, int, int, int, char);
            tSurfaceClearRect fnClear = *(tSurfaceClearRect*)(vt + 140);
            if (fnClear) {
                fnClear(surface, 0, 0, PANEL_W, PANEL_H, (char)0x00);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (emitLog) {
            WriteLogFmt("[%s] EXCEPTION clear surface=0x%08X",
                logTag ? logTag : "NativeDraw", (DWORD)surface);
        }
    }

    VARIANTARG alphaVar = {};
    alphaVar.vt = VT_I4;
    alphaVar.lVal = 255;

    void* overlayObj = nullptr;
    const unsigned short* overlayPath = reinterpret_cast<const unsigned short*>(ADDR_STR_SkillExMainBackgrnd);
    bool overlayOk = ResolveNativeImage(overlayPath, &overlayObj, "NativeImg");
    if (!overlayOk) {
        overlayPath = reinterpret_cast<const unsigned short*>(ADDR_STR_SkillMacroBackgrnd);
        overlayOk = ResolveNativeImage(overlayPath, &overlayObj, "NativeImgFallback");
    }

    if (!overlayOk) {
        ClearGameVariant(&alphaVar);
        return false;
    }

    bool ok = false;
    __try {
        if (overlayObj) {
            ((tSurfaceDrawImage)ADDR_401C90)(surface, drawX, drawY, (int)overlayObj, reinterpret_cast<DWORD*>(&alphaVar));
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (emitLog) {
            WriteLogFmt("[%s] EXCEPTION draw overlay=%S",
                logTag ? logTag : "NativeDraw", overlayPath);
        }
    }

    if (emitLog) {
        WriteLogFmt("[%s] %s overlay=%S draw=(%d,%d) surface=0x%08X overlayObj=0x%08X",
            logTag ? logTag : "NativeDraw",
            ok ? "OK" : "FAIL",
            overlayPath,
            drawX, drawY,
            (DWORD)surface, (DWORD)overlayObj);
    }

    ReleaseUiObj(overlayObj);
    ClearGameVariant(&alphaVar);
    return ok;
}

static bool MoveNativeChildWnd(uintptr_t wndObj, int x, int y, const char* logTag)
{
    if (!wndObj) return false;

    LONG result = 0;
    __try {
        result = ((tMoveNativeWnd)ADDR_56D630)(wndObj, x, y);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_moveExceptLogCount = 0;
        if (s_moveExceptLogCount < 16) {
            WriteLogFmt("[%s] EXCEPTION: sub_56D630 0x%08X",
                logTag ? logTag : "MoveChild", GetExceptionCode());
            s_moveExceptLogCount++;
        }
        return false;
    }

    if (result < 0) {
        static int s_moveFailLogCount = 0;
        if (s_moveFailLogCount < 16) {
            WriteLogFmt("[%s] FAIL: sub_56D630 hr=0x%08X",
                logTag ? logTag : "MoveChild", (DWORD)result);
            s_moveFailLogCount++;
        }
        return false;
    }
    return true;
}

static bool MoveSuperChildBySkillAnchor(const char* logTag, bool markDirty)
{
    if (!g_SkillWndThis || !g_SuperExpanded || !g_SuperCWnd) return false;

    int vtX = 0, vtY = 0;
    if (!GetSkillWndAnchorPos(g_SkillWndThis, &vtX, &vtY)) {
        return false;
    }

    g_PanelDrawX = vtX - PANEL_W + SUPER_CHILD_VT_DELTA_X;
    g_PanelDrawY = vtY + SUPER_CHILD_VT_DELTA_Y;

    if (!MoveNativeChildWnd(g_SuperCWnd, g_PanelDrawX, g_PanelDrawY, logTag ? logTag : "MoveChildDirect")) {
        return false;
    }

    if (markDirty) {
        MarkSuperWndDirty(g_SuperCWnd, logTag ? logTag : "MoveChildDirect");
    }
    return true;
}

static bool SyncSkillWndActiveFocus(const char* logTag, bool force = false)
{
    if (!g_SkillWndThis) return false;
    if (SafeIsBadReadPtr((void*)ADDR_CWndMan, 4)) return false;

    uintptr_t wndMan = *(uintptr_t*)ADDR_CWndMan;
    if (!wndMan) return false;

    uintptr_t wndIface = g_SkillWndThis + 4;
    if (SafeIsBadReadPtr((void*)wndIface, 4)) return false;

    DWORD now = GetTickCount();
    if (!force && (now - g_LastFocusSyncTick) < 120) {
        return false;
    }
    g_LastFocusSyncTick = now;

    DWORD fnSync = ADDR_B9EEA0;
    __try {
        __asm {
            push [wndIface]
            mov ecx, [wndMan]
            call [fnSync]
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_focusSyncExceptLogCount = 0;
        if (s_focusSyncExceptLogCount < 16) {
            WriteLogFmt("[%s] EXCEPTION: sub_B9EEA0 0x%08X",
                logTag ? logTag : "FocusSync", GetExceptionCode());
            s_focusSyncExceptLogCount++;
        }
        return false;
    }

    static int s_focusSyncLogCount = 0;
    if (s_focusSyncLogCount < 40) {
        WriteLogFmt("[%s] OK: wndMan=0x%08X skillIface=0x%08X",
            logTag ? logTag : "FocusSync", (DWORD)wndMan, (DWORD)wndIface);
        s_focusSyncLogCount++;
    }
    return true;
}

static bool RebuildNativeChildSurface(uintptr_t wndObj, int x, int y, int width, int height, const char* logTag)
{
    if (!wndObj || width <= 0 || height <= 0) return false;

    // 证据：
    // 1. B9AB50 伪代码已确认 a2/a3/a4/a5 = x/y/width/height（A级）
    // 2. 轻量 child family 最终仍要落到 layer=10 + showable surface；这里沿用 a6=10、a7=1，保持行为保守一致
    // 3. a8 在伪代码中未直接使用；当前保守传 0，避免引入额外业务副作用
    int result = 0;
    DWORD fnResize = ADDR_B9AB50;
    int a6 = 10;
    int a7 = 1;
    int a8 = 0;

    __try {
        __asm {
            push [a8]
            push [a7]
            push [a6]
            push [height]
            push [width]
            push [y]
            push [x]
            mov ecx, [wndObj]
            call [fnResize]
            mov [result], eax
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_resizeExceptLogCount = 0;
        if (s_resizeExceptLogCount < 16) {
            WriteLogFmt("[%s] EXCEPTION: sub_B9AB50 0x%08X",
                logTag ? logTag : "ResizeChild", GetExceptionCode());
            s_resizeExceptLogCount++;
        }
        return false;
    }

    if (result != width) {
        static int s_resizeRetLogCount = 0;
        if (s_resizeRetLogCount < 16) {
            WriteLogFmt("[%s] WARN: sub_B9AB50 returned %d (expect width=%d)",
                logTag ? logTag : "ResizeChild", result, width);
            s_resizeRetLogCount++;
        }
    }

    return true;
}

static void LogNativeChildSurfaceShape(uintptr_t wndObj, const char* logTag)
{
    if (!wndObj || SafeIsBadReadPtr((void*)wndObj, 0x30)) return;

    int wndW = *(int*)(wndObj + 10 * 4);
    int wndH = *(int*)(wndObj + 11 * 4);
    uintptr_t surfaceObj = *(uintptr_t*)(wndObj + 6 * 4);
    int canvasW = -1, canvasH = -1;
    int logicalW2 = -1, logicalH2 = -1;

    if (surfaceObj && !SafeIsBadReadPtr((void*)(surfaceObj + 0x78), 8)) {
        canvasW = *(int*)(surfaceObj + 0x5C);
        canvasH = *(int*)(surfaceObj + 0x60);
        logicalW2 = *(int*)(surfaceObj + 0x6C);
        logicalH2 = *(int*)(surfaceObj + 0x70);
    }

    WriteLogFmt("[%s] wndSize=(%d,%d) surface=0x%08X canvas=(%d,%d) logical2=(%d,%d)",
        logTag ? logTag : "SurfaceShape",
        wndW, wndH, (DWORD)surfaceObj, canvasW, canvasH, logicalW2, logicalH2);
}

static bool IsOfficialSecondChildObject(uintptr_t wndObj, bool allowCustomVT1)
{
    if (!wndObj || SafeIsBadReadPtr((void*)wndObj, 0x84)) {
        return false;
    }

    DWORD vt1 = *(DWORD*)(wndObj + 0x00);
    DWORD vt2 = *(DWORD*)(wndObj + 0x04);
    DWORD vt3 = *(DWORD*)(wndObj + 0x08);
    bool vt1Ok = (vt1 == ADDR_VT_SkillWndSecondChild1);
    if (!vt1Ok && allowCustomVT1 && g_CustomVTable1) {
        vt1Ok = (vt1 == (DWORD)g_CustomVTable1);
    }

    if (!vt1Ok || vt2 != ADDR_VT_SkillWndSecondChild2 || vt3 != ADDR_VT_SkillWndSecondChild3) {
        return false;
    }

    int refCount = *(int*)(wndObj + CWND_OFF_REFCNT * 4);
    int width = *(int*)(wndObj + CWND_OFF_W * 4);
    int height = *(int*)(wndObj + CWND_OFF_H * 4);
    return refCount > 0 && refCount < 100 && width > 0 && height > 0 && width <= 4096 && height <= 4096;
}

static bool ApplySuperChildCustomDrawVTable(uintptr_t wndObj)
{
    if (!wndObj || SafeIsBadReadPtr((void*)wndObj, 0x0C)) {
        WriteLog("[NativeWnd] FAIL: custom vtable target unreadable");
        return false;
    }

    if (!IsOfficialSecondChildObject(wndObj, false)) {
        DWORD vt1 = SafeIsBadReadPtr((void*)wndObj, 12) ? 0 : *(DWORD*)(wndObj + 0x00);
        DWORD vt2 = SafeIsBadReadPtr((void*)wndObj, 12) ? 0 : *(DWORD*)(wndObj + 0x04);
        DWORD vt3 = SafeIsBadReadPtr((void*)wndObj, 12) ? 0 : *(DWORD*)(wndObj + 0x08);
        WriteLogFmt("[NativeWnd] FAIL: custom vtable target is not official second-child vt=(%08X,%08X,%08X)",
            vt1, vt2, vt3);
        return false;
    }

    DWORD origVT1 = *(DWORD*)wndObj;
    if (!origVT1 || SafeIsBadReadPtr((void*)origVT1, 256 * sizeof(DWORD))) {
        WriteLogFmt("[NativeWnd] FAIL: original VT1 invalid 0x%08X", origVT1);
        return false;
    }

    if (!g_CustomVTable1) {
        g_CustomVTable1 = (DWORD*)VirtualAlloc(nullptr, 256 * sizeof(DWORD),
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!g_CustomVTable1) {
            WriteLog("[NativeWnd] FAIL: VirtualAlloc custom VT1");
            return false;
        }
    }

    memcpy(g_CustomVTable1, (void*)origVT1, 256 * sizeof(DWORD));
    g_CustomVTable1[11] = (DWORD)&SuperCWndDraw; // 经验槽位：旧项目已跑通，同属 CWnd-family
    *(DWORD*)wndObj = (DWORD)g_CustomVTable1;
    WriteLogFmt("[NativeWnd] Custom VT1 installed: orig=0x%08X new=0x%08X draw=0x%08X",
        origVT1, (DWORD)g_CustomVTable1, (DWORD)&SuperCWndDraw);
    return true;
}

static void MarkSuperWndDirty(uintptr_t wndObj, const char* logTag)
{
    if (!wndObj) return;
    __try {
        DWORD fnDirty = ADDR_B9A5D0;
        __asm {
            push 0
            mov ecx, [wndObj]
            call [fnDirty]
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_dirtyExceptLogCount = 0;
        if (s_dirtyExceptLogCount < 16) {
            WriteLogFmt("[%s] EXCEPTION dirty 0x%08X",
                logTag ? logTag : "Dirty", GetExceptionCode());
            s_dirtyExceptLogCount++;
        }
    }
}

static void GetButtonSizeEstimate(int* outW, int* outH)
{
    int w = 50;
    int h = 16;
    if (g_SuperBtnObj && !SafeIsBadReadPtr((void*)(g_SuperBtnObj + 10 * 4), 8)) {
        int tw = *(int*)(g_SuperBtnObj + 10 * 4);
        int th = *(int*)(g_SuperBtnObj + 11 * 4);
        if (tw >= 24 && tw <= 256) w = tw;
        if (th >= 12 && th <= 64) h = th;
    }
    if (outW) *outW = w;
    if (outH) *outH = h;
}

static bool MoveSuperButtonToExpectedPos(const char* logTag)
{
    if (!g_SuperBtnObj || !g_SkillWndThis)
        return false;

    int x = 0, y = 0, w = 0, h = 0;
    if (!GetExpectedButtonRectCom(&x, &y, &w, &h) &&
        !GetExpectedButtonRectVt(&x, &y, &w, &h)) {
        return false;
    }

    if (x < 0 || y < 0 || x > 4096 || y > 4096) {
        WriteLogFmt("[%s] skip invalid target pos=(%d,%d) btn=0x%08X",
            logTag ? logTag : "BtnMove",
            x, y,
            (DWORD)g_SuperBtnObj);
        return false;
    }

    __try {
        ((tMoveNativeButton)ADDR_50AEB0)(reinterpret_cast<DWORD*>(g_SuperBtnObj), x, y);
        WriteLogFmt("[%s] move btn=0x%08X pos=(%d,%d) size=(%d,%d)",
            logTag ? logTag : "BtnMove",
            (DWORD)g_SuperBtnObj,
            x, y, w, h);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLogFmt("[%s] EXCEPTION btn=0x%08X code=0x%08X",
            logTag ? logTag : "BtnMove",
            (DWORD)g_SuperBtnObj,
            GetExceptionCode());
        return false;
    }
}

static bool GetExpectedButtonRectCom(int* outX, int* outY, int* outW, int* outH)
{
    if (!outX || !outY || !outW || !outH) return false;
    if (!g_SkillWndThis) return false;

    int sx = 0, sy = 0;
    if (!GetSkillWndComPos(g_SkillWndThis, &sx, &sy)) return false;

    int w = 0, h = 0;
    GetButtonSizeEstimate(&w, &h);
    *outX = sx + BTN_X_OFFSET;
    *outY = sy + BTN_Y_OFFSET;
    *outW = w;
    *outH = h;
    return true;
}

static bool GetExpectedButtonRectVt(int* outX, int* outY, int* outW, int* outH)
{
    if (!outX || !outY || !outW || !outH) return false;
    int sx = 0, sy = 0;
    if (g_SuperBtnObj && GetUiObjPosByVtablePlus4(g_SuperBtnObj, &sx, &sy)) {
        int w = 0, h = 0;
        GetButtonSizeEstimate(&w, &h);
        *outX = sx;
        *outY = sy;
        *outW = w;
        *outH = h;
        return true;
    }

    if (!g_SkillWndThis) return false;
    if (!GetSkillWndAnchorPos(g_SkillWndThis, &sx, &sy)) return false;

    int w = 0, h = 0;
    GetButtonSizeEstimate(&w, &h);
    *outX = sx + BTN_X_OFFSET;
    *outY = sy + BTN_Y_OFFSET;
    *outW = w;
    *outH = h;
    return true;
}

static bool GetSuperButtonScreenRect(int* outX, int* outY, int* outW, int* outH)
{
    if (!outX || !outY || !outW || !outH) return false;
    if (!g_SuperBtnObj) return false;

    int x = 0, y = 0;
    bool hasVt = GetUiObjPosByVtablePlus4(g_SuperBtnObj, &x, &y);
    if (!hasVt) {
        x = CWnd_GetX(g_SuperBtnObj);
        y = CWnd_GetY(g_SuperBtnObj);
    }
    if (x < -9000 || y < -9000 || x > 10000 || y > 10000) return false;

    int w = 0;
    int h = 0;
    GetButtonSizeEstimate(&w, &h);

    *outX = x;
    *outY = y;
    *outW = w;
    *outH = h;
    return true;
}

static bool GetPreferredButtonRect(int* outX, int* outY, int* outW, int* outH, const char** outSrc = nullptr)
{
    if (outSrc) *outSrc = "none";

    if (GetSuperButtonScreenRect(outX, outY, outW, outH)) {
        if (outSrc) *outSrc = "obj";
        return true;
    }

    if (GetExpectedButtonRectCom(outX, outY, outW, outH)) {
        if (outSrc) *outSrc = "com";
        return true;
    }

    if (GetExpectedButtonRectVt(outX, outY, outW, outH)) {
        if (outSrc) *outSrc = "vt";
        return true;
    }

    return false;
}

static void LogSuperButtonGeometry(const char* tag)
{
    if (!g_SuperBtnObj) {
        WriteLogFmt("[%s] btn=null", tag ? tag : "BtnGeom");
        return;
    }

    auto logBtnGeom = [&](uintptr_t btnObj, const char* localTag) {
        int objX = 0, objY = 0, objW = 0, objH = 0;
        bool hasObj = false;
        if (btnObj == g_SuperBtnObj)
            hasObj = GetSuperButtonScreenRect(&objX, &objY, &objW, &objH);
        else {
            int vtX2 = 0, vtY2 = 0;
            bool hasPos = GetUiObjPosByVtablePlus4(btnObj, &vtX2, &vtY2);
            if (hasPos && !SafeIsBadReadPtr((void*)(btnObj + 0x1C), 8)) {
                objX = vtX2;
                objY = vtY2;
                objW = *(int*)(btnObj + 0x1C);
                objH = *(int*)(btnObj + 0x20);
                hasObj = true;
            }
        }

        int rawX = CWnd_GetX(btnObj);
        int rawY = CWnd_GetY(btnObj);
        int vtX = 0, vtY = 0;
        bool hasObjVt = GetUiObjPosByVtablePlus4(btnObj, &vtX, &vtY);

        DWORD surf = 0;
        int surfX = 0;
        int surfY = 0;
        if (!SafeIsBadReadPtr((void*)(btnObj + 0x18), 4)) {
            surf = *(DWORD*)(btnObj + 0x18);
            if (surf && !SafeIsBadReadPtr((void*)(surf + 0x58), 8)) {
                surfX = *(int*)(surf + 0x54);
                surfY = *(int*)(surf + 0x58);
            }
        }

        WriteLogFmt("[%s] obj=0x%08X raw=(%d,%d) objRect=%s(%d,%d,%d,%d) objVt=%s(%d,%d) surf=0x%08X surfPos=(%d,%d)",
            localTag ? localTag : "BtnGeom",
            (DWORD)btnObj,
            rawX, rawY,
            hasObj ? "Y" : "N", objX, objY, objW, objH,
            hasObjVt ? "Y" : "N", vtX, vtY,
            surf, surfX, surfY);
    };

    int objX = 0, objY = 0, objW = 0, objH = 0;
    bool hasObj = GetSuperButtonScreenRect(&objX, &objY, &objW, &objH);

    int expComX = 0, expComY = 0, expComW = 0, expComH = 0;
    bool hasCom = GetExpectedButtonRectCom(&expComX, &expComY, &expComW, &expComH);

    int expVtX = 0, expVtY = 0, expVtW = 0, expVtH = 0;
    bool hasVt = GetExpectedButtonRectVt(&expVtX, &expVtY, &expVtW, &expVtH);

    int rawX = CWnd_GetX(g_SuperBtnObj);
    int rawY = CWnd_GetY(g_SuperBtnObj);
    int vtX = 0, vtY = 0;
    bool hasObjVt = GetUiObjPosByVtablePlus4(g_SuperBtnObj, &vtX, &vtY);

    DWORD surf = 0;
    int surfX = 0;
    int surfY = 0;
    if (!SafeIsBadReadPtr((void*)(g_SuperBtnObj + 0x18), 4)) {
        surf = *(DWORD*)(g_SuperBtnObj + 0x18);
        if (surf && !SafeIsBadReadPtr((void*)(surf + 0x58), 8)) {
            surfX = *(int*)(surf + 0x54);
            surfY = *(int*)(surf + 0x58);
        }
    }

    WriteLogFmt("[%s] obj=0x%08X raw=(%d,%d) objRect=%s(%d,%d,%d,%d) objVt=%s(%d,%d) com=%s(%d,%d,%d,%d) vt=%s(%d,%d,%d,%d) surf=0x%08X surfPos=(%d,%d)",
        tag ? tag : "BtnGeom",
        (DWORD)g_SuperBtnObj,
        rawX, rawY,
        hasObj ? "Y" : "N", objX, objY, objW, objH,
        hasObjVt ? "Y" : "N", vtX, vtY,
        hasCom ? "Y" : "N", expComX, expComY, expComW, expComH,
        hasVt ? "Y" : "N", expVtX, expVtY, expVtW, expVtH,
        surf, surfX, surfY);

    if (ENABLE_DEBUG_VISIBLE_COMPARE_BUTTON && g_SuperBtnSkinDonorObj && g_SuperBtnSkinDonorObj != g_SuperBtnObj)
        logBtnGeom(g_SuperBtnSkinDonorObj, "BtnGeomCompare");
}

static void LogNativeButtonCoreFields(uintptr_t btnObj, const char* tag)
{
    if (!btnObj) {
        WriteLogFmt("[%s] btn=null", tag ? tag : "BtnCore");
        return;
    }

    auto rd = [&](int offset) -> DWORD {
        if (SafeIsBadReadPtr((void*)(btnObj + offset), 4))
            return 0xFFFFFFFF;
        return *(DWORD*)(btnObj + offset);
    };

    WriteLogFmt("[%s] obj=0x%08X +18=%08X +1C=%08X +20=%08X +24=%08X +28=%08X +2C=%08X +30=%08X +34=%08X +38=%08X +3C=%08X +40=%08X +44=%08X slots=[%08X,%08X,%08X,%08X,%08X]",
        tag ? tag : "BtnCore",
        (DWORD)btnObj,
        rd(0x18), rd(0x1C), rd(0x20), rd(0x24), rd(0x28), rd(0x2C),
        rd(0x30), rd(0x34), rd(0x38), rd(0x3C), rd(0x40), rd(0x44),
        rd(30 * 4), rd(31 * 4), rd(32 * 4), rd(33 * 4), rd(34 * 4));
}

static bool ComputeSuperPanelPos(int* outX, int* outY, const char** outSrc)
{
    if (!outX || !outY) return false;
    if (outSrc) *outSrc = "none";
    static int s_decisionLogCount = 0;

    int comX = 0, comY = 0, vtX = 0, vtY = 0;
    bool hasCom = g_SkillWndThis && GetSkillWndComPos(g_SkillWndThis, &comX, &comY);
    bool hasVt  = g_SkillWndThis && GetSkillWndAnchorPos(g_SkillWndThis, &vtX, &vtY);

    if (ENABLE_IMGUI_OVERLAY_PANEL) {
        if (hasVt) {
            if (hasCom && s_decisionLogCount < 80) {
                int dx = vtX - comX; if (dx < 0) dx = -dx;
                int dy = vtY - comY; if (dy < 0) dy = -dy;
                WriteLogFmt("[AnchorDecision] overlay use=VT dx=%d dy=%d com=(%d,%d) vt=(%d,%d)",
                    dx, dy, comX, comY, vtX, vtY);
                s_decisionLogCount++;
            }
            *outX = vtX - PANEL_W - PANEL_LEFT_GAP;
            *outY = vtY;
            if (outSrc) *outSrc = "skill_vtable_overlay";
            return true;
        }

        int bx = 0, by = 0, bw = 0, bh = 0;
        if (GetPreferredButtonRect(&bx, &by, &bw, &bh)) {
            *outX = bx - PANEL_W - PANEL_LEFT_GAP;
            *outY = by - 8;
            if (outSrc) *outSrc = "button_fallback_overlay";
            return true;
        }

        if (hasCom && comX >= 0 && comY >= 0) {
            *outX = comX - PANEL_W - PANEL_LEFT_GAP;
            *outY = comY;
            if (outSrc) *outSrc = "skill_com_overlay_fallback";
            return true;
        }
    }

    // v7.2: COM坐标代表最终屏幕位置，优先作为面板锚点；VT仅在COM不可用时退化
    if (hasCom) {
        *outX = comX - PANEL_W - PANEL_LEFT_GAP;
        *outY = comY;
        if (outSrc) *outSrc = hasVt ? "skill_com_pref" : "skill_com_only";
        if (hasVt && s_decisionLogCount < 80) {
            int dx = vtX - comX; if (dx < 0) dx = -dx;
            int dy = vtY - comY; if (dy < 0) dy = -dy;
            WriteLogFmt("[AnchorDecision] prefer=COM dx=%d dy=%d com=(%d,%d) vt=(%d,%d) vt_delta=%d",
                dx, dy, comX, comY, vtX, vtY, PREFER_VT_DELTA);
            s_decisionLogCount++;
        }
        return true;
    }

    if (hasVt) {
        *outX = vtX - PANEL_W - PANEL_LEFT_GAP;
        *outY = vtY;
        if (outSrc) *outSrc = "skill_vtable";
        return true;
    }

    // 最后再退化到按钮坐标（某些版本按钮对象并非标准CWnd，可靠性较低）
    int bx = 0, by = 0, bw = 0, bh = 0;
    if (GetSuperButtonScreenRect(&bx, &by, &bw, &bh)) {
        *outX = bx - PANEL_W - PANEL_LEFT_GAP;
        *outY = by - 8;
        if (outSrc) *outSrc = "button";
        return true;
    }

    return false;
}

// ============================================================================
// 获取游戏主窗口
// ============================================================================
static HWND GetRealGameWindow()
{
    struct Param { HWND hwnd; DWORD pid; };
    Param p = {NULL, GetCurrentProcessId()};

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        Param* pp = (Param*)lParam;
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == pp->pid && IsWindowVisible(hwnd)) {
            char cn[256];
            GetClassNameA(hwnd, cn, sizeof(cn));
            if (strcmp(cn, "ConsoleWindowClass") != 0) {
                pp->hwnd = hwnd;
                return FALSE;
            }
        }
        return TRUE;
    }, (LPARAM)&p);
    return p.hwnd;
}

// ============================================================================
// PNG纹理从DLL资源加载（面板背景用，后续迁移到原生后可移除）
// ============================================================================
static IDirect3DTexture9* LoadTextureFromResource(IDirect3DDevice9* dev, int resID)
{
    if (!dev) return nullptr;

    HRSRC hRes = FindResourceA(g_hModule, MAKEINTRESOURCEA(resID), RT_RCDATA);
    if (!hRes) { WriteLogFmt("[Tex] FindResource(%d) failed", resID); return nullptr; }

    HGLOBAL hMem = LoadResource(g_hModule, hRes);
    DWORD sz = SizeofResource(g_hModule, hRes);
    if (!hMem || !sz) { WriteLogFmt("[Tex] LoadResource(%d) failed", resID); return nullptr; }

    void* pData = LockResource(hMem);
    if (!pData) return nullptr;

    int w, h, ch;
    unsigned char* pixels = stbi_load_from_memory((const unsigned char*)pData, (int)sz, &w, &h, &ch, 4);
    if (!pixels) { WriteLogFmt("[Tex] stbi_load(%d) failed", resID); return nullptr; }

    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr))) {
        stbi_image_free(pixels);
        return nullptr;
    }

    D3DLOCKED_RECT lr;
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0))) {
        for (int y2 = 0; y2 < h; y2++) {
            unsigned char* src = pixels + y2 * w * 4;
            unsigned char* dst = (unsigned char*)lr.pBits + y2 * lr.Pitch;
            for (int x2 = 0; x2 < w; x2++) {
                dst[x2*4+0] = src[x2*4+2]; // B
                dst[x2*4+1] = src[x2*4+1]; // G
                dst[x2*4+2] = src[x2*4+0]; // R
                dst[x2*4+3] = src[x2*4+3]; // A
            }
        }
        tex->UnlockRect(0);
    }

    stbi_image_free(pixels);
    WriteLogFmt("[Tex] Loaded #%d: %dx%d", resID, w, h);
    return tex;
}

static IDirect3DTexture9* LoadTextureFromFilePath(IDirect3DDevice9* dev, const char* path)
{
    if (!dev || !path || !path[0])
        return nullptr;

    int w = 0, h = 0, ch = 0;
    unsigned char* pixels = stbi_load(path, &w, &h, &ch, 4);
    if (!pixels) {
        WriteLogFmt("[Tex] stbi_load(file) failed path=%s", path);
        return nullptr;
    }

    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr))) {
        stbi_image_free(pixels);
        return nullptr;
    }

    D3DLOCKED_RECT lr = {};
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0))) {
        for (int y2 = 0; y2 < h; y2++) {
            unsigned char* src = pixels + y2 * w * 4;
            unsigned char* dst = (unsigned char*)lr.pBits + y2 * lr.Pitch;
            for (int x2 = 0; x2 < w; x2++) {
                dst[x2*4+0] = src[x2*4+2];
                dst[x2*4+1] = src[x2*4+1];
                dst[x2*4+2] = src[x2*4+0];
                dst[x2*4+3] = src[x2*4+3];
            }
        }
        tex->UnlockRect(0);
    }

    stbi_image_free(pixels);
    WriteLogFmt("[Tex] Loaded file: %s -> %dx%d", path, w, h);
    return tex;
}

static void LoadAllTextures(IDirect3DDevice9* dev)
{
    if (g_TexturesLoaded || !dev) return;
    g_texPanelBg = LoadTextureFromResource(dev, IDR_PANEL_BG);
    g_texBtnNormal = LoadTextureFromResource(dev, IDR_BTN_NORMAL);
    g_texBtnHover = LoadTextureFromResource(dev, IDR_BTN_HOVER);
    g_texBtnPressed = LoadTextureFromResource(dev, IDR_BTN_PRESSED);
    g_texBtnDisabled = LoadTextureFromResource(dev, IDR_BTN_DISABLED);
    g_texCursorNormal = LoadTextureFromResource(dev, IDR_CURSOR_NORMAL);
    g_texCursorHoverA = LoadTextureFromResource(dev, IDR_CURSOR_HOVER_A);
    g_texCursorHoverB = LoadTextureFromResource(dev, IDR_CURSOR_HOVER_B);
    g_texCursorPressed = LoadTextureFromResource(dev, IDR_CURSOR_PRESSED);
    g_TexturesLoaded = true;
    WriteLogFmt("[Tex] loaded panel=%p btn=[%p,%p,%p,%p] cursor=[%p,%p,%p,%p]",
        g_texPanelBg, g_texBtnNormal, g_texBtnHover, g_texBtnPressed, g_texBtnDisabled,
        g_texCursorNormal, g_texCursorHoverA, g_texCursorHoverB, g_texCursorPressed);
}

static void ReleaseAllD3D9Textures(const char* reason)
{
    if (g_texPanelBg) {
        WriteLogFmt("[Tex] release panel=%p reason=%s", g_texPanelBg, reason ? reason : "unknown");
        g_texPanelBg->Release();
        g_texPanelBg = nullptr;
    }
    if (g_texBtnNormal) { g_texBtnNormal->Release(); g_texBtnNormal = nullptr; }
    if (g_texBtnHover) { g_texBtnHover->Release(); g_texBtnHover = nullptr; }
    if (g_texBtnPressed) { g_texBtnPressed->Release(); g_texBtnPressed = nullptr; }
    if (g_texBtnDisabled) { g_texBtnDisabled->Release(); g_texBtnDisabled = nullptr; }
    if (g_texCursorNormal) { g_texCursorNormal->Release(); g_texCursorNormal = nullptr; }
    if (g_texCursorHoverA) { g_texCursorHoverA->Release(); g_texCursorHoverA = nullptr; }
    if (g_texCursorHoverB) { g_texCursorHoverB->Release(); g_texCursorHoverB = nullptr; }
    if (g_texCursorPressed) { g_texCursorPressed->Release(); g_texCursorPressed = nullptr; }
    g_TexturesLoaded = false;
}

static void PrepareForD3DDeviceReset(const char* reason)
{
    WriteLogFmt("[D3D9] hard rebuild begin reason=%s overlay=%d textures=%d",
        reason ? reason : "unknown",
        SuperImGuiOverlayIsInitialized() ? 1 : 0,
        g_TexturesLoaded ? 1 : 0);

    if (Win32InputSpoofIsInstalled())
        Win32InputSpoofSetSuppressMouse(false);
    g_LastOverlaySuppressMouse = false;

    if (ENABLE_IMGUI_OVERLAY_PANEL)
        SuperImGuiOverlayOnDeviceLost();

    // Keep managed textures alive across Reset. Releasing them inside Reset has
    // proven fragile and is unnecessary for D3DPOOL_MANAGED resources.
    g_pDevice = nullptr;
}

// ============================================================================
// 自定义Draw函数（替换vtable1[11]，即byte offset +44）
// 在RenderAll的dirty list遍历中调用
// ============================================================================
static int g_DrawCallCount = 0;
static int g_UpdatePosLogCount = 0;
static int g_LastMsgID = -1;
static DWORD g_LastMsgTick = 0;

static void __fastcall SuperCWndDraw(uintptr_t thisPtr, void* /*edx_unused*/, int* clipRegion)
{
    if (!thisPtr) return;

    int drawX = g_PanelDrawX;
    int drawY = g_PanelDrawY;
    if (drawX <= -9000 || drawY <= -9000) {
        drawX = CWnd_GetX(thisPtr);
        drawY = CWnd_GetY(thisPtr);
    }

    if (!g_SuperExpanded) return;

    if (g_DrawCallCount < 20) {
        int cx = CWnd_GetX(thisPtr);
        int cy = CWnd_GetY(thisPtr);
        int rx = CWnd_GetRenderX(thisPtr);
        int ry = CWnd_GetRenderY(thisPtr);
        int w = *(int*)(thisPtr + 10*4);
        int h = *(int*)(thisPtr + 11*4);
        if (IsOfficialSecondChildObject(thisPtr, true)) {
            int refCount = *(int*)(thisPtr + CWND_OFF_REFCNT * 4);
            WriteLogFmt("[Draw] #%d officialSecond=1 ref=%d com=(%d,%d) render=(%d,%d) size=%dx%d",
                g_DrawCallCount, refCount, cx, cy, rx, ry, w, h);
        } else {
            int hx = CWnd_GetHomeX(thisPtr);
            int hy = CWnd_GetHomeY(thisPtr);
            WriteLogFmt("[Draw] #%d officialSecond=0 home=(%d,%d) com=(%d,%d) render=(%d,%d) size=%dx%d",
                g_DrawCallCount, hx, hy, cx, cy, rx, ry, w, h);
        }
        g_DrawCallCount++;
    }

    DWORD* surface = nullptr;
    __try {
        ((tGetSurface)ADDR_435A50)(thisPtr, &surface);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        static int s_surfaceExceptLog = 0;
        if (s_surfaceExceptLog < 12) {
            WriteLogFmt("[SuperWndDraw] EXCEPTION: sub_435A50 0x%08X", GetExceptionCode());
            s_surfaceExceptLog++;
        }
        return;
    }

    if (!surface) {
        static int s_surfaceNullLog = 0;
        if (s_surfaceNullLog < 12) {
            WriteLog("[SuperWndDraw] FAIL: surface null");
            s_surfaceNullLog++;
        }
        return;
    }

    static int s_superNativeDrawLogCount = 0;
    bool emitLog = (s_superNativeDrawLogCount < 40);
    if (DrawNativePanelOnSurface(surface, 0, 0, "SuperWndDraw", emitLog)) {
        if (emitLog) s_superNativeDrawLogCount++;
    }
}

// ============================================================================
// 原生按钮创建（复刻 BtMacro 模式）
//
// 证据：
//   sub_66A770 是 __thiscall, ECX = SkillWndEx+0xBEC（控件容器地址）
//   参数：(resultBuf, off_资源路径, 控件ID, X偏移, Y偏移, 0, 0, alpha=0xFF)
//   asm 009E1B67~009E1B88 直接确认
//
// 这次改动会不会影响原本稳定逻辑：不会，新增独立按钮，不覆盖任何原有偏移
// 这次新增call的证据是否足够：A级，asm直接确认调用约定和参数
// this/ecx/edx/参数/返回值是否确认：ECX=lea容器地址，7个push参数，返回值[+4]是对象
// 新增了哪些空指针和时机保护：SkillWndThis非空、+0xBEC可读
// 仍不确定需补查的数据：无
// ============================================================================
static bool CreateSuperButton(uintptr_t skillWndThis)
{
    if (!skillWndThis) return false;

    for (int i = 0; i < 5; ++i) {
        InterlockedExchange(&g_SuperBtnMetricOverrideX[i], LONG_MIN);
        InterlockedExchange(&g_SuperBtnMetricOverrideY[i], LONG_MIN);
    }

    // 控件容器 = SkillWndEx + 0xBEC（lea，不是解引用）
    uintptr_t ctrlContainer = skillWndThis + 0xBEC;

    // 安全检查：控件容器的前3个DWORD应该已被 sub_6688B0 初始化
    if (SafeIsBadReadPtr((void*)ctrlContainer, 12)) {
        WriteLog("[NativeBtn] FAIL: ctrl container unreadable");
        return false;
    }
    // 检查 ctrlContainer[0] 应该是父窗口指针（即 SkillWndThis 本身）
    DWORD parentPtr = *(DWORD*)ctrlContainer;
    if (parentPtr != (DWORD)skillWndThis) {
        WriteLogFmt("[NativeBtn] WARNING: container[0]=%08X != this=%08X, continue anyway",
            parentPtr, (DWORD)skillWndThis);
    }

    DWORD createdObj = 0;
    const unsigned short* usedPath = reinterpret_cast<const unsigned short*>(SUPER_BTN_RES_PATH);
    bool createCallOk = CreateNativeButtonInstance(
        skillWndThis,
        usedPath,
        SUPER_BTN_ID,
        BTN_X_OFFSET,
        BTN_Y_OFFSET,
        false,
        &createdObj);

    if (createCallOk) {
        WriteLogFmt("[NativeBtn] primary create OK path=%S obj=0x%08X",
            reinterpret_cast<const wchar_t*>(usedPath),
            createdObj);
    } else {
        usedPath = reinterpret_cast<const unsigned short*>(SUPER_BTN_RES_PATH_ALT);
        createCallOk = CreateNativeButtonInstance(
            skillWndThis,
            usedPath,
            SUPER_BTN_ID,
            BTN_X_OFFSET,
            BTN_Y_OFFSET,
            false,
            &createdObj);
        if (createCallOk) {
            WriteLogFmt("[NativeBtn] primary alt create OK path=%S obj=0x%08X",
                reinterpret_cast<const wchar_t*>(usedPath),
                createdObj);
        }
    }

    if (!createCallOk) {
        usedPath = reinterpret_cast<const unsigned short*>(ADDR_OFF_SkillEx_BtMacro);
        createCallOk = CreateNativeButtonInstance(
            skillWndThis,
            usedPath,
            SUPER_BTN_ID,
            BTN_X_OFFSET,
            BTN_Y_OFFSET,
            true,
            &createdObj);
        if (!createCallOk) {
            WriteLogFmt("[NativeBtn] create returned null obj path=%S",
                reinterpret_cast<const wchar_t*>(usedPath));
            return false;
        }
        WriteLogFmt("[NativeBtn] fallback BtMacro create OK path=%S obj=0x%08X",
            reinterpret_cast<const wchar_t*>(usedPath),
            createdObj);
    }

    g_SuperBtnObj = createdObj;
    if (!g_SuperBtnObj) {
        WriteLog("[NativeBtn] FAIL: resultBuf[1] is NULL");
        return false;
    }

    WriteLogFmt("[NativeBtn] OK: obj=0x%08X", (DWORD)g_SuperBtnObj);
    WriteLogFmt("[NativeBtn] basePath=%S", reinterpret_cast<const wchar_t*>(usedPath));
    LogNativeButtonCoreFields(g_SuperBtnObj, "BtnCoreCreate");

    __try {
        ((tRefreshButtonState)ADDR_5095A0)(reinterpret_cast<DWORD*>(g_SuperBtnObj), nullptr);
        WriteLogFmt("[NativeBtn] state refresh OK obj=0x%08X", (DWORD)g_SuperBtnObj);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLogFmt("[NativeBtn] state refresh EXCEPTION obj=0x%08X code=0x%08X",
            (DWORD)g_SuperBtnObj, GetExceptionCode());
    }

    if (ENABLE_SUPERBTN_D3D_BUTTON_MODE) {
        MoveNativeButtonRaw(g_SuperBtnObj, -4096, -4096, "BtnHideD3D");
        ResetSuperBtnD3DInteractionState();
    } else {
        MoveNativeButtonRaw(g_SuperBtnObj, BTN_X_OFFSET, BTN_Y_OFFSET, "BtnMoveCreate");
    }
    SeedSuperBtnMetricOverridesIfEmpty(BTN_METRIC_FALLBACK_X, BTN_METRIC_FALLBACK_Y, "BtnMetricSeedCreate");
    if (ENABLE_SUPERBTN_SELF_DRAWOBJ_PATCH) {
        PatchSuperBtnOwnDrawObjectsFromResources(g_SuperBtnObj, "create");
    }
    if (ENABLE_SUPERBTN_RUNTIME_WRAPPER_PATCH) {
        PatchSuperBtnCurrentWrapperFromResources(g_SuperBtnObj, "create");
    }

    if (!ENABLE_SUPERBTN_D3D_BUTTON_MODE && (ENABLE_SUPERBTN_STATE_DRAWOBJ_OVERRIDE || ENABLE_SUPERBTN_DRAWOBJ_AB_FALLBACK || ENABLE_SUPERBTN_NATIVE_DONOR_DRAWOBJ) &&
        (usedPath == reinterpret_cast<const unsigned short*>(SUPER_BTN_RES_PATH) ||
         usedPath == reinterpret_cast<const unsigned short*>(SUPER_BTN_RES_PATH_ALT)))
    {
        DWORD compareObj = 0;
        if (CreateNativeButtonInstance(
                skillWndThis,
                reinterpret_cast<const unsigned short*>(ADDR_OFF_SkillEx_BtMacro),
                SUPER_BTN_ID + 1,
                BTN_X_OFFSET,
                BTN_Y_OFFSET,
                false,
                &compareObj) && compareObj)
        {
            g_SuperBtnSkinDonorObj = compareObj;
            for (int i = 0; i < 5; ++i) {
                g_SuperBtnStateDonorObj[i] = 0;
                g_SuperBtnStateDonorPatched[i] = false;
            }
            DWORD compareState = 0;
            if (!SafeIsBadReadPtr((void*)(compareObj + 0x34), 4))
                compareState = *(DWORD*)(compareObj + 0x34);
            if (compareState < 5 && oButtonMetric507DF0 && oButtonMetric507ED0) {
                int mx = 0;
                int my = 0;
                __try {
                    mx = oButtonMetric507DF0(compareObj);
                    my = oButtonMetric507ED0(compareObj);
                    if (IsReasonableButtonMetric(mx) && IsReasonableButtonMetric(my)) {
                        InterlockedExchange(&g_SuperBtnMetricOverrideX[compareState], mx);
                        InterlockedExchange(&g_SuperBtnMetricOverrideY[compareState], my);
                        WriteLogFmt("[BtnMetricPrime] state=%u x=%d y=%d", compareState, mx, my);
                    } else {
                        WriteLogFmt("[BtnMetricPrimeSkip] state=%u x=%d y=%d", compareState, mx, my);
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    WriteLogFmt("[BtnMetricPrime] EXCEPTION state=%u code=0x%08X", compareState, GetExceptionCode());
                }
            }
            g_SuperBtnCompareObj = compareObj;
            if (ENABLE_DEBUG_VISIBLE_COMPARE_BUTTON) {
                MoveNativeButtonRaw(compareObj, BTN_X_OFFSET + BTN_COMPARE_DEBUG_DX, BTN_Y_OFFSET, "BtnCompareDebug");
            } else {
                MoveNativeButtonRaw(compareObj, -4096, -4096, "BtnCompareHide");
                if (!ENABLE_SUPERBTN_NATIVE_DONOR_DRAWOBJ) {
                    // compare按钮只用于取一次原版metric，隐藏后不再参与跟踪，避免离屏值持续污染缓存。
                    g_SuperBtnSkinDonorObj = 0;
                }
            }
            LogNativeButtonCoreFields(compareObj, "BtnCoreCompareBtMacro");
            if (ENABLE_SUPERBTN_NATIVE_DONOR_DRAWOBJ) {
                for (DWORD donorState = 0; donorState <= 4; ++donorState) {
                    DWORD donorObj = 0;
                    if (CreateNativeButtonInstance(
                            skillWndThis,
                            reinterpret_cast<const unsigned short*>(ADDR_OFF_SkillEx_BtMacro),
                            SUPER_BTN_ID + 1 + donorState,
                            BTN_X_OFFSET,
                            BTN_Y_OFFSET,
                            false,
                            &donorObj) && donorObj)
                    {
                        g_SuperBtnStateDonorObj[donorState] = donorObj;
                        MoveNativeButtonRaw(donorObj, -4096, -4096, "BtnDonorHide");
                        WriteLogFmt("[BtnDonorCreate] state=%u obj=0x%08X", donorState, donorObj);
                    }
                    else
                    {
                        WriteLogFmt("[BtnDonorCreate] state=%u FAILED", donorState);
                    }
                }
                if (!g_SuperBtnStateDonorObj[0]) {
                    g_SuperBtnStateDonorObj[0] = compareObj;
                    WriteLogFmt("[BtnDonorCreate] state=0 FALLBACK compare=0x%08X", compareObj);
                }
                PatchSuperBtnDonorDrawObjectsFromResources();
                // v17.6: 把所有 5 个 slot 钉成 normal donor，然后启用 stableNormal 模式。
                // 这样 hkButtonRefreshState5095A0 会拦截后续所有状态刷新（包括 hover），
                // 按钮保持 state=0，draw 链不会被破坏。
                if (g_SuperBtnObj) {
                    ForceSuperButtonAllStatesToNormalDonor(g_SuperBtnObj);
                }
                // v17.7b: slot 值已经复制到 SuperBtn，现在把 donor/compare 宽高清零
                // 防止 sub_529640 在 UI tree 遍历时为 donor 生成 -4096 矩形覆盖 SuperBtn
                for (int di = 0; di < 5; ++di) {
                    uintptr_t dObj = g_SuperBtnStateDonorObj[di];
                    if (dObj) {
                        __try {
                            if (!SafeIsBadReadPtr((void*)(dObj + 0x1C), 8)) {
                                *(DWORD*)(dObj + 0x1C) = 0;
                                *(DWORD*)(dObj + 0x20) = 0;
                            }
                        } __except (EXCEPTION_EXECUTE_HANDLER) {}
                    }
                }
                if (g_SuperBtnCompareObj) {
                    __try {
                        uintptr_t cObj = g_SuperBtnCompareObj;
                        if (!SafeIsBadReadPtr((void*)(cObj + 0x1C), 8)) {
                            *(DWORD*)(cObj + 0x1C) = 0;
                            *(DWORD*)(cObj + 0x20) = 0;
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
                WriteLog("[BtnDonorPostPatch] donor+compare wh zeroed");
            }
        }
        else
        {
            WriteLog("[BtnCoreCompareBtMacro] create FAILED");
        }
    }

    LogSuperButtonGeometry("BtnGeomCreate");

    g_NativeBtnCreated = true;
    return true;
}

// ============================================================================
// 原生子窗口创建（v12.0：改走 SkillWndEx 官方 second-child 包装链）
//
// 证据：
// 1. 9DDB30 对 3001~3004 直接走 9DC220(this, a2-750)
// 2. 9DC220 会分配 0x84，调用 9DB2B0，并把 child 存到 this+3048
// 3. CE 已证明 generic child 从未进入 SkillWnd 的 +3048 槽位，这是它和真实 child 的核心差异
// 4. 因此这次不再自己 gameMalloc+family ctor，而是直接复用官方 second-child create/replace 包装链
// ============================================================================
static uintptr_t GetSkillWndSecondChildPtr(uintptr_t skillWndThis)
{
    if (!skillWndThis || SafeIsBadReadPtr((void*)(skillWndThis + 3048), 4)) return 0;
    return *(DWORD*)(skillWndThis + 3048);
}

static bool ReleaseSkillWndSecondChild(uintptr_t skillWndThis, const char* reason)
{
    if (!skillWndThis) return false;

    uintptr_t child = GetSkillWndSecondChildPtr(skillWndThis);
    if (!child) return false;

    WriteLogFmt("[Lifecycle] releasing second child (reason=%s) ptr=0x%08X",
        reason ? reason : "unknown", (DWORD)child);

    DWORD fnClose = ADDR_B9E880;
    __try {
        __asm {
            mov ecx, [child]
            call [fnClose]
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLogFmt("[Lifecycle] EXCEPTION in second-child close: 0x%08X", GetExceptionCode());
    }

    uintptr_t childAfterClose = GetSkillWndSecondChildPtr(skillWndThis);
    if (childAfterClose) {
        DWORD fnRelease = ADDR_9D93A0;
        uintptr_t wrapPtr = skillWndThis + 3044;
        int zero = 0;
        __try {
            __asm {
                push [zero]
                mov ecx, [wrapPtr]
                call [fnRelease]
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            WriteLogFmt("[Lifecycle] EXCEPTION in second-child release: 0x%08X", GetExceptionCode());
        }
        if (!SafeIsBadReadPtr((void*)(skillWndThis + 3048), 4)) {
            *(DWORD*)(skillWndThis + 3048) = 0;
        }
    }

    return true;
}

static bool CreateSuperWnd(uintptr_t skillWndThis)
{
    if (!skillWndThis) return false;

    if (ENABLE_IMGUI_OVERLAY_PANEL) {
        if (!g_GameHwnd || !g_pDevice) {
            WriteLogFmt("[ImGuiOverlay] FAIL: hwnd/device not ready (hwnd=0x%08X device=0x%08X)", (DWORD)(uintptr_t)g_GameHwnd, (DWORD)(uintptr_t)g_pDevice);
            return false;
        }

        if (!SuperImGuiOverlayEnsureInitialized(g_GameHwnd, g_pDevice, 1.0f, IMGUI_PANEL_ASSET_PATH)) {
            WriteLog("[ImGuiOverlay] FAIL: initialization failed");
            return false;
        }

        g_SuperCWnd = 1;
        g_NativeWndCreated = true;
        g_SuperUsesSkillWndSecondSlot = false;
        SuperImGuiOverlaySetVisible(false);
        WriteLog("[ImGuiOverlay] overlay route ready");
        return true;
    }

    if (!g_SuperChildHooksReady) {
        WriteLog("[NativeWnd] FAIL: route-B child hooks not ready");
        return false;
    }

    uintptr_t existingSecond = GetSkillWndSecondChildPtr(skillWndThis);
    if (existingSecond) {
        WriteLogFmt("[NativeWnd] second-child slot busy: ptr=0x%08X, abort create", (DWORD)existingSecond);
        return false;
    }

    // Step 1: 走 SkillWnd 官方 second-child 包装链。
    // 关键修正：
    //   9DDB30 伪代码把 ctrlID 反编译成 int* a2，"a2 - 750" 是按指针步长算的，
    //   所以 3001..3004 实际映射到的有效模式是 1..4，而不是 2251..2254。
    //   之前传 2251 会让 9DB2B0 在 sub_419110(..., (char*)a2 - 1) 这条链上直接异常。
    DWORD fnCreateSlot = ADDR_9DC220;
    int mode = SUPER_CHILD_DONOR_MODE;
    __try {
        __asm {
            push [mode]
            mov ecx, [skillWndThis]
            call [fnCreateSlot]
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLogFmt("[NativeWnd] EXCEPTION in second-child wrapper 0x%08X", GetExceptionCode());
        return false;
    }

    uintptr_t wndObj = GetSkillWndSecondChildPtr(skillWndThis);
    if (!wndObj) {
        WriteLog("[NativeWnd] FAIL: second-child wrapper returned null slot");
        return false;
    }

    if (SafeIsBadReadPtr((void*)wndObj, 0x84)) {
        WriteLog("[NativeWnd] FAIL: second-child slot object unreadable");
        return false;
    }

    WriteLogFmt("[NativeWnd] official second-child OK: slot=0x%08X mode=%d", (DWORD)wndObj, mode);
    LogNativeChildSurfaceShape(wndObj, "NativeWndCtor");

    // Step 2: 算初始锚点，并尝试把官方 second-child 的壳收缩到我们的目标尺寸。
    int swX = 0, swY = 0;
    bool fromVTable = GetSkillWndAnchorPos(skillWndThis, &swX, &swY);
    if (!fromVTable) {
        swX = CWnd_GetX(skillWndThis);
        swY = CWnd_GetY(skillWndThis);
    }
    int xPos = swX - PANEL_W - PANEL_LEFT_GAP;
    int yPos = swY;
    WriteLogFmt("[NativeWnd] second-child pos (%s): sw=(%d,%d) -> x=%d y=%d",
        fromVTable ? "vtable" : "com", swX, swY, xPos, yPos);

    RebuildNativeChildSurface(wndObj, xPos, yPos, PANEL_W, PANEL_H, "NativeWndSecondSlotResize");
    LogNativeChildSurfaceShape(wndObj, "NativeWndInitSecondSlot");

    // Step 3: 同步官方 0x84 child 内部安全坐标，避免 draw/move 初期取到旧值。
    // 注意：official second-child 不是 A996B0-family，不能写 +2756/+2760 home 坐标。
    CWnd_SetRenderPos(wndObj, xPos, yPos);
    CWnd_SetComPos(wndObj, xPos, yPos);

    // Step 4: 替换 VT1 draw 槽位，接入我们的面板绘制
    if (!ApplySuperChildCustomDrawVTable(wndObj)) {
        WriteLog("[NativeWnd] FAIL: custom draw vtable install failed");
        ReleaseSkillWndSecondChild(skillWndThis, "custom_vt_fail");
        return false;
    }

    // Step 5: 再补一次 move，确保初始化后逻辑位置与我们的锚点一致
    MoveNativeChildWnd(wndObj, xPos, yPos, "NativeWndInitMove");

    // Step 6: official second-child 没有可靠 show/hide 槽位；收起时走 close+release。
    MarkSuperWndDirty(wndObj, "NativeWndInit");

    g_SuperCWnd = wndObj;
    g_NativeWndCreated = true;
    g_SuperUsesSkillWndSecondSlot = true;
    WriteLogFmt("[NativeWnd] === SUCCESS(second_child_slot): 0x%08X ===", (DWORD)wndObj);
    return true;
}

static void SetSuperWndVisible(uintptr_t wndObj, int showVal)
{
    if (ENABLE_IMGUI_OVERLAY_PANEL) {
        SuperImGuiOverlaySetVisible(showVal != 0);
        return;
    }

    if (!wndObj) return;

    if (IsOfficialSecondChildObject(wndObj, true)) {
        static int s_officialSecondVisibleNoopLogCount = 0;
        if (s_officialSecondVisibleNoopLogCount < 8) {
            WriteLogFmt("[Visible] official second-child has no reliable show/hide slot; no-op show=%d wnd=0x%08X",
                showVal, (DWORD)wndObj);
            s_officialSecondVisibleNoopLogCount++;
        }
        return;
    }

    uintptr_t thisForVT2 = wndObj + 4;  // 与sub_9E9B50一致
    if (SafeIsBadReadPtr((void*)thisForVT2, 4)) return;

    DWORD vtable2 = *(DWORD*)thisForVT2;
    if (!vtable2 || SafeIsBadReadPtr((void*)(vtable2 + 0x28), 4)) return;

    DWORD fnShow = *(DWORD*)(vtable2 + 0x28);
    DWORD fnVis  = *(DWORD*)(vtable2 + 0x20);
    DWORD ecxVal = (DWORD)thisForVT2;

    if (!fnShow || !fnVis) return;

    __try {
        __asm {
            push [showVal]
            mov ecx, [ecxVal]
            call [fnShow]
        }
        __asm {
            push [showVal]
            mov ecx, [ecxVal]
            call [fnVis]
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        WriteLogFmt("[Visible] EXCEPTION: 0x%08X", GetExceptionCode());
    }
}

static void SafeCloseSuperWnd(const char* reason)
{
    if (ENABLE_IMGUI_OVERLAY_PANEL) {
        WriteLogFmt("[Lifecycle] hiding imgui overlay (reason=%s)", reason ? reason : "unknown");
        SuperImGuiOverlaySetVisible(false);
        return;
    }

    if (!g_SuperCWnd) return;

    uintptr_t oldWnd = g_SuperCWnd;
    WriteLogFmt("[Lifecycle] closing super wnd (reason=%s) ptr=0x%08X",
        reason ? reason : "unknown", (DWORD)oldWnd);

    SetSuperWndVisible(oldWnd, 0);

    if (g_SuperUsesSkillWndSecondSlot && g_SkillWndThis) {
        uintptr_t slotChild = GetSkillWndSecondChildPtr(g_SkillWndThis);
        if (slotChild == oldWnd) {
            ReleaseSkillWndSecondChild(g_SkillWndThis, reason ? reason : "unknown");
            return;
        }
    }

    if (!SafeIsBadReadPtr((void*)oldWnd, 4)) {
        DWORD fnClose = ADDR_B9E880;
        __try {
            __asm {
                mov ecx, [oldWnd]
                call [fnClose]
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            WriteLogFmt("[Lifecycle] EXCEPTION in close: 0x%08X", GetExceptionCode());
        }
    }
}

static void DestroySuperWndOnly(const char* reason)
{
    if (ENABLE_IMGUI_OVERLAY_PANEL) {
        SetSuperWndVisible(g_SuperCWnd, 0);
        g_PanelDrawX = -9999;
        g_PanelDrawY = -9999;
        return;
    }

    if (!g_SuperCWnd) return;
    SafeCloseSuperWnd(reason);
    g_SuperCWnd = 0;
    g_NativeWndCreated = false;
    g_SuperUsesSkillWndSecondSlot = false;
    g_PanelDrawX = -9999;
    g_PanelDrawY = -9999;
}

static void ResetSuperRuntimeState(bool closeWnd, const char* reason)
{
    if (ENABLE_IMGUI_OVERLAY_PANEL) {
        if (g_IsD3D8Mode) {
            SuperD3D8OverlaySetVisible(false);
            SuperD3D8OverlayResetPanelState();
        } else {
            SuperImGuiOverlaySetVisible(false);
            SuperImGuiOverlayResetPanelState();
        }
    }

    if (closeWnd && g_SuperCWnd) {
        SafeCloseSuperWnd(reason);
    }

    g_SuperExpanded = false;
    g_LastToggleTick = 0;
    g_LastNativeMsgToggleTick = 0;
    g_LastFallbackHitLogTick = 0;
    g_LastSkillWndSeenTick = 0;
    g_PanelDrawX = -9999;
    g_PanelDrawY = -9999;

    g_SuperBtnObj = 0;
    g_SuperBtnSkinDonorObj = 0;
    g_SuperBtnCompareObj = 0;
    g_SuperBtnForcedStableNormalMode = false;
    for (int i = 0; i < 5; ++i) {
        g_SuperBtnSelfStatePatched[i] = false;
    }
    for (int i = 0; i < 5; ++i) {
        g_SuperBtnStateDonorObj[i] = 0;
        g_SuperBtnStateDonorPatched[i] = false;
        g_SuperBtnStateDonorRetryTick[i] = 0;
    }
    g_SuperCWnd = 0;
    g_NativeBtnCreated = false;
    ResetSuperBtnD3DInteractionState();
    g_NativeWndCreated = false;
    g_SuperUsesSkillWndSecondSlot = false;
}

static void OnSkillWndPointerObserved(uintptr_t observed, const char* srcTag)
{
    DWORD now = GetTickCount();
    if (observed && SafeIsBadReadPtr((void*)observed, 0x20)) {
        observed = 0;
    }

    if (observed == g_SkillWndThis) {
        if (observed) g_LastSkillWndSeenTick = now;
        return;
    }

    if (!observed) {
        if (g_SkillWndThis && g_LastSkillWndSeenTick && (now - g_LastSkillWndSeenTick) < SKILLWND_GONE_DEBOUNCE_MS) {
            return;
        }
        if (g_SkillWndThis) {
            WriteLogFmt("[Lifecycle] SkillWnd gone (src=%s old=0x%08X)",
                srcTag ? srcTag : "unknown", (DWORD)g_SkillWndThis);
            ResetSuperRuntimeState(g_SuperCWnd != 0, "skillwnd_gone");
        }
        g_SkillWndThis = 0;
        SkillOverlayBridgeSetSkillWnd(0);
        g_Ready = false;
        return;
    }

    if (g_SkillWndThis && g_SkillWndThis != observed) {
        WriteLogFmt("[Lifecycle] SkillWnd switched (src=%s old=0x%08X new=0x%08X)",
            srcTag ? srcTag : "unknown", (DWORD)g_SkillWndThis, (DWORD)observed);
        ResetSuperRuntimeState(g_SuperCWnd != 0, "skillwnd_switched");
    }

    g_SkillWndThis = observed;
    SkillOverlayBridgeSetSkillWnd(g_SkillWndThis);
    g_Ready = true;
    g_LastSkillWndSeenTick = now;
}

// ============================================================================
// 切换超级技能栏窗口显示/隐藏
// 复刻 sub_9E9B50 的逻辑
// ============================================================================
static void ToggleSuperWnd(const char* srcTag)
{
    if (!g_SkillWndThis) return;

    DWORD now = GetTickCount();
    if (now - g_LastToggleTick < 120) {
        return;
    }
    g_LastToggleTick = now;

    g_SuperExpanded = !g_SuperExpanded;
    WriteLogFmt("[Toggle:%s] expanded=%d", srcTag ? srcTag : "unknown", g_SuperExpanded);

    // D3D8 mode owns the shared ImGui panel from hkD3D8Present.
    // It does not need the D3D9 CreateSuperWnd route or a native child window.
    if (g_IsD3D8Mode) {
        WriteLogFmt("[Toggle] D3D8 mode: panel %s", g_SuperExpanded ? "ON" : "OFF");
        return;
    }

    if (g_SuperExpanded && !g_NativeWndCreated) {
        WriteLog(ENABLE_IMGUI_OVERLAY_PANEL ? "[Toggle] creating imgui overlay panel..." : "[Toggle] creating official second-slot super child...");
        if (CreateSuperWnd(g_SkillWndThis)) {
            WriteLog(ENABLE_IMGUI_OVERLAY_PANEL ? "[Toggle] imgui overlay panel ready" : "[Toggle] official second-slot super child created OK");
        } else {
            WriteLog(ENABLE_IMGUI_OVERLAY_PANEL ? "[Toggle] imgui overlay panel create FAILED" : "[Toggle] official second-slot super child create FAILED");
            g_SuperExpanded = false;
            g_PanelDrawX = -9999;
            g_PanelDrawY = -9999;
            WriteLog("[Toggle] rollback: create failed, expanded reset to 0");
            return;
        }
    }
    if (g_SuperCWnd) {
        SetSuperWndVisible(g_SuperCWnd, g_SuperExpanded ? 1 : 0);
    }
    if (g_SuperExpanded) {
        UpdateSuperCWnd();
        if (ENABLE_TOGGLE_FOCUS_SYNC) {
            SyncSkillWndActiveFocus("ToggleFocusSync", true);
        }
    }
    if (!g_SuperExpanded) {
        DestroySuperWndOnly("toggle_hide");
    }
}

static bool IsPointInRectPad(int mx, int my, int x, int y, int w, int h, int pad)
{
    return (mx >= (x - pad) && mx < (x + w + pad) &&
            my >= (y - pad) && my < (y + h + pad));
}

static bool TryToggleByMousePoint(int mx, int my, const char* srcTag)
{
    if (ENABLE_SUPERBTN_D3D_BUTTON_MODE) return false;
    if (!g_Ready || !g_NativeBtnCreated || !g_SkillWndThis) return false;

    DWORD now = GetTickCount();
    if (now - g_LastNativeMsgToggleTick < 180) {
        return false;
    }

    int bxObj = 0, byObj = 0, bwObj = 0, bhObj = 0;
    int bxCom = 0, byCom = 0, bwCom = 0, bhCom = 0;
    int bxVt = 0, byVt = 0, bwVt = 0, bhVt = 0;
    bool hasObj = GetSuperButtonScreenRect(&bxObj, &byObj, &bwObj, &bhObj);
    bool hasCom = GetExpectedButtonRectCom(&bxCom, &byCom, &bwCom, &bhCom);
    bool hasVt  = GetExpectedButtonRectVt(&bxVt, &byVt, &bwVt, &bhVt);

    // resultBuf对象上的宽高有时不可信（曾出现高度=1），这里做一次兜底过滤
    if (hasObj && (bwObj < 20 || bhObj < 10 || bwObj > 256 || bhObj > 64)) {
        hasObj = false;
    }

    const int kPad = 0;
    bool hitObj = hasObj && IsPointInRectPad(mx, my, bxObj, byObj, bwObj, bhObj, kPad);
    bool hitCom = !hitObj && hasCom && IsPointInRectPad(mx, my, bxCom, byCom, bwCom, bhCom, kPad);
    bool hitVt  = !hitObj && !hitCom && hasVt && IsPointInRectPad(mx, my, bxVt, byVt, bwVt, bhVt, kPad);
    if (!(hitObj || hitCom || hitVt)) {
        static DWORD s_lastMissLogTick = 0;
        if (now - s_lastMissLogTick > 200) {
            s_lastMissLogTick = now;
            WriteLogFmt("[BtnMiss:%s] mx=%d my=%d obj=%s(%d,%d,%d,%d) com=%s(%d,%d,%d,%d) vt=%s(%d,%d,%d,%d)",
                srcTag ? srcTag : "unknown", mx, my,
                hasObj ? "Y" : "N", bxObj, byObj, bxObj + bwObj, byObj + bhObj,
                hasCom ? "Y" : "N", bxCom, byCom, bxCom + bwCom, byCom + bhCom,
                hasVt  ? "Y" : "N", bxVt, byVt, bxVt + bwVt, byVt + bhVt);
        }
        return false;
    }

    if (now - g_LastFallbackHitLogTick > 120) {
        g_LastFallbackHitLogTick = now;
        WriteLogFmt("[BtnHit:%s] mx=%d my=%d obj=%s(%d,%d,%d,%d) com=%s(%d,%d,%d,%d) vt=%s(%d,%d,%d,%d)",
            srcTag ? srcTag : "unknown", mx, my,
            hitObj ? "HIT" : "no", bxObj, byObj, bxObj + bwObj, byObj + bhObj,
            hitCom ? "HIT" : "no", bxCom, byCom, bxCom + bwCom, byCom + bhCom,
            hitVt  ? "HIT" : "no", bxVt, byVt, bxVt + bwVt, byVt + bhVt);
    }

    // 与原生消息路径共用节流时间戳，避免同一点击在不同路径重复toggle
    g_LastNativeMsgToggleTick = now;
    ToggleSuperWnd("fallback_hit");
    return true;
}

// ============================================================================
// SkillWndEx子控件初始化Hook（sub_9E17D0）
// 策略：调用原函数后，追加创建我们的按钮和窗口
//
// sub_9E17D0: __thiscall(ecx=SkillWndEx, push a2), void, retn 4
// ============================================================================
typedef void (__thiscall *tSkillWndInitChildren)(uintptr_t thisptr, int** a2);
static tSkillWndInitChildren oSkillWndInitChildren = nullptr;

static void __cdecl hkSkillWndPostInit(uintptr_t skillWndThis)
{
    OnSkillWndPointerObserved(skillWndThis, "hook_postinit");
    WriteLogFmt("[Hook] SkillWndEx captured: 0x%08X", (DWORD)g_SkillWndThis);

    // 创建原生按钮
    if (!g_NativeBtnCreated) {
        WriteLog("[Hook] Creating native button...");
        if (CreateSuperButton(g_SkillWndThis)) {
            WriteLog("[Hook] Native button created OK");
        } else {
            WriteLog("[Hook] Native button creation FAILED");
        }
    }

    if (!g_NativeWndCreated) {
        WriteLog("[Hook] Super child deferred: create on first toggle");
    }
}

// naked thunk: 保存ecx→调用原函数→调用post-init→恢复栈→ret
__declspec(naked) static void hkSkillWndInitChildren()
{
    __asm {
        // 保存寄存器
        push ebp
        mov ebp, esp
        push esi
        push edi
        mov esi, ecx          // esi = SkillWndEx this

        // 调用原函数：__thiscall(ecx=this, push a2), retn 4
        // a2 在 [ebp+8] (因为我们push了ebp，原来的[esp+4]变成[ebp+8])
        mov eax, [ebp + 8]    // a2
        push eax
        mov ecx, esi
        call [oSkillWndInitChildren]

        // 调用post-init回调（__cdecl, push this）
        push esi
        call hkSkillWndPostInit
        add esp, 4

        // 恢复并返回（原函数是 retn 4，我们也要 retn 4）
        pop edi
        pop esi
        pop ebp
        ret 4
    }
}

// ============================================================================
// 消息处理Hook（sub_9DDB30）
// __thiscall(ecx=SkillWndEx, push ctrlID), void, retn 4
// ============================================================================
typedef void (__thiscall *tSkillWndMsg)(uintptr_t thisptr, int ctrlID);
static tSkillWndMsg oSkillWndMsg = nullptr;

static void __cdecl hkMsgHandler(uintptr_t thisPtr, int ctrlID)
{
    DWORD now = GetTickCount();
    if (ctrlID != g_LastMsgID || (now - g_LastMsgTick) > 400) {
        WriteLogFmt("[Msg] ctrlID=0x%X this=0x%08X", ctrlID, (DWORD)thisPtr);
        g_LastMsgID = ctrlID;
        g_LastMsgTick = now;
    }

    if ((DWORD)ctrlID == SUPER_BTN_ID) {
        WriteLogFmt("[Msg] Super button clicked (ID=0x%X)", ctrlID);
        // WndProc fallback 可能已经在同一点击里先切过一次；这里直接复用节流时间戳，避免二次翻转
        bool skipToggle = ((now - g_LastNativeMsgToggleTick) < 180) || ((now - g_LastToggleTick) < 120);
        if (skipToggle) {
            if (now - g_LastNativeMsgSkipTick > 200) {
                g_LastNativeMsgSkipTick = now;
                WriteLog("[Msg] Super button native toggle skipped (already handled by fallback)");
            }
        } else {
            g_LastNativeMsgToggleTick = now;
            ToggleSuperWnd("native_msg");
        }

        // 调用 sub_A99550 消费消息（__thiscall, ecx=SkillWndEx, push ctrlID）
        DWORD fnConsume = ADDR_A99550;
        uintptr_t thisVal = thisPtr;
        int id = ctrlID;
        __asm {
            push [id]
            mov ecx, [thisVal]
            call [fnConsume]
        }
        return;
    }

    // 其他消息交给原函数
    DWORD fnOrig = (DWORD)oSkillWndMsg;
    uintptr_t thisVal = thisPtr;
    int idVal = ctrlID;
    __asm {
        push [idVal]
        mov ecx, [thisVal]
        call [fnOrig]
    }
}

__declspec(naked) static void hkSkillWndMsgNaked()
{
    __asm {
        // sub_9ECFD0: ecx=this, [esp+4]=ctrlID, retn 4
        mov eax, [esp + 4]    // ctrlID
        push eax
        push ecx              // this
        call hkMsgHandler
        add esp, 8
        ret 4
    }
}

// ============================================================================
// SkillWndEx 移动 hook（sub_9D95A0）
// 证据：
//   9D95A0 是 SkillWndEx 父窗移动时，原生同步 Macro child 的入口
//   原函数最终调用 sub_56D630(child, parentX+174, parentY)
// ============================================================================
typedef LONG (__thiscall *tSkillWndMove)(uintptr_t thisptr, int a2, int a3);
static tSkillWndMove oSkillWndMove = nullptr;

static LONG __cdecl hkSkillWndMoveHandler(uintptr_t thisPtr, int a2, int a3)
{
    LONG ret = oSkillWndMove ? oSkillWndMove(thisPtr, a2, a3) : 0;
    if (thisPtr == g_SkillWndThis && g_SuperExpanded && g_SuperCWnd) {
        MoveSuperChildBySkillAnchor("MoveHookDirect", false);
        if (ENABLE_MOVE_FOCUS_SYNC) {
            SyncSkillWndActiveFocus("MoveFocusSync");
        }
    }
    return ret;
}

__declspec(naked) static void hkSkillWndMoveNaked()
{
    __asm {
        mov eax, [esp + 8]
        mov edx, [esp + 4]
        push eax
        push edx
        push ecx
        call hkSkillWndMoveHandler
        add esp, 12
        ret 8
    }
}

// ============================================================================
// SkillWndEx refresh hook（sub_9E1770）
// 证据：
//   9E1770 是 SkillWndEx 刷新 helper，末尾会 B9A5D0(0)
//   用它做“父窗静止但内部刷新后”的兜底位置同步
// ============================================================================
typedef int (__thiscall *tSkillWndRefresh)(uintptr_t thisptr);
static tSkillWndRefresh oSkillWndRefresh = nullptr;

static int __cdecl hkSkillWndRefreshHandler(uintptr_t thisPtr)
{
    int ret = oSkillWndRefresh ? oSkillWndRefresh(thisPtr) : 0;
    if (thisPtr == g_SkillWndThis && g_SuperExpanded && g_SuperCWnd) {
        if (ENABLE_REFRESH_NATIVE_CHILD_UPDATE) {
            UpdateSuperCWnd();
        }
    }
    return ret;
}

__declspec(naked) static void hkSkillWndRefreshNaked()
{
    __asm {
        push ecx
        call hkSkillWndRefreshHandler
        add esp, 4
        ret
    }
}

// ============================================================================
// SkillWndEx 绘制 Hook（sub_9DEE30）
// 证据：
//   sub_9DEE30 是 __thiscall(ecx=this, push clipRegion), retn 4
//   asm 009DEE63~009DEE69 直接确认 ecx=this, [esp+4]=clip 参数
//   原函数一开始先 sub_B9B800(a2)，然后在同一帧继续绘制 SkillWnd 内容
//
// 这次改动会不会影响原本稳定逻辑：只在 SkillWnd 原函数返回后追加画我们自己的扩展层
// 这次新增 call 的证据是否足够：A级，asm/pseudo 直接确认 9DEE30 调用约定和时机
// this / ecx / edx / 参数 / 返回值 是否确认：确认，返回 int，retn 4
// 这次新增了哪些空指针和时机保护：仅当 this==当前 SkillWnd、expanded=1、device/texture 可用时绘制
// 目前仍不确定、需要我补查的数据：技能栏上游 hit-test/hover 入口仍需继续补
// ============================================================================
typedef int (__thiscall *tSkillWndDraw)(uintptr_t thisptr, int clipRegion);
static tSkillWndDraw oSkillWndDraw = nullptr;

static bool DrawSuperPanelNativeBackgrnd(uintptr_t skillWndThis)
{
    if (!skillWndThis || !g_SuperExpanded) return false;
    if (g_SuperChildHooksReady) return false; // v11.1: route-B 是主路线，创建失败时 fail-closed，不再偷偷退回 fallback surface draw
    if (g_NativeWndCreated && g_SuperCWnd) return false;

    UpdateSuperCWnd();
    if (g_PanelDrawX <= -9000 || g_PanelDrawY <= -9000) return false;

    DWORD* surface = nullptr;
    __try {
        ((tGetSurface)ADDR_435A50)(skillWndThis, &surface);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLog("[SkillDrawNative] EXCEPTION: sub_435A50");
        return false;
    }
    if (!surface) {
        WriteLog("[SkillDrawNative] FAIL: surface null");
        return false;
    }

    int skillComX = 0, skillComY = 0;
    bool hasSkillCom = GetSkillWndComPos(skillWndThis, &skillComX, &skillComY);
    int localX = g_PanelDrawX;
    int localY = g_PanelDrawY;
    if (hasSkillCom) {
        localX = g_PanelDrawX - skillComX;
        localY = g_PanelDrawY - skillComY;
    }

    bool ok = DrawNativePanelOnSurface(surface, localX, localY, "SkillDrawNative", false);

    static int s_nativeDrawLogCount = 0;
    if (s_nativeDrawLogCount < 60) {
        WriteLogFmt("[SkillDrawNative] %s screen=(%d,%d) local=(%d,%d) skillCom=%s(%d,%d) surface=0x%08X",
            ok ? "OK" : "FAIL",
            g_PanelDrawX, g_PanelDrawY,
            localX, localY,
            hasSkillCom ? "Y" : "N", skillComX, skillComY,
            (DWORD)surface);
        s_nativeDrawLogCount++;
    }
    return ok;
}

static void DrawSuperPanelInSkillWnd(uintptr_t skillWndThis)
{
    if (ENABLE_IMGUI_OVERLAY_PANEL) return;
    if (!skillWndThis || !g_SuperExpanded) return;

    bool nativeOk = DrawSuperPanelNativeBackgrnd(skillWndThis);

    static int s_drawHookLogCount = 0;
    if (s_drawHookLogCount < 40) {
        WriteLogFmt("[SkillDraw] native=%d panel=(%d,%d) skill=0x%08X",
            nativeOk ? 1 : 0, g_PanelDrawX, g_PanelDrawY, (DWORD)skillWndThis);
        s_drawHookLogCount++;
    }
}

static int __cdecl hkSkillWndDrawHandler(uintptr_t thisPtr, int clipRegion)
{
    int ret = 0;
    SkillOverlayBridgeFilterNativeSkillWindow(thisPtr);
    if (oSkillWndDraw) {
        ret = oSkillWndDraw(thisPtr, clipRegion);
    }

    OnSkillWndPointerObserved(thisPtr, "skill_draw");

    // v17.6b diag: 确认 draw handler 是否持续被调，以及 thisPtr 是否匹配
    {
        static LONG s_drawHandlerCallCount = 0;
        LONG count = InterlockedIncrement(&s_drawHandlerCallCount);
        if (count <= 20 || (count % 500 == 0)) {
            WriteLogFmt("[DrawHandler] #%d this=0x%08X g_this=0x%08X match=%d btn=0x%08X created=%d",
                (int)count, (DWORD)thisPtr, (DWORD)g_SkillWndThis,
                thisPtr == g_SkillWndThis ? 1 : 0,
                (DWORD)g_SuperBtnObj, g_NativeBtnCreated ? 1 : 0);
        }
    }

    if (thisPtr == g_SkillWndThis) {
        if (g_NativeBtnCreated && g_SuperBtnObj && !ENABLE_SUPERBTN_D3D_BUTTON_MODE) {
            MoveSuperButtonToExpectedPos("BtnMoveDraw");
        }
        DrawSuperButtonTextureInSkillWndDraw(thisPtr);
        DrawSuperPanelInSkillWnd(thisPtr);
    }
    return ret;
}

__declspec(naked) static void hkSkillWndDrawNaked()
{
    __asm {
        mov eax, [esp + 4]
        push eax
        push ecx
        call hkSkillWndDrawHandler
        add esp, 8
        ret 4
    }
}

__declspec(naked) static void hkPostB9F6E0DrawNaked()
{
    __asm {
        pushfd
        pushad
        call DrawPostB9F6E0NativeTimingTest
        popad
        popfd
        mov eax, oPostB9F6E0DrawContinue
        jmp eax
    }
}

// ============================================================================
// 技能列表构建过滤 Hook（sub_7DD420 LABEL_42 入口 0x007DD67D）
// 证据：
//   sub_7DD420 是 __stdcall, retn 14h — 技能列表构建的核心函数
//   0x007DD67D 是 LABEL_42: 技能通过所有检查后、即将被加入 entries 数组的入口
//   此时 ebp = 技能数据指针, [ebp+0] = skillId
//   跳过时跳到 0x007DD6E8 (loop continue)
//   正常继续时执行原指令: mov eax,[esp+20h]; mov esi,[ebx+8] 然后跳到 0x007DD684
//
// 这次改动会不会影响原本稳定逻辑：不会，只在技能加入列表前做一次 skillId 检查
// 这次新增 call 的证据是否足够：A级，asm 直接确认 ebp=[skillData], [ebp+0]=skillId
// this / ecx / edx / 参数 / 返回值 是否确认：不涉及 call，只检查寄存器
// 这次新增了哪些空指针和时机保护：ebp 检查
// 目前仍不确定、需要补查的数据：无
// ============================================================================
static void* oSkillListBuildContinue = nullptr;  // trampoline (原 7 字节: mov eax,[esp+20h]; mov esi,[ebx+8])

// C function called from naked hook — must be __cdecl, preserves no state
static int __cdecl CheckHideSkillFromNativeList(int skillId)
{
    return SkillOverlayBridgeShouldHideFromNativeList(skillId) ? 1 : 0;
}

// Storage for the indirect jmp target
static DWORD s_skipSkillAddr = ADDR_7DD6E8;

__declspec(naked) static void hkSkillListBuildFilterNaked()
{
    __asm {
        // At this point: ebp = skill data ptr, [ebp+0] = skillId
        // Save all registers we'll use
        push eax
        push ecx
        push edx

        // Call our C check function with [ebp+0] as argument
        mov eax, [ebp]
        push eax
        call CheckHideSkillFromNativeList
        add esp, 4
        test eax, eax

        // Restore registers
        pop edx
        pop ecx
        pop eax

        jnz skip_skill

        // Normal path: execute the original 7 bytes and continue
        jmp [oSkillListBuildContinue]

    skip_skill:
        // Skip this skill: jump to loop continue at 0x007DD6E8
        jmp dword ptr [s_skipSkillAddr]
    }
}

static bool SetupSkillListBuildFilterHook()
{
    // Hook at 0x007DD67D, need to copy 7 bytes:
    //   007DD67D: 8B 44 24 20   mov eax, [esp+20h]    (4 bytes)
    //   007DD681: 8B 73 08      mov esi, [ebx+8]       (3 bytes)
    oSkillListBuildContinue = GenericInlineHook5(
        (BYTE*)ADDR_7DD67D, (void*)hkSkillListBuildFilterNaked, 7);
    if (!oSkillListBuildContinue)
    {
        WriteLog("[SkillListFilter] Hook failed at 7DD67D");
        return false;
    }
    WriteLogFmt("[SkillListFilter] OK: tramp=0x%08X", (DWORD)oSkillListBuildContinue);
    return true;
}

// ============================================================================
// SkillWndEx 析构 Hook（sub_9E14D0）
// 证据：
//   sub_9E14D0 是 __thiscall(ecx=this), retn
//   asm 009E14F4 显示 esi=this，末尾是普通 retn；伪代码明确清理 MacroWnd 链和 dword_F6A0C0
//
// 这次改动会不会影响原本稳定逻辑：不会修改游戏析构顺序，只在调用原析构前清我们自己的外部状态
// 这次新增 call 的证据是否足够：A级，伪代码+asm 确认析构职责与调用约定
// this / ecx / edx / 参数 / 返回值 是否确认：确认，只有 this
// 这次新增了哪些空指针和时机保护：仅当 this==当前 SkillWnd 时才清理
// 目前仍不确定、需要我补查的数据：无
// ============================================================================
typedef int (__thiscall *tSkillWndDtor)(uintptr_t thisptr);
static tSkillWndDtor oSkillWndDtor = nullptr;

static int __cdecl hkSkillWndDtorHandler(uintptr_t thisPtr)
{
    if (thisPtr && thisPtr == g_SkillWndThis) {
        WriteLogFmt("[Lifecycle] SkillWnd dtor: this=0x%08X", (DWORD)thisPtr);
        ResetSuperRuntimeState(g_SuperCWnd != 0, "skillwnd_dtor");
        g_SkillWndThis = 0;
        g_Ready = false;
    }

    if (oSkillWndDtor) {
        return oSkillWndDtor(thisPtr);
    }
    return 0;
}

__declspec(naked) static void hkSkillWndDtorNaked()
{
    __asm {
        push ecx
        call hkSkillWndDtorHandler
        add esp, 4
        ret
    }
}

// ============================================================================
// 通用发包 Hook：在已知代理技能发包后，把 skillId 改写成自定义技能
// 入口证据：0043D94D，栈参数 [esp+4]=packetData, [esp+8]=packetLen
// ============================================================================
static void* oSendPacket = nullptr;
static DWORD g_SendPacketOriginalCallTarget = 0;
static void* oSkillReleaseClassifierRoot = nullptr;
static void* oSkillReleaseClassifier = nullptr;
static void* oSkillReleaseClassifierB2F370 = nullptr;
typedef BOOL (__cdecl *tSkillNativeIdGateFn)(int skillId);
static tSkillNativeIdGateFn oSkillNativeIdGate7CE790 = nullptr;
static tSkillNativeIdGateFn oSkillNativeIdGate7D0000 = nullptr;
typedef int (__thiscall *tNativeGlyphLookupFn)(void* fontCache, unsigned int codepoint, RECT* outRectOrNull);
static tNativeGlyphLookupFn oNativeGlyphLookup = nullptr;
typedef int (__thiscall *tSkillLevelBaseFn)(void* thisPtr, DWORD playerObj, int skillId, void* cachePtr);
typedef int (__thiscall *tSkillLevelCurrentFn)(void* thisPtr, DWORD playerObj, int skillId, void* cachePtr, int flags);
static tSkillLevelBaseFn oSkillLevelBase = nullptr;
static tSkillLevelCurrentFn oSkillLevelCurrent = nullptr;
typedef void (__thiscall *tSkillPresentationDispatch)(void* thisPtr, int* skillData, int a3, int a4, int a5, int a6, int a7);
static tSkillPresentationDispatch oSkillPresentationDispatch = nullptr;
static volatile DWORD g_ClassifierOverrideSkillId = 0;
static volatile DWORD g_ForcedNativeReleaseJump = 0;

static void __cdecl hkSendPacketInspect(void* packetData, int packetLen, uintptr_t callerRetAddr)
{
    SkillOverlayBridgeInspectOutgoingPacket(packetData, packetLen, callerRetAddr);
}

__declspec(naked) static void hkSendPacketNaked()
{
    __asm {
        pushad
        mov edx, [esp + 32]
        mov eax, [esp + 40]
        mov ecx, [esp + 36]
        push edx
        push eax
        push ecx
        call hkSendPacketInspect
        add esp, 12
        popad
        call dword ptr [g_SendPacketOriginalCallTarget]
        jmp [oSendPacket]
    }
}

static void __cdecl hkSkillReleaseClassifierDispatch(int skillId)
{
    g_ForcedNativeReleaseJump = SkillOverlayBridgeResolveNativeReleaseJumpTarget(skillId);
}

static void __cdecl hkSkillReleaseClassifierRootDispatch(int skillId)
{
    g_ClassifierOverrideSkillId = (DWORD)SkillOverlayBridgeResolveNativeClassifierOverrideSkillId(skillId);
}

static void __cdecl hkSkillReleaseClassifierB2F370Dispatch(int skillId)
{
    const int overrideSkillId = SkillOverlayBridgeResolveNativeClassifierOverrideSkillId(skillId);
    g_ClassifierOverrideSkillId = (DWORD)overrideSkillId;
    if (overrideSkillId > 0 && overrideSkillId != skillId)
    {
        WriteLogFmt("[SkillReleaseHook] B2F370 override skillId=%d -> %d", skillId, overrideSkillId);
    }
}

static BOOL __cdecl hkSkillNativeIdGate7CE790(int skillId)
{
    const int mappedSkillId = SkillOverlayBridgeResolveNativeGateSkillId(skillId);
    const BOOL result = oSkillNativeIdGate7CE790
        ? oSkillNativeIdGate7CE790(mappedSkillId)
        : FALSE;
    if (mappedSkillId != skillId)
    {
        WriteLogFmt("[SkillGate] 7CE790 map custom=%d donor=%d result=%d",
            skillId, mappedSkillId, result ? 1 : 0);
    }
    return result;
}

static int __fastcall hkNativeGlyphLookup(void* thisPtr, void* /*edxUnused*/, unsigned int codepoint, RECT* outRectOrNull)
{
    if (thisPtr && codepoint > 0 && codepoint <= 0xFFFF)
        RetroSkillDWriteObserveGlyphLookup(thisPtr, codepoint);

    if (!oNativeGlyphLookup)
        return 0;

    return oNativeGlyphLookup(thisPtr, codepoint, outRectOrNull);
}

static BOOL __cdecl hkSkillNativeIdGate7D0000(int skillId)
{
    const int mappedSkillId = SkillOverlayBridgeResolveNativeGateSkillId(skillId);
    const BOOL result = oSkillNativeIdGate7D0000
        ? oSkillNativeIdGate7D0000(mappedSkillId)
        : FALSE;
    if (mappedSkillId != skillId)
    {
        WriteLogFmt("[SkillGate] 7D0000 map custom=%d donor=%d result=%d",
            skillId, mappedSkillId, result ? 1 : 0);
    }
    return result;
}

static int __fastcall hkSkillLevelBase(void* thisPtr, void* /*edxUnused*/, DWORD playerObj, int skillId, void* cachePtr)
{
    SkillOverlayBridgeObserveLevelQueryContext(thisPtr, playerObj);
    int lookupSkillId = SkillOverlayBridgeResolveNativeLevelLookupSkillId(skillId);
    if (lookupSkillId > 0)
    {
        const uintptr_t remappedEntry = SkillOverlayBridgeLookupSkillEntryPointer(lookupSkillId);
        if (!remappedEntry && lookupSkillId != skillId)
        {
            static DWORD s_lastBaseFallbackLogTick = 0;
            const DWORD nowTick = GetTickCount();
            if (nowTick - s_lastBaseFallbackLogTick > 1000)
            {
                s_lastBaseFallbackLogTick = nowTick;
                WriteLogFmt("[SkillLevelHook] 7DA7D0 fallback remap=%d -> donor=%d (entry missing)",
                    lookupSkillId, skillId);
            }
            lookupSkillId = skillId;
        }
    }

    int result = 0;
    if (oSkillLevelBase)
    {
        __try
        {
            result = oSkillLevelBase(thisPtr, playerObj, lookupSkillId, cachePtr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            static DWORD s_lastBaseExceptionLogTick = 0;
            const DWORD nowTick = GetTickCount();
            if (nowTick - s_lastBaseExceptionLogTick > 1000)
            {
                s_lastBaseExceptionLogTick = nowTick;
                WriteLogFmt("[SkillLevelHook] 7DA7D0 EXCEPTION query=%d lookup=%d player=0x%08X cache=0x%08X code=0x%08X",
                    skillId,
                    lookupSkillId,
                    playerObj,
                    (DWORD)(uintptr_t)cachePtr,
                    GetExceptionCode());
            }
            result = 0;
        }
    }
    if (lookupSkillId != skillId)
    {
        WriteLogFmt("[SkillLevelHook] 7DA7D0 query=%d -> %d result=%d",
            skillId, lookupSkillId, result);
    }
    SkillOverlayBridgeObserveLevelResult(lookupSkillId, result, true);
    return result;
}

static int __fastcall hkSkillLevelCurrent(void* thisPtr, void* /*edxUnused*/, DWORD playerObj, int skillId, void* cachePtr, int flags)
{
    SkillOverlayBridgeObserveLevelQueryContext(thisPtr, playerObj);
    int lookupSkillId = SkillOverlayBridgeResolveNativeLevelLookupSkillId(skillId);
    if (lookupSkillId > 0)
    {
        const uintptr_t remappedEntry = SkillOverlayBridgeLookupSkillEntryPointer(lookupSkillId);
        if (!remappedEntry && lookupSkillId != skillId)
        {
            static DWORD s_lastCurrentFallbackLogTick = 0;
            const DWORD nowTick = GetTickCount();
            if (nowTick - s_lastCurrentFallbackLogTick > 1000)
            {
                s_lastCurrentFallbackLogTick = nowTick;
                WriteLogFmt("[SkillLevelHook] 7DBC50 fallback remap=%d -> donor=%d (entry missing)",
                    lookupSkillId, skillId);
            }
            lookupSkillId = skillId;
        }
    }

    int result = 0;
    if (oSkillLevelCurrent)
    {
        __try
        {
            result = oSkillLevelCurrent(thisPtr, playerObj, lookupSkillId, cachePtr, flags);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            static DWORD s_lastCurrentExceptionLogTick = 0;
            const DWORD nowTick = GetTickCount();
            if (nowTick - s_lastCurrentExceptionLogTick > 1000)
            {
                s_lastCurrentExceptionLogTick = nowTick;
                WriteLogFmt("[SkillLevelHook] 7DBC50 EXCEPTION query=%d lookup=%d player=0x%08X cache=0x%08X flags=%d code=0x%08X",
                    skillId,
                    lookupSkillId,
                    playerObj,
                    (DWORD)(uintptr_t)cachePtr,
                    flags,
                    GetExceptionCode());
            }
            result = 0;
        }
    }
    if (lookupSkillId != skillId)
    {
        WriteLogFmt("[SkillLevelHook] 7DBC50 query=%d -> %d flags=%d result=%d",
            skillId, lookupSkillId, flags, result);
    }
    SkillOverlayBridgeObserveLevelResult(lookupSkillId, result, false);
    return result;
}

__declspec(naked) static void hkSkillReleaseClassifierRootNaked()
{
    __asm {
        pushad
        push esi
        call hkSkillReleaseClassifierRootDispatch
        add esp, 4
        popad

        mov eax, dword ptr [g_ClassifierOverrideSkillId]
        test eax, eax
        je continue_original
        mov esi, eax
        mov dword ptr [g_ClassifierOverrideSkillId], 0

    continue_original:
        jmp [oSkillReleaseClassifierRoot]
    }
}

__declspec(naked) static void hkSkillReleaseClassifierNaked()
{
    __asm {
        pushad
        push esi
        call hkSkillReleaseClassifierDispatch
        add esp, 4
        popad

        mov eax, dword ptr [g_ForcedNativeReleaseJump]
        test eax, eax
        jne force_jump
        jmp [oSkillReleaseClassifier]

    force_jump:
        mov dword ptr [g_ForcedNativeReleaseJump], 0
        jmp eax
    }
}

// sub_B2F370 是独立函数入口：这里只改它的 arg0(skillId)，不做“跨函数 jump”。
__declspec(naked) static void hkSkillReleaseClassifierB2F370Naked()
{
    __asm {
        pushad
        mov ecx, [esp + 12]      // pushad 保存的原始 ESP
        mov ecx, [ecx + 4]       // arg0 = skillId
        push ecx
        call hkSkillReleaseClassifierB2F370Dispatch
        add esp, 4
        popad

        mov eax, dword ptr [g_ClassifierOverrideSkillId]
        test eax, eax
        je continue_original
        mov dword ptr [esp + 4], eax
        mov dword ptr [g_ClassifierOverrideSkillId], 0

    continue_original:
        jmp [oSkillReleaseClassifierB2F370]
    }
}

static void __fastcall hkSkillPresentationDispatch(void* thisPtr, void* /*edxUnused*/, int* skillData, int a3, int a4, int a5, int a6, int a7)
{
    int originalSkillId = 0;
    int desiredSkillId = 0;
    int patchedFromSkillId = 0;
    bool patchedSkillId = false;
    bool patchedSkillDataPtr = false;
    bool keepOverrideAfterDispatch = false;
    static const int kSkillDataScanSlots = 12;
    int patchedSlots[kSkillDataScanSlots] = {};
    int patchedSlotCount = 0;
    int* originalSkillDataPtr = skillData;

    if (skillData)
    {
        __try
        {
            originalSkillId = *skillData;
            desiredSkillId = SkillOverlayBridgeResolveNativePresentationDesiredSkillId(originalSkillId);
            if (desiredSkillId > 0)
            {
                const uintptr_t desiredSkillDataPtr = SkillOverlayBridgeLookupSkillEntryPointer(desiredSkillId);
                if (desiredSkillDataPtr && desiredSkillDataPtr != (uintptr_t)skillData)
                {
                    skillData = (int*)desiredSkillDataPtr;
                    patchedSkillDataPtr = true;
                    WriteLogFmt("[SkillVisual] swap skillData observed=%d desired=%d ptr=0x%08X->0x%08X via ABAF70",
                        originalSkillId,
                        desiredSkillId,
                        (DWORD)(uintptr_t)originalSkillDataPtr,
                        (DWORD)desiredSkillDataPtr);
                }
                else if (!desiredSkillDataPtr)
                {
                    static DWORD s_lastMissingSkillEntryLogTick = 0;
                    const DWORD nowTick = GetTickCount();
                    if (nowTick - s_lastMissingSkillEntryLogTick > 1000)
                    {
                        s_lastMissingSkillEntryLogTick = nowTick;
                        WriteLogFmt("[SkillVisual] WARN: desired skillEntry missing skillId=%d observed=%d via ABAF70",
                            desiredSkillId,
                            originalSkillId);
                    }
                }
            }

            if (skillData && desiredSkillId > 0)
            {
                patchedFromSkillId = *skillData;
                if (patchedFromSkillId != desiredSkillId)
                {
                    // 一些技能分支会在同一 skillData 结构中多处读取 skillId；统一替换可减少 donor 残留。
                    for (int i = 0; i < kSkillDataScanSlots; ++i)
                    {
                        if (skillData[i] == patchedFromSkillId)
                        {
                            skillData[i] = desiredSkillId;
                            patchedSlots[patchedSlotCount++] = i;
                        }
                    }

                    if (patchedSlotCount <= 0)
                    {
                        *skillData = desiredSkillId;
                        patchedSlots[patchedSlotCount++] = 0;
                    }

                    patchedSkillId = true;
                    keepOverrideAfterDispatch = SkillOverlayBridgeShouldKeepPresentationOverrideAfterDispatch(
                        originalSkillId,
                        desiredSkillId);
                    WriteLogFmt("[SkillVisual] override visual observed=%d visual=%d via ABAF70 patchedSlots=%d keep=%d ptrSwap=%d",
                        patchedFromSkillId,
                        desiredSkillId,
                        patchedSlotCount,
                        keepOverrideAfterDispatch ? 1 : 0,
                        patchedSkillDataPtr ? 1 : 0);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            patchedSkillId = false;
        }
    }

    if (oSkillPresentationDispatch)
        oSkillPresentationDispatch(thisPtr, skillData, a3, a4, a5, a6, a7);

    // 始终恢复：skillData 指向的是游戏内部共享结构体，
    // 修改后不恢复会污染后续所有使用该 entry 的代码（包括原生技能释放的特效）。
    if (patchedSkillId && skillData)
    {
        __try
        {
            for (int i = 0; i < patchedSlotCount; ++i)
                skillData[patchedSlots[i]] = patchedFromSkillId;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }
}

// ============================================================================
// 每帧刷新面板锚点（v8.0：只维护 SkillWnd 原生扩展层的屏幕坐标）
// ============================================================================
static void UpdateSuperCWnd()
{
    if (!g_SkillWndThis || !g_SuperExpanded) {
        g_PanelDrawX = -9999;
        g_PanelDrawY = -9999;
        return;
    }

    static int s_lastPanelX = 0x7FFFFFFF;
    static int s_lastPanelY = 0x7FFFFFFF;
    static int s_missPosLogCount = 0;

    int panelX = -9999, panelY = -9999;
    const char* src = "none";
    if (!ComputeSuperPanelPos(&panelX, &panelY, &src)) {
        if (s_missPosLogCount < 16) {
            WriteLog("[UpdatePos] FAIL: ComputeSuperPanelPos");
            s_missPosLogCount++;
        }
        return;
    }

    g_PanelDrawX = panelX;
    g_PanelDrawY = panelY;

    if (ENABLE_IMGUI_OVERLAY_PANEL) {
        if (g_IsD3D8Mode)
            SuperD3D8OverlaySetAnchor(g_PanelDrawX, g_PanelDrawY);
        else
            SuperImGuiOverlaySetAnchor(g_PanelDrawX, g_PanelDrawY);
        if ((g_PanelDrawX != s_lastPanelX || g_PanelDrawY != s_lastPanelY) && g_UpdatePosLogCount < 200) {
            int swComX = 0, swComY = 0, swVtX = 0, swVtY = 0;
            bool hasCom = GetSkillWndComPos(g_SkillWndThis, &swComX, &swComY);
            bool hasVt = GetSkillWndAnchorPos(g_SkillWndThis, &swVtX, &swVtY);
            WriteLogFmt("[UpdatePos] src=%s panel=(%d,%d) swCom=%s(%d,%d) swVt=%s(%d,%d) overlay=%s",
                src, g_PanelDrawX, g_PanelDrawY,
                hasCom ? "Y" : "N", swComX, swComY,
                hasVt ? "Y" : "N", swVtX, swVtY,
                g_IsD3D8Mode ? "imgui_d3d8" : "imgui_d3d9");
            g_UpdatePosLogCount++;
        }
        s_lastPanelX = g_PanelDrawX;
        s_lastPanelY = g_PanelDrawY;
        return;
    }

    if (g_NativeWndCreated && g_SuperCWnd) {
        int vtX = 0, vtY = 0;
        if (GetSkillWndAnchorPos(g_SkillWndThis, &vtX, &vtY)) {
            g_PanelDrawX = vtX - PANEL_W + SUPER_CHILD_VT_DELTA_X;
            g_PanelDrawY = vtY + SUPER_CHILD_VT_DELTA_Y;
            src = "skill_vt_child";
        } else {
            g_PanelDrawX += SUPER_CHILD_OFFSET_X;
            g_PanelDrawY += SUPER_CHILD_OFFSET_Y;
        }
    }

    if (g_SuperCWnd && !SafeIsBadReadPtr((void*)g_SuperCWnd, 0x30)) {
        SetSuperWndVisible(g_SuperCWnd, 1);
        MoveNativeChildWnd(g_SuperCWnd, g_PanelDrawX, g_PanelDrawY, "UpdatePosMove");
        MarkSuperWndDirty(g_SuperCWnd, "UpdatePosDirty");
    }

    if ((g_PanelDrawX != s_lastPanelX || g_PanelDrawY != s_lastPanelY) && g_UpdatePosLogCount < 200) {
        int swComX = 0, swComY = 0, swVtX = 0, swVtY = 0;
        bool hasCom = GetSkillWndComPos(g_SkillWndThis, &swComX, &swComY);
        bool hasVt = GetSkillWndAnchorPos(g_SkillWndThis, &swVtX, &swVtY);
        WriteLogFmt("[UpdatePos] src=%s panel=(%d,%d) raw=(%d,%d) childOff=(%d,%d) vtChildDelta=(%d,%d) swCom=%s(%d,%d) swVt=%s(%d,%d)",
            src, g_PanelDrawX, g_PanelDrawY, panelX, panelY, SUPER_CHILD_OFFSET_X, SUPER_CHILD_OFFSET_Y,
            SUPER_CHILD_VT_DELTA_X, SUPER_CHILD_VT_DELTA_Y,
            hasCom ? "Y" : "N", swComX, swComY,
            hasVt ? "Y" : "N", swVtX, swVtY);
        g_UpdatePosLogCount++;
    }

    s_lastPanelX = g_PanelDrawX;
    s_lastPanelY = g_PanelDrawY;
}

// ============================================================================
// WndProc Hook — F9切换 + 面板内点击（原生按钮不需要坐标检测了）
// ============================================================================
static LRESULT CALLBACK GameWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (ENABLE_IMGUI_OVERLAY_PANEL) {
        switch (m) {
        case WM_CAPTURECHANGED:
        case WM_KILLFOCUS:
            if (g_IsD3D8Mode)
                SuperD3D8OverlayCancelMouseCapture();
            else
                SuperImGuiOverlayCancelMouseCapture();
            break;
        case WM_ACTIVATEAPP:
            if (!w) {
                if (g_IsD3D8Mode)
                    SuperD3D8OverlayCancelMouseCapture();
                else
                    SuperImGuiOverlayCancelMouseCapture();
            }
            break;
        default:
            break;
        }
    }

    // F9：切换面板
    if (m == WM_KEYDOWN && w == VK_F9) {
        if (g_SkillWndThis) {
            ToggleSuperWnd();
        }
        return 0;
    }

    if (m == WM_SETCURSOR && g_MouseSuppressFallbackActive) {
        SetCursor(nullptr);
        return TRUE;
    }

    if (HandleSuperBtnD3DWndProc(h, m, w, l))
        return 0;

    // 兜底：即使原生消息没分发到sub_9ECFD0，也保证按钮可点
    // 注意：不能吞掉 WM_LBUTTONUP，否则原生按钮状态机会丢失 mouseUp，光标可能卡在 pressed
    if (g_Ready && g_NativeBtnCreated && !oSkillWndMsg) {
        if (m == WM_LBUTTONUP) {
            int mx = (short)LOWORD(l), my = (short)HIWORD(l);
            if (TryToggleByMousePoint(mx, my, "wndproc")) {
                static DWORD s_lastPassThroughLogTick = 0;
                DWORD now = GetTickCount();
                if (now - s_lastPassThroughLogTick > 200) {
                    s_lastPassThroughLogTick = now;
                    WriteLog("[WndProc] fallback toggle hit, pass WM_LBUTTONUP through");
                }
                // 不 return：继续传给原WndProc，让原生按钮完成 mouseUp 收尾
            }
        }
    }

    if (ENABLE_IMGUI_OVERLAY_PANEL && g_SuperExpanded) {
        if (m == WM_MOUSEACTIVATE && (g_IsD3D8Mode ? SuperD3D8OverlayShouldSuppressGameMouse() : SuperImGuiOverlayShouldSuppressGameMouse())) {
            return MA_NOACTIVATEANDEAT;
        }

        bool overlayHandled = g_IsD3D8Mode ?
            SuperD3D8OverlayHandleWndProc(h, m, w, l) :
            SuperImGuiOverlayHandleWndProc(h, m, w, l);
        bool suppressGameMouse = g_IsD3D8Mode ?
            SuperD3D8OverlayShouldSuppressGameMouse() :
            SuperImGuiOverlayShouldSuppressGameMouse();

        if (!overlayHandled) {
            switch (m) {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_XBUTTONDBLCLK:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
                if (g_IsD3D8Mode)
                    SuperD3D8OverlayCancelMouseCapture();
                else
                    SuperImGuiOverlayCancelMouseCapture();
                if (Win32InputSpoofIsInstalled()) {
                    Win32InputSpoofSetSuppressMouse(false);
                }
                break;
            default:
                break;
            }
        }

        if (overlayHandled && suppressGameMouse) {
            auto forwardOffscreenMouseToGame = [&]() {
                if (g_OriginalWndProc) {
                    CallWindowProc(g_OriginalWndProc, h, m, w, Win32InputSpoofMakeOffscreenMouseLParam());
                }
            };

            switch (m) {
            default:
                break;
            }

            if (m == WM_SETCURSOR) {
                SetCursor(nullptr);
                return TRUE;
            }

            if (m == WM_MOUSEMOVE) {
                forwardOffscreenMouseToGame();
                return 0;
            }

            switch (m) {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_XBUTTONDBLCLK:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
                return 0;
            default:
                break;
            }
        }

        if (overlayHandled) {
            return 0;
        }
    }

    // 面板内点击（当前native child 仍未接上原生输入协议，先由WndProc兜底接管，避免点击穿透到底层窗口）
    if (!ENABLE_IMGUI_OVERLAY_PANEL && g_Ready && g_SkillWndThis && g_SuperExpanded) {
        if (m == WM_LBUTTONDOWN || m == WM_LBUTTONUP) {
            if (g_PanelDrawX <= -9000 || g_PanelDrawY <= -9000) {
                UpdateSuperCWnd();
            }
            int mx = (short)LOWORD(l), my = (short)HIWORD(l);
            int cx = g_PanelDrawX;
            int cy = g_PanelDrawY;
            if (cx > -9000 && cy > -9000) {
                int relX = mx - cx;
                int relY = my - cy;
                if (relX >= 0 && relX < PANEL_W && relY >= 0 && relY < PANEL_H) {
                    static int s_panelHitLogCount = 0;
                    if (s_panelHitLogCount < 40) {
                        WriteLogFmt("[PanelHit:wndproc] msg=%u mx=%d my=%d rel=(%d,%d) panel=(%d,%d,%d,%d)",
                            m, mx, my, relX, relY, cx, cy, PANEL_W, PANEL_H);
                        s_panelHitLogCount++;
                    }
                    if (m == WM_LBUTTONDOWN) {
                        if (relY < 28) {
                            static int s_panelTabLogCount = 0;
                            if (s_panelTabLogCount < 40) {
                                WriteLogFmt("[PanelAction] tab=%d rel=(%d,%d)", relX / 56, relX, relY);
                                s_panelTabLogCount++;
                            }
                            g_SkillMgr.SetTab(relX / 56);
                        } else {
                            int rowIdx = (relY - 28) / 34;
                            SkillTab* tab = g_SkillMgr.GetCurrentTab();
                            if (tab && rowIdx >= 0 && rowIdx < tab->count) {
                                static int s_panelSkillLogCount = 0;
                                if (s_panelSkillLogCount < 60) {
                                    WriteLogFmt("[PanelAction] skillRow=%d rel=(%d,%d) tabCount=%d",
                                        rowIdx, relX, relY, tab->count);
                                    s_panelSkillLogCount++;
                                }
                                tab->skills[rowIdx].Use();
                            }
                        }
                    }
                    return 0;
                }
            }
        }
    }

    return CallWindowProc(g_OriginalWndProc, h, m, w, l);
}

// ============================================================================
// D3D9 Present Hook — 仅用于设备获取、纹理加载和 overlay 面板更新
// ============================================================================
typedef HRESULT (__stdcall *tPresent)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT (__stdcall *tReset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
typedef HRESULT (__stdcall *tResetEx)(IDirect3DDevice9Ex*, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*);
static HRESULT __stdcall hkReset(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);
static HRESULT __stdcall hkResetEx(IDirect3DDevice9Ex* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX* pFullscreenDisplayMode);
static HRESULT __stdcall hkPresent(IDirect3DDevice9* pDevice, const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion);
static tPresent oPresent = nullptr;
static tReset oReset = nullptr;
static tResetEx oResetEx = nullptr;
typedef HRESULT (__stdcall *tEndScene)(IDirect3DDevice9*);
static tEndScene oEndScene = nullptr;
static tPresent g_LiveDevicePresent = nullptr;
static bool g_LivePresentVtableHooked = false;
static tReset g_LiveDeviceReset = nullptr;
static tResetEx g_LiveDeviceResetEx = nullptr;
static void** g_LiveDeviceVTable9 = nullptr;
static void** g_LiveDeviceVTable9Ex = nullptr;

// Forward declarations for CreateDevice interception strategy
static HRESULT __stdcall hkEndScene(IDirect3DDevice9* pDevice);
static bool PatchLiveDeviceVTableEntry(void** vtable, int index, void* hookFunc, void** outOriginal);
static void EnsureLiveDeviceResetHooks(IDirect3DDevice9* pDevice);

// --- CreateDevice interception strategy ---
// When inline hooks on Present/EndScene don't fire (e.g., D3D9on12, wrapper layers),
// we hook Direct3DCreate9 -> vtable-hook IDirect3D9::CreateDevice -> vtable-hook the real device's Present.
typedef IDirect3D9* (WINAPI *tDirect3DCreate9Fn)(UINT SDKVersion);
typedef HRESULT (__stdcall *tCreateDevice)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
static tDirect3DCreate9Fn oDirectCreate9 = nullptr;
static tCreateDevice oCreateDevice = nullptr;
static bool g_DeviceCapturedViaCreateHook = false;

static HRESULT __stdcall hkCreateDevice(IDirect3D9* pD3D, UINT Adapter, D3DDEVTYPE DeviceType,
    HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPP, IDirect3DDevice9** ppDevice)
{
    HRESULT hr = oCreateDevice(pD3D, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPP, ppDevice);
    if (SUCCEEDED(hr) && ppDevice && *ppDevice && !g_DeviceCapturedViaCreateHook)
    {
        IDirect3DDevice9* pDevice = *ppDevice;
        g_pDevice = pDevice;
        g_DeviceCapturedViaCreateHook = true;
        WriteLogFmt("[D3D9-CreateHook] Captured real device=0x%08X type=%d", (DWORD)(uintptr_t)pDevice, (int)DeviceType);

        // Vtable-hook Present on the real device
        void** vtable = *(void***)pDevice;
        if (vtable)
        {
            void* origPresent = nullptr;
            if (PatchLiveDeviceVTableEntry(vtable, 17, (void*)hkPresent, &origPresent))
            {
                if (origPresent && origPresent != (void*)hkPresent)
                    g_LiveDevicePresent = (tPresent)origPresent;
                g_LivePresentVtableHooked = true;
                WriteLogFmt("[D3D9-CreateHook] Present vtable hooked orig=0x%08X", (DWORD)(uintptr_t)origPresent);
            }

            void* origEndScene = nullptr;
            if (PatchLiveDeviceVTableEntry(vtable, 42, (void*)hkEndScene, &origEndScene))
            {
                if (origEndScene && origEndScene != (void*)hkEndScene && !oEndScene)
                    oEndScene = (tEndScene)origEndScene;
                WriteLogFmt("[D3D9-CreateHook] EndScene vtable hooked orig=0x%08X", (DWORD)(uintptr_t)origEndScene);
            }
        }
        EnsureLiveDeviceResetHooks(pDevice);
    }
    return hr;
}

static IDirect3D9* WINAPI hkDirect3DCreate9(UINT SDKVersion)
{
    IDirect3D9* pD3D = oDirectCreate9(SDKVersion);
    if (pD3D)
    {
        WriteLogFmt("[D3D9-CreateHook] Direct3DCreate9 called SDK=%d result=0x%08X", SDKVersion, (DWORD)(uintptr_t)pD3D);
        // Vtable-hook CreateDevice (index 16) on the real IDirect3D9
        void** vtable = *(void***)pD3D;
        if (vtable)
        {
            void* origCreateDevice = nullptr;
            if (PatchLiveDeviceVTableEntry(vtable, 16, (void*)hkCreateDevice, &origCreateDevice))
            {
                if (origCreateDevice && origCreateDevice != (void*)hkCreateDevice)
                    oCreateDevice = (tCreateDevice)origCreateDevice;
                WriteLogFmt("[D3D9-CreateHook] CreateDevice vtable hooked orig=0x%08X", (DWORD)(uintptr_t)origCreateDevice);
            }
        }
    }
    return pD3D;
}

static bool PatchLiveDeviceVTableEntry(void** vtable, int index, void* hookFunc, void** outOriginal)
{
    if (!vtable || index < 0)
        return false;

    void** slot = &vtable[index];
    if (SafeIsBadReadPtr(slot, sizeof(void*)))
        return false;

    if (*slot == hookFunc)
        return true;

    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    if (outOriginal)
        *outOriginal = *slot;
    *slot = hookFunc;
    VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    return true;
}

static void EnsureLiveDeviceResetHooks(IDirect3DDevice9* pDevice)
{
    if (!pDevice)
        return;

    void** vtable9 = *(void***)pDevice;
    if (vtable9 && vtable9 != g_LiveDeviceVTable9)
    {
        void* originalReset = nullptr;
        if (PatchLiveDeviceVTableEntry(vtable9, 16, (void*)hkReset, &originalReset))
        {
            if (originalReset && originalReset != (void*)hkReset)
                g_LiveDeviceReset = (tReset)originalReset;
            g_LiveDeviceVTable9 = vtable9;
            WriteLogFmt("[D3D9] live vtbl Reset patched vtbl=0x%08X orig=0x%08X",
                (DWORD)(uintptr_t)vtable9, (DWORD)(uintptr_t)g_LiveDeviceReset);
        }
    }

    IDirect3DDevice9Ex* pDeviceEx = nullptr;
    if (SUCCEEDED(pDevice->QueryInterface(__uuidof(IDirect3DDevice9Ex), (void**)&pDeviceEx)) && pDeviceEx)
    {
        void** vtableEx = *(void***)pDeviceEx;
        if (vtableEx && vtableEx != g_LiveDeviceVTable9Ex)
        {
            void* originalResetEx = nullptr;
            if (PatchLiveDeviceVTableEntry(vtableEx, 132, (void*)hkResetEx, &originalResetEx))
            {
                if (originalResetEx && originalResetEx != (void*)hkResetEx)
                    g_LiveDeviceResetEx = (tResetEx)originalResetEx;
                g_LiveDeviceVTable9Ex = vtableEx;
                WriteLogFmt("[D3D9Ex] live vtbl ResetEx patched vtbl=0x%08X orig=0x%08X",
                    (DWORD)(uintptr_t)vtableEx, (DWORD)(uintptr_t)g_LiveDeviceResetEx);
            }
        }
        pDeviceEx->Release();
    }
}

static volatile bool g_InReset = false;

static HRESULT __stdcall hkReset(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters)
{
    // Guard against infinite recursion: inline hook on function body + vtable hook
    // both redirect to hkReset. If re-entered, go straight to trampoline.
    if (g_InReset) {
        tReset resetFn = g_LiveDeviceReset ? g_LiveDeviceReset : oReset;
        return resetFn ? resetFn(pDevice, pPresentationParameters) : D3DERR_INVALIDCALL;
    }
    g_InReset = true;

    PrepareForD3DDeviceReset("reset");

    tReset resetFn = g_LiveDeviceReset ? g_LiveDeviceReset : oReset;
    HRESULT hr = resetFn ? resetFn(pDevice, pPresentationParameters) : D3DERR_INVALIDCALL;

    if (SUCCEEDED(hr)) {
        g_pDevice = pDevice;
        if (ENABLE_IMGUI_OVERLAY_PANEL)
            SuperImGuiOverlayOnDeviceReset(pDevice);
        WriteLog("[D3D9] Reset OK: hard rebuild pending on next Present");
    } else {
        WriteLogFmt("[D3D9] Reset FAIL hr=0x%08X", (DWORD)hr);
    }

    g_InReset = false;
    return hr;
}

static volatile bool g_InResetEx = false;

static HRESULT __stdcall hkResetEx(IDirect3DDevice9Ex* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX* pFullscreenDisplayMode)
{
    if (g_InResetEx) {
        tResetEx resetFn = g_LiveDeviceResetEx ? g_LiveDeviceResetEx : oResetEx;
        return resetFn ? resetFn(pDevice, pPresentationParameters, pFullscreenDisplayMode) : D3DERR_INVALIDCALL;
    }
    g_InResetEx = true;

    PrepareForD3DDeviceReset("reset_ex");

    tResetEx resetFn = g_LiveDeviceResetEx ? g_LiveDeviceResetEx : oResetEx;
    HRESULT hr = resetFn ? resetFn(pDevice, pPresentationParameters, pFullscreenDisplayMode) : D3DERR_INVALIDCALL;

    if (SUCCEEDED(hr)) {
        g_pDevice = (IDirect3DDevice9*)pDevice;
        if (ENABLE_IMGUI_OVERLAY_PANEL)
            SuperImGuiOverlayOnDeviceReset((IDirect3DDevice9*)pDevice);
        WriteLog("[D3D9Ex] ResetEx OK: hard rebuild pending on next Present");
    } else {
        WriteLogFmt("[D3D9Ex] ResetEx FAIL hr=0x%08X", (DWORD)hr);
    }

    g_InResetEx = false;
    return hr;
}

// EndScene hook — fallback device capture when Present inline hook misses
// (e.g., game uses D3D9Ex or wrapper d3d9.dll with different Present address)
static HRESULT __stdcall hkEndScene(IDirect3DDevice9* pDevice)
{
    if (pDevice && !g_pDevice)
    {
        g_pDevice = pDevice;
        WriteLogFmt("[D3D9] EndScene captured device=0x%08X", (DWORD)(uintptr_t)pDevice);

        // Vtable-hook Present on the live device so hkPresent fires from now on
        if (!g_LivePresentVtableHooked)
        {
            void** vtable = *(void***)pDevice;
            if (vtable)
            {
                void* origPresent = nullptr;
                if (PatchLiveDeviceVTableEntry(vtable, 17, (void*)hkPresent, &origPresent))
                {
                    if (origPresent && origPresent != (void*)hkPresent)
                        g_LiveDevicePresent = (tPresent)origPresent;
                    g_LivePresentVtableHooked = true;
                    WriteLogFmt("[D3D9] live vtbl Present patched vtbl=0x%08X orig=0x%08X",
                        (DWORD)(uintptr_t)vtable, (DWORD)(uintptr_t)g_LiveDevicePresent);
                }
            }
        }

        EnsureLiveDeviceResetHooks(pDevice);
    }

    return oEndScene(pDevice);
}

static HRESULT __stdcall hkPresent(IDirect3DDevice9* pDevice,
    const RECT* pSourceRect, const RECT* pDestRect,
    HWND hDestWindowOverride, const RGNDATA* pDirtyRegion)
{
    g_pDevice = pDevice;

    // Choose the correct original Present to call at the end:
    // - oPresent = inline hook trampoline (works when inline hook caught the right function)
    // - g_LiveDevicePresent = vtable original (works when EndScene fallback patched vtable)
    tPresent fnOrigPresent = oPresent ? oPresent : g_LiveDevicePresent;
    EnsureLiveDeviceResetHooks(pDevice);

    // 纹理加载（一次性，面板用）
    if (!g_TexturesLoaded) {
        LoadAllTextures(pDevice);
    }

    // 每帧同步SkillWnd全局指针，防止角色切换/重开窗口后悬空指针
    bool wasReady = g_Ready;
    uintptr_t swGlobal = 0;
    if (!SafeIsBadReadPtr((void*)ADDR_SkillWndEx, 4)) {
        swGlobal = *(uintptr_t*)ADDR_SkillWndEx;
    }
    OnSkillWndPointerObserved(swGlobal, "present");

    if (!wasReady && g_Ready && g_SkillWndThis) {
        WriteLogFmt("[Present] SkillWnd: 0x%08X", (DWORD)g_SkillWndThis);
        WriteLog("[Present] Ready");
    }

    // 仅当sub_9E17D0 Hook失败时启用Present兜底创建，避免干扰主流程生命周期
    if (g_Ready && g_SkillWndThis && !oSkillWndInitChildren) {
        if (!g_NativeBtnCreated) {
            WriteLog("[Present] Fallback create native button...");
            if (CreateSuperButton(g_SkillWndThis))
                WriteLog("[Present] Fallback native button OK");
            else
                WriteLog("[Present] Fallback native button FAILED");
        }
    }

    if (ENABLE_IMGUI_OVERLAY_PANEL) {
        if (!SuperImGuiOverlayIsInitialized() && g_GameHwnd && pDevice) {
            if (!SuperImGuiOverlayEnsureInitialized(g_GameHwnd, pDevice, 1.0f, IMGUI_PANEL_ASSET_PATH)) {
                static DWORD s_lastOverlayInitFailLogTick = 0;
                DWORD now = GetTickCount();
                if (now - s_lastOverlayInitFailLogTick > 1000) {
                    WriteLogFmt("[ImGuiOverlay] ensure init failed in Present device=0x%08X hwnd=0x%08X",
                        (DWORD)(uintptr_t)pDevice, (DWORD)(uintptr_t)g_GameHwnd);
                    s_lastOverlayInitFailLogTick = now;
                }
            }
        }

        DrawSuperButtonTextureInPresent(pDevice);

        if (g_SkillWndThis && g_SuperExpanded) {
            UpdateSuperCWnd();
            SuperImGuiOverlaySetVisible(true);
            SuperImGuiOverlayRender(pDevice);
        } else if (SuperImGuiOverlayIsInitialized()) {
            SuperImGuiOverlaySetVisible(false);
        }

        DrawSuperButtonCursorInPresent(pDevice);

        const bool suppressMouse = SuperImGuiOverlayShouldSuppressGameMouse() || ShouldSuppressGameMouseForSuperBtnD3D();
        if (Win32InputSpoofIsInstalled()) {
            Win32InputSpoofSetSuppressMouse(suppressMouse);
        }
        UpdateGameMouseSuppressionFallback(suppressMouse);
        if (g_LastOverlaySuppressMouse && !suppressMouse) {
            RefreshGameCursorImmediately();
        }
        g_LastOverlaySuppressMouse = suppressMouse;

        return fnOrigPresent ? fnOrigPresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion) : D3D_OK;
    }

    // 每帧刷新扩展层锚点（真正绘制在 sub_9DEE30 里做）
    // v10.4+: native child 已有 move/refresh/toggle 三条同步链，这里先停掉 Present 中的每帧搬运，
    // 避免与原生移动链互相打架，导致拖动抽搐和视口轻微漂移。
    if (g_SkillWndThis && g_SuperExpanded) {
        if (!g_NativeWndCreated || !g_SuperCWnd || ENABLE_PRESENT_NATIVE_CHILD_UPDATE) {
            UpdateSuperCWnd();
        }
    }

    // v7.6: 拖动过程中原生dirty链不会稳定重画，直接在Present里按最新锚点绘制面板纹理。
    if (ENABLE_PRESENT_PANEL_DRAW && g_SuperExpanded && g_texPanelBg) {
        int drawX = g_PanelDrawX;
        int drawY = g_PanelDrawY;
        if ((drawX <= -9000 || drawY <= -9000) && g_SuperCWnd) {
            drawX = CWnd_GetRenderX(g_SuperCWnd);
            drawY = CWnd_GetRenderY(g_SuperCWnd);
        }
        if (drawX > -9000 && drawY > -9000) {
            static int s_presentDrawLogCount = 0;
            if (s_presentDrawLogCount < 20) {
                WriteLogFmt("[PresentDraw] panel=(%d,%d)", drawX, drawY);
                s_presentDrawLogCount++;
            }
            DrawTexturedQuad(pDevice, g_texPanelBg,
                (float)drawX, (float)drawY, (float)PANEL_W, (float)PANEL_H);
        }
    }

    DrawSuperButtonTextureInPresent(pDevice);

    // v7.2: 默认关闭Present点击轮询，避免与WndProc路径双触发
    if (ENABLE_PRESENT_CLICK_POLL && g_Ready && g_NativeBtnCreated) {
        static bool s_prevLBtnDown = false;
        bool isDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (s_prevLBtnDown && !isDown) {
            POINT pt = {};
            if (GetCursorPos(&pt)) {
                HWND hwndForClient = g_GameHwnd ? g_GameHwnd : hDestWindowOverride;
                if (hwndForClient) {
                    ScreenToClient(hwndForClient, &pt);
                }
                TryToggleByMousePoint(pt.x, pt.y, "present");
            }
        }
        s_prevLBtnDown = isDown;
    }

    return fnOrigPresent ? fnOrigPresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion) : D3D_OK;
}

// ============================================================================
// D3D9 Hook安装
// ============================================================================
static bool SetupD3D9Hook()
{
    WriteLog("[D3D9] Setup...");

    WNDCLASSEXA wc = {sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProc, 0L, 0L,
        GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "SSWDummy", NULL};
    RegisterClassExA(&wc);
    HWND hWnd = CreateWindowA("SSWDummy", "", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100,
        NULL, NULL, wc.hInstance, NULL);

    // Use the d3d9.dll that the game actually loaded (may be a wrapper/proxy in the game dir)
    // rather than always calling the system Direct3DCreate9
    typedef IDirect3D9* (WINAPI *tDirect3DCreate9)(UINT);
    tDirect3DCreate9 pfnCreate9 = nullptr;
    HMODULE hD3D9 = ::GetModuleHandleA("d3d9.dll");
    if (hD3D9)
        pfnCreate9 = (tDirect3DCreate9)::GetProcAddress(hD3D9, "Direct3DCreate9");
    if (!pfnCreate9)
        pfnCreate9 = Direct3DCreate9; // fallback to linked import

    IDirect3D9* pD3D = pfnCreate9(D3D_SDK_VERSION);
    if (!pD3D) {
        WriteLog("[D3D9] FAIL: Direct3DCreate9");
        DestroyWindow(hWnd);
        UnregisterClassA("SSWDummy", wc.hInstance);
        return false;
    }

    WriteLogFmt("[D3D9] d3d9.dll=0x%08X create9=0x%08X",
        (DWORD)(uintptr_t)hD3D9, (DWORD)(uintptr_t)pfnCreate9);

    D3DPRESENT_PARAMETERS d3dpp = {};
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    d3dpp.hDeviceWindow = hWnd;

    IDirect3DDevice9* pDev = nullptr;
    // Try HAL first (shares vtable with real game device), fall back to NULLREF
    HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
        hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDev);
    if (FAILED(hr) || !pDev) {
        WriteLogFmt("[D3D9] HAL device failed (hr=0x%08X), trying NULLREF", (DWORD)hr);
        hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF,
            hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDev);
    }

    if (FAILED(hr) || !pDev) {
        pD3D->Release();
        DestroyWindow(hWnd);
        UnregisterClassA("SSWDummy", wc.hInstance);
        return false;
    }

    DWORD* vtable = *(DWORD**)pDev;
    DWORD presentAddr = vtable[17];
    DWORD endSceneAddr = vtable[42];
    BYTE* pPresent = FollowJmpChain((void*)presentAddr);
    BYTE* pEndScene = FollowJmpChain((void*)endSceneAddr);

    WriteLogFmt("[D3D9] vtable=0x%08X Present=[0x%08X->0x%08X] EndScene=[0x%08X->0x%08X]",
        (DWORD)(uintptr_t)vtable,
        presentAddr, (DWORD)(uintptr_t)pPresent,
        endSceneAddr, (DWORD)(uintptr_t)pEndScene);

    // Detect which module owns these functions
    {
        HMODULE hModPresent = nullptr, hModEndScene = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)pPresent, &hModPresent);
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)pEndScene, &hModEndScene);
        char modNameP[260] = {}, modNameE[260] = {};
        if (hModPresent) GetModuleFileNameA(hModPresent, modNameP, sizeof(modNameP));
        if (hModEndScene) GetModuleFileNameA(hModEndScene, modNameE, sizeof(modNameE));
        WriteLogFmt("[D3D9] Present in: %s", modNameP[0] ? modNameP : "(unknown)");
        WriteLogFmt("[D3D9] EndScene in: %s", modNameE[0] ? modNameE : "(unknown)");
    }

    int copyLen = CalcMinCopyLen(pPresent);
    if (copyLen < 5) copyLen = 5;
    oReset = nullptr;
    oResetEx = nullptr;
    g_LiveDeviceReset = nullptr;
    g_LiveDeviceResetEx = nullptr;

    oPresent = (tPresent)GenericInlineHook5(pPresent, (void*)hkPresent, copyLen);
    if (!oPresent) {
        WriteLog("[D3D9] Present inline hook failed (will rely on EndScene fallback)");
    }

    // EndScene fallback: hook vtable[42] to capture device even if Present inline hook
    // doesn't match the game's actual Present function (e.g., D3D9Ex / wrapper d3d9.dll)
    int esLen = CalcMinCopyLen(pEndScene);
    if (esLen < 5) esLen = 5;
    oEndScene = (tEndScene)GenericInlineHook5(pEndScene, (void*)hkEndScene, esLen);
    if (!oEndScene) {
        WriteLog("[D3D9] EndScene inline hook failed");
    }

    if (!oPresent && !oEndScene) {
        WriteLog("[D3D9] FAIL: both Present and EndScene hooks failed");
        pDev->Release(); pD3D->Release();
        DestroyWindow(hWnd);
        UnregisterClassA("SSWDummy", wc.hInstance);
        return false;
    }

    WriteLogFmt("[D3D9] Hooked OK, present=0x%08X endScene=0x%08X reset=vtable_only",
        (DWORD)(uintptr_t)oPresent, (DWORD)(uintptr_t)oEndScene);

    // --- Strategy 3: Hook Direct3DCreate9 to intercept device creation ---
    // When inline hooks on Present/EndScene install but never fire (D3D9on12, wrapper layers,
    // or the game's device uses a different vtable than our dummy device), this catches the
    // real device at creation time and vtable-hooks its Present directly.
    {
        HMODULE hD3D9Live = ::GetModuleHandleA("d3d9.dll");
        if (hD3D9Live)
        {
            BYTE* pCreate9 = (BYTE*)::GetProcAddress(hD3D9Live, "Direct3DCreate9");
            if (pCreate9)
            {
                pCreate9 = FollowJmpChain(pCreate9);
                int createLen = CalcMinCopyLen(pCreate9);
                if (createLen < 5) createLen = 5;
                oDirectCreate9 = (tDirect3DCreate9Fn)GenericInlineHook5(pCreate9, (void*)hkDirect3DCreate9, createLen);
                if (oDirectCreate9)
                    WriteLogFmt("[D3D9] Direct3DCreate9 hooked at 0x%08X (CreateDevice interception ready)", (DWORD)(uintptr_t)pCreate9);
                else
                    WriteLog("[D3D9] Direct3DCreate9 inline hook failed (non-fatal, game may have already called it)");
            }
        }
    }

    // --- Strategy 4: Vtable-patch the dummy device's vtable directly ---
    // If the game creates a HAL device from the same d3d9.dll, it will share this vtable.
    // The vtable lives in d3d9.dll's data segment and persists after pDev->Release().
    // This catches D3D9on12 scenarios where inline hooks on the function body don't fire
    // but the vtable entries are still used by the game device.
    {
        void** dummyVtable = *(void***)pDev;
        if (dummyVtable)
        {
            void* origPresentVtbl = nullptr;
            void* origEndSceneVtbl = nullptr;
            if (PatchLiveDeviceVTableEntry(dummyVtable, 17, (void*)hkPresent, &origPresentVtbl))
            {
                // If inline hook already installed, the original function entry is patched with jmp->hkPresent
                // so calling it would recurse. Only use vtable original when we DON'T have an inline trampoline.
                if (!oPresent && !g_LiveDevicePresent && origPresentVtbl && origPresentVtbl != (void*)hkPresent)
                    g_LiveDevicePresent = (tPresent)origPresentVtbl;
                WriteLogFmt("[D3D9] dummy vtable Present patched orig=0x%08X (using=%s)",
                    (DWORD)(uintptr_t)origPresentVtbl,
                    oPresent ? "inline_tramp" : (g_LiveDevicePresent ? "vtbl_orig" : "none"));
            }
            if (PatchLiveDeviceVTableEntry(dummyVtable, 42, (void*)hkEndScene, &origEndSceneVtbl))
            {
                if (!oEndScene && origEndSceneVtbl && origEndSceneVtbl != (void*)hkEndScene)
                    oEndScene = (tEndScene)origEndSceneVtbl;
                WriteLogFmt("[D3D9] dummy vtable EndScene patched orig=0x%08X", (DWORD)(uintptr_t)origEndSceneVtbl);
            }

            // Also patch Reset (index 16) on the dummy vtable
            void* origResetVtbl = nullptr;
            if (PatchLiveDeviceVTableEntry(dummyVtable, 16, (void*)hkReset, &origResetVtbl))
            {
                if (!oReset && origResetVtbl && origResetVtbl != (void*)hkReset)
                    oReset = (tReset)origResetVtbl;
                WriteLogFmt("[D3D9] dummy vtable Reset patched orig=0x%08X", (DWORD)(uintptr_t)origResetVtbl);
            }
        }
    }

    pDev->Release(); pD3D->Release();
    DestroyWindow(hWnd);
    UnregisterClassA("SSWDummy", wc.hInstance);
    return true;
}

// ============================================================================
// D3D8 兼容层 — 运行时检测 + D3D8 Present hook + shared ImGui panel
// ============================================================================

// D3D8 function pointer types (用 void* 代替 IDirect3DDevice8* 因为不包含 d3d8.h)
typedef HRESULT (__stdcall *tD3D8Present)(void* pDevice8, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT (__stdcall *tD3D8Reset)(void* pDevice8, void* pPresentationParameters);
static tD3D8Present oD3D8Present = nullptr;
static tD3D8Reset   oD3D8Reset  = nullptr;
static HWND g_D3D8GameHwnd = nullptr;

// D3D8 Present hook — 直接在游戏 D3D8 设备上渲染 overlay
static HRESULT __stdcall hkD3D8Present(void* pDevice8,
    const RECT* pSourceRect, const RECT* pDestRect,
    HWND hDestWindowOverride, const RGNDATA* pDirtyRegion)
{
    // 首次调用：获取游戏 HWND
    if (!g_D3D8GameHwnd) {
        HWND hwnd = hDestWindowOverride;
        if (!hwnd) hwnd = g_GameHwnd;
        if (!hwnd) {
            DWORD pid = GetCurrentProcessId();
            struct FindCtx { DWORD pid; HWND result; };
            FindCtx ctx = { pid, nullptr };
            EnumWindows([](HWND h, LPARAM lp) -> BOOL {
                FindCtx* c = (FindCtx*)lp;
                DWORD wp;
                GetWindowThreadProcessId(h, &wp);
                if (wp == c->pid && IsWindowVisible(h)) {
                    c->result = h;
                    return FALSE;
                }
                return TRUE;
            }, (LPARAM)&ctx);
            hwnd = ctx.result;
        }
        if (hwnd) {
            g_D3D8GameHwnd = hwnd;
            if (!g_GameHwnd) g_GameHwnd = hwnd;
            WriteLogFmt("[D3D8] first Present: hwnd=0x%08X", (DWORD)(uintptr_t)hwnd);
        }
    }

    // 每帧同步 SkillWnd 全局指针
    bool wasReady = g_Ready;
    uintptr_t swGlobal = 0;
    if (!SafeIsBadReadPtr((void*)ADDR_SkillWndEx, 4)) {
        swGlobal = *(uintptr_t*)ADDR_SkillWndEx;
    }
    OnSkillWndPointerObserved(swGlobal, "d3d8_present");

    if (!wasReady && g_Ready && g_SkillWndThis) {
        WriteLogFmt("[D3D8-Present] SkillWnd: 0x%08X", (DWORD)g_SkillWndThis);
        WriteLog("[D3D8-Present] Ready");
    }

    // Fallback 创建原生按钮
    if (g_Ready && g_SkillWndThis && !oSkillWndInitChildren) {
        if (!g_NativeBtnCreated) {
            WriteLog("[D3D8-Present] Fallback create native button...");
            if (CreateSuperButton(g_SkillWndThis))
                WriteLog("[D3D8-Present] Fallback native button OK");
            else
                WriteLog("[D3D8-Present] Fallback native button FAILED");
        }
    }

    if (ENABLE_IMGUI_OVERLAY_PANEL) {
        if (!SuperD3D8OverlayIsInitialized() && g_D3D8GameHwnd && pDevice8) {
            if (!SuperD3D8OverlayEnsureInitialized(g_D3D8GameHwnd, pDevice8, 1.0f, IMGUI_PANEL_ASSET_PATH)) {
                static DWORD s_lastD3D8OverlayInitFailLogTick = 0;
                DWORD now = GetTickCount();
                if (now - s_lastD3D8OverlayInitFailLogTick > 1000) {
                    WriteLogFmt("[D3D8ImGuiOverlay] ensure init failed in Present device=0x%08X hwnd=0x%08X",
                        (DWORD)(uintptr_t)pDevice8, (DWORD)(uintptr_t)g_D3D8GameHwnd);
                    s_lastD3D8OverlayInitFailLogTick = now;
                }
            }
        }

        if (g_SkillWndThis && g_SuperExpanded) {
            UpdateSuperCWnd();
            SuperD3D8OverlaySetVisible(true);
            SuperD3D8OverlayRender(pDevice8);
        } else if (SuperD3D8OverlayIsInitialized()) {
            SuperD3D8OverlaySetVisible(false);
        }

        const bool suppressMouse = SuperD3D8OverlayShouldSuppressGameMouse() || ShouldSuppressGameMouseForSuperBtnD3D();
        if (Win32InputSpoofIsInstalled()) {
            Win32InputSpoofSetSuppressMouse(suppressMouse);
        }
        UpdateGameMouseSuppressionFallback(suppressMouse);
        if (g_LastOverlaySuppressMouse && !suppressMouse) {
            RefreshGameCursorImmediately();
        }
        g_LastOverlaySuppressMouse = suppressMouse;
    }

    // 调用原始 D3D8 Present
    HRESULT hr = oD3D8Present ? oD3D8Present(pDevice8, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion) : D3D_OK;
    return hr;
}

// D3D8 Reset hook — 释放 D3D8 纹理
static HRESULT __stdcall hkD3D8Reset(void* pDevice8, void* pPresentationParameters)
{
    WriteLog("[D3D8] Reset called, releasing D3D8 textures");
    if (ENABLE_IMGUI_OVERLAY_PANEL) {
        SuperD3D8OverlayOnDeviceLost();
    }

    HRESULT hr = oD3D8Reset ? oD3D8Reset(pDevice8, pPresentationParameters) : D3DERR_INVALIDCALL;

    if (SUCCEEDED(hr)) {
        if (ENABLE_IMGUI_OVERLAY_PANEL) {
            SuperD3D8OverlayOnDeviceReset(pDevice8);
        }
        WriteLog("[D3D8] Reset OK, overlay device objects refreshed");
    } else {
        WriteLogFmt("[D3D8] Reset FAIL hr=0x%08X", (DWORD)hr);
    }
    return hr;
}

// 安装 D3D8 hooks
static bool SetupD3D8Hook()
{
    WriteLog("[D3D8] Setup...");

    HMODULE hD3D8 = ::GetModuleHandleA("d3d8.dll");
    if (!hD3D8) {
        WriteLog("[D3D8] FAIL: d3d8.dll not loaded");
        return false;
    }

    typedef void* (__stdcall *tDirect3DCreate8)(UINT);
    tDirect3DCreate8 pfnCreate8 = (tDirect3DCreate8)::GetProcAddress(hD3D8, "Direct3DCreate8");
    if (!pfnCreate8) {
        WriteLog("[D3D8] FAIL: Direct3DCreate8 not found");
        return false;
    }

    WriteLogFmt("[D3D8] d3d8.dll=0x%08X Direct3DCreate8=0x%08X",
        (DWORD)(uintptr_t)hD3D8, (DWORD)(uintptr_t)pfnCreate8);

    // 创建 dummy 窗口和 D3D8 设备来获取 vtable
    WNDCLASSEXA wc = {sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProc, 0L, 0L,
        GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "SSWDummyD3D8", NULL};
    RegisterClassExA(&wc);
    HWND hWnd = CreateWindowA("SSWDummyD3D8", "", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100,
        NULL, NULL, wc.hInstance, NULL);

    // 调用 Direct3DCreate8(220) — D3D8 SDK version
    void* pD3D8 = pfnCreate8(220);
    if (!pD3D8) {
        WriteLog("[D3D8] FAIL: Direct3DCreate8 returned null");
        DestroyWindow(hWnd);
        UnregisterClassA("SSWDummyD3D8", wc.hInstance);
        return false;
    }

    // IDirect3D8::CreateDevice = vtable[15]
    // IDirect3D8 vtable: 0=QI, 1=AddRef, 2=Release, ..., 15=CreateDevice
    DWORD* vtableD3D8 = *(DWORD**)pD3D8;

    // 查询当前显示模式以获取有效的 BackBufferFormat
    // IDirect3D8::GetAdapterDisplayMode = vtable[8]
    // HRESULT GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE* pMode)
    // D3DDISPLAYMODE: { Width(4), Height(4), RefreshRate(4), Format(4) } = 16 bytes
    typedef HRESULT (__stdcall *tGetAdapterDisplayMode8)(void*, UINT, void*);
    tGetAdapterDisplayMode8 pfnGetDisplayMode = (tGetAdapterDisplayMode8)vtableD3D8[8];
    BYTE displayMode[16] = {};
    DWORD backBufferFormat = 22; // D3DFMT_X8R8G8B8 fallback
    if (pfnGetDisplayMode) {
        HRESULT hrDM = pfnGetDisplayMode(pD3D8, 0, displayMode);
        if (SUCCEEDED(hrDM)) {
            DWORD dmFormat = *(DWORD*)(displayMode + 12); // Format at offset 12
            if (dmFormat != 0) {
                backBufferFormat = dmFormat;
                WriteLogFmt("[D3D8] display mode: %dx%d fmt=%d",
                    *(DWORD*)(displayMode + 0), *(DWORD*)(displayMode + 4), dmFormat);
            }
        }
    }

    // D3D8 D3DPRESENT_PARAMETERS layout (不同于 D3D9，没有 MultiSampleQuality):
    //   0x00: BackBufferWidth (DWORD)
    //   0x04: BackBufferHeight (DWORD)
    //   0x08: BackBufferFormat (D3DFORMAT)  <-- D3D8 不支持 D3DFMT_UNKNOWN(0)！必须指定有效格式
    //   0x0C: BackBufferCount (DWORD)
    //   0x10: MultiSampleType (D3DMULTISAMPLE_TYPE)
    //   0x14: SwapEffect (D3DSWAPEFFECT)
    //   0x18: hDeviceWindow (HWND)
    //   0x1C: Windowed (BOOL)
    //   0x20: EnableAutoDepthStencil (BOOL)
    //   0x24: AutoDepthStencilFormat (D3DFORMAT)
    //   0x28: Flags (DWORD)
    //   0x2C: FullScreen_RefreshRateInHz (UINT)
    //   0x30: FullScreen_PresentationInterval (UINT)
    BYTE d3dpp8[64] = {};
    *(DWORD*)(d3dpp8 + 0x08) = backBufferFormat; // BackBufferFormat — 必须是有效格式
    *(DWORD*)(d3dpp8 + 0x14) = 1; // SwapEffect = D3DSWAPEFFECT_DISCARD
    *(HWND*)(d3dpp8 + 0x18) = hWnd; // hDeviceWindow
    *(DWORD*)(d3dpp8 + 0x1C) = 1; // Windowed = TRUE

    // 调用 IDirect3D8::CreateDevice (vtable[15])
    // HRESULT CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
    //                      DWORD BehaviorFlags, D3DPRESENT_PARAMETERS8* pPP, IDirect3DDevice8** ppDevice)
    typedef HRESULT (__stdcall *tD3D8CreateDevice)(void* pD3D8, UINT, DWORD, HWND, DWORD, void*, void**);
    tD3D8CreateDevice pfnCreateDevice8 = (tD3D8CreateDevice)vtableD3D8[15];

    void* pDummyDevice8 = nullptr;
    HRESULT hr = pfnCreateDevice8(pD3D8, 0, 1/*D3DDEVTYPE_HAL*/, hWnd,
        0x20/*D3DCREATE_SOFTWARE_VERTEXPROCESSING*/, d3dpp8, &pDummyDevice8);

    if (FAILED(hr) || !pDummyDevice8) {
        WriteLogFmt("[D3D8] dummy device creation failed hr=0x%08X, trying NULLREF", (DWORD)hr);
        // D3D8 没有 D3DDEVTYPE_NULLREF，尝试 REF (2)
        hr = pfnCreateDevice8(pD3D8, 0, 2/*D3DDEVTYPE_REF*/, hWnd,
            0x20, d3dpp8, &pDummyDevice8);
    }

    if (FAILED(hr) || !pDummyDevice8) {
        WriteLogFmt("[D3D8] FAIL: all device creation attempts failed hr=0x%08X", (DWORD)hr);
        // 释放 IDirect3D8 (vtable[2] = Release)
        typedef ULONG (__stdcall *tRelease)(void*);
        ((tRelease)vtableD3D8[2])(pD3D8);
        DestroyWindow(hWnd);
        UnregisterClassA("SSWDummyD3D8", wc.hInstance);
        return false;
    }

    // 从 dummy device 获取 vtable
    DWORD* vtableDevice8 = *(DWORD**)pDummyDevice8;
    // IDirect3DDevice8 vtable indices:
    //   14 = Reset
    //   15 = Present
    DWORD presentAddr = vtableDevice8[15];
    DWORD resetAddr = vtableDevice8[14];

    WriteLogFmt("[D3D8] device vtable=0x%08X Present=[%d]=0x%08X Reset=[%d]=0x%08X",
        (DWORD)(uintptr_t)vtableDevice8, 15, presentAddr, 14, resetAddr);

    // Inline hook D3D8 Present
    BYTE* pPresent8 = FollowJmpChain((void*)presentAddr);
    int copyLenPresent = CalcMinCopyLen(pPresent8);
    if (copyLenPresent < 5) copyLenPresent = 5;
    oD3D8Present = (tD3D8Present)GenericInlineHook5(pPresent8, (void*)hkD3D8Present, copyLenPresent);
    if (!oD3D8Present) {
        WriteLog("[D3D8] Present inline hook failed, trying vtable patch");
        // Fallback: vtable patch
        DWORD oldProt;
        VirtualProtect(&vtableDevice8[15], sizeof(void*), PAGE_READWRITE, &oldProt);
        oD3D8Present = (tD3D8Present)vtableDevice8[15];
        vtableDevice8[15] = (DWORD)(uintptr_t)hkD3D8Present;
        VirtualProtect(&vtableDevice8[15], sizeof(void*), oldProt, &oldProt);
        WriteLogFmt("[D3D8] Present vtable patched, orig=0x%08X", (DWORD)(uintptr_t)oD3D8Present);
    } else {
        WriteLogFmt("[D3D8] Present inline hooked at 0x%08X tramp=0x%08X",
            (DWORD)(uintptr_t)pPresent8, (DWORD)(uintptr_t)oD3D8Present);
    }

    // Inline hook D3D8 Reset
    BYTE* pReset8 = FollowJmpChain((void*)resetAddr);
    int copyLenReset = CalcMinCopyLen(pReset8);
    if (copyLenReset < 5) copyLenReset = 5;
    oD3D8Reset = (tD3D8Reset)GenericInlineHook5(pReset8, (void*)hkD3D8Reset, copyLenReset);
    if (!oD3D8Reset) {
        WriteLog("[D3D8] Reset inline hook failed, trying vtable patch");
        DWORD oldProt;
        VirtualProtect(&vtableDevice8[14], sizeof(void*), PAGE_READWRITE, &oldProt);
        oD3D8Reset = (tD3D8Reset)vtableDevice8[14];
        vtableDevice8[14] = (DWORD)(uintptr_t)hkD3D8Reset;
        VirtualProtect(&vtableDevice8[14], sizeof(void*), oldProt, &oldProt);
        WriteLogFmt("[D3D8] Reset vtable patched, orig=0x%08X", (DWORD)(uintptr_t)oD3D8Reset);
    } else {
        WriteLogFmt("[D3D8] Reset inline hooked at 0x%08X tramp=0x%08X",
            (DWORD)(uintptr_t)pReset8, (DWORD)(uintptr_t)oD3D8Reset);
    }

    // 释放 dummy 设备和 IDirect3D8
    typedef ULONG (__stdcall *tRelease)(void*);
    ((tRelease)(*(DWORD**)pDummyDevice8)[2])(pDummyDevice8);
    ((tRelease)vtableD3D8[2])(pD3D8);
    DestroyWindow(hWnd);
    UnregisterClassA("SSWDummyD3D8", wc.hInstance);

    WriteLog("[D3D8] Hook setup complete");
    return true;
}

// ============================================================================
// SkillWnd Hook安装
// ============================================================================
static bool SetupSkillWndHook()
{
    oSkillWndInitChildren = (tSkillWndInitChildren)InstallInlineHook(
        ADDR_9E17D0, (void*)hkSkillWndInitChildren);
    if (!oSkillWndInitChildren) {
        WriteLog("[SkillHook] Init hook failed");
        return false;
    }
    WriteLogFmt("[SkillHook] Init: tramp=0x%08X", (DWORD)oSkillWndInitChildren);
    return true;
}

// ============================================================================
// 消息处理Hook安装
// ============================================================================
static bool SetupMsgHook()
{
    oSkillWndMsg = (tSkillWndMsg)InstallInlineHook(
        ADDR_9DDB30, (void*)hkSkillWndMsgNaked);
    if (!oSkillWndMsg) {
        WriteLog("[MsgHook] Hook failed");
        return false;
    }
    WriteLogFmt("[MsgHook] OK(9DDB30): tramp=0x%08X", (DWORD)oSkillWndMsg);
    return true;
}

// ============================================================================
// 发包 Hook安装
// ============================================================================
static bool SetupPacketHook()
{
    BYTE* pTarget = FollowJmpChain((void*)ADDR_43D94D);
    if (!pTarget) {
        WriteLog("[PacketHook] target missing");
        return false;
    }

    if (pTarget[0] != 0xE8) {
        WriteLogFmt("[PacketHook] unexpected prologue at 43D94D: opcode=0x%02X", pTarget[0]);
        return false;
    }

    g_SendPacketOriginalCallTarget = (DWORD)(uintptr_t)(pTarget + 5 + *(int*)(pTarget + 1));
    oSendPacket = pTarget + 5;

    DWORD oldProtect = 0;
    if (!VirtualProtect(pTarget, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        WriteLog("[PacketHook] VirtualProtect failed");
        g_SendPacketOriginalCallTarget = 0;
        oSendPacket = nullptr;
        return false;
    }

    pTarget[0] = 0xE9;
    *(int*)(pTarget + 1) = (int)((uintptr_t)hkSendPacketNaked - (uintptr_t)pTarget - 5);
    VirtualProtect(pTarget, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), pTarget, 5);

    WriteLogFmt("[PacketHook] OK(43D94D): originalCall=0x%08X continue=0x%08X",
        g_SendPacketOriginalCallTarget,
        (DWORD)(uintptr_t)oSendPacket);
    return true;
}

// ============================================================================
// 技能释放分类 hook安装
// 说明：
//   00B31349 是完整 skillId 分类树入口，用来做“继承原生模板 skillId”的通用模式。
//   00B3144D 是释放高层分类块，不是函数入口；首条指令长度 6 字节。
//   00B2F370 是独立函数分支入口（部分技能路径会直接走这里），需要在入口改 arg0(skillId)。
//   这里使用 GenericInlineHook5(copyLen=6)，保证整条 cmp 指令被完整搬到 trampoline。
// ============================================================================
static bool SetupSkillReleaseClassifierHook()
{
    bool rootOk = false;
    BYTE* pRoot = FollowJmpChain((void*)ADDR_B31349);
    if (pRoot)
    {
        oSkillReleaseClassifierRoot = GenericInlineHook5(pRoot, (void*)hkSkillReleaseClassifierRootNaked, 10);
        if (oSkillReleaseClassifierRoot)
        {
            rootOk = true;
            WriteLogFmt("[SkillReleaseHook] OK(B31349): tramp=0x%08X", (DWORD)(uintptr_t)oSkillReleaseClassifierRoot);
        }
        else
        {
            WriteLog("[SkillReleaseHook] root hook failed");
        }
    }
    else
    {
        WriteLog("[SkillReleaseHook] classifier root target missing");
    }

    bool branchOk = false;
    BYTE* pTarget = FollowJmpChain((void*)ADDR_B3144D);
    if (!pTarget) {
        WriteLog("[SkillReleaseHook] classifier branch target missing");
    }
    else
    {
        oSkillReleaseClassifier = GenericInlineHook5(pTarget, (void*)hkSkillReleaseClassifierNaked, 6);
        if (!oSkillReleaseClassifier) {
            WriteLog("[SkillReleaseHook] classifier branch hook failed");
        }
        else
        {
            branchOk = true;
            WriteLogFmt("[SkillReleaseHook] OK(B3144D): tramp=0x%08X", (DWORD)(uintptr_t)oSkillReleaseClassifier);
        }
    }

    bool b2f370Ok = false;
    BYTE* pB2F370 = FollowJmpChain((void*)ADDR_B2F370);
    if (!pB2F370)
    {
        WriteLog("[SkillReleaseHook] B2F370 target missing");
    }
    else
    {
        // 00B2F370 前三条指令长度 = 2 + 5 + 6 = 13
        oSkillReleaseClassifierB2F370 = GenericInlineHook5(
            pB2F370,
            (void*)hkSkillReleaseClassifierB2F370Naked,
            13);
        if (!oSkillReleaseClassifierB2F370)
        {
            WriteLog("[SkillReleaseHook] B2F370 hook failed");
        }
        else
        {
            b2f370Ok = true;
            WriteLogFmt("[SkillReleaseHook] OK(B2F370): tramp=0x%08X", (DWORD)(uintptr_t)oSkillReleaseClassifierB2F370);
        }
    }

    if (!rootOk && !branchOk && !b2f370Ok) {
        return false;
    }
    return true;
}

static bool SetupSkillPresentationHook()
{
    oSkillPresentationDispatch = (tSkillPresentationDispatch)InstallInlineHook(
        ADDR_ABAF70, (void*)hkSkillPresentationDispatch);
    if (!oSkillPresentationDispatch)
    {
        WriteLog("[SkillVisualHook] hook failed");
        return false;
    }

    WriteLogFmt("[SkillVisualHook] OK(ABAF70): tramp=0x%08X", (DWORD)(uintptr_t)oSkillPresentationDispatch);
    return true;
}

static bool SetupNativeButtonAssetPathHook()
{
    if (!ENABLE_NATIVE_BUTTON_SKIN_REMAP)
    {
        WriteLog("[BtnSkinHook] disabled");
        return true;
    }

    oMakeGameWStringHooked = (tMakeGameWString)InstallInlineHook(
        ADDR_402F60, (void*)hkMakeGameWString);
    if (!oMakeGameWStringHooked)
    {
        WriteLog("[BtnSkinHook] hook failed: 402F60");
        return false;
    }

    WriteLogFmt("[BtnSkinHook] OK(402F60): tramp=0x%08X", (DWORD)(uintptr_t)oMakeGameWStringHooked);
    return true;
}

static bool SetupNativeButtonResolveHook()
{
    oButtonResolveCurrentDrawObj = (tButtonResolveCurrentDrawObj)InstallInlineHook(
        ADDR_506EE0, (void*)hkButtonResolveCurrentDrawObj);
    if (!oButtonResolveCurrentDrawObj)
    {
        WriteLog("[BtnResolveHook] hook failed: 506EE0");
        return false;
    }

    WriteLogFmt("[BtnResolveHook] OK(506EE0): tramp=0x%08X", (DWORD)(uintptr_t)oButtonResolveCurrentDrawObj);
    return true;
}

static bool SetupNativeButtonDrawHook()
{
    oButtonDrawCurrentState = (tButtonDrawCurrentState)InstallInlineHook(
        ADDR_507020, (void*)hkButtonDrawCurrentState);
    if (!oButtonDrawCurrentState)
    {
        WriteLog("[BtnDrawHook] hook failed: 507020");
        return false;
    }

    WriteLogFmt("[BtnDrawHook] OK(507020): tramp=0x%08X", (DWORD)(uintptr_t)oButtonDrawCurrentState);
    return true;
}

static bool SetupNativeButtonMetricHooks()
{
    bool ok = false;

    oButtonMetric507DF0 = (tButtonMetricCurrent)InstallInlineHook(
        ADDR_507DF0, (void*)hkButtonMetric507DF0);
    if (oButtonMetric507DF0)
    {
        ok = true;
        WriteLogFmt("[BtnMetricHook] OK(507DF0): tramp=0x%08X", (DWORD)(uintptr_t)oButtonMetric507DF0);
    }
    else
    {
        WriteLog("[BtnMetricHook] hook failed: 507DF0");
    }

    oButtonMetric507ED0 = (tButtonMetricCurrent)InstallInlineHook(
        ADDR_507ED0, (void*)hkButtonMetric507ED0);
    if (oButtonMetric507ED0)
    {
        ok = true;
        WriteLogFmt("[BtnMetricHook] OK(507ED0): tramp=0x%08X", (DWORD)(uintptr_t)oButtonMetric507ED0);
    }
    else
    {
        WriteLog("[BtnMetricHook] hook failed: 507ED0");
    }

    // v16.1: hook 5095A0 to block state changes for SuperBtn in stableNormal mode
    oButtonRefreshState5095A0 = (tRefreshButtonState)InstallInlineHook(
        ADDR_5095A0, (void*)hkButtonRefreshState5095A0);
    if (oButtonRefreshState5095A0)
    {
        ok = true;
        WriteLogFmt("[BtnRefreshHook] OK(5095A0): tramp=0x%08X", (DWORD)(uintptr_t)oButtonRefreshState5095A0);
    }
    else
    {
        WriteLog("[BtnRefreshHook] hook failed: 5095A0");
    }

    // v17.7b: hook sub_529640 的矩形写入点 (0x52972E)
    // 覆盖 8 字节: 52972E(4) + 529732(4) → 5字节jmp + 3字节nop
    {
        BYTE* pPatch = (BYTE*)ADDR_52972E;
        DWORD oldProt = 0;
        if (VirtualProtect(pPatch, 8, PAGE_EXECUTE_READWRITE, &oldProt)) {
            // 5 字节 jmp rel32
            pPatch[0] = 0xE9;
            *(DWORD*)(pPatch + 1) = (DWORD)((uintptr_t)hkRectWrite52972E - (uintptr_t)pPatch - 5);
            // 3 字节 nop
            pPatch[5] = 0x90;
            pPatch[6] = 0x90;
            pPatch[7] = 0x90;
            VirtualProtect(pPatch, 8, oldProt, &oldProt);
            FlushInstructionCache(GetCurrentProcess(), pPatch, 8);
            WriteLog("[RectWriteHook] OK: 0x52972E patched");
        } else {
            WriteLog("[RectWriteHook] FAIL: VirtualProtect failed");
        }
    }

    return ok;
}

static bool SetupSkillNativeIdGateHooks()
{
    bool ok = false;

    BYTE* pGate7CE790 = FollowJmpChain((void*)ADDR_7CE790);
    if (pGate7CE790)
    {
        oSkillNativeIdGate7CE790 = (tSkillNativeIdGateFn)GenericInlineHook5(
            pGate7CE790, (void*)hkSkillNativeIdGate7CE790, 9);
    }
    else
    {
        oSkillNativeIdGate7CE790 = nullptr;
    }
    if (oSkillNativeIdGate7CE790)
    {
        ok = true;
        WriteLogFmt("[SkillGate] OK(7CE790): tramp=0x%08X", (DWORD)(uintptr_t)oSkillNativeIdGate7CE790);
    }
    else
    {
        WriteLog("[SkillGate] hook failed: 7CE790");
    }

    BYTE* pGate7D0000 = FollowJmpChain((void*)ADDR_7D0000);
    if (pGate7D0000)
    {
        oSkillNativeIdGate7D0000 = (tSkillNativeIdGateFn)GenericInlineHook5(
            pGate7D0000, (void*)hkSkillNativeIdGate7D0000, 9);
    }
    else
    {
        oSkillNativeIdGate7D0000 = nullptr;
    }
    if (oSkillNativeIdGate7D0000)
    {
        ok = true;
        WriteLogFmt("[SkillGate] OK(7D0000): tramp=0x%08X", (DWORD)(uintptr_t)oSkillNativeIdGate7D0000);
    }
    else
    {
        WriteLog("[SkillGate] hook failed: 7D0000");
    }

    return ok;
}

static bool SetupSkillLevelLookupHooks()
{
    bool ok = false;

    oSkillLevelBase = (tSkillLevelBaseFn)InstallInlineHook(
        ADDR_7DA7D0, (void*)hkSkillLevelBase);
    if (oSkillLevelBase)
    {
        ok = true;
        WriteLogFmt("[SkillLevelHook] OK(7DA7D0): tramp=0x%08X", (DWORD)(uintptr_t)oSkillLevelBase);
    }
    else
    {
        WriteLog("[SkillLevelHook] hook failed: 7DA7D0");
    }

    oSkillLevelCurrent = (tSkillLevelCurrentFn)InstallInlineHook(
        ADDR_7DBC50, (void*)hkSkillLevelCurrent);
    if (oSkillLevelCurrent)
    {
        ok = true;
        WriteLogFmt("[SkillLevelHook] OK(7DBC50): tramp=0x%08X", (DWORD)(uintptr_t)oSkillLevelCurrent);
    }
    else
    {
        WriteLog("[SkillLevelHook] hook failed: 7DBC50");
    }

    return ok;
}

static bool SetupNativeTextGlyphHook()
{
    if (SafeIsBadReadPtr((void*)ADDR_5000E520, 8))
    {
        WriteLogFmt("[NativeText] glyph target unreadable: 0x%08X", ADDR_5000E520);
        return false;
    }

    oNativeGlyphLookup = (tNativeGlyphLookupFn)InstallInlineHook(
        ADDR_5000E520, (void*)hkNativeGlyphLookup);
    if (!oNativeGlyphLookup)
    {
        WriteLog("[NativeText] glyph hook failed: 5000E520");
        return false;
    }

    RetroSkillDWriteRegisterNativeGlyphLookup((void*)oNativeGlyphLookup);
    WriteLogFmt("[NativeText] OK(5000E520): tramp=0x%08X", (DWORD)(uintptr_t)oNativeGlyphLookup);
    return true;
}

// ============================================================================
// route-B child draw接入确认
// 说明：
//   v10.1 起不再 hook 52AA90 函数体；draw 由自定义 VT1 槽位接到 SuperCWndDraw
// ============================================================================
static bool SetupSuperChildDrawHook()
{
    WriteLog("[SuperChildDrawHook] VT1-draw route enabled");
    return true;
}

// ============================================================================
// SkillWnd 移动 hook安装
// ============================================================================
static bool SetupSkillWndMoveHook()
{
    oSkillWndMove = (tSkillWndMove)InstallInlineHook(
        ADDR_9D95A0, (void*)hkSkillWndMoveNaked);
    if (!oSkillWndMove) {
        WriteLog("[MoveHook] Hook failed");
        return false;
    }
    WriteLogFmt("[MoveHook] OK: tramp=0x%08X", (DWORD)oSkillWndMove);
    return true;
}

// ============================================================================
// SkillWnd refresh hook安装
// ============================================================================
static bool SetupSkillWndRefreshHook()
{
    oSkillWndRefresh = (tSkillWndRefresh)InstallInlineHook(
        ADDR_9E1770, (void*)hkSkillWndRefreshNaked);
    if (!oSkillWndRefresh) {
        WriteLog("[RefreshHook] Hook failed");
        return false;
    }
    WriteLogFmt("[RefreshHook] OK: tramp=0x%08X", (DWORD)oSkillWndRefresh);
    return true;
}

// ============================================================================
// SkillWnd 绘制 Hook安装
// ============================================================================
static bool SetupSkillWndDrawHook()
{
    oSkillWndDraw = (tSkillWndDraw)InstallInlineHook(
        ADDR_9DEE30, (void*)hkSkillWndDrawNaked);
    if (!oSkillWndDraw) {
        WriteLog("[DrawHook] Hook failed");
        return false;
    }
    WriteLogFmt("[DrawHook] OK: tramp=0x%08X", (DWORD)oSkillWndDraw);
    return true;
}

static bool SetupPostB9F6E0TimingTestHook()
{
    if (!ENABLE_POST_B9F6E0_NATIVE_TIMING_TEST)
        return true;

    oPostB9F6E0DrawContinue = GenericInlineHook5(
        (BYTE*)ADDR_BBC965,
        (void*)hkPostB9F6E0DrawNaked,
        9);
    if (!oPostB9F6E0DrawContinue) {
        WriteLog("[PostUiTimingTest] hook failed at BBC965");
        return false;
    }

    WriteLogFmt("[PostUiTimingTest] OK(BBC965): tramp=0x%08X", (DWORD)(uintptr_t)oPostB9F6E0DrawContinue);
    return true;
}

// ============================================================================
// SkillWnd 析构 Hook安装
// ============================================================================
static bool SetupSkillWndDtorHook()
{
    oSkillWndDtor = (tSkillWndDtor)InstallInlineHook(
        ADDR_9E14D0, (void*)hkSkillWndDtorNaked);
    if (!oSkillWndDtor) {
        WriteLog("[DtorHook] Hook failed");
        return false;
    }
    WriteLogFmt("[DtorHook] OK: tramp=0x%08X", (DWORD)oSkillWndDtor);
    return true;
}

// ============================================================================
// WndProc Hook安装
// ============================================================================
static bool SetupWndProcHook()
{
    g_GameHwnd = GetRealGameWindow();
    if (!g_GameHwnd) {
        WriteLog("[WndProc] Game window not found");
        return false;
    }
    g_OriginalWndProc = (WNDPROC)SetWindowLongPtrA(g_GameHwnd, GWLP_WNDPROC, (LONG_PTR)GameWndProc);
    WriteLogFmt("[WndProc] Hooked: 0x%08X", (DWORD)g_OriginalWndProc);
    return g_OriginalWndProc != nullptr;
}

// ============================================================================
// 初始化线程
// ============================================================================
static DWORD WINAPI InitThread(LPVOID)
{
    WriteLog("=== SuperSkillWnd Init v14.9 (ImGui Overlay Panel) ===");
    WriteLogFmt("[Build] marker=%s", BUILD_MARKER);
    WriteLogFmt("[Build] panel_gap=%d btn_off=(%d,%d) vt_delta=%d gone_debounce=%u present_click_poll=%d present_panel_draw=%d routeb_alloc=0x%X focusSync(toggle=%d,move=%d) presentNativeChildUpdate=%d refreshNativeChildUpdate=%d",
        PANEL_LEFT_GAP, BTN_X_OFFSET, BTN_Y_OFFSET, PREFER_VT_DELTA, SKILLWND_GONE_DEBOUNCE_MS,
        ENABLE_PRESENT_CLICK_POLL ? 1 : 0, ENABLE_PRESENT_PANEL_DRAW ? 1 : 0, ROUTEB_CHILD_ALLOC_SIZE,
        ENABLE_TOGGLE_FOCUS_SYNC ? 1 : 0, ENABLE_MOVE_FOCUS_SYNC ? 1 : 0,
        ENABLE_PRESENT_NATIVE_CHILD_UPDATE ? 1 : 0, ENABLE_REFRESH_NATIVE_CHILD_UPDATE ? 1 : 0);
    WriteLogFmt("[Build] native_button_skin_remap=%d", ENABLE_NATIVE_BUTTON_SKIN_REMAP ? 1 : 0);
    Sleep(2000);

    g_SkillMgr.Initialize();
    SkillOverlayBridgeInitialize(&g_SkillMgr);

    // 运行时检测 D3D8 还是 D3D9
    if (::GetModuleHandleA("d3d8.dll")) {
        g_IsD3D8Mode = true;
        WriteLog("[D3D] Detected D3D8 mode (d3d8.dll loaded)");
        if (!SetupD3D8Hook()) { WriteLog("D3D8 hook FAILED"); return 1; }
    } else {
        WriteLog("[D3D] Using D3D9 mode");
        if (!SetupD3D9Hook()) { WriteLog("D3D9 hook FAILED"); return 1; }
    }
    if (!SetupNativeButtonAssetPathHook()) { WriteLog("BtnSkinHook failed (non-fatal)"); }
    if (!SetupNativeButtonResolveHook()) { WriteLog("BtnResolveHook failed (non-fatal)"); }
    if (!SetupNativeButtonDrawHook()) { WriteLog("BtnDrawHook failed (non-fatal)"); }
    if (!SetupNativeButtonMetricHooks()) { WriteLog("BtnMetricHook failed (non-fatal)"); }
    if (!SetupPacketHook())    { WriteLog("PacketHook failed (non-fatal)"); }
    if (!SetupSkillReleaseClassifierHook()) { WriteLog("SkillReleaseHook failed (non-fatal)"); }
    if (!SetupSkillPresentationHook()) { WriteLog("SkillVisualHook failed (non-fatal)"); }
    if (!SetupSkillNativeIdGateHooks()) { WriteLog("SkillGate hooks failed (non-fatal)"); }
    if (!SetupSkillLevelLookupHooks()) { WriteLog("SkillLevel hooks failed (non-fatal)"); }
    WriteLog("[NativeText] disabled: using self renderer");
    bool childDrawHookOk = true;
    bool moveHookOk = true;
    bool refreshHookOk = true;
    if (!ENABLE_IMGUI_OVERLAY_PANEL) {
        childDrawHookOk = SetupSuperChildDrawHook();
        moveHookOk = SetupSkillWndMoveHook();
        refreshHookOk = SetupSkillWndRefreshHook();
        if (!childDrawHookOk) { WriteLog("Super child draw route failed (route-B blocked)"); }
        if (!moveHookOk) { WriteLog("SkillWnd move hook failed (route-B degraded)"); }
        if (!refreshHookOk) { WriteLog("SkillWnd refresh hook failed (route-B degraded)"); }
    }
    g_SuperChildHooksReady = childDrawHookOk && moveHookOk && refreshHookOk;
    if (!SetupSkillWndHook())  { WriteLog("SkillWnd hook failed (non-fatal)"); }
    if (!SetupSkillWndDrawHook()) { WriteLog("SkillWnd draw hook failed (non-fatal)"); }
    if (!SetupPostB9F6E0TimingTestHook()) { WriteLog("PostUiTimingTest hook failed (non-fatal)"); }
    if (!SetupSkillListBuildFilterHook()) { WriteLog("SkillListFilter hook failed (non-fatal)"); }
    if (!SetupSkillWndDtorHook()) { WriteLog("SkillWnd dtor hook failed (non-fatal)"); }
    if (!SetupMsgHook())       { WriteLog("MsgHook failed (non-fatal)"); }
    if (!SetupWndProcHook())   { WriteLog("WndProc hook FAILED"); return 1; }
    if (!Win32InputSpoofInstall()) { WriteLog("[InputSpoof] install failed (non-fatal)"); }

    WriteLogFmt("[Build] routeB_hooks_ready=%d imgui_overlay=%d", g_SuperChildHooksReady ? 1 : 0, ENABLE_IMGUI_OVERLAY_PANEL ? 1 : 0);
    WriteLog("=== SuperSkillWnd Ready v15.0 ===");
    return 0;
}

// ============================================================================
// 清理
// ============================================================================
static void CleanupSuperCWnd()
{
    ResetSuperRuntimeState(false, "dll_detach");
    if (ENABLE_IMGUI_OVERLAY_PANEL) {
        if (g_IsD3D8Mode)
            SuperD3D8OverlayShutdown();
        else
            SuperImGuiOverlayShutdown();
    }
    ReleaseAllD3D9Textures("dll_detach");
    SkillOverlayBridgeShutdown();
    if (Win32InputSpoofIsInstalled()) {
        Win32InputSpoofSetSuppressMouse(false);
        Win32InputSpoofUninstall();
    }
}

// ============================================================================
// DLL入口
// ============================================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID)
{
    if (dwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        g_hModule = hModule;
        FILE* f = fopen(LOG_FILE, "w");
    if (f) { fprintf(f, "=== SuperSkillWnd v14.9 (ImGui Overlay Panel) ===\n"); fclose(f); }
        char dllPath[MAX_PATH] = {};
        if (GetModuleFileNameA(hModule, dllPath, MAX_PATH) > 0) {
            WriteLogFmt("[Build] module=%s", dllPath);
        }
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        CleanupSuperCWnd();
        if (g_OriginalWndProc && g_GameHwnd)
            SetWindowLongPtrA(g_GameHwnd, GWLP_WNDPROC, (LONG_PTR)g_OriginalWndProc);
    }
    return TRUE;
}
