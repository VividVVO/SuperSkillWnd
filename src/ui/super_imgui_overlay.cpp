#include "ui/super_imgui_overlay.h"

#include "core/Common.h"
#include "core/GameAddresses.h"
#include "skill/skill_overlay_bridge.h"
#include "ui/retro_skill_app.h"
#include "ui/overlay_cursor_utils.h"
#include "ui/overlay_input_utils.h"
#include "ui/retro_skill_panel.h"
#include "ui/overlay_style_utils.h"
#include "ui/retro_skill_text_dwrite.h"

#include "third_party/imgui/imgui.h"
#include "third_party/imgui/backends/imgui_impl_dx9.h"
#include "third_party/imgui/backends/imgui_impl_win32.h"

#include <string>
#include <cstdint>
#include <cfloat>
#include <cmath>
#include <vector>

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
        IDirect3DDevice9* device = nullptr;
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

    SuperOverlayRuntime g_overlay;
    std::vector<RECT> g_overlayVisiblePieces;
    LONG g_overlayClipLogBudget = 32;

    bool GetClientMousePointFromMessage(HWND hwnd, UINT msg, LPARAM lParam, POINT* outPoint);
    bool IsPointInsidePanel(int x, int y);
    bool OverlayOwnsMouseInput();

    bool RectHasArea(const RECT& rc)
    {
        return rc.right > rc.left && rc.bottom > rc.top;
    }

    RECT MakeRectXYWH(int x, int y, int w, int h)
    {
        RECT rc = { x, y, x + w, y + h };
        return rc;
    }

    bool HasSuperButtonRect()
    {
        return g_overlay.superButtonVisible && RectHasArea(g_overlay.superButtonRect);
    }

    bool IsPointInsideSuperButton(int x, int y)
    {
        if (!HasSuperButtonRect())
            return false;
        const RECT& rc = g_overlay.superButtonRect;
        return x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
    }

    void ResetSuperButtonState()
    {
        g_overlay.superButtonHover = false;
        g_overlay.superButtonPressed = false;
        g_overlay.superButtonToggleRequested = false;
        g_overlay.superButtonHoverStartTick = 0;
        g_overlay.superButtonHoverInstantUseNormal1 = false;
    }

    void SetSuperButtonHoverState(bool hover)
    {
        if (g_overlay.superButtonHover == hover)
            return;

        g_overlay.superButtonHover = hover;
        if (hover)
        {
            g_overlay.superButtonHoverStartTick = static_cast<uint64_t>(GetTickCount64());
            g_overlay.superButtonHoverInstantUseNormal1 = ((GetTickCount64() & 1ULL) != 0ULL);
        }
        else
        {
            g_overlay.superButtonHoverStartTick = 0;
            g_overlay.superButtonHoverInstantUseNormal1 = false;
        }
    }

    void RenderOverlaySuperButton()
    {
        if (!HasSuperButtonRect())
            return;

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        const ImVec2 minPos((float)g_overlay.superButtonRect.left, (float)g_overlay.superButtonRect.top);
        const ImVec2 maxPos((float)g_overlay.superButtonRect.right, (float)g_overlay.superButtonRect.bottom);
        if (!g_overlay.superButtonVisible)
        {
            UITexture* disabled = GetRetroSkillTexture(g_overlay.assets, "surpe.disabled");
            if (disabled && disabled->texture)
                drawList->AddImage((ImTextureID)disabled->texture, minPos, maxPos);
            return;
        }

        if (g_overlay.superButtonPressed)
        {
            UITexture* pressed = GetRetroSkillTexture(g_overlay.assets, "surpe.pressed");
            if (!pressed || !pressed->texture)
                pressed = GetRetroSkillTexture(g_overlay.assets, "surpe.mouseOver");
            if (!pressed || !pressed->texture)
                pressed = GetRetroSkillTexture(g_overlay.assets, "surpe.normal");
            if (pressed && pressed->texture)
                drawList->AddImage((ImTextureID)pressed->texture, minPos, maxPos);
            return;
        }

        UITexture* normal = GetRetroSkillTexture(g_overlay.assets, "surpe.normal");
        UITexture* hover = GetRetroSkillTexture(g_overlay.assets, "surpe.mouseOver");
        if (normal && normal->texture)
            drawList->AddImage((ImTextureID)normal->texture, minPos, maxPos);

        if (g_overlay.superButtonHover && hover && hover->texture)
        {
            const uint64_t nowTick = static_cast<uint64_t>(GetTickCount64());
            const uint64_t hoverStartTick = g_overlay.superButtonHoverStartTick ? g_overlay.superButtonHoverStartTick : nowTick;
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
            if (g_overlay.superButtonPressed || hit)
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
            g_overlay.superButtonPressed = true;
            SetSuperButtonHoverState(true);
            return true;
        }

        if (msg == WM_LBUTTONUP)
        {
            if (!g_overlay.superButtonPressed)
                return false;
            g_overlay.superButtonPressed = false;
            SetSuperButtonHoverState(hit);
            if (hit)
                g_overlay.superButtonToggleRequested = true;
            return true;
        }

        return false;
    }

    void AppendRectIfValid(std::vector<RECT>& rects, const RECT& rc)
    {
        if (RectHasArea(rc))
            rects.push_back(rc);
    }

    void SubtractRectFromPiece(const RECT& src, const RECT& cut, std::vector<RECT>& out)
    {
        RECT inter = {};
        if (!IntersectRect(&inter, &src, &cut))
        {
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

    uintptr_t GetActiveSkillWndObj()
    {
        if (SafeIsBadReadPtr((void*)ADDR_SkillWndEx, 4))
            return 0;
        return *(uintptr_t*)ADDR_SkillWndEx;
    }

    bool GetCWndManTopLevelVector(uintptr_t* outVec, int* outCount, int maxCount)
    {
        if (!outVec || !outCount || maxCount <= 0)
            return false;
        *outVec = 0;
        *outCount = 0;

        if (SafeIsBadReadPtr((void*)ADDR_CWndMan, 4))
            return false;
        const uintptr_t wndMan = *(uintptr_t*)ADDR_CWndMan;
        if (!wndMan || SafeIsBadReadPtr((void*)(wndMan + CWNDMAN_TOPLEVEL_OFF), 4))
            return false;

        // CWndMan+0x4A74 is a vector data pointer; the item count is stored at data[-1].
        const uintptr_t vec = *(uintptr_t*)(wndMan + CWNDMAN_TOPLEVEL_OFF);
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

    bool GetOverlayPanelRect(RECT* outRect)
    {
        if (!outRect || g_overlay.anchorX <= -9000 || g_overlay.anchorY <= -9000)
            return false;
        const PanelMetrics metrics = GetPanelMetrics(g_overlay.mainScale);
        *outRect = MakeRectXYWH(
            g_overlay.anchorX,
            g_overlay.anchorY,
            (int)metrics.width,
            (int)metrics.height);
        return true;
    }

    bool GetResetConfirmRectForHitTest(RECT* outRect)
    {
        if (!outRect || !g_overlay.state.superSkillResetConfirmVisible ||
            g_overlay.anchorX <= -9000 || g_overlay.anchorY <= -9000)
        {
            return false;
        }

        const PanelMetrics metrics = GetPanelMetrics(g_overlay.mainScale);
        UITexture* noticeBg = GetRetroSkillTexture(g_overlay.assets, "initial.backgrnd");
        const float noticeWidth = ((noticeBg && noticeBg->width > 0) ? (float)noticeBg->width : 260.0f) * g_overlay.mainScale;
        const float noticeHeight = ((noticeBg && noticeBg->height > 0) ? (float)noticeBg->height : 131.0f) * g_overlay.mainScale;
        float noticeX = floorf((float)g_overlay.anchorX + (metrics.width - noticeWidth) * 0.5f);
        float noticeY = floorf((float)g_overlay.anchorY + (metrics.height - noticeHeight) * 0.5f);

        RECT clientRect = {};
        if (g_overlay.hwnd && ::GetClientRect(g_overlay.hwnd, &clientRect))
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

    bool GetCWndRectForOverlayClip(uintptr_t wndObj, RECT* outRect)
    {
        if (!wndObj || !outRect)
            return false;

        const int w = CWnd_GetWidth(wndObj);
        const int h = CWnd_GetHeight(wndObj);
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
            return false;

        int x = CWnd_GetRenderX(wndObj);
        int y = CWnd_GetRenderY(wndObj);
        if (x < -10000 || x > 10000 || y < -10000 || y > 10000)
        {
            x = CWnd_GetX(wndObj);
            y = CWnd_GetY(wndObj);
        }
        if (x < -10000 || x > 10000 || y < -10000 || y > 10000)
            return false;

        *outRect = MakeRectXYWH(x, y, w, h);
        return true;
    }

    int GetCWndZOrderValueForOverlay(uintptr_t wndObj)
    {
        if (!wndObj || SafeIsBadReadPtr((void*)(wndObj + CWND_OFF_ZORDER * 4), 4))
            return 0;
        return *(int*)(wndObj + CWND_OFF_ZORDER * 4);
    }

    bool IsIgnoredPanelOccluder(uintptr_t wndObj, uintptr_t skillWndObj)
    {
        if (!wndObj)
            return true;
        if (wndObj == skillWndObj)
            return true;
        return false;
    }

    bool UpdateOverlayVisiblePieces(const char* reason)
    {
        g_overlayVisiblePieces.clear();

        RECT panelRect = {};
        if (!GetOverlayPanelRect(&panelRect))
            return false;

        RECT skillRect = {};
        const uintptr_t skillWndObj = GetActiveSkillWndObj();
        if (skillWndObj && GetCWndRectForOverlayClip(skillWndObj, &skillRect))
        {
            RECT clipped = {};
            if (!IntersectRect(&clipped, &panelRect, &skillRect))
                return false;
            panelRect = clipped;
        }

        std::vector<RECT> pieces;
        pieces.push_back(panelRect);

        std::vector<RECT> occluders;
        uintptr_t topVec = 0;
        int topCount = 0;
        if (GetCWndManTopLevelVector(&topVec, &topCount, 512))
        {
            for (int i = 0; i < topCount; ++i)
            {
                const uintptr_t slotAddr = topVec + i * 4;
                if (SafeIsBadReadPtr((void*)slotAddr, 4))
                    break;

                const uintptr_t wndObj = *(DWORD*)slotAddr;
                if (IsIgnoredPanelOccluder(wndObj, skillWndObj))
                    continue;

                RECT wndRect = {};
                RECT inter = {};
                if (!GetCWndRectForOverlayClip(wndObj, &wndRect))
                    continue;
                if (!IntersectRect(&inter, &panelRect, &wndRect))
                    continue;

                bool duplicate = false;
                for (size_t k = 0; k < occluders.size(); ++k)
                {
                    if (EqualRect(&occluders[k], &wndRect))
                    {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate)
                    occluders.push_back(wndRect);
            }
        }

        for (size_t i = 0; i < occluders.size() && !pieces.empty(); ++i)
        {
            std::vector<RECT> nextPieces;
            nextPieces.reserve(pieces.size() * 2 + 4);
            for (size_t j = 0; j < pieces.size(); ++j)
                SubtractRectFromPiece(pieces[j], occluders[i], nextPieces);
            pieces.swap(nextPieces);
            if (pieces.size() > 64)
                pieces.resize(64);
        }

        for (size_t i = 0; i < pieces.size(); ++i)
            AppendRectIfValid(g_overlayVisiblePieces, pieces[i]);

        LONG after = InterlockedDecrement(&g_overlayClipLogBudget);
        if (after >= 0)
        {
            WriteLogFmt("[PanelClip] reason=%s panel=(%ld,%ld,%ld,%ld) skill=%s(%ld,%ld,%ld,%ld) occluders=%d pieces=%d",
                reason ? reason : "-",
                panelRect.left, panelRect.top, panelRect.right, panelRect.bottom,
                RectHasArea(skillRect) ? "Y" : "N",
                skillRect.left, skillRect.top, skillRect.right, skillRect.bottom,
                (int)occluders.size(),
                (int)g_overlayVisiblePieces.size());
        }

        return !g_overlayVisiblePieces.empty();
    }

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

    int ToImGuiMouseButton(UINT msg, WPARAM wParam)
    {
        return OverlayToImGuiMouseButton(msg, wParam);
    }

    bool IsMouseButtonDownMessage(UINT msg)
    {
        return OverlayIsMouseButtonDownMessage(msg);
    }

    bool IsMouseButtonUpMessage(UINT msg)
    {
        return OverlayIsMouseButtonUpMessage(msg);
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
        return OverlayGetClientMousePointFromMessage(hwnd, msg, lParam, outPoint);
    }

    bool IsPointInsidePanel(int x, int y)
    {
        if (IsPointInsideSuperButton(x, y))
            return true;

        if (g_overlay.anchorX <= -9000 || g_overlay.anchorY <= -9000)
            return false;
        RECT resetConfirmRect = {};
        if (GetResetConfirmRectForHitTest(&resetConfirmRect) &&
            x >= resetConfirmRect.left && x < resetConfirmRect.right &&
            y >= resetConfirmRect.top && y < resetConfirmRect.bottom)
        {
            return true;
        }
        if (UpdateOverlayVisiblePieces("hit"))
        {
            for (size_t i = 0; i < g_overlayVisiblePieces.size(); ++i)
            {
                const RECT& rc = g_overlayVisiblePieces[i];
                if (x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom)
                    return true;
            }
            return false;
        }

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
        return g_overlay.mouseCapture || g_overlay.state.isDraggingSkill || g_overlay.superButtonPressed;
    }

    bool IsOverlayWindowInteractive()
    {
        return g_overlay.hwnd != nullptr;
    }

    bool IsGameWindowForeground()
    {
        return g_overlay.hwnd && ::GetForegroundWindow() == g_overlay.hwnd;
    }

    bool AreAnyPhysicalMouseButtonsDown()
    {
        return OverlayAreAnyPhysicalMouseButtonsDown();
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
        if (!outProbe)
            return false;

        uintptr_t topVec = 0;
        int topCount = 0;
        if (!GetCWndManTopLevelVector(&topVec, &topCount, 512))
            return false;

        const int expectedCenterX = expectedOriginX + (SKILL_BAR_COLS * SKILL_BAR_SLOT_SIZE) / 2;
        const int expectedCenterY = expectedOriginY + (SKILL_BAR_ROWS * SKILL_BAR_SLOT_SIZE) / 2;
        int bestScore = 0x7fffffff;
        QuickSlotWindowProbe bestProbe = {};

        for (int i = 0; i < topCount; ++i)
        {
            const uintptr_t slotAddr = topVec + i * sizeof(uintptr_t);
            if (SafeIsBadReadPtr((void*)slotAddr, sizeof(uintptr_t)))
                break;
            uintptr_t wnd = *(uintptr_t*)slotAddr;
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
        OverlayUpdateCursorSuppression(
            g_overlay.cursorSuppressed,
            g_overlay.showCursorHidden,
            g_overlay.savedCursor,
            shouldSuppress);
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

        OverlayConfigureImGuiStyle(g_overlay.mainScale);
        OverlayLoadMainAndConsolasFonts(g_overlay.mainScale, &g_overlay.mainFont, &g_overlay.consolasFont);

        if (!ImGui_ImplWin32_Init(hwnd))
            return false;
        if (!ImGui_ImplDX9_Init(device))
            return false;

        ResetRetroSkillData(g_overlay.state);
        const RetroDeviceRef deviceRef = { device, RetroRenderBackend_D3D9 };
        InitializeRetroSkillApp(g_overlay.state, g_overlay.assets, deviceRef, g_overlay.assetPath.c_str());
        ConfigureRetroSkillDefaultBehaviorHooks(g_overlay.hooks, g_overlay.state);
        SkillOverlayBridgeConfigureHooks(g_overlay.hooks);
        RetroSkillDWriteInitialize(deviceRef);

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
        ResetSuperButtonState();
        g_overlayVisiblePieces.clear();
        UpdateCursorSuppression(false);
    }
}

void SuperImGuiOverlaySetPanelExpanded(bool expanded)
{
    g_overlay.panelExpanded = expanded;
}

void SuperImGuiOverlaySetAnchor(int x, int y)
{
    g_overlay.anchorX = x;
    g_overlay.anchorY = y;
}

void SuperImGuiOverlaySetSuperButtonVisible(bool visible)
{
    g_overlay.superButtonVisible = visible;
    if (!visible)
        ResetSuperButtonState();
}

void SuperImGuiOverlaySetSuperButtonRect(const RECT* rect)
{
    if (rect)
    {
        g_overlay.superButtonRect = *rect;
    }
    else
    {
        SetRectEmpty(&g_overlay.superButtonRect);
    }
}

void SuperImGuiOverlayResetPanelState()
{
    ResetRetroSkillData(g_overlay.state);
    g_overlayVisiblePieces.clear();
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
    {
        const RetroDeviceRef deviceRef = { g_overlay.device, RetroRenderBackend_D3D9 };
        RetroSkillDWriteOnDeviceReset(deviceRef);
    }
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

    if (HandleOverlaySuperButtonMouseEvent(hwnd, msg, wParam, lParam))
    {
        g_overlay.mouseHover = g_overlay.superButtonHover;
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
    if (!HasSuperButtonRect() && (g_overlay.anchorX <= -9000 || g_overlay.anchorY <= -9000))
        return;

    ImGui::SetCurrentContext(g_overlay.context);
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = false;
    UpdateOverlayVisiblePieces("render");
    const bool gameForeground = IsGameWindowForeground();

    if (g_overlay.mouseCapture && !AreAnyPhysicalMouseButtonsDown())
    {
        SuperImGuiOverlayCancelMouseCapture();
    }

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    if (!gameForeground)
    {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
    ImGui::NewFrame();

    POINT mousePt = {};
    const bool hasMousePt = TryGetCurrentMouseClientPos(&mousePt);
    if (!g_overlay.superButtonPressed)
        SetSuperButtonHoverState(hasMousePt && IsPointInsideSuperButton(mousePt.x, mousePt.y));

    RenderOverlaySuperButton();

    if (g_overlay.panelExpanded && g_overlay.anchorX > -9000 && g_overlay.anchorY > -9000)
    {
        ImGui::SetNextWindowPos(ImVec2((float)g_overlay.anchorX, (float)g_overlay.anchorY), ImGuiCond_Always);
        if (g_overlay.mainFont)
            ImGui::PushFont(g_overlay.mainFont);

        SkillOverlayBridgeSyncRetroState(g_overlay.state);
        UpdateQuickSlotBarState(g_overlay.state);
        RenderRetroSkillPanel(g_overlay.state, g_overlay.assets, device, g_overlay.mainScale, &g_overlay.hooks);
    }

    g_overlay.mouseHover = hasMousePt && IsPointInsidePanel(mousePt.x, mousePt.y);
    const bool shouldDrawOverlayCursor = hasMousePt && (g_overlay.state.isDraggingSkill || g_overlay.mouseHover || g_overlay.mouseCapture);
    if (shouldDrawOverlayCursor)
    {
        const ImVec2 savedMousePos = io.MousePos;
        io.MousePos = ImVec2((float)mousePt.x, (float)mousePt.y);

        RenderRetroSkillCursorOverlay(
            g_overlay.state,
            g_overlay.assets,
            g_overlay.mainScale,
            g_overlay.superButtonHover,
            g_overlay.superButtonPressed,
            g_overlay.superButtonHoverStartTick,
            g_overlay.superButtonHoverInstantUseNormal1);

        io.MousePos = savedMousePos;
    }

    if (g_overlay.panelExpanded && g_overlay.mainFont)
        ImGui::PopFont();

    ImGui::EndFrame();

    g_overlay.mouseCapture = gameForeground && io.WantCaptureMouse && IsOverlayWindowInteractive();
    UpdateCursorSuppression(gameForeground && ShouldUseOverlayCursor());

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
    g_overlay.superButtonPressed = false;
    g_overlay.superButtonHover = false;

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

bool SuperImGuiOverlayConsumeToggleRequested()
{
    const bool requested = g_overlay.superButtonToggleRequested;
    g_overlay.superButtonToggleRequested = false;
    return requested;
}

HWND SuperImGuiOverlayGetGameHwnd()
{
    return g_overlay.hwnd;
}

ImFont* SuperImGuiOverlayGetConsolasFont()
{
    return g_overlay.consolasFont;
}
