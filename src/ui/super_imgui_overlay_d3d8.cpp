#include "ui/super_imgui_overlay_d3d8.h"

#include "core/Common.h"
#include "core/GameAddresses.h"
#include "d3d8/d3d8_renderer.h"
#include "skill/skill_local_data.h"
#include "skill/skill_overlay_bridge.h"
#include "ui/retro_skill_app.h"
#include "ui/retro_skill_assets.h"
#include "ui/overlay_cursor_utils.h"
#include "ui/overlay_input_utils.h"
#include "ui/retro_skill_panel.h"
#include "ui/overlay_style_utils.h"
#include "ui/retro_skill_text_dwrite.h"

#include "third_party/imgui/imgui.h"
#include "third_party/imgui/backends/imgui_impl_d3d8.h"
#include "third_party/imgui/backends/imgui_impl_win32.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <cstdint>
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

    struct TopLevelVectorCandidateStats
    {
        int sampledEntries = 0;
        int readableEntries = 0;
        int saneSizeEntries = 0;
        int windowishEntries = 0;
        uintptr_t firstReadableWnd = 0;
    };

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
    const int kIndependentBuffOverlayMaxColumns = 8;
    bool g_independentBuffRightButtonWasDown = false;

    bool ShouldKeepOverlayVisibleForIndependentBuff()
    {
        return SkillOverlayBridgeHasIndependentBuffOverlayEntries();
    }

    bool TryFindIndependentBuffOverlaySkillIdAtPoint(int x, int y, int* outSkillId);

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

    std::string ResolveIndependentBuffDisplayName(int skillId)
    {
        std::string name;
        if (skillId > 0 && SkillLocalDataGetName(skillId, name) && !name.empty())
            return name;

        char buf[32] = {};
        sprintf_s(buf, "Skill %d", skillId);
        return buf;
    }

    std::string FormatIndependentBuffRemainingText(const IndependentBuffOverlayEntry& entry)
    {
        if (entry.totalDurationMs <= 0)
            return "INF";

        int totalSeconds = (entry.remainingMs + 999) / 1000;
        if (totalSeconds < 0)
            totalSeconds = 0;

        char buf[32] = {};
        if (totalSeconds >= 60)
            sprintf_s(buf, "%d:%02d", totalSeconds / 60, totalSeconds % 60);
        else
            sprintf_s(buf, "%ds", totalSeconds);
        return buf;
    }

    bool IsLikelyWndVtable(uintptr_t value)
    {
        return value >= 0x00D00000 && value < 0x00F80000;
    }

    void AnalyzeTopLevelVectorCandidate(uintptr_t vec, int count, int sampleLimit, TopLevelVectorCandidateStats* outStats)
    {
        if (!outStats)
            return;

        *outStats = {};
        if (!vec || count <= 0)
            return;

        const int limit = count < sampleLimit ? count : sampleLimit;
        for (int i = 0; i < limit; ++i)
        {
            const uintptr_t slotAddr = vec + i * sizeof(uintptr_t);
            if (SafeIsBadReadPtr((void*)slotAddr, sizeof(uintptr_t)))
                break;

            ++outStats->sampledEntries;
            const uintptr_t wnd = *(uintptr_t*)slotAddr;
            if (!wnd || SafeIsBadReadPtr((void*)wnd, 0x30))
                continue;

            ++outStats->readableEntries;
            if (!outStats->firstReadableWnd)
                outStats->firstReadableWnd = wnd;

            const uintptr_t vt1 = *(uintptr_t*)wnd;
            const uintptr_t vt2 = *(uintptr_t*)(wnd + 4);
            const int w = CWnd_GetWidth(wnd);
            const int h = CWnd_GetHeight(wnd);
            const bool saneSize = w > 0 && h > 0 && w < 4000 && h < 4000;
            if (saneSize)
                ++outStats->saneSizeEntries;

            if (saneSize && IsLikelyWndVtable(vt1) && IsLikelyWndVtable(vt2))
                ++outStats->windowishEntries;
        }
    }

    bool IsValidTopLevelWindowVector(uintptr_t vec, int count, TopLevelVectorCandidateStats* outStats)
    {
        AnalyzeTopLevelVectorCandidate(vec, count, 24, outStats);
        if (!outStats)
            return false;

        if (outStats->windowishEntries >= 2)
            return true;

        if (outStats->readableEntries >= 4 && outStats->saneSizeEntries >= 2)
            return true;

        return false;
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

        auto tryVectorField = [&](uintptr_t fieldAddr, uintptr_t* foundVec, int* foundCount, TopLevelVectorCandidateStats* outStats) -> bool
        {
            if (!fieldAddr || SafeIsBadReadPtr((void*)fieldAddr, 4))
                return false;
            const uintptr_t vec = *(uintptr_t*)fieldAddr;
            if (!vec || vec < 4 || SafeIsBadReadPtr((void*)(vec - 4), 4))
                return false;
            int count = *(int*)(vec - 4);
            if (count <= 0 || count > 4096)
                return false;
            if (count > maxCount)
                count = maxCount;
            TopLevelVectorCandidateStats stats = {};
            if (!IsValidTopLevelWindowVector(vec, count, &stats))
            {
                if (outStats)
                    *outStats = stats;
                return false;
            }
            *foundVec = vec;
            *foundCount = count;
            if (outStats)
                *outStats = {};
            return true;
        };

        uintptr_t vec = 0;
        int count = 0;
        TopLevelVectorCandidateStats rejectedStats = {};
        if (!tryVectorField(wndMan + CWNDMAN_TOPLEVEL_OFF, &vec, &count, &rejectedStats))
        {
            uintptr_t rejectedVec = 0;
            int rejectedCount = 0;
            if (!SafeIsBadReadPtr((void*)(wndMan + CWNDMAN_TOPLEVEL_OFF), 4))
            {
                rejectedVec = *(uintptr_t*)(wndMan + CWNDMAN_TOPLEVEL_OFF);
                if (rejectedVec >= 4 && !SafeIsBadReadPtr((void*)(rejectedVec - 4), 4))
                    rejectedCount = *(int*)(rejectedVec - 4);
            }

            static DWORD s_lastRejectedVecLogTick = 0;
            const DWORD rejectedNowTick = GetTickCount();
            if (rejectedVec && rejectedNowTick - s_lastRejectedVecLogTick > 1000)
            {
                s_lastRejectedVecLogTick = rejectedNowTick;
                WriteLogFmt(
                    "[IndependentBuffOverlayTopLevel] d3d8 rejectVec off=0x%X vec=0x%08X count=%d sampled=%d readable=%d sane=%d windowish=%d firstWnd=0x%08X",
                    CWNDMAN_TOPLEVEL_OFF,
                    (DWORD)rejectedVec,
                    rejectedCount,
                    rejectedStats.sampledEntries,
                    rejectedStats.readableEntries,
                    rejectedStats.saneSizeEntries,
                    rejectedStats.windowishEntries,
                    (DWORD)rejectedStats.firstReadableWnd);
            }

            for (int delta = -0x40; delta <= 0x40; delta += 4)
            {
                if (delta == 0)
                    continue;

                TopLevelVectorCandidateStats stats = {};
                if (tryVectorField(wndMan + CWNDMAN_TOPLEVEL_OFF + delta, &vec, &count, &stats))
                {
                    static DWORD s_lastFallbackVecLogTick = 0;
                    const DWORD nowTick = GetTickCount();
                    if (nowTick - s_lastFallbackVecLogTick > 1000)
                    {
                        s_lastFallbackVecLogTick = nowTick;
                        WriteLogFmt("[IndependentBuffOverlayTopLevel] d3d8 fallbackVec off=0x%X vec=0x%08X count=%d",
                            CWNDMAN_TOPLEVEL_OFF + delta,
                            (DWORD)vec,
                            count);
                    }
                    break;
                }
                else
                {
                    const uintptr_t fieldAddr = wndMan + CWNDMAN_TOPLEVEL_OFF + delta;
                    uintptr_t candidateVec = 0;
                    int candidateCount = 0;
                    if (!SafeIsBadReadPtr((void*)fieldAddr, 4))
                    {
                        candidateVec = *(uintptr_t*)fieldAddr;
                        if (candidateVec >= 4 && !SafeIsBadReadPtr((void*)(candidateVec - 4), 4))
                            candidateCount = *(int*)(candidateVec - 4);
                    }

                    static DWORD s_lastRejectedFallbackVecLogTick = 0;
                    const DWORD nowTick = GetTickCount();
                    if (candidateVec && nowTick - s_lastRejectedFallbackVecLogTick > 1000)
                    {
                        s_lastRejectedFallbackVecLogTick = nowTick;
                        WriteLogFmt(
                            "[IndependentBuffOverlayTopLevel] d3d8 rejectVec off=0x%X vec=0x%08X count=%d sampled=%d readable=%d sane=%d windowish=%d firstWnd=0x%08X",
                            CWNDMAN_TOPLEVEL_OFF + delta,
                            (DWORD)candidateVec,
                            candidateCount,
                            stats.sampledEntries,
                            stats.readableEntries,
                            stats.saneSizeEntries,
                            stats.windowishEntries,
                            (DWORD)stats.firstReadableWnd);
                    }
                }
            }
        }

        if (!vec || count <= 0)
            return false;

        *outVec = vec;
        *outCount = count;
        return true;
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

    void DumpQuickSlotProbeCandidatesD3D8(int clientW, int clientH, int expectedOriginX, int expectedOriginY)
    {
        uintptr_t topVec = 0;
        int topCount = 0;
        if (!GetCWndManTopLevelVector(&topVec, &topCount, 512))
        {
            uintptr_t wndMan = 0;
            uintptr_t vec = 0;
            int rawCount = 0;
            bool addrReadable = !SafeIsBadReadPtr((void*)ADDR_CWndMan, 4);
            if (addrReadable)
            {
                wndMan = *(uintptr_t*)ADDR_CWndMan;
                if (wndMan && !SafeIsBadReadPtr((void*)(wndMan + CWNDMAN_TOPLEVEL_OFF), 4))
                {
                    vec = *(uintptr_t*)(wndMan + CWNDMAN_TOPLEVEL_OFF);
                    if (vec >= 4 && !SafeIsBadReadPtr((void*)(vec - 4), 4))
                        rawCount = *(int*)(vec - 4);
                }
            }
            WriteLogFmt("[IndependentBuffOverlayTopLevel] d3d8 unavailable client=%dx%d origin=(%d,%d) addrReadable=%d wndMan=0x%08X vec=0x%08X rawCount=%d",
                clientW,
                clientH,
                expectedOriginX,
                expectedOriginY,
                addrReadable ? 1 : 0,
                (DWORD)wndMan,
                (DWORD)vec,
                rawCount);

            if (wndMan)
            {
                for (int delta = -0x40; delta <= 0x40; delta += 4)
                {
                    const uintptr_t fieldAddr = wndMan + CWNDMAN_TOPLEVEL_OFF + delta;
                    if (SafeIsBadReadPtr((void*)fieldAddr, 4))
                        continue;

                    const uintptr_t candidateVec = *(uintptr_t*)fieldAddr;
                    if (!candidateVec || candidateVec < 4 || SafeIsBadReadPtr((void*)(candidateVec - 4), 4))
                        continue;

                    const int candidateCount = *(int*)(candidateVec - 4);
                    if (candidateCount <= 0 || candidateCount > 4096)
                        continue;

                    TopLevelVectorCandidateStats stats = {};
                    AnalyzeTopLevelVectorCandidate(candidateVec, candidateCount, 16, &stats);

                    WriteLogFmt("[IndependentBuffOverlayTopLevel] d3d8 vecCandidate off=0x%X field=0x%08X vec=0x%08X count=%d sampled=%d readable=%d sane=%d windowish=%d firstWnd=0x%08X",
                        CWNDMAN_TOPLEVEL_OFF + delta,
                        (DWORD)fieldAddr,
                        (DWORD)candidateVec,
                        candidateCount,
                        stats.sampledEntries,
                        stats.readableEntries,
                        stats.saneSizeEntries,
                        stats.windowishEntries,
                        (DWORD)stats.firstReadableWnd);
                }
            }
            return;
        }

        const int expectedCenterX = expectedOriginX + (SKILL_BAR_COLS * SKILL_BAR_SLOT_SIZE) / 2;
        const int expectedCenterY = expectedOriginY + (SKILL_BAR_ROWS * SKILL_BAR_SLOT_SIZE) / 2;
        WriteLogFmt("[IndependentBuffOverlayTopLevel] d3d8 scan client=%dx%d origin=(%d,%d) center=(%d,%d) topCount=%d",
            clientW,
            clientH,
            expectedOriginX,
            expectedOriginY,
            expectedCenterX,
            expectedCenterY,
            topCount);

        int logged = 0;
        for (int i = 0; i < topCount && logged < 20; ++i)
        {
            const uintptr_t slotAddr = topVec + i * sizeof(uintptr_t);
            if (SafeIsBadReadPtr((void*)slotAddr, sizeof(uintptr_t)))
                break;
            const uintptr_t wnd = *(uintptr_t*)slotAddr;
            if (!wnd || SafeIsBadReadPtr((void*)wnd, 0x30))
                continue;

            const int w = CWnd_GetWidth(wnd);
            const int h = CWnd_GetHeight(wnd);
            if (w <= 0 || h <= 0 || w > 4000 || h > 4000)
                continue;

            int x = 0;
            int y = 0;
            const char* posSource = "none";
            bool hasPos = ResolveQuickSlotProbePos(wnd, &x, &y, &posSource);
            const int centerX = hasPos ? (x + w / 2) : 0;
            const int centerY = hasPos ? (y + h / 2) : 0;
            const int dx = hasPos ? abs(centerX - expectedCenterX) : -1;
            const int dy = hasPos ? abs(centerY - expectedCenterY) : -1;

            WriteLogFmt("[IndependentBuffOverlayTopLevel] d3d8 cand index=%d wnd=0x%08X pos=%s hasPos=%d rect=(%d,%d,%d,%d) center=(%d,%d) delta=(%d,%d)",
                i,
                (DWORD)wnd,
                posSource,
                hasPos ? 1 : 0,
                x,
                y,
                w,
                h,
                centerX,
                centerY,
                dx,
                dy);
            ++logged;
        }
    }

    bool TryGetIndependentBuffOverlayClientRect(RECT* outClientRect)
    {
        if (!outClientRect || !g_overlay8.hwnd)
            return false;
        return ::GetClientRect(g_overlay8.hwnd, outClientRect) != FALSE;
    }

    int ResolveIndependentBuffOverlayColumnCount(const std::vector<IndependentBuffOverlayEntry>& entries)
    {
        int maxSlotIndex = -1;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const int slotIndex = entries[i].slotIndex >= 0 ? entries[i].slotIndex : (int)i;
            if (slotIndex > maxSlotIndex)
                maxSlotIndex = slotIndex;
        }

        if (maxSlotIndex < 0)
            return 1;
        return (std::max)(1, (std::min)(kIndependentBuffOverlayMaxColumns, maxSlotIndex + 1));
    }

    struct IndependentBuffOverlayLayout
    {
        RECT clientRect = {};
        RECT overlayRect = {};
        int columns = 1;
        int slotSize = 32;
        int gap = 2;
        bool usedNativeSlotLayout = false;
        std::vector<RECT> explicitSlotRects;
    };

    struct NativeBuffSlotMetrics
    {
        bool valid = false;
        int firstVisibleX = -1;
        int firstVisibleY = -1;
        int visibleCount = 0;
        int slotSize = 32;
        int stepX = 32;
        int baseX = 0;
        int baseY = 0;
        bool topRowOccupied[6] = {};
    };

    bool TryGetIndependentBuffOverlayAnchorRect(RECT* outRect)
    {
        if (!outRect)
            return false;

        if (g_overlay8.anchorX > -9000 && g_overlay8.anchorY > -9000)
        {
            const PanelMetrics metrics = GetPanelMetrics(g_overlay8.mainScale);
            *outRect = MakeRectXYWH(
                g_overlay8.anchorX,
                g_overlay8.anchorY,
                (int)floorf(metrics.width),
                (int)floorf(metrics.height));
            return true;
        }

        return false;
    }

    bool TryGetNativeBuffSlotMetrics(NativeBuffSlotMetrics* outMetrics)
    {
        if (!outMetrics)
            return false;

        *outMetrics = NativeBuffSlotMetrics{};

        static DWORD s_lastNativeScanEarlyLogTick = 0;
        const DWORD earlyNowTick = GetTickCount();

        if (SafeIsBadReadPtr((void*)ADDR_StatusBar, 4))
        {
            if (earlyNowTick - s_lastNativeScanEarlyLogTick > 1000)
            {
                s_lastNativeScanEarlyLogTick = earlyNowTick;
                WriteLogFmt("[IndependentBuffOverlayNativeScan] d3d8 fail stage=global_ptr addr=0x%08X",
                    (DWORD)ADDR_StatusBar);
            }
            return false;
        }

        uintptr_t statusBar = *(uintptr_t*)ADDR_StatusBar;
        const char* statusSource = "global";
        QuickSlotWindowProbe probe = {};
        int probeClientW = 0;
        int probeClientH = 0;
        int probeOriginX = SKILL_BAR_ORIGIN_X;
        int probeOriginY = SKILL_BAR_ORIGIN_Y;
        if (!statusBar)
        {
            statusBar = SkillOverlayBridgeGetObservedStatusBarPtr();
            if (statusBar)
                statusSource = "cached";
        }
        if (!statusBar && g_overlay8.hwnd)
        {
            RECT clientRect = {};
            if (::GetClientRect(g_overlay8.hwnd, &clientRect))
            {
                probeClientW = clientRect.right - clientRect.left;
                probeClientH = clientRect.bottom - clientRect.top;
                if (probeClientW == 1024 && probeClientH == 768)
                {
                    probeOriginX = 883;
                    probeOriginY = 697;
                }
                if (ProbeQuickSlotTopLevelWindow(probeOriginX, probeOriginY, false, &probe) ||
                    ProbeQuickSlotTopLevelWindow(probeOriginX, probeOriginY, true, &probe))
                {
                    statusBar = probe.wnd;
                    statusSource = "probe";
                }
            }
        }

        if (!statusBar || SafeIsBadReadPtr((void*)statusBar, 0xB30 + 4))
        {
            if (earlyNowTick - s_lastNativeScanEarlyLogTick > 1000)
            {
                s_lastNativeScanEarlyLogTick = earlyNowTick;
                WriteLogFmt("[IndependentBuffOverlayNativeScan] d3d8 fail stage=status_bar_ptr statusBar=0x%08X readable=%d source=%s cached=0x%08X probeWnd=0x%08X probeXYWH=(%d,%d,%d,%d)",
                    (DWORD)statusBar,
                    (!statusBar || SafeIsBadReadPtr((void*)statusBar, 0xB30 + 4)) ? 0 : 1,
                    statusSource,
                    (DWORD)SkillOverlayBridgeGetObservedStatusBarPtr(),
                    (DWORD)probe.wnd,
                    probe.x,
                    probe.y,
                    probe.w,
                    probe.h);
                if (!probe.wnd && probeClientW > 0 && probeClientH > 0)
                    DumpQuickSlotProbeCandidatesD3D8(probeClientW, probeClientH, probeOriginX, probeOriginY);
            }
            return false;
        }

        struct SlotRect
        {
            int x = 0;
            int y = 0;
            int w = 0;
            int h = 0;
        };

        std::vector<std::pair<int, SlotRect> > visibleTopRowSlots;
        visibleTopRowSlots.reserve(6);
        unsigned int wrapperMask = 0;
        unsigned int childMask = 0;
        unsigned int visibleMask = 0;
        for (int i = 0; i < 6; ++i)
        {
            const uintptr_t slotAddr = statusBar + (i < 6 ? (0xAE8 + i * 8) : (0xB18 + (i - 6) * 8));
            if (SafeIsBadReadPtr((void*)slotAddr, 8))
                continue;

            const uintptr_t wrapper = *(uintptr_t*)slotAddr;
            if (!wrapper || SafeIsBadReadPtr((void*)wrapper, 8))
                continue;
            wrapperMask |= (1u << i);

            const uintptr_t child = wrapper + 4;
            if (!child || SafeIsBadReadPtr((void*)child, 0x4C))
                continue;
            childMask |= (1u << i);

            SlotRect slot = {};
            slot.x = CWnd_GetX(child);
            slot.y = CWnd_GetY(child);
            const int w = CWnd_GetWidth(child);
            const int h = CWnd_GetHeight(child);
            slot.w = w;
            slot.h = h;
            const int renderX = CWnd_GetRenderX(child);
            const int renderY = CWnd_GetRenderY(child);
            const bool slotLooksVisible =
                w >= 16 &&
                h >= 16 &&
                (slot.x != 0 || slot.y != 0 || renderX != 0 || renderY != 0);
            if (!slotLooksVisible)
                continue;

            visibleMask |= (1u << i);
            visibleTopRowSlots.push_back(std::make_pair(i, slot));
            outMetrics->topRowOccupied[i] = true;
        }

        if (visibleTopRowSlots.empty())
        {
            static DWORD s_lastNativeScanFailLogTick = 0;
            const DWORD nowTick = GetTickCount();
            if (nowTick - s_lastNativeScanFailLogTick > 1000)
            {
                s_lastNativeScanFailLogTick = nowTick;
                WriteLogFmt("[IndependentBuffOverlayNativeScan] d3d8 statusBar=0x%08X source=%s topWr=0x%02X topChild=0x%02X topVisible=0x%02X",
                    (DWORD)statusBar,
                    statusSource,
                    wrapperMask,
                    childMask,
                    visibleMask);
            }
            return false;
        }

        std::sort(visibleTopRowSlots.begin(), visibleTopRowSlots.end(),
            [](const std::pair<int, SlotRect>& left, const std::pair<int, SlotRect>& right)
            {
                if (left.first != right.first)
                    return left.first < right.first;
                if (left.second.x != right.second.x)
                    return left.second.x < right.second.x;
                return left.second.y < right.second.y;
            });

        int stepX = 0;
        for (size_t i = 1; i < visibleTopRowSlots.size(); ++i)
        {
            const int deltaIndex = visibleTopRowSlots[i].first - visibleTopRowSlots[i - 1].first;
            const int deltaX = visibleTopRowSlots[i].second.x - visibleTopRowSlots[i - 1].second.x;
            if (deltaIndex > 0 && deltaX > 0)
            {
                const int candidate = deltaX / deltaIndex;
                if (candidate > 0 && (stepX == 0 || candidate < stepX))
                    stepX = candidate;
            }
        }

        outMetrics->valid = true;
        outMetrics->firstVisibleX = visibleTopRowSlots[0].second.x;
        outMetrics->firstVisibleY = visibleTopRowSlots[0].second.y;
        outMetrics->visibleCount = (int)visibleTopRowSlots.size();
        outMetrics->slotSize = (std::max)(visibleTopRowSlots[0].second.w, visibleTopRowSlots[0].second.h);
        outMetrics->stepX = stepX > 0 ? stepX : outMetrics->slotSize;
        outMetrics->baseX = visibleTopRowSlots[0].second.x - visibleTopRowSlots[0].first * outMetrics->stepX;
        outMetrics->baseY = visibleTopRowSlots[0].second.y;

        static DWORD s_lastNativeScanOkLogTick = 0;
        const DWORD nowTick = GetTickCount();
        if (nowTick - s_lastNativeScanOkLogTick > 1000)
        {
            s_lastNativeScanOkLogTick = nowTick;
            WriteLogFmt("[IndependentBuffOverlayNativeScan] d3d8 statusBar=0x%08X source=%s topWr=0x%02X topChild=0x%02X topVisible=0x%02X baseX=%d stepX=%d firstX=%d visibleCount=%d",
                (DWORD)statusBar,
                statusSource,
                wrapperMask,
                childMask,
                visibleMask,
                outMetrics->baseX,
                outMetrics->stepX,
                outMetrics->firstVisibleX,
                outMetrics->visibleCount);
        }

        return true;
    }

    bool TryBuildIndependentBuffOverlayLayout(
        const std::vector<IndependentBuffOverlayEntry>& entries,
        IndependentBuffOverlayLayout* outLayout)
    {
        if (!outLayout)
            return false;

        outLayout->explicitSlotRects.clear();
        outLayout->usedNativeSlotLayout = false;

        RECT clientRect = {};
        if (!TryGetIndependentBuffOverlayClientRect(&clientRect))
            return false;

        const float scale = (g_overlay8.mainScale > 0.0f) ? g_overlay8.mainScale : 1.0f;
        int slotSize = (int)floorf(32.0f * scale);
        int gap = (int)floorf(2.0f * scale);
        const int marginX = (int)floorf(10.0f * scale);
        const int marginY = (int)floorf(8.0f * scale);
        const int offsetX = (int)floorf(7.0f * scale);
        const int offsetY = -(int)floorf(5.0f * scale);
        const int columns = ResolveIndependentBuffOverlayColumnCount(entries);
        int maxSlotIndex = 0;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const int slotIndex = entries[i].slotIndex >= 0 ? entries[i].slotIndex : (int)i;
            if (slotIndex > maxSlotIndex)
                maxSlotIndex = slotIndex;
        }
        const int rows = (maxSlotIndex / columns) + 1;
        int width = columns * slotSize + (columns - 1) * gap;
        int height = rows * slotSize + (rows - 1) * gap;
        int x = 0;
        int y = 0;

        NativeBuffSlotMetrics nativeMetrics = {};
        if (TryGetNativeBuffSlotMetrics(&nativeMetrics) && nativeMetrics.valid)
        {
            slotSize = nativeMetrics.slotSize > 0 ? nativeMetrics.slotSize : slotSize;
            gap = nativeMetrics.stepX > slotSize ? (nativeMetrics.stepX - slotSize) : 0;
            width = columns * slotSize + (columns - 1) * gap;
            height = rows * slotSize + (rows - 1) * gap;

            int minOccupied = INT_MAX;
            int maxOccupied = INT_MIN;
            for (int slot = 0; slot < 6; ++slot)
            {
                if (nativeMetrics.topRowOccupied[slot])
                {
                    if (slot < minOccupied) minOccupied = slot;
                    if (slot > maxOccupied) maxOccupied = slot;
                }
            }

            if (minOccupied == INT_MAX)
                minOccupied = 0;
            if (maxOccupied == INT_MIN)
                maxOccupied = minOccupied - 1;

            std::vector<int> plannedSlots;
            plannedSlots.reserve(entries.size());
            const int firstCustomSlot = minOccupied - (int)entries.size();
            for (int slot = firstCustomSlot; slot < minOccupied; ++slot)
            {
                plannedSlots.push_back(slot);
            }

            outLayout->explicitSlotRects.reserve(plannedSlots.size());
            int minLeft = INT_MAX;
            int minTop = INT_MAX;
            int maxRight = INT_MIN;
            int maxBottom = INT_MIN;
            for (size_t i = 0; i < plannedSlots.size(); ++i)
            {
                const int slotLeft = nativeMetrics.baseX + plannedSlots[i] * nativeMetrics.stepX;
                const int slotTop = nativeMetrics.baseY;
                RECT slotRect = MakeRectXYWH(slotLeft, slotTop, slotSize, slotSize);
                outLayout->explicitSlotRects.push_back(slotRect);
                if (slotRect.left < minLeft) minLeft = slotRect.left;
                if (slotRect.top < minTop) minTop = slotRect.top;
                if (slotRect.right > maxRight) maxRight = slotRect.right;
                if (slotRect.bottom > maxBottom) maxBottom = slotRect.bottom;
            }
            x = minLeft;
            y = minTop;
            width = maxRight - minLeft;
            height = maxBottom - minTop;
            outLayout->usedNativeSlotLayout = true;
        }
        else
        {
            const int clientWidth = clientRect.right - clientRect.left;
            std::vector<int> semanticNativeSlots;
            SkillOverlayBridgeGetObservedNativeVisibleSemanticSlots(semanticNativeSlots);
            std::sort(semanticNativeSlots.begin(), semanticNativeSlots.end());
            semanticNativeSlots.erase(std::unique(semanticNativeSlots.begin(), semanticNativeSlots.end()), semanticNativeSlots.end());
            if (!semanticNativeSlots.empty())
            {
                const int maxOccupiedSlot = semanticNativeSlots.back();
                std::vector<int> plannedSlots;
                plannedSlots.reserve(entries.size());
                const int firstCustomSlot = -(int)entries.size();
                for (int slot = firstCustomSlot; slot < 0; ++slot)
                {
                    plannedSlots.push_back(slot);
                }

                static DWORD s_lastSemanticLayoutLogTick = 0;
                const DWORD nowTick = GetTickCount();
                if (nowTick - s_lastSemanticLayoutLogTick > 1000)
                {
                    s_lastSemanticLayoutLogTick = nowTick;
                    std::string slotsText;
                    std::string plannedText;
                    for (size_t i = 0; i < semanticNativeSlots.size(); ++i)
                    {
                        if (!slotsText.empty()) slotsText += ",";
                        slotsText += std::to_string(semanticNativeSlots[i]);
                    }
                    for (size_t i = 0; i < plannedSlots.size(); ++i)
                    {
                        if (!plannedText.empty()) plannedText += ",";
                        plannedText += std::to_string(plannedSlots[i]);
                    }
                    WriteLogFmt("[IndependentBuffOverlaySemanticLayout] d3d8 slots=%s planned=%s entryCount=%d span=%d leftOnly=1",
                        slotsText.c_str(),
                        plannedText.c_str(),
                        (int)entries.size(),
                        maxOccupiedSlot + 1);
                }

                const int semanticSpanSlots = maxOccupiedSlot + 1;
                const int baseX = clientWidth - marginX - semanticSpanSlots * slotSize + offsetX;
                outLayout->explicitSlotRects.reserve(plannedSlots.size());
                int minLeft = INT_MAX;
                int minTop = INT_MAX;
                int maxRight = INT_MIN;
                int maxBottom = INT_MIN;
                for (size_t i = 0; i < plannedSlots.size(); ++i)
                {
                    const int slotLeft = baseX + plannedSlots[i] * slotSize;
                    const int slotTop = marginY + offsetY;
                    RECT slotRect = MakeRectXYWH(slotLeft, slotTop, slotSize, slotSize);
                    outLayout->explicitSlotRects.push_back(slotRect);
                    if (slotRect.left < minLeft) minLeft = slotRect.left;
                    if (slotRect.top < minTop) minTop = slotRect.top;
                    if (slotRect.right > maxRight) maxRight = slotRect.right;
                    if (slotRect.bottom > maxBottom) maxBottom = slotRect.bottom;
                }
                x = minLeft;
                y = minTop;
                width = maxRight - minLeft;
                height = maxBottom - minTop;
            }
            else
            {
                const int nativeCount = SkillOverlayBridgeGetNativeVisibleBuffVisualCount();
                const int nativeColumns = nativeCount > 0 ? (std::min)(kIndependentBuffOverlayMaxColumns, nativeCount) : 0;
                const int nativeWidth = nativeColumns > 0
                    ? (nativeColumns * slotSize + (nativeColumns - 1) * gap)
                    : 0;
                x = clientWidth - marginX - nativeWidth - width + offsetX;
                const int observedNativeAnchorX = SkillOverlayBridgeGetObservedNativeVisibleBuffAnchorX();
                if (observedNativeAnchorX >= 0 && observedNativeAnchorX <= clientWidth)
                    x = observedNativeAnchorX - width + offsetX;
                y = marginY + offsetY;
            }
        }

        if (x < 0)
            x = 0;
        if (y < 0)
            y = 0;

        outLayout->clientRect = clientRect;
        outLayout->overlayRect = MakeRectXYWH(x, y, width, height);
        outLayout->columns = columns;
        outLayout->slotSize = slotSize;
        outLayout->gap = gap;
        return true;
    }

    bool TryGetIndependentBuffOverlaySlotRect(
        const IndependentBuffOverlayEntry& entry,
        int fallbackSlotIndex,
        const IndependentBuffOverlayLayout& layout,
        RECT* outRect)
    {
        if (!outRect)
            return false;

        if (!layout.explicitSlotRects.empty())
        {
            const int slotIndex = entry.slotIndex >= 0 ? entry.slotIndex : fallbackSlotIndex;
            if (slotIndex < 0 || slotIndex >= (int)layout.explicitSlotRects.size())
                return false;
            *outRect = layout.explicitSlotRects[slotIndex];
            return true;
        }

        if (layout.columns <= 0)
            return false;

        const int slotIndex = entry.slotIndex >= 0 ? entry.slotIndex : fallbackSlotIndex;
        const int row = slotIndex / layout.columns;
        const int col = slotIndex % layout.columns;
        const int slotLeft = layout.overlayRect.left + col * (layout.slotSize + layout.gap);
        const int slotTop = layout.overlayRect.top + row * (layout.slotSize + layout.gap);
        *outRect = MakeRectXYWH(slotLeft, slotTop, layout.slotSize, layout.slotSize);
        return true;
    }

    bool GetIndependentBuffOverlayRect(RECT* outRect, std::vector<IndependentBuffOverlayEntry>* outEntries = nullptr)
    {
        if (!outRect)
            return false;
        if (!g_overlay8.hwnd)
        {
            WriteLog("[IndependentBuffOverlayRect] d3d8 fail: hwnd missing");
            return false;
        }

        std::vector<IndependentBuffOverlayEntry> localEntries;
        std::vector<IndependentBuffOverlayEntry>& entries = outEntries ? *outEntries : localEntries;
        SkillOverlayBridgeGetIndependentBuffOverlayEntries(entries);
        if (entries.empty())
        {
            WriteLog("[IndependentBuffOverlayRect] d3d8 fail: entries empty");
            return false;
        }

        IndependentBuffOverlayLayout layout = {};
        if (!TryBuildIndependentBuffOverlayLayout(entries, &layout))
        {
            WriteLog("[IndependentBuffOverlayRect] d3d8 fail: GetClientRect");
            return false;
        }
        *outRect = layout.overlayRect;
        return true;
    }

    bool IsPointInsideIndependentBuffOverlay(int x, int y)
    {
        int skillId = 0;
        return TryFindIndependentBuffOverlaySkillIdAtPoint(x, y, &skillId);
    }

    bool TryFindIndependentBuffOverlaySkillIdAtPoint(int x, int y, int* outSkillId)
    {
        RECT overlayRect = {};
        std::vector<IndependentBuffOverlayEntry> entries;
        if (!GetIndependentBuffOverlayRect(&overlayRect, &entries))
            return false;

        if (x < overlayRect.left || x >= overlayRect.right ||
            y < overlayRect.top || y >= overlayRect.bottom)
        {
            return false;
        }

        IndependentBuffOverlayLayout layout = {};
        if (!TryBuildIndependentBuffOverlayLayout(entries, &layout))
            return false;

        for (size_t i = 0; i < entries.size(); ++i)
        {
            RECT slotRect = {};
            if (!TryGetIndependentBuffOverlaySlotRect(entries[i], (int)i, layout, &slotRect))
                continue;

            if (x >= slotRect.left && x < slotRect.right && y >= slotRect.top && y < slotRect.bottom)
            {
                if (outSkillId)
                    *outSkillId = entries[i].skillId;
                return true;
            }
        }

        return false;
    }

    void RenderIndependentBuffOverlayBar(const std::vector<IndependentBuffOverlayEntry>& providedEntries)
    {
        std::vector<IndependentBuffOverlayEntry> entries = providedEntries;
        RECT overlayRect = {};
        if (!GetIndependentBuffOverlayRect(&overlayRect, &entries))
            return;

        const float scale = (g_overlay8.mainScale > 0.0f) ? g_overlay8.mainScale : 1.0f;
        IndependentBuffOverlayLayout layout = {};
        if (!TryBuildIndependentBuffOverlayLayout(entries, &layout))
            return;

        static DWORD s_lastRenderLogTick = 0;
        const DWORD nowTick = GetTickCount();
        if (nowTick - s_lastRenderLogTick > 1000)
        {
            s_lastRenderLogTick = nowTick;
            WriteLogFmt("[IndependentBuffOverlayRender] d3d8 count=%d rect=(%d,%d,%d,%d) mode=%s explicitSlots=%d",
                (int)entries.size(),
                overlayRect.left,
                overlayRect.top,
                overlayRect.right,
                overlayRect.bottom,
                layout.usedNativeSlotLayout ? "fixed-child" : "fallback",
                (int)layout.explicitSlotRects.size());
        }

        ImGui::SetNextWindowPos(ImVec2((float)overlayRect.left, (float)overlayRect.top), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)(overlayRect.right - overlayRect.left), (float)(overlayRect.bottom - overlayRect.top)), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
        if (ImGui::Begin("##IndependentBuffOverlayD3D8", nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBackground))
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 origin = ImGui::GetWindowPos();

            for (int index = 0; index < (int)entries.size(); ++index)
            {
                const IndependentBuffOverlayEntry& entry = entries[index];
                RECT slotRect = {};
                if (!TryGetIndependentBuffOverlaySlotRect(entry, index, layout, &slotRect))
                    continue;

                const float localX = (float)(slotRect.left - overlayRect.left);
                const float localY = (float)(slotRect.top - overlayRect.top);
                const float slotSize = (float)(slotRect.right - slotRect.left);
                const ImVec2 cursorPos(localX, localY);
                const ImVec2 iconMin(origin.x + localX, origin.y + localY);
                const ImVec2 iconMax(iconMin.x + slotSize, iconMin.y + slotSize);

                ImGui::SetCursorPos(cursorPos);
                ImGui::PushID(entry.skillId);
                ImGui::InvisibleButton("independent_buff_icon", ImVec2(slotSize, slotSize));
                const bool hovered = ImGui::IsItemHovered();

                const ImU32 backColor = IM_COL32(0, 0, 0, 18);
                const ImU32 borderColor = IM_COL32(255, 255, 255, 46);
                drawList->AddRectFilled(iconMin, iconMax, backColor, 2.0f * scale);
                drawList->AddRect(iconMin, iconMax, borderColor, 2.0f * scale);

                UITexture* iconTexture = GetRetroSkillSkillIconTexture(g_overlay8.assets, entry.iconSkillId);

                if (iconTexture && iconTexture->texture)
                {
                    drawList->AddImage(
                        (ImTextureID)iconTexture->texture,
                        iconMin,
                        iconMax,
                        ImVec2(0.0f, 0.0f),
                        ImVec2(1.0f, 1.0f),
                        IM_COL32(255, 255, 255, 217));
                }
                else
                {
                    drawList->AddRectFilled(iconMin, iconMax, IM_COL32(82, 97, 120, 217), 2.0f * scale);
                }

                if (entry.totalDurationMs > 0 && entry.remainingMs > 0)
                {
                    float expiredRatio = 1.0f - ((float)entry.remainingMs / (float)entry.totalDurationMs);
                    if (expiredRatio < 0.0f) expiredRatio = 0.0f;
                    if (expiredRatio > 1.0f) expiredRatio = 1.0f;
                    const float fadeStartY = iconMax.y - floorf((iconMax.y - iconMin.y) * expiredRatio);
                    if (fadeStartY < iconMax.y)
                    {
                        drawList->AddRectFilled(
                            ImVec2(iconMin.x, fadeStartY),
                            iconMax,
                            IM_COL32(72, 72, 72, 150));
                    }
                }

                if (hovered)
                {
                    SkillEntry tooltipSkill = {};
                    tooltipSkill.skillId = entry.skillId;
                    tooltipSkill.iconId = entry.iconSkillId > 0 ? entry.iconSkillId : entry.skillId;
                    tooltipSkill.name = !entry.name.empty()
                        ? entry.name
                        : ResolveIndependentBuffDisplayName(entry.skillId);
                    tooltipSkill.maxLevel = entry.maxLevel;
                    tooltipSkill.tooltipPreview = entry.tooltipPreview;
                    tooltipSkill.tooltipDescription = entry.tooltipDescription;
                    tooltipSkill.tooltipDetail = entry.tooltipDetail;
                    RenderRetroBuffTooltipCard(tooltipSkill, g_overlay8.assets, scale);
                }

                ImGui::PopID();
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
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

    void RenderObservedSceneFadeMask(int alpha, const std::vector<IndependentBuffOverlayEntry>& activeEntries)
    {
        if (!g_overlay8.hwnd || alpha <= 0)
            return;

        std::vector<RECT> maskRects;
        maskRects.reserve(3);

        if (g_overlay8.panelExpanded)
        {
            RECT panelRect = {};
            if (TryGetIndependentBuffOverlayAnchorRect(&panelRect) && RectHasArea(panelRect))
                maskRects.push_back(panelRect);
        }

        if (HasSuperButtonRect())
            maskRects.push_back(g_overlay8.superButtonRect);

        if (!activeEntries.empty())
        {
            IndependentBuffOverlayLayout layout = {};
            if (TryBuildIndependentBuffOverlayLayout(activeEntries, &layout) &&
                RectHasArea(layout.overlayRect))
            {
                maskRects.push_back(layout.overlayRect);
            }
        }

        if (maskRects.empty())
            return;

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        for (size_t i = 0; i < maskRects.size(); ++i)
        {
            const RECT& rc = maskRects[i];
            drawList->AddRectFilled(
                ImVec2((float)rc.left, (float)rc.top),
                ImVec2((float)rc.right, (float)rc.bottom),
                IM_COL32(0, 0, 0, alpha));
        }

        static DWORD s_lastFadeMaskLogTick = 0;
        const DWORD nowTick = GetTickCount();
        if (nowTick - s_lastFadeMaskLogTick > 1000)
        {
            s_lastFadeMaskLogTick = nowTick;
            WriteLogFmt("[ObservedSceneFade] d3d8 apply alpha=%d parts=%d panel=%d btn=%d buff=%d",
                alpha,
                (int)maskRects.size(),
                (g_overlay8.panelExpanded && g_overlay8.anchorX > -9000 && g_overlay8.anchorY > -9000) ? 1 : 0,
                HasSuperButtonRect() ? 1 : 0,
                activeEntries.empty() ? 0 : 1);
        }
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
        if (IsPointInsideIndependentBuffOverlay(x, y))
            return true;
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
    if (!visible && ShouldKeepOverlayVisibleForIndependentBuff())
        visible = true;

    static bool s_lastVisible = false;
    if (visible != s_lastVisible)
    {
        s_lastVisible = visible;
        WriteLogFmt("[D3D8ImGuiOverlay] visible=%d", visible ? 1 : 0);
    }
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

    if (IsMouseMessage(msg))
    {
        POINT buffPt = {};
        bool hasBuffPt = GetClientMousePointFromMessage(hwnd, msg, lParam, &buffPt);
        int buffSkillId = 0;
        bool buffHit = hasBuffPt && TryFindIndependentBuffOverlaySkillIdAtPoint(buffPt.x, buffPt.y, &buffSkillId);
        if (!buffHit)
        {
            hasBuffPt = TryGetCurrentMouseClientPos(&buffPt);
            buffHit = hasBuffPt && TryFindIndependentBuffOverlaySkillIdAtPoint(buffPt.x, buffPt.y, &buffSkillId);
        }

        if (buffHit)
        {
            g_overlay8.mouseHover = true;
            if (IsMouseButtonDownMessage(msg))
                g_overlay8.mouseCapture = true;
            else if (IsMouseButtonUpMessage(msg) && !OverlayAreAnyPhysicalMouseButtonsDown())
                g_overlay8.mouseCapture = false;

            UpdateCursorSuppression(true);
            return true;
        }
    }

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
    std::vector<IndependentBuffOverlayEntry> independentBuffEntries;
    SkillOverlayBridgeGetIndependentBuffOverlayEntries(independentBuffEntries);
    const bool hasIndependentBuffOverlay = !independentBuffEntries.empty();
    const int sceneFadeAlpha = SkillOverlayBridgeGetObservedSceneFadeAlpha();
    const bool hasSceneFadeMask = sceneFadeAlpha > 0;
    static DWORD s_lastGateLogTick = 0;
    const DWORD gateNow = GetTickCount();
    if (hasIndependentBuffOverlay && gateNow - s_lastGateLogTick > 1000)
    {
        s_lastGateLogTick = gateNow;
        WriteLogFmt("[IndependentBuffOverlayRenderGate] d3d8 init=%d visible=%d ctx=%d hasEntries=%d btn=%d anchor=(%d,%d)",
            g_overlay8.initialized ? 1 : 0,
            g_overlay8.visible ? 1 : 0,
            g_overlay8.context ? 1 : 0,
            hasIndependentBuffOverlay ? 1 : 0,
            HasSuperButtonRect() ? 1 : 0,
            g_overlay8.anchorX,
            g_overlay8.anchorY);
    }
    if (!HasSuperButtonRect() &&
        (g_overlay8.anchorX <= -9000 || g_overlay8.anchorY <= -9000) &&
        !hasIndependentBuffOverlay &&
        !hasSceneFadeMask)
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

    const bool rightButtonDown = ((::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
    if (gameForeground && hasIndependentBuffOverlay && hasMousePt)
    {
        int hoveredBuffSkillId = 0;
        if (TryFindIndependentBuffOverlaySkillIdAtPoint(mousePt.x, mousePt.y, &hoveredBuffSkillId))
        {
            if (rightButtonDown && !g_independentBuffRightButtonWasDown && hoveredBuffSkillId > 0)
            {
                WriteLogFmt("[IndependentBuffOverlay] polled cancel request skillId=%d via render d3d8 pt=(%d,%d)",
                    hoveredBuffSkillId,
                    mousePt.x,
                    mousePt.y);
                SkillOverlayBridgeCancelIndependentBuff(hoveredBuffSkillId);
                independentBuffEntries.clear();
            }
        }
    }
    g_independentBuffRightButtonWasDown = rightButtonDown;

    if (hasIndependentBuffOverlay)
        RenderIndependentBuffOverlayBar(independentBuffEntries);
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

    RenderObservedSceneFadeMask(sceneFadeAlpha, independentBuffEntries);

    g_overlay8.mouseHover = hasMousePt && IsPointInsidePanel(mousePt.x, mousePt.y);
    const bool shouldDrawOverlayCursor = hasMousePt && (g_overlay8.state.isDraggingSkill || g_overlay8.mouseHover || g_overlay8.mouseCapture);
    if (shouldDrawOverlayCursor)
    {
        const ImVec2 savedMousePos = io.MousePos;
        io.MousePos = ImVec2((float)mousePt.x, (float)mousePt.y);
        const int observedNativeCursorState = SkillOverlayBridgeGetObservedNativeCursorState();

        RenderRetroSkillCursorOverlay(
            g_overlay8.state,
            g_overlay8.assets,
            g_overlay8.mainScale,
            g_overlay8.superButtonHover,
            g_overlay8.superButtonPressed,
            g_overlay8.superButtonHoverStartTick,
            g_overlay8.superButtonHoverInstantUseNormal1,
            observedNativeCursorState);

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
    return g_overlay8.mouseHover || ShouldUseOverlayCursor();
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
