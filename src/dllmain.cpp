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
#include "runtime/init_pipeline.h"
#include "runtime/cleanup_pipeline.h"
#include <algorithm>
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
static bool g_D3D8TexturesLoaded = false;
static void* g_D3D8TextureDevice = nullptr;
static D3D8Texture g_d3d8TexBtnNormal = {};
static D3D8Texture g_d3d8TexBtnHover = {};
static D3D8Texture g_d3d8TexBtnPressed = {};
static D3D8Texture g_d3d8TexBtnDisabled = {};
static D3D8Texture g_d3d8TexCursorNormal = {};
static D3D8Texture g_d3d8TexCursorHoverA = {};
static D3D8Texture g_d3d8TexCursorHoverB = {};
static D3D8Texture g_d3d8TexCursorPressed = {};
static HWND g_D3D8GameHwnd = nullptr;
static volatile LONG g_PresentBtnDrawLogBudget = 32;
static volatile LONG g_BtnD3DDrawLogBudget = 40;
static volatile LONG g_SkillBtnD3DDrawLogBudget = 40;
static volatile LONG g_SuperBtnClipLogBudget = 40;
static volatile LONG g_PostUiTimingTestLogBudget = 48;
static volatile LONG g_PresentCursorDrawLogBudget = 24;

enum SuperCursorFrameKind
{
    SUPER_CURSOR_FRAME_NORMAL = 0,
    SUPER_CURSOR_FRAME_HOVER_A,
    SUPER_CURSOR_FRAME_HOVER_B,
    SUPER_CURSOR_FRAME_PRESSED,
};

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
static DWORD* g_CustomVTable2     = nullptr;
static DWORD g_OriginalSecondChildMsgFn = ADDR_9D98F0;
static uintptr_t g_CustomVTable2OwnerChild = 0;
static bool g_SuperChildHooksReady = false;
static bool g_SuperUsesSkillWndSecondSlot = false;
#if defined(SSW_ENABLE_SECOND_CHILD_CARRIER_PROBE_RUNTIME)
static const bool ENABLE_IMGUI_OVERLAY_PANEL = false;
#else
static const bool ENABLE_IMGUI_OVERLAY_PANEL = true;
#endif
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
static const int BTN_X_OFFSET = -216;   // 还原到已验证稳定基线
static const int BTN_Y_OFFSET = -44;    // 还原到已验证稳定基线
static const int BTN_COMPARE_DEBUG_DX = 60;
static const int BTN_METRIC_FALLBACK_X = 10;
static const int BTN_METRIC_FALLBACK_Y = 255;
static const int PREFER_VT_DELTA = 80;
static const int ROUTEB_CHILD_ALLOC_SIZE = 0xAE8;
static const int SUPER_CHILD_DONOR_MODE = 1; // 9DDB30 的 a2 实际是“伪指针型 ctrlID”；3001 -> a2-750 的有效模式是 1..4，不是 2251
static const DWORD SKILLWND_GONE_DEBOUNCE_MS = 800;
static const bool ENABLE_NATIVE_BUTTON_INSTANCE_SKIN = true;
static const bool ENABLE_SUPERBTN_D3D_BUTTON_MODE = false; // 回退到原生可见按钮，避免 D3D 按钮模式坐标漂移后“消失”
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
#if defined(SSW_ENABLE_SECOND_CHILD_CARRIER_PROBE_RUNTIME)
static const char* BUILD_MARKER = "v20.4-2026-04-11-second-child-vt2-log";
#else
static const char* BUILD_MARKER = "v21.0-2026-04-20-d3d8-mount-rootfix";
#endif
static const wchar_t* SUPER_BTN_RES_PATH = L"UI/UIWindow2.img/Skill/main/BtMacro";
static const wchar_t* SUPER_BTN_RES_PATH_ALT = L"/UIWindow2.img/Skill/main/BtMacro";
static int g_PanelDrawX = -9999;
static int g_PanelDrawY = -9999;
static LONG g_SecondChildStateLogBudget = 160;
static LONG g_SecondChildMouseSwallowBudget = 32;
static LONG g_SecondChildMousePassBudget = 48;
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
static uintptr_t GetSkillWndSecondChildPtr(uintptr_t skillWndThis);
static bool IsOfficialSecondChildObject(uintptr_t wndObj, bool allowCustomVT1, bool allowCustomVT2 = false);
static void LogOfficialSecondChildState(uintptr_t wndObj, const char* tag);
static bool ApplySuperChildCustomMouseGuardVTable(uintptr_t wndObj);
static void RestoreSuperChildCustomMouseGuardVTable(uintptr_t wndObj, const char* reason);
static void MarkSuperWndDirty(uintptr_t wndObj, const char* logTag);
static void ForwardGameMouseOffscreenNow();
static void UpdateGameMouseSuppressionFallback(bool suppressMouse);
static void RefreshGameCursorImmediately();
static void EnsureDeferredInteractionHooks(const char* reason);
static bool SetupD3D8Hook();
static bool SetupD3D9Hook();
static void ClearGameVariant(VARIANTARG* pv);
static void ReleaseUiObj(void* obj);
static bool ResolveNativeImage(const unsigned short* pathWide, void** outImage, const char* logTag);
static bool IsPointInRectPad(int mx, int my, int x, int y, int w, int h, int pad);
static bool GetExpectedButtonRectCom(int* outX, int* outY, int* outW, int* outH);
static bool GetExpectedButtonRectVt(int* outX, int* outY, int* outW, int* outH);
static void LogSuperButtonGeometry(const char* tag);
static void DrawPostB9F6E0NativeTimingTest();
static void DrawSuperButtonCursorInPresent(IDirect3DDevice9* pDevice);
#if defined(SSW_ENABLE_SECOND_CHILD_CARRIER_PROBE_RUNTIME)
extern "C" void __stdcall SSW_SecondChildCarrierProbe_RunOnce(DWORD skillWndThis32, DWORD flags, int explicitPanelX, int explicitPanelY);
extern "C" void __stdcall SSW_SecondChildCarrierProbe_ObserveWndProc(UINT msg, WPARAM wParam, LPARAM lParam);
extern "C" void __stdcall SSW_SecondChildCarrierProbe_Poll(DWORD skillWndThis32, DWORD reasonCode);
extern "C" void __stdcall SSW_SecondChildCarrierProbe_Release(DWORD skillWndThis32);
static const DWORD SECOND_CHILD_CARRIER_PROBE_FLAGS =
    0x00000001 |
    0x00000002 |
    0x00000004 |
    0x00000008 |
    0x00000010 |
    0x00000020 |
    0x00000040;
