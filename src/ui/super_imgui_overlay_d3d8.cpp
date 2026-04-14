#include "ui/super_imgui_overlay_d3d8.h"

#include "core/Common.h"
#include "core/GameAddresses.h"
#include "d3d8/d3d8_renderer.h"
#include "skill/skill_overlay_bridge.h"
#include "ui/retro_skill_app.h"
#include "ui/overlay_cursor_utils.h"
#include "ui/overlay_input_utils.h"
#include "ui/retro_skill_panel.h"
#include "ui/overlay_style_utils.h"
#include "ui/retro_skill_text_dwrite.h"

#include "third_party/imgui/imgui.h"
#include "third_party/imgui/backends/imgui_impl_d3d8.h"
#include "third_party/imgui/backends/imgui_impl_win32.h"

#include <cfloat>
#include <cmath>
#include <string>
#include <cstdint>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    struct SuperD3D8OverlayRuntime
    {
        bool initialized = false;
        bool visible = false;
        bool panelExpanded = false;
        bool mouseCapture = false;
        bool mouseHover = false;
        bool superButtonVisible = false;
        bool superButtonHover = false;
        bool superButtonPressed = false;
        bool superButtonToggleRequested = false;
        bool superButtonHoverInstantUseNormal1 = false;
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
        uint64_t superButtonHoverStartTick = 0;
        RECT superButtonRect = { 0, 0, 0, 0 };
        RetroSkillRuntimeState state;
        RetroSkillAssets assets;
        RetroSkillBehaviorHooks hooks;
        std::string assetPath;
    };

    SuperD3D8OverlayRuntime g_overlay8;
    typedef HRESULT (__stdcall* pfn_D3D8Scene)(void*);

    bool IsMouseMessage(UINT msg)
    {
        return OverlayIsMouseMessage(msg);
    }

    bool IsKeyboardMessage(UINT msg)
    {
        return OverlayIsKeyboardMessage(msg);
    }

    bool IsMouseButtonMessage(UINT msg)
    {
        return OverlayIsMouseButtonMessage(msg);
    }

    bool IsMouseButtonDownMessage(UINT msg)
    {
        return OverlayIsMouseButtonDownMessage(msg);
    }

    bool IsMouseButtonUpMessage(UINT msg)
    {
        return OverlayIsMouseButtonUpMessage(msg);
    }

    int ToImGuiMouseButton(UINT msg, WPARAM wParam)
    {
        return OverlayToImGuiMouseButton(msg, wParam);
    }

    bool GetClientMousePointFromMessage(HWND hwnd, UINT msg, LPARAM lParam, POINT* outPoint)
    {
        return OverlayGetClientMousePointFromMessage(hwnd, msg, lParam, outPoint);
    }

    RECT MakeRectXYWH(int x, int y, int w, int h)
    {
        RECT rc = { x, y, x + w, y + h };
        return rc;
    }

    bool RectHasArea(const RECT& rc)
    {
        return rc.right > rc.left && rc.bottom > rc.top;
    }

    bool HasSuperButtonRect()
    {
        return g_overlay8.superButtonVisible && RectHasArea(g_overlay8.superButtonRect);
    }

    bool IsPointInsideSuperButton(int x, int y)
    {
        if (!HasSuperButtonRect())
            return false;
        const RECT& rc = g_overlay8.superButtonRect;
        return x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
    }

    void ResetSuperButtonState()
    {
        g_overlay8.superButtonHover = false;
        g_overlay8.superButtonPressed = false;
        g_overlay8.superButtonToggleRequested = false;
        g_overlay8.superButtonHoverStartTick = 0;
        g_overlay8.superButtonHoverInstantUseNormal1 = false;
    }

    void SetSuperButtonHoverState(bool hover)
    {
        if (g_overlay8.superButtonHover == hover)
            return;

        g_overlay8.superButtonHover = hover;
        if (hover)
        {
            g_overlay8.superButtonHoverStartTick = static_cast<uint64_t>(GetTickCount64());
            g_overlay8.superButtonHoverInstantUseNormal1 = ((GetTickCount64() & 1ULL) != 0ULL);
        }
        else
        {
            g_overlay8.superButtonHoverStartTick = 0;
            g_overlay8.superButtonHoverInstantUseNormal1 = false;
        }
    }

    void RenderOverlaySuperButton()
    {
        if (!HasSuperButtonRect())
            return;

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        const ImVec2 minPos((float)g_overlay8.superButtonRect.left, (float)g_overlay8.superButtonRect.top);
        const ImVec2 maxPos((float)g_overlay8.superButtonRect.right, (float)g_overlay8.superButtonRect.bottom);
        if (!g_overlay8.superButtonVisible)
        {
            UITexture* disabled = GetRetroSkillTexture(g_overlay8.assets, "surpe.disabled");
            if (disabled && disabled->texture)
                drawList->AddImage((ImTextureID)disabled->texture, minPos, maxPos);
            return;
        }

        if (g_overlay8.superButtonPressed)
        {
            UITexture* pressed = GetRetroSkillTexture(g_overlay8.assets, "surpe.pressed");
            if (!pressed || !pressed->texture)
                pressed = GetRetroSkillTexture(g_overlay8.assets, "surpe.mouseOver");
            if (!pressed || !pressed->texture)
                pressed = GetRetroSkillTexture(g_overlay8.assets, "surpe.normal");
            if (pressed && pressed->texture)
                drawList->AddImage((ImTextureID)pressed->texture, minPos, maxPos);
            return;
        }

        UITexture* normal = GetRetroSkillTexture(g_overlay8.assets, "surpe.normal");
        UITexture* hover = GetRetroSkillTexture(g_overlay8.assets, "surpe.mouseOver");
        if (normal && normal->texture)
            drawList->AddImage((ImTextureID)normal->texture, minPos, maxPos);

        if (g_overlay8.superButtonHover && hover && hover->texture)
        {
            const uint64_t nowTick = static_cast<uint64_t>(GetTickCount64());
            const uint64_t hoverStartTick = g_overlay8.superButtonHoverStartTick ? g_overlay8.superButtonHoverStartTick : nowTick;
            const float hoverElapsed = (float)(nowTick - hoverStartTick) / 1000.0f;
            const float pulse = 0.70f + 0.30f * (0.5f + 0.5f * sinf(hoverElapsed * 7.2f));
            const int alpha = (int)floorf(pulse * 255.0f + 0.5f);
            drawList->AddImage((ImTextureID)hover->texture, minPos, maxPos, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, alpha));
            return;
        }

        if ((!normal || !normal->texture) && hover && hover->texture)
            drawList->AddImage((ImTextureID)hover->texture, minPos, maxPos);
    }

    bool HandleOverlaySuperButtonMouseEvent(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        UNREFERENCED_PARAMETER(wParam);

        if (!HasSuperButtonRect())
            return false;

        if (msg == WM_CAPTURECHANGED || msg == WM_KILLFOCUS)
        {
            ResetSuperButtonState();
            return false;
        }
        if (msg == WM_ACTIVATEAPP)
        {
            if (!wParam)
                ResetSuperButtonState();
            return false;
        }

        if (msg != WM_MOUSEMOVE && msg != WM_LBUTTONDOWN && msg != WM_LBUTTONUP)
            return false;

        POINT pt = {};
        if (!GetClientMousePointFromMessage(hwnd, msg, lParam, &pt))
            return false;

        const bool hit = IsPointInsideSuperButton(pt.x, pt.y);
        if (msg == WM_MOUSEMOVE)
        {
            if (g_overlay8.superButtonPressed || hit)
            {
                SetSuperButtonHoverState(hit);
                return true;
            }
            SetSuperButtonHoverState(false);
            return false;
        }

        if (msg == WM_LBUTTONDOWN)
        {
            if (!hit)
                return false;
            g_overlay8.superButtonPressed = true;
            SetSuperButtonHoverState(true);
            return true;
        }

        if (msg == WM_LBUTTONUP)
        {
            if (!g_overlay8.superButtonPressed)
                return false;
            g_overlay8.superButtonPressed = false;
            SetSuperButtonHoverState(hit);
            if (hit)
                g_overlay8.superButtonToggleRequested = true;
            return true;
        }

        return false;
    }

    bool GetResetConfirmRectForHitTest(RECT* outRect)
    {
        if (!outRect || !g_overlay8.state.superSkillResetConfirmVisible ||
            g_overlay8.anchorX <= -9000 || g_overlay8.anchorY <= -9000)
        {
            return false;
        }

        const PanelMetrics metrics = GetPanelMetrics(g_overlay8.mainScale);
        UITexture* noticeBg = GetRetroSkillTexture(g_overlay8.assets, "initial.backgrnd");
        const float noticeWidth = ((noticeBg && noticeBg->width > 0) ? (float)noticeBg->width : 260.0f) * g_overlay8.mainScale;
        const float noticeHeight = ((noticeBg && noticeBg->height > 0) ? (float)noticeBg->height : 131.0f) * g_overlay8.mainScale;
        float noticeX = floorf((float)g_overlay8.anchorX + (metrics.width - noticeWidth) * 0.5f);
        float noticeY = floorf((float)g_overlay8.anchorY + (metrics.height - noticeHeight) * 0.5f);

        RECT clientRect = {};
        if (g_overlay8.hwnd && ::GetClientRect(g_overlay8.hwnd, &clientRect))
        {
            const float clientW = (float)(clientRect.right - clientRect.left);
            const float clientH = (float)(clientRect.bottom - clientRect.top);
            if (noticeX < 0.0f)
                noticeX = 0.0f;
            if (noticeY < 0.0f)
                noticeY = 0.0f;
            if (noticeX + noticeWidth > clientW)
                noticeX = floorf(clientW - noticeWidth);
            if (noticeY + noticeHeight > clientH)
                noticeY = floorf(clientH - noticeHeight);
            if (noticeX < 0.0f)
                noticeX = 0.0f;
            if (noticeY < 0.0f)
                noticeY = 0.0f;
        }

        *outRect = MakeRectXYWH(
            (int)floorf(noticeX),
            (int)floorf(noticeY),
            (int)ceilf(noticeWidth),
            (int)ceilf(noticeHeight));
        return true;
    }

    bool IsPointInsidePanel(int x, int y)
    {
        if (IsPointInsideSuperButton(x, y))
            return true;
        if (g_overlay8.anchorX <= -9000 || g_overlay8.anchorY <= -9000)
            return false;
        RECT resetConfirmRect = {};
        if (GetResetConfirmRectForHitTest(&resetConfirmRect) &&
            x >= resetConfirmRect.left && x < resetConfirmRect.right &&
            y >= resetConfirmRect.top && y < resetConfirmRect.bottom)
        {
            return true;
        }
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
        return g_overlay8.mouseCapture || g_overlay8.state.isDraggingSkill || g_overlay8.superButtonPressed;
    }

    bool IsOverlayWindowInteractive()
    {
        return g_overlay8.hwnd != nullptr;
    }

    bool IsGameWindowForeground()
    {
        return g_overlay8.hwnd && ::GetForegroundWindow() == g_overlay8.hwnd;
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
        return OverlayAreAnyPhysicalMouseButtonsDown();
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
        OverlayUpdateCursorSuppression(
            g_overlay8.cursorSuppressed,
            g_overlay8.showCursorHidden,
            g_overlay8.savedCursor,
            shouldSuppress);
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

        OverlayConfigureImGuiStyle(g_overlay8.mainScale);
        OverlayLoadMainAndConsolasFonts(g_overlay8.mainScale, &g_overlay8.mainFont, &g_overlay8.consolasFont);

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
        ResetSuperButtonState();
        UpdateCursorSuppression(false);
    }
}

