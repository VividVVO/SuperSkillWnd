#include "ui/super_imgui_overlay_d3d8.h"

#include "core/Common.h"
#include "core/GameAddresses.h"
#include "d3d8/d3d8_renderer.h"
#include "skill/skill_overlay_bridge.h"
#include "ui/retro_skill_app.h"
#include "ui/retro_skill_panel.h"
#include "ui/retro_skill_text_dwrite.h"

#include "third_party/imgui/imgui.h"
#include "third_party/imgui/backends/imgui_impl_d3d8.h"
#include "third_party/imgui/backends/imgui_impl_win32.h"

#include <cfloat>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    struct SuperD3D8OverlayRuntime
    {
        bool initialized = false;
        bool visible = false;
        bool mouseCapture = false;
        bool mouseHover = false;
        bool cursorSuppressed = false;
        bool showCursorHidden = false;
        HWND hwnd = nullptr;
        HCURSOR savedCursor = nullptr;
        void* device = nullptr;
        ImGuiContext* context = nullptr;
        ImFont* mainFont = nullptr;
        ImFont* consolasFont = nullptr;
        float mainScale = 1.0f;
        int anchorX = -9999;
        int anchorY = -9999;
        RetroSkillRuntimeState state;
        RetroSkillAssets assets;
        RetroSkillBehaviorHooks hooks;
        std::string assetPath;
    };

    SuperD3D8OverlayRuntime g_overlay8;
    typedef HRESULT (__stdcall* pfn_D3D8Scene)(void*);

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

    bool GetClientMousePointFromMessage(HWND hwnd, UINT msg, LPARAM lParam, POINT* outPoint)
    {
        if (!outPoint)
            return false;

        auto getSignedX = [](LPARAM lp) -> LONG { return (LONG)(short)LOWORD(lp); };
        auto getSignedY = [](LPARAM lp) -> LONG { return (LONG)(short)HIWORD(lp); };

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
        if (g_overlay8.anchorX <= -9000 || g_overlay8.anchorY <= -9000)
            return false;
        const PanelMetrics metrics = GetPanelMetrics(g_overlay8.mainScale);
        return x >= g_overlay8.anchorX &&
               x < (int)(g_overlay8.anchorX + metrics.width) &&
               y >= g_overlay8.anchorY &&
               y < (int)(g_overlay8.anchorY + metrics.height);
    }

    bool IsPointInsidePanelClientRect(HWND hwnd, UINT msg, LPARAM lParam)
    {
        POINT pt = {};
        if (!GetClientMousePointFromMessage(hwnd, msg, lParam, &pt))
            return false;
        return IsPointInsidePanel(pt.x, pt.y);
    }

    bool OverlayOwnsMouseInput()
    {
        return g_overlay8.mouseCapture || g_overlay8.state.isDraggingSkill;
    }

    bool IsOverlayWindowInteractive()
    {
        return g_overlay8.hwnd != nullptr;
    }

    bool IsCurrentMouseInsidePanel()
    {
        if (!g_overlay8.hwnd)
            return false;
        POINT pt = {};
        if (!::GetCursorPos(&pt) || !::ScreenToClient(g_overlay8.hwnd, &pt))
            return false;
        return IsPointInsidePanel(pt.x, pt.y);
    }

    bool TryGetCurrentMouseClientPos(POINT* outPoint)
    {
        if (!outPoint || !g_overlay8.hwnd)
            return false;
        POINT pt = {};
        if (!::GetCursorPos(&pt) || !::ScreenToClient(g_overlay8.hwnd, &pt))
            return false;
        *outPoint = pt;
        return true;
    }

    bool AreAnyPhysicalMouseButtonsDown()
    {
        return ((::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) ||
               ((::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0) ||
               ((::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0) ||
               ((::GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0) ||
               ((::GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0);
    }

    bool ShouldUseOverlayCursor()
    {
        return g_overlay8.initialized &&
               g_overlay8.visible &&
               IsOverlayWindowInteractive() &&
               (OverlayOwnsMouseInput() || IsCurrentMouseInsidePanel());
    }

    void UpdateCursorSuppression(bool shouldSuppress)
    {
        if (shouldSuppress)
        {
            if (!g_overlay8.cursorSuppressed)
            {
                g_overlay8.savedCursor = ::GetCursor();
                g_overlay8.cursorSuppressed = true;
            }
            if (!g_overlay8.showCursorHidden)
            {
                while (::ShowCursor(FALSE) >= 0) {}
                g_overlay8.showCursorHidden = true;
            }
            ::SetCursor(nullptr);
            return;
        }

        if (g_overlay8.cursorSuppressed)
        {
            if (g_overlay8.showCursorHidden)
            {
                while (::ShowCursor(TRUE) < 0) {}
                g_overlay8.showCursorHidden = false;
            }
            ::SetCursor(g_overlay8.savedCursor);
            g_overlay8.savedCursor = nullptr;
            g_overlay8.cursorSuppressed = false;
        }
    }

    bool FeedMouseEventToImGui(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        ImGuiIO& io = ImGui::GetIO();

        if (msg == WM_MOUSELEAVE || msg == WM_NCMOUSELEAVE)
        {
            g_overlay8.mouseHover = false;
            if (!OverlayOwnsMouseInput())
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            return OverlayOwnsMouseInput();
        }

        POINT pt = {};
        if (!GetClientMousePointFromMessage(hwnd, msg, lParam, &pt))
            return false;

        const bool insidePanel = IsPointInsidePanel(pt.x, pt.y);

        if (msg == WM_MOUSEMOVE)
        {
            if (insidePanel || OverlayOwnsMouseInput())
            {
                io.AddMousePosEvent((float)pt.x, (float)pt.y);
                g_overlay8.mouseHover = insidePanel;
                return true;
            }
            g_overlay8.mouseHover = false;
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
                g_overlay8.mouseHover = insidePanel;
                g_overlay8.mouseCapture = true;
                return true;
            }

            if (IsMouseButtonUpMessage(msg))
            {
                if (!(insidePanel || OverlayOwnsMouseInput()))
                    return false;
                io.AddMousePosEvent((float)pt.x, (float)pt.y);
                io.AddMouseButtonEvent(button, false);
                g_overlay8.mouseHover = insidePanel;
                if (!io.MouseDown[0] && !io.MouseDown[1] && !io.MouseDown[2] && !io.MouseDown[3] && !io.MouseDown[4])
                    g_overlay8.mouseCapture = false;
                return true;
            }
        }

        if (msg == WM_MOUSEWHEEL)
        {
            if (!(insidePanel || OverlayOwnsMouseInput()))
                return false;
            io.AddMousePosEvent((float)pt.x, (float)pt.y);
            io.AddMouseWheelEvent(0.0f, (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA);
            g_overlay8.mouseHover = insidePanel;
            return true;
        }

        if (msg == WM_MOUSEHWHEEL)
        {
            if (!(insidePanel || OverlayOwnsMouseInput()))
                return false;
            io.AddMousePosEvent((float)pt.x, (float)pt.y);
            io.AddMouseWheelEvent(-(float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA, 0.0f);
            g_overlay8.mouseHover = insidePanel;
            return true;
        }

        return false;
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
        if (g_overlay8.hwnd && ::GetClientRect(g_overlay8.hwnd, &clientRect))
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

        if (statusBar && !SafeIsBadReadPtr((void*)statusBar, 0x30))
        {
            const int statusBarX = CWnd_GetX(statusBar);
            const int statusBarY = CWnd_GetY(statusBar);
            const int statusBarW = CWnd_GetWidth(statusBar);
            const int statusBarH = CWnd_GetHeight(statusBar);
            if (statusBarX > -10000 && statusBarX < 10000 &&
                statusBarY > -10000 && statusBarY < 10000 &&
                statusBarW > 0 && statusBarW < 4000 &&
                statusBarH > 0 && statusBarH < 4000)
            {
                state.quickSlotBarCollapsed = (statusBarW < 170) || (statusBarH < 55);
                state.quickSlotBarVisible = true;
                state.quickSlotBarAcceptDrop = !state.quickSlotBarCollapsed;
            }
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
        g_overlay8.mainFont = io.Fonts->AddFontFromFileTTF(
            fontPath.c_str(),
            14.0f * mainScale,
            &fontConfig,
            io.Fonts->GetGlyphRangesChineseFull());

        if (!g_overlay8.mainFont)
            g_overlay8.mainFont = io.Fonts->AddFontDefault();

        std::string consolasPath = std::string(winDir) + "\\Fonts\\times.ttf";
        ImFontConfig consolasCfg = {};
        consolasCfg.OversampleH = 1;
        consolasCfg.OversampleV = 1;
        consolasCfg.PixelSnapH = true;
        static const ImWchar digitRanges[] = { 0x20, 0x7E, 0 };
        g_overlay8.consolasFont = io.Fonts->AddFontFromFileTTF(
            consolasPath.c_str(),
            14.0f * mainScale,
            &consolasCfg,
            digitRanges);
    }

    bool Reinitialize(HWND hwnd, void* device8, float mainScale, const char* assetPath)
    {
        if (g_overlay8.initialized)
            SuperD3D8OverlayShutdown();

        g_overlay8.hwnd = hwnd;
        g_overlay8.device = device8;
        g_overlay8.mainScale = mainScale > 0.0f ? mainScale : 1.0f;
        g_overlay8.assetPath = assetPath ? assetPath : "";

        IMGUI_CHECKVERSION();
        g_overlay8.context = ImGui::CreateContext();
        if (!g_overlay8.context)
            return false;

        ImGui::SetCurrentContext(g_overlay8.context);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.IniFilename = nullptr;
        io.MouseDrawCursor = false;

        ConfigureStyle(g_overlay8.mainScale);
        LoadMainFont(g_overlay8.mainScale);

        if (!ImGui_ImplWin32_Init(hwnd))
        {
            ImGui::DestroyContext(g_overlay8.context);
            g_overlay8 = SuperD3D8OverlayRuntime{};
            return false;
        }
        if (!ImGui_ImplD3D8_Init(device8))
        {
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext(g_overlay8.context);
            g_overlay8 = SuperD3D8OverlayRuntime{};
            return false;
        }

        ResetRetroSkillData(g_overlay8.state);
        const RetroDeviceRef deviceRef = { device8, RetroRenderBackend_D3D8 };
        InitializeRetroSkillApp(g_overlay8.state, g_overlay8.assets, deviceRef, g_overlay8.assetPath.c_str());
        ConfigureRetroSkillDefaultBehaviorHooks(g_overlay8.hooks, g_overlay8.state);
        SkillOverlayBridgeConfigureHooks(g_overlay8.hooks);
        RetroSkillDWriteInitialize(deviceRef);

        g_overlay8.initialized = true;
        WriteLogFmt("[D3D8ImGuiOverlay] initialized hwnd=0x%08X device=0x%08X scale=%.2f assetPath=%s",
            (DWORD)(uintptr_t)hwnd, (DWORD)(uintptr_t)device8, g_overlay8.mainScale, g_overlay8.assetPath.c_str());
        return true;
    }
}

bool SuperD3D8OverlayEnsureInitialized(HWND hwnd, void* device8, float mainScale, const char* assetPath)
{
    if (!hwnd || !device8)
        return false;

    if (g_overlay8.initialized && g_overlay8.hwnd == hwnd && g_overlay8.device == device8)
        return true;

    return Reinitialize(hwnd, device8, mainScale, assetPath);
}

void SuperD3D8OverlayShutdown()
{
    UpdateCursorSuppression(false);

    if (!g_overlay8.context)
    {
        g_overlay8 = SuperD3D8OverlayRuntime{};
        return;
    }

    ImGui::SetCurrentContext(g_overlay8.context);
    RetroSkillDWriteShutdown();
    ShutdownRetroSkillApp(g_overlay8.assets);
    ImGui_ImplD3D8_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(g_overlay8.context);
    g_overlay8 = SuperD3D8OverlayRuntime{};
    WriteLog("[D3D8ImGuiOverlay] shutdown");
}

void SuperD3D8OverlaySetVisible(bool visible)
{
    g_overlay8.visible = visible;
    if (!visible)
    {
        g_overlay8.mouseCapture = false;
        g_overlay8.mouseHover = false;
        UpdateCursorSuppression(false);
    }
}

void SuperD3D8OverlaySetAnchor(int x, int y)
{
    g_overlay8.anchorX = x;
    g_overlay8.anchorY = y;
}

void SuperD3D8OverlayResetPanelState()
{
    ResetRetroSkillData(g_overlay8.state);
}

void SuperD3D8OverlayOnDeviceLost()
{
    if (!g_overlay8.initialized || !g_overlay8.context)
        return;

    ImGui::SetCurrentContext(g_overlay8.context);
    RetroSkillDWriteOnDeviceLost();
    ImGui_ImplD3D8_InvalidateDeviceObjects();
}

void SuperD3D8OverlayOnDeviceReset(void* device8)
{
    if (!g_overlay8.initialized || !g_overlay8.context)
        return;

    if (device8)
        g_overlay8.device = device8;

    ImGui::SetCurrentContext(g_overlay8.context);
    if (ImGui_ImplD3D8_CreateDeviceObjects())
    {
        const RetroDeviceRef deviceRef = { g_overlay8.device, RetroRenderBackend_D3D8 };
        RetroSkillDWriteOnDeviceReset(deviceRef);
    }
}

bool SuperD3D8OverlayHandleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!g_overlay8.initialized || !g_overlay8.visible || !g_overlay8.context)
        return false;

    if (!IsOverlayWindowInteractive())
    {
        g_overlay8.mouseCapture = false;
        g_overlay8.mouseHover = false;
        UpdateCursorSuppression(false);
        return false;
    }

    ImGui::SetCurrentContext(g_overlay8.context);
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
        g_overlay8.mouseHover = false;
        UpdateCursorSuppression(OverlayOwnsMouseInput());
    }

    return handledByImGui;
}

void SuperD3D8OverlayRender(void* device8)
{
    if (!g_overlay8.initialized || !g_overlay8.visible || !g_overlay8.context)
        return;
    if (!device8 || device8 != g_overlay8.device)
        return;
    if (g_overlay8.anchorX <= -9000 || g_overlay8.anchorY <= -9000)
        return;

    ImGui::SetCurrentContext(g_overlay8.context);
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = false;

    if (g_overlay8.mouseCapture && !AreAnyPhysicalMouseButtonsDown())
        SuperD3D8OverlayCancelMouseCapture();

    ImGui_ImplD3D8_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2((float)g_overlay8.anchorX, (float)g_overlay8.anchorY), ImGuiCond_Always);
    if (g_overlay8.mainFont)
        ImGui::PushFont(g_overlay8.mainFont);

    SkillOverlayBridgeSyncRetroState(g_overlay8.state);
    UpdateQuickSlotBarState(g_overlay8.state);
    RenderRetroSkillPanel(g_overlay8.state, g_overlay8.assets, nullptr, g_overlay8.mainScale, &g_overlay8.hooks);

    g_overlay8.mouseHover = IsCurrentMouseInsidePanel();
    const bool shouldDrawOverlayCursor = g_overlay8.state.isDraggingSkill || g_overlay8.mouseHover || g_overlay8.mouseCapture;
    if (shouldDrawOverlayCursor)
    {
        POINT mousePt = {};
        const bool hasMousePt = TryGetCurrentMouseClientPos(&mousePt);
        const ImVec2 savedMousePos = io.MousePos;
        if (hasMousePt)
            io.MousePos = ImVec2((float)mousePt.x, (float)mousePt.y);

        RenderRetroSkillCursorOverlay(g_overlay8.state, g_overlay8.assets, g_overlay8.mainScale);

        if (hasMousePt)
            io.MousePos = savedMousePos;
    }

    if (g_overlay8.mainFont)
        ImGui::PopFont();

    ImGui::EndFrame();

    g_overlay8.mouseCapture = io.WantCaptureMouse && IsOverlayWindowInteractive();
    UpdateCursorSuppression(ShouldUseOverlayCursor());

    DWORD* vt = *(DWORD**)device8;
    auto fnBeginScene = (pfn_D3D8Scene)(void*)vt[D3D8VT::BeginScene];
    auto fnEndScene = (pfn_D3D8Scene)(void*)vt[D3D8VT::EndScene];
    if (fnBeginScene && fnEndScene && fnBeginScene(device8) >= 0)
    {
        ImGui::Render();
        ImGui_ImplD3D8_RenderDrawData(ImGui::GetDrawData());
        fnEndScene(device8);
    }
}

bool SuperD3D8OverlayIsInitialized()
{
    return g_overlay8.initialized;
}

bool SuperD3D8OverlayWantsMouseCapture()
{
    return OverlayOwnsMouseInput();
}

bool SuperD3D8OverlayShouldSuppressGameMouse()
{
    if (!g_overlay8.initialized || !g_overlay8.visible)
        return false;
    return ShouldUseOverlayCursor();
}

void SuperD3D8OverlayCancelMouseCapture()
{
    if (!g_overlay8.initialized)
        return;

    g_overlay8.mouseCapture = false;
    g_overlay8.mouseHover = false;

    if (g_overlay8.context)
    {
        ImGui::SetCurrentContext(g_overlay8.context);
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

HWND SuperD3D8OverlayGetGameHwnd()
{
    return g_overlay8.hwnd;
}

ImFont* SuperD3D8OverlayGetConsolasFont()
{
    return g_overlay8.consolasFont;
}