static DWORD g_LastCarrierProbePollTick = 0;
static void RunSecondChildCarrierProbeHotkey();
static void PollSecondChildCarrierProbeTick(DWORD reasonCode, bool force);
static void ReleaseSecondChildCarrierProbeHotkey();
#endif

// Runtime implementation sections (extracted from dllmain for modular readability)
#include "runtime/dllmain_section_core_ui.inl"
#include "runtime/dllmain_section_button_patch.inl"
#include "runtime/dllmain_section_runtime_hooks.inl"

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
#if defined(SSW_ENABLE_SECOND_CHILD_CARRIER_PROBE_RUNTIME)
    WriteLog("[CarrierProbe] runtime enabled hotkeys: F10=run once, F11=poll, F12=release");
#endif
    Sleep(2000);

    g_SkillMgr.Initialize();
    SkillOverlayBridgeInitialize(&g_SkillMgr);

    g_IsD3D8Mode = (::GetModuleHandleA("d3d8.dll") != nullptr) ||
                   (::GetModuleHandleA("d3d8thk.dll") != nullptr);

    SuperRuntimeInstallOptions installOptions = {};
    installOptions.isD3D8Mode = g_IsD3D8Mode;
    installOptions.enableImguiOverlayPanel = ENABLE_IMGUI_OVERLAY_PANEL;

    SuperRuntimeInstallCallbacks installCallbacks = {};
    installCallbacks.setupD3D8Hook = &SetupD3D8Hook;
    installCallbacks.setupD3D9Hook = &SetupD3D9Hook;
    installCallbacks.setupNativeButtonAssetPathHook = &SetupNativeButtonAssetPathHook;
    installCallbacks.setupNativeButtonResolveHook = &SetupNativeButtonResolveHook;
    installCallbacks.setupNativeButtonDrawHook = &SetupNativeButtonDrawHook;
    installCallbacks.setupNativeButtonMetricHooks = &SetupNativeButtonMetricHooks;
    installCallbacks.setupPacketHook = &SetupPacketHook;
    installCallbacks.setupSkillReleaseClassifierHook = &SetupSkillReleaseClassifierHook;
    installCallbacks.setupSkillPresentationHook = &SetupSkillPresentationHook;
    installCallbacks.setupSkillNativeIdGateHooks = &SetupSkillNativeIdGateHooks;
    installCallbacks.setupSkillLevelLookupHooks = &SetupSkillLevelLookupHooks;
    installCallbacks.setupSuperChildDrawHook = &SetupSuperChildDrawHook;
    installCallbacks.setupSkillWndMoveHook = &SetupSkillWndMoveHook;
    installCallbacks.setupSkillWndRefreshHook = &SetupSkillWndRefreshHook;
    installCallbacks.setupSkillWndHook = &SetupSkillWndHook;
    installCallbacks.setupSkillWndDrawHook = &SetupSkillWndDrawHook;
    installCallbacks.setupPostB9F6E0TimingTestHook = &SetupPostB9F6E0TimingTestHook;
    installCallbacks.setupSkillListBuildFilterHook = &SetupSkillListBuildFilterHook;
    installCallbacks.setupSkillWndDtorHook = &SetupSkillWndDtorHook;
    installCallbacks.setupMsgHook = &SetupMsgHook;
    installCallbacks.setupWndProcHook = &SetupWndProcHook;
    installCallbacks.installInputSpoof = &Win32InputSpoofInstall;

    SuperRuntimeInstallResult installResult = {};
    if (!SuperRuntimeRunInstallPipeline(installOptions, installCallbacks, &installResult))
        return 1;

    g_IsD3D8Mode = installResult.isD3D8Mode;
    g_SuperChildHooksReady = installResult.superChildHooksReady;

    WriteLogFmt("[Build] routeB_hooks_ready=%d imgui_overlay=%d", g_SuperChildHooksReady ? 1 : 0, ENABLE_IMGUI_OVERLAY_PANEL ? 1 : 0);
    WriteLog("=== SuperSkillWnd Ready v15.0 ===");
    return 0;
}