void SuperD3D8OverlaySetPanelExpanded(bool expanded)
{
    g_overlay8.panelExpanded = expanded;
}

void SuperD3D8OverlaySetAnchor(int x, int y)
{
    g_overlay8.anchorX = x;
    g_overlay8.anchorY = y;
}

void SuperD3D8OverlaySetSuperButtonVisible(bool visible)
{
    g_overlay8.superButtonVisible = visible;
    if (!visible)
        ResetSuperButtonState();
}

void SuperD3D8OverlaySetSuperButtonRect(const RECT* rect)
{
    if (rect)
    {
        g_overlay8.superButtonRect = *rect;
    }
    else
    {
        SetRectEmpty(&g_overlay8.superButtonRect);
    }
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

    if (HandleOverlaySuperButtonMouseEvent(hwnd, msg, wParam, lParam))
    {
        g_overlay8.mouseHover = g_overlay8.superButtonHover;
        UpdateCursorSuppression(ShouldUseOverlayCursor());
        return true;
    }

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
    if (!HasSuperButtonRect() && (g_overlay8.anchorX <= -9000 || g_overlay8.anchorY <= -9000))
        return;

    ImGui::SetCurrentContext(g_overlay8.context);
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = false;
    const bool gameForeground = IsGameWindowForeground();

    if (g_overlay8.mouseCapture && !AreAnyPhysicalMouseButtonsDown())
        SuperD3D8OverlayCancelMouseCapture();

    ImGui_ImplD3D8_NewFrame();
    ImGui_ImplWin32_NewFrame();
    if (!gameForeground)
    {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
    ImGui::NewFrame();

    POINT mousePt = {};
    const bool hasMousePt = TryGetCurrentMouseClientPos(&mousePt);
    if (!g_overlay8.superButtonPressed)
        SetSuperButtonHoverState(hasMousePt && IsPointInsideSuperButton(mousePt.x, mousePt.y));

    RenderOverlaySuperButton();

    if (g_overlay8.panelExpanded && g_overlay8.anchorX > -9000 && g_overlay8.anchorY > -9000)
    {
        ImGui::SetNextWindowPos(ImVec2((float)g_overlay8.anchorX, (float)g_overlay8.anchorY), ImGuiCond_Always);
        if (g_overlay8.mainFont)
            ImGui::PushFont(g_overlay8.mainFont);

        SkillOverlayBridgeSyncRetroState(g_overlay8.state);
        UpdateQuickSlotBarState(g_overlay8.state);
        RenderRetroSkillPanel(g_overlay8.state, g_overlay8.assets, nullptr, g_overlay8.mainScale, &g_overlay8.hooks);
    }

    g_overlay8.mouseHover = hasMousePt && IsPointInsidePanel(mousePt.x, mousePt.y);
    const bool shouldDrawOverlayCursor = hasMousePt && (g_overlay8.state.isDraggingSkill || g_overlay8.mouseHover || g_overlay8.mouseCapture);
    if (shouldDrawOverlayCursor)
    {
        const ImVec2 savedMousePos = io.MousePos;
        io.MousePos = ImVec2((float)mousePt.x, (float)mousePt.y);

        RenderRetroSkillCursorOverlay(
            g_overlay8.state,
            g_overlay8.assets,
            g_overlay8.mainScale,
            g_overlay8.superButtonHover,
            g_overlay8.superButtonPressed,
            g_overlay8.superButtonHoverStartTick,
            g_overlay8.superButtonHoverInstantUseNormal1);

        io.MousePos = savedMousePos;
    }

    if (g_overlay8.panelExpanded && g_overlay8.mainFont)
        ImGui::PopFont();

    ImGui::EndFrame();

    g_overlay8.mouseCapture = gameForeground && io.WantCaptureMouse && IsOverlayWindowInteractive();
    UpdateCursorSuppression(gameForeground && ShouldUseOverlayCursor());

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
    g_overlay8.superButtonPressed = false;
    g_overlay8.superButtonHover = false;

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

bool SuperD3D8OverlayConsumeToggleRequested()
{
    const bool requested = g_overlay8.superButtonToggleRequested;
    g_overlay8.superButtonToggleRequested = false;
    return requested;
}

HWND SuperD3D8OverlayGetGameHwnd()
{
    return g_overlay8.hwnd;
}

ImFont* SuperD3D8OverlayGetConsolasFont()
{
    return g_overlay8.consolasFont;
}
