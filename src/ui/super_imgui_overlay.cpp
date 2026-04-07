#include "ui/super_imgui_overlay.h"

#include "core/Common.h"
#include "core/GameAddresses.h"
#include "skill/skill_overlay_bridge.h"
#include "ui/retro_skill_app.h"
#include "ui/retro_skill_panel.h"
#include "ui/retro_skill_text_dwrite.h"

#include "third_party/imgui/imgui.h"
#include "third_party/imgui/backends/imgui_impl_dx9.h"
#include "third_party/imgui/backends/imgui_impl_win32.h"

#include <string>
#include <cfloat>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    struct QuickSlotWindowProbe
    {
        bool found = false;
        uintptr_t wnd = 0;
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        const char* posSource = "none";
    };

    struct SuperOverlayRuntime
    {
        bool initialized = false;
        bool visible = false;
        bool mouseCapture = false;
        bool mouseHover = false;
        bool cursorSuppressed = false;
        bool showCursorHidden = false;
        HWND hwnd = nullptr;
        HCURSOR savedCursor = nullptr;
        IDirect3DDevice9* device = nullptr;
        ImGuiContext* context = nullptr;
        ImFont* mainFont = nullptr;
        float mainScale = 1.0f;
        int anchorX = -9999;
        int anchorY = -9999;
        RetroSkillRuntimeState state;
        RetroSkillAssets assets;
        RetroSkillBehaviorHooks hooks;
        std::string assetPath;
    };

    SuperOverlayRuntime g_overlay;

    bool GetClientMousePointFromMessage(HWND hwnd, UINT msg, LPARAM lParam, POINT* outPoint);
    bool IsPointInsidePanel(int x, int y);
    bool OverlayOwnsMouseInput();

    bool IsMouseMessage(UINT msg)
    {
        switch (msg)
        {
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
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
            return true;
        default:
            return false;
        }
    }

    bool IsKeyboardMessage(UINT msg)
    {
        switch (msg)
        {
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_IME_CHAR:
            return true;
        default:
            return false;
        }
    }

    bool IsMouseButtonMessage(UINT msg)
    {
        switch (msg)
        {
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
            return true;
        default:
            return false;
        }
    }

    int ToImGuiMouseButton(UINT msg, WPARAM wParam)
    {
        switch (msg)
        {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            return 0;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
            return 1;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
            return 2;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
            return (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? 3 : 4;
        default:
            return -1;
        }
    }

    bool IsMouseButtonDownMessage(UINT msg)
    {
        switch (msg)
        {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONDBLCLK:
            return true;
        default:
            return false;
        }
    }

    bool IsMouseButtonUpMessage(UINT msg)
    {
        switch (msg)
        {
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP:
            return true;
        default:
            return false;
        }
    }

    bool FeedMouseEventToImGui(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        ImGuiIO& io = ImGui::GetIO();
        bool insidePanel = false;

        if (msg == WM_MOUSELEAVE || msg == WM_NCMOUSELEAVE)
        {
            g_overlay.mouseHover = false;
            if (!OverlayOwnsMouseInput())
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            return OverlayOwnsMouseInput();
        }

        POINT pt = {};
        if (!GetClientMousePointFromMessage(hwnd, msg, lParam, &pt))
            return false;

        insidePanel = IsPointInsidePanel(pt.x, pt.y);

        if (msg == WM_MOUSEMOVE)
        {
            if (insidePanel || OverlayOwnsMouseInput())
            {
                io.AddMousePosEvent((float)pt.x, (float)pt.y);
                g_overlay.mouseHover = insidePanel;
                return true;
            }
            g_overlay.mouseHover = false;
            return false;
        }

        if (IsMouseButtonMessage(msg))
        {
            const int button = ToImGuiMouseButton(msg, wParam);
            if (button < 0)
                return false;

            if (IsMouseButtonDownMessage(msg))
            {
                if (!(insidePanel || OverlayOwnsMouseInput()))
                    return false;

                io.AddMousePosEvent((float)pt.x, (float)pt.y);
                io.AddMouseButtonEvent(button, true);
                g_overlay.mouseHover = insidePanel;
                g_overlay.mouseCapture = true;
                return true;
            }

            if (IsMouseButtonUpMessage(msg))
            {
                if (!(insidePanel || OverlayOwnsMouseInput()))
                    return false;

                io.AddMousePosEvent((float)pt.x, (float)pt.y);
                io.AddMouseButtonEvent(button, false);
                g_overlay.mouseHover = insidePanel;
                if (!io.MouseDown[0] && !io.MouseDown[1] && !io.MouseDown[2] && !io.MouseDown[3] && !io.MouseDown[4])
                    g_overlay.mouseCapture = false;
                return true;
            }
        }

        if (msg == WM_MOUSEWHEEL)
        {
            if (!(insidePanel || OverlayOwnsMouseInput()))
                return false;
            io.AddMousePosEvent((float)pt.x, (float)pt.y);
            io.AddMouseWheelEvent(0.0f, (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA);
            g_overlay.mouseHover = insidePanel;
            return true;
        }

        if (msg == WM_MOUSEHWHEEL)
        {
            if (!(insidePanel || OverlayOwnsMouseInput()))
                return false;
            io.AddMousePosEvent((float)pt.x, (float)pt.y);
            io.AddMouseWheelEvent(-(float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA, 0.0f);
            g_overlay.mouseHover = insidePanel;
            return true;
        }

        return false;
    }

    bool GetClientMousePointFromMessage(HWND hwnd, UINT msg, LPARAM lParam, POINT* outPoint)
    {
        if (!outPoint)
            return false;

        auto getSignedX = [](LPARAM lp) -> LONG {
            return (LONG)(short)LOWORD(lp);
        };
        auto getSignedY = [](LPARAM lp) -> LONG {
            return (LONG)(short)HIWORD(lp);
        };

        POINT pt = {};
        switch (msg)
        {
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            pt.x = getSignedX(lParam);
            pt.y = getSignedY(lParam);
            if (!hwnd || !::ScreenToClient(hwnd, &pt))
                return false;
            break;
        default:
            pt.x = getSignedX(lParam);
            pt.y = getSignedY(lParam);
            break;
        }

        *outPoint = pt;
        return true;
    }

    bool IsPointInsidePanel(int x, int y)
    {
        if (g_overlay.anchorX <= -9000 || g_overlay.anchorY <= -9000)
            return false;
        const PanelMetrics metrics = GetPanelMetrics(g_overlay.mainScale);
        return x >= g_overlay.anchorX &&
               x < (int)(g_overlay.anchorX + metrics.width) &&
               y >= g_overlay.anchorY &&
               y < (int)(g_overlay.anchorY + metrics.height);
    }

    bool IsPointInsidePanelClientRect(HWND hwnd, UINT msg, LPARAM lParam)
    {
        POINT pt = {};
        if (!GetClientMousePointFromMessage(hwnd, msg, lParam, &pt))
            return false;
        return IsPointInsidePanel(pt.x, pt.y);
    }

    bool IsCurrentMouseInsidePanel()
    {
        if (!g_overlay.hwnd)
            return false;

        POINT pt = {};
        if (!::GetCursorPos(&pt))
            return false;
        if (!::ScreenToClient(g_overlay.hwnd, &pt))
            return false;
        return IsPointInsidePanel(pt.x, pt.y);
    }

    bool TryGetCurrentMouseClientPos(POINT* outPoint)
    {
        if (!outPoint || !g_overlay.hwnd)
            return false;

        POINT pt = {};
        if (!::GetCursorPos(&pt))
            return false;
        if (!::ScreenToClient(g_overlay.hwnd, &pt))
            return false;

        *outPoint = pt;
        return true;
    }

    bool OverlayOwnsMouseInput()
    {
        return g_overlay.mouseCapture || g_overlay.state.isDraggingSkill;
    }

    bool IsOverlayWindowInteractive()
    {
        return g_overlay.hwnd && (::GetForegroundWindow() == g_overlay.hwnd);
    }

    bool AreAnyPhysicalMouseButtonsDown()
    {
        return ((::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) ||
               ((::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0) ||
               ((::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0) ||
               ((::GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0) ||
               ((::GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0);
    }

    bool IsReasonableWindowCoord(int value)
    {
        return value > -10000 && value < 10000;
    }

    bool ResolveQuickSlotProbePos(uintptr_t wnd, int* outX, int* outY, const char** outSource)
    {
        if (!wnd || !outX || !outY)
            return false;

        const int renderX = CWnd_GetRenderX(wnd);
        const int renderY = CWnd_GetRenderY(wnd);
        if (IsReasonableWindowCoord(renderX) && IsReasonableWindowCoord(renderY) &&
            !(renderX == 0 && renderY == 0))
        {
            *outX = renderX;
            *outY = renderY;
            if (outSource)
                *outSource = "render";
            return true;
        }

        const int comX = CWnd_GetX(wnd);
        const int comY = CWnd_GetY(wnd);
        if (IsReasonableWindowCoord(comX) && IsReasonableWindowCoord(comY))
        {
            *outX = comX;
            *outY = comY;
            if (outSource)
                *outSource = "com";
            return true;
        }

        const int homeX = CWnd_GetHomeX(wnd);
        const int homeY = CWnd_GetHomeY(wnd);
        if (IsReasonableWindowCoord(homeX) && IsReasonableWindowCoord(homeY))
        {
            *outX = homeX;
            *outY = homeY;
            if (outSource)
                *outSource = "home";
            return true;
        }

        return false;
    }

    bool ProbeQuickSlotTopLevelWindow(int expectedOriginX, int expectedOriginY, bool wantCollapsedCandidate, QuickSlotWindowProbe* outProbe)
    {
        if (!outProbe || SafeIsBadReadPtr((void*)ADDR_CWndMan, 4))
            return false;

        uintptr_t wndMan = *(uintptr_t*)ADDR_CWndMan;
        if (!wndMan)
            return false;

        const uintptr_t topLevelArray = wndMan + CWNDMAN_TOPLEVEL_OFF;
        const int kScanCount = 512;
        if (SafeIsBadReadPtr((void*)topLevelArray, kScanCount * sizeof(uintptr_t)))
            return false;

        const int expectedCenterX = expectedOriginX + (SKILL_BAR_COLS * SKILL_BAR_SLOT_SIZE) / 2;
        const int expectedCenterY = expectedOriginY + (SKILL_BAR_ROWS * SKILL_BAR_SLOT_SIZE) / 2;
        int bestScore = 0x7fffffff;
        QuickSlotWindowProbe bestProbe = {};

        for (int i = 0; i < kScanCount; ++i)
        {
            uintptr_t wnd = *(uintptr_t*)(topLevelArray + i * sizeof(uintptr_t));
            if (!wnd || SafeIsBadReadPtr((void*)wnd, 0x30))
                continue;

            const int w = CWnd_GetWidth(wnd);
            const int h = CWnd_GetHeight(wnd);
            if (w <= 0 || h <= 0 || w > 400 || h > 160)
                continue;

            int x = 0;
            int y = 0;
            const char* posSource = "none";
            if (!ResolveQuickSlotProbePos(wnd, &x, &y, &posSource))
                continue;

            const int centerX = x + w / 2;
            const int centerY = y + h / 2;
            const int dx = abs(centerX - expectedCenterX);
            const int dy = abs(centerY - expectedCenterY);
            if (dx > 180 || dy > 120)
                continue;

            bool sizeMatch = false;
            int sizePenalty = 0;
            if (wantCollapsedCandidate)
            {
                sizeMatch = (w >= 12 && w <= 96 && h >= 12 && h <= 56);
                sizePenalty = abs(w - 34) + abs(h - 34);
            }
            else
            {
                sizeMatch = (w >= 110 && w <= 220 && h >= 45 && h <= 95);
                sizePenalty = abs(w - (SKILL_BAR_COLS * SKILL_BAR_SLOT_SIZE)) + abs(h - (SKILL_BAR_ROWS * SKILL_BAR_SLOT_SIZE));
            }

            if (!sizeMatch)
                continue;

            const int score = dx + dy + sizePenalty;
            if (score >= bestScore)
                continue;

            bestScore = score;
            bestProbe.found = true;
            bestProbe.wnd = wnd;
            bestProbe.x = x;
            bestProbe.y = y;
            bestProbe.w = w;
            bestProbe.h = h;
            bestProbe.posSource = posSource;
        }

        if (!bestProbe.found)
            return false;

        *outProbe = bestProbe;
        return true;
    }

    bool ShouldUseOverlayCursor()
    {
        return g_overlay.initialized &&
               g_overlay.visible &&
               IsOverlayWindowInteractive() &&
               (OverlayOwnsMouseInput() || IsCurrentMouseInsidePanel());
    }

    void UpdateCursorSuppression(bool shouldSuppress)
    {
        if (shouldSuppress)
        {
            if (!g_overlay.cursorSuppressed)
            {
                g_overlay.savedCursor = ::GetCursor();
                g_overlay.cursorSuppressed = true;
            }
            if (!g_overlay.showCursorHidden)
            {
                while (::ShowCursor(FALSE) >= 0) {}
                g_overlay.showCursorHidden = true;
            }
            ::SetCursor(nullptr);
            return;
        }

        if (g_overlay.cursorSuppressed)
        {
            if (g_overlay.showCursorHidden)
            {
                while (::ShowCursor(TRUE) < 0) {}
                g_overlay.showCursorHidden = false;
            }
            ::SetCursor(g_overlay.savedCursor);
            g_overlay.savedCursor = nullptr;
            g_overlay.cursorSuppressed = false;
        }
    }

    void UpdateQuickSlotBarState(RetroSkillRuntimeState& state)
    {
        state.quickSlotBarOriginX = SKILL_BAR_ORIGIN_X;
        state.quickSlotBarOriginY = SKILL_BAR_ORIGIN_Y;
        state.quickSlotBarSlotSize = SKILL_BAR_SLOT_SIZE;
        state.quickSlotBarCols = SKILL_BAR_COLS;
        state.quickSlotBarRows = SKILL_BAR_ROWS;
        state.quickSlotBarVisible = true;
        state.quickSlotBarCollapsed = false;
        state.quickSlotBarAcceptDrop = true;

        RECT clientRect = {};
        int clientW = 0;
        int clientH = 0;
        if (g_overlay.hwnd && ::GetClientRect(g_overlay.hwnd, &clientRect))
        {
            clientW = clientRect.right - clientRect.left;
            clientH = clientRect.bottom - clientRect.top;

            if (clientW == 800 && clientH == 600)
            {
                state.quickSlotBarOriginX = 661;
                state.quickSlotBarOriginY = 470;
            }
            else if (clientW == 1024 && clientH == 768)
            {
                state.quickSlotBarOriginX = 883;
                state.quickSlotBarOriginY = 697;
            }
            else if (clientW > 0 && clientH > 0 && clientW <= 820 && clientH <= 620)
            {
                state.quickSlotBarOriginX = 661;
                state.quickSlotBarOriginY = 470;
            }
        }

        uintptr_t statusBar = 0;
        if (!SafeIsBadReadPtr((void*)ADDR_StatusBar, 4))
            statusBar = *(uintptr_t*)ADDR_StatusBar;

        int statusBarX = 0;
        int statusBarY = 0;
        int statusBarW = 0;
        int statusBarH = 0;
        bool hasStatusBar = false;
        bool usedTopLevelProbe = false;
        QuickSlotWindowProbe expandedProbe = {};
        QuickSlotWindowProbe collapsedProbe = {};
        if (statusBar && !SafeIsBadReadPtr((void*)statusBar, 0x30))
        {
            statusBarX = CWnd_GetX(statusBar);
            statusBarY = CWnd_GetY(statusBar);
            statusBarW = CWnd_GetWidth(statusBar);
            statusBarH = CWnd_GetHeight(statusBar);
            hasStatusBar =
                statusBarX > -10000 && statusBarX < 10000 &&
                statusBarY > -10000 && statusBarY < 10000 &&
                statusBarW > 0 && statusBarW < 4000 &&
                statusBarH > 0 && statusBarH < 4000;
        }

        if (hasStatusBar)
        {
            state.quickSlotBarCollapsed = (statusBarW < 170) || (statusBarH < 55);
            state.quickSlotBarVisible = true;
            state.quickSlotBarAcceptDrop = !state.quickSlotBarCollapsed;
        }
        else
        {
            if (ProbeQuickSlotTopLevelWindow(state.quickSlotBarOriginX, state.quickSlotBarOriginY, false, &expandedProbe))
            {
                usedTopLevelProbe = true;
                state.quickSlotBarCollapsed = false;
                state.quickSlotBarVisible = true;
                state.quickSlotBarAcceptDrop = true;
                statusBarX = expandedProbe.x;
                statusBarY = expandedProbe.y;
                statusBarW = expandedProbe.w;
                statusBarH = expandedProbe.h;
            }
            else if (ProbeQuickSlotTopLevelWindow(state.quickSlotBarOriginX, state.quickSlotBarOriginY, true, &collapsedProbe))
            {
                usedTopLevelProbe = true;
                state.quickSlotBarCollapsed = true;
                state.quickSlotBarVisible = true;
                state.quickSlotBarAcceptDrop = false;
                statusBarX = collapsedProbe.x;
                statusBarY = collapsedProbe.y;
                statusBarW = collapsedProbe.w;
                statusBarH = collapsedProbe.h;
            }
        }

        static int s_quickSlotLogCount = 0;
        static int s_lastClientW = -1;
        static int s_lastClientH = -1;
        static int s_lastOriginX = -1;
        static int s_lastOriginY = -1;
        static int s_lastStatusBarW = -1;
        static int s_lastStatusBarH = -1;
        static int s_lastCollapsed = -1;
        if (s_quickSlotLogCount < 80 &&
            (clientW != s_lastClientW || clientH != s_lastClientH ||
             state.quickSlotBarOriginX != s_lastOriginX || state.quickSlotBarOriginY != s_lastOriginY ||
             statusBarW != s_lastStatusBarW || statusBarH != s_lastStatusBarH ||
             (state.quickSlotBarCollapsed ? 1 : 0) != s_lastCollapsed))
        {
            const char* probeTag = hasStatusBar ? "status" : (usedTopLevelProbe ? "toplevel" : "none");
            WriteLogFmt("[QuickSlotBar] client=%dx%d origin=(%d,%d) accept=%d collapsed=%d probe=%s statusBar=%s x=%d y=%d w=%d h=%d",
                clientW, clientH,
                state.quickSlotBarOriginX, state.quickSlotBarOriginY,
                state.quickSlotBarAcceptDrop ? 1 : 0,
                state.quickSlotBarCollapsed ? 1 : 0,
                probeTag,
                hasStatusBar ? "Y" : "N",
                statusBarX, statusBarY, statusBarW, statusBarH);
            s_lastClientW = clientW;
            s_lastClientH = clientH;
            s_lastOriginX = state.quickSlotBarOriginX;
            s_lastOriginY = state.quickSlotBarOriginY;
            s_lastStatusBarW = statusBarW;
            s_lastStatusBarH = statusBarH;
            s_lastCollapsed = state.quickSlotBarCollapsed ? 1 : 0;
            ++s_quickSlotLogCount;
        }
    }

    void ConfigureStyle(float mainScale)
    {
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(mainScale);
        style.FontScaleDpi = 1.0f;
        style.WindowRounding = 0.0f;
        style.FrameRounding = 2.0f * mainScale;
        style.ScrollbarRounding = 2.0f * mainScale;
        style.WindowBorderSize = 0.0f;
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
        style.Colors[ImGuiCol_Border] = ImVec4(0, 0, 0, 0);
        style.Colors[ImGuiCol_Button] = ImVec4(0, 0, 0, 0);
    }

    void LoadMainFont(float mainScale)
    {
        ImGuiIO& io = ImGui::GetIO();
        char winDir[MAX_PATH] = {};
        GetWindowsDirectoryA(winDir, MAX_PATH);
        std::string fontPath = std::string(winDir) + "\\Fonts\\msyh.ttc";

        ImFontConfig fontConfig = {};
        fontConfig.OversampleH = 1;
        fontConfig.OversampleV = 1;
        fontConfig.PixelSnapH = true;

        g_overlay.mainFont = io.Fonts->AddFontFromFileTTF(
            fontPath.c_str(),
            14.0f * mainScale,
            &fontConfig,
            io.Fonts->GetGlyphRangesChineseFull());

        if (!g_overlay.mainFont)
            g_overlay.mainFont = io.Fonts->AddFontDefault();
    }

    bool Reinitialize(HWND hwnd, IDirect3DDevice9* device, float mainScale, const char* assetPath)
    {
        if (g_overlay.initialized)
            SuperImGuiOverlayShutdown();

        g_overlay.hwnd = hwnd;
        g_overlay.device = device;
        g_overlay.mainScale = mainScale > 0.0f ? mainScale : 1.0f;
        g_overlay.assetPath = assetPath ? assetPath : "";

        IMGUI_CHECKVERSION();
        g_overlay.context = ImGui::CreateContext();
        if (!g_overlay.context)
            return false;

        ImGui::SetCurrentContext(g_overlay.context);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.IniFilename = nullptr;
        io.MouseDrawCursor = false;

        ConfigureStyle(g_overlay.mainScale);
        LoadMainFont(g_overlay.mainScale);

        if (!ImGui_ImplWin32_Init(hwnd))
            return false;
        if (!ImGui_ImplDX9_Init(device))
            return false;

        ResetRetroSkillData(g_overlay.state);
        InitializeRetroSkillApp(g_overlay.state, g_overlay.assets, device, g_overlay.assetPath.c_str());
        ConfigureRetroSkillDefaultBehaviorHooks(g_overlay.hooks, g_overlay.state);
        SkillOverlayBridgeConfigureHooks(g_overlay.hooks);
        RetroSkillDWriteInitialize(device);

        g_overlay.initialized = true;
        WriteLogFmt("[ImGuiOverlay] initialized hwnd=0x%08X device=0x%08X scale=%.2f assetPath=%s",
            (DWORD)(uintptr_t)hwnd, (DWORD)(uintptr_t)device, g_overlay.mainScale, g_overlay.assetPath.c_str());
        return true;
    }
}

bool SuperImGuiOverlayEnsureInitialized(HWND hwnd, IDirect3DDevice9* device, float mainScale, const char* assetPath)
{
    if (!hwnd || !device)
        return false;

    if (g_overlay.initialized && g_overlay.hwnd == hwnd && g_overlay.device == device)
        return true;

    return Reinitialize(hwnd, device, mainScale, assetPath);
}

void SuperImGuiOverlayShutdown()
{
    UpdateCursorSuppression(false);

    if (!g_overlay.context)
    {
        g_overlay = SuperOverlayRuntime{};
        return;
    }

    ImGui::SetCurrentContext(g_overlay.context);
    RetroSkillDWriteShutdown();
    ShutdownRetroSkillApp(g_overlay.assets);
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(g_overlay.context);
    g_overlay = SuperOverlayRuntime{};
    WriteLog("[ImGuiOverlay] shutdown");
}

void SuperImGuiOverlaySetVisible(bool visible)
{
    g_overlay.visible = visible;
    if (!visible)
    {
        g_overlay.mouseCapture = false;
        g_overlay.mouseHover = false;
        UpdateCursorSuppression(false);
    }
}

void SuperImGuiOverlaySetAnchor(int x, int y)
{
    g_overlay.anchorX = x;
    g_overlay.anchorY = y;
}

void SuperImGuiOverlayResetPanelState()
{
    ResetRetroSkillData(g_overlay.state);
}

void SuperImGuiOverlayOnDeviceLost()
{
    if (!g_overlay.initialized || !g_overlay.context)
        return;

    ImGui::SetCurrentContext(g_overlay.context);
    RetroSkillDWriteOnDeviceLost();
    ImGui_ImplDX9_InvalidateDeviceObjects();
}

void SuperImGuiOverlayOnDeviceReset(IDirect3DDevice9* device)
{
    if (!g_overlay.initialized || !g_overlay.context)
        return;

    if (device)
        g_overlay.device = device;

    ImGui::SetCurrentContext(g_overlay.context);
    if (ImGui_ImplDX9_CreateDeviceObjects())
        RetroSkillDWriteOnDeviceReset(g_overlay.device);
}

bool SuperImGuiOverlayHandleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!g_overlay.initialized || !g_overlay.visible || !g_overlay.context)
        return false;

    if (!IsOverlayWindowInteractive())
    {
        g_overlay.mouseCapture = false;
        g_overlay.mouseHover = false;
        UpdateCursorSuppression(false);
        return false;
    }

    ImGui::SetCurrentContext(g_overlay.context);
    bool handledByImGui = false;
    bool messageInsidePanel = false;

    if (IsMouseMessage(msg))
        messageInsidePanel = IsPointInsidePanelClientRect(hwnd, msg, lParam);

    if (IsMouseMessage(msg))
    {
        handledByImGui = FeedMouseEventToImGui(hwnd, msg, wParam, lParam);
    }
    else if (IsKeyboardMessage(msg))
    {
        if (ImGui::GetIO().WantCaptureKeyboard)
            handledByImGui = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam) ? true : false;
    }

    if (msg == WM_SETCURSOR)
    {
        const bool useOverlayCursor = ShouldUseOverlayCursor();
        UpdateCursorSuppression(useOverlayCursor);
        if (useOverlayCursor)
            return true;
    }

    if (IsMouseMessage(msg))
    {
        UpdateCursorSuppression(ShouldUseOverlayCursor());
        if (handledByImGui || OverlayOwnsMouseInput() || messageInsidePanel)
            return true;
    }
    else if (msg == WM_MOUSELEAVE || msg == WM_NCMOUSELEAVE)
    {
        g_overlay.mouseHover = false;
        UpdateCursorSuppression(OverlayOwnsMouseInput());
    }

    return handledByImGui;
}

void SuperImGuiOverlayRender(IDirect3DDevice9* device)
{
    if (!g_overlay.initialized || !g_overlay.visible || !g_overlay.context)
        return;
    if (!device || device != g_overlay.device)
        return;
    if (g_overlay.anchorX <= -9000 || g_overlay.anchorY <= -9000)
        return;

    ImGui::SetCurrentContext(g_overlay.context);
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = false;

    if (g_overlay.mouseCapture && !AreAnyPhysicalMouseButtonsDown())
    {
        SuperImGuiOverlayCancelMouseCapture();
    }

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2((float)g_overlay.anchorX, (float)g_overlay.anchorY), ImGuiCond_Always);
    if (g_overlay.mainFont)
        ImGui::PushFont(g_overlay.mainFont);

    SkillOverlayBridgeSyncRetroState(g_overlay.state);
    UpdateQuickSlotBarState(g_overlay.state);
    RenderRetroSkillPanel(g_overlay.state, g_overlay.assets, device, g_overlay.mainScale, &g_overlay.hooks);

    g_overlay.mouseHover = IsCurrentMouseInsidePanel();
    const bool shouldDrawOverlayCursor = g_overlay.state.isDraggingSkill || g_overlay.mouseHover || g_overlay.mouseCapture;
    if (shouldDrawOverlayCursor)
    {
        POINT mousePt = {};
        const bool hasMousePt = TryGetCurrentMouseClientPos(&mousePt);
        const ImVec2 savedMousePos = io.MousePos;
        if (hasMousePt)
            io.MousePos = ImVec2((float)mousePt.x, (float)mousePt.y);

        RenderRetroSkillCursorOverlay(g_overlay.state, g_overlay.assets, g_overlay.mainScale);

        if (hasMousePt)
            io.MousePos = savedMousePos;
    }

    if (g_overlay.mainFont)
        ImGui::PopFont();

    ImGui::EndFrame();

    g_overlay.mouseCapture = io.WantCaptureMouse && IsOverlayWindowInteractive();
    UpdateCursorSuppression(ShouldUseOverlayCursor());

    if (device->BeginScene() >= 0)
    {
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        device->EndScene();
    }
}

bool SuperImGuiOverlayIsInitialized()
{
    return g_overlay.initialized;
}

bool SuperImGuiOverlayWantsMouseCapture()
{
    return OverlayOwnsMouseInput();
}

bool SuperImGuiOverlayShouldSuppressGameMouse()
{
    if (!g_overlay.initialized || !g_overlay.visible)
        return false;
    return ShouldUseOverlayCursor();
}

void SuperImGuiOverlayCancelMouseCapture()
{
    if (!g_overlay.initialized)
        return;

    g_overlay.mouseCapture = false;
    g_overlay.mouseHover = false;

    if (g_overlay.context)
    {
        ImGui::SetCurrentContext(g_overlay.context);
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseButtonEvent(0, false);
        io.AddMouseButtonEvent(1, false);
        io.AddMouseButtonEvent(2, false);
        io.AddMouseButtonEvent(3, false);
        io.AddMouseButtonEvent(4, false);
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }

    UpdateCursorSuppression(false);
}

HWND SuperImGuiOverlayGetGameHwnd()
{
    return g_overlay.hwnd;
}