// ============================================================================
// 清理
// ============================================================================
static void CleanupSuperCWnd()
{
    SuperRuntimeCleanupOptions options = {};
    options.enableImguiOverlayPanel = ENABLE_IMGUI_OVERLAY_PANEL;
    options.isD3D8Mode = g_IsD3D8Mode;
    options.reason = "dll_detach";

    SuperRuntimeCleanupCallbacks callbacks = {};
    callbacks.resetSuperRuntimeState = &ResetSuperRuntimeState;
    callbacks.shutdownD3D8Overlay = &SuperD3D8OverlayShutdown;
    callbacks.shutdownD3D9Overlay = &SuperImGuiOverlayShutdown;
    callbacks.releaseAllD3D8Textures = &ReleaseAllD3D8Textures;
    callbacks.releaseAllD3D9Textures = &ReleaseAllD3D9Textures;
    callbacks.shutdownSkillOverlayBridge = &SkillOverlayBridgeShutdown;
    callbacks.isInputSpoofInstalled = &Win32InputSpoofIsInstalled;
    callbacks.setInputSpoofSuppressMouse = &Win32InputSpoofSetSuppressMouse;
    callbacks.uninstallInputSpoof = &Win32InputSpoofUninstall;

    SuperRuntimeRunCleanupPipeline(options, callbacks);
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
