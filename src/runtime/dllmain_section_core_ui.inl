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
    if (ENABLE_HOTPATH_DIAGNOSTIC_LOGS && after >= 0) {
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

    __try
    {
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
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        WriteLogFmt("[SuperBtnD3D] EXCEPTION msg=%u code=0x%08X", m, GetExceptionCode());
        ResetSuperBtnD3DInteractionState();
        return false;
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
    if (!ENABLE_HOTPATH_DIAGNOSTIC_LOGS)
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

    const bool hardenSuperBtnEdges =
        resID == IDR_BTN_NORMAL ||
        resID == IDR_BTN_HOVER ||
        resID == IDR_BTN_PRESSED ||
        resID == IDR_BTN_DISABLED;
    if (hardenSuperBtnEdges)
    {
        HardenPixelArtAlphaEdgesRgba(rgba, w, h);
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

static D3D8Texture* GetSuperBtnTextureForStateD3D8(DWORD state)
{
    switch (state) {
    case 0: return g_d3d8TexBtnNormal.pTexture8 ? &g_d3d8TexBtnNormal : nullptr;
    case 1: return g_d3d8TexBtnPressed.pTexture8 ? &g_d3d8TexBtnPressed : nullptr;
    case 2: return g_d3d8TexBtnDisabled.pTexture8 ? &g_d3d8TexBtnDisabled :
                    (g_d3d8TexBtnPressed.pTexture8 ? &g_d3d8TexBtnPressed : nullptr);
    case 3: return g_d3d8TexBtnHover.pTexture8 ? &g_d3d8TexBtnHover :
                    (g_d3d8TexBtnNormal.pTexture8 ? &g_d3d8TexBtnNormal : nullptr);
    case 4: return g_d3d8TexBtnPressed.pTexture8 ? &g_d3d8TexBtnPressed :
                    (g_d3d8TexBtnNormal.pTexture8 ? &g_d3d8TexBtnNormal : nullptr);
    default: return g_d3d8TexBtnNormal.pTexture8 ? &g_d3d8TexBtnNormal : nullptr;
    }
}

static SuperCursorFrameKind GetSuperCursorFrameKindForCurrentState()
{
    if (g_SuperBtnD3DPressed)
        return SUPER_CURSOR_FRAME_PRESSED;

    if (g_SuperBtnD3DHover) {
        const DWORD nowTick = GetTickCount();
        const DWORD hoverStartTick = g_SuperBtnD3DHoverStartTick ? g_SuperBtnD3DHoverStartTick : nowTick;
        const DWORD hoverElapsed = nowTick - hoverStartTick;
        if (hoverElapsed < 500)
            return SUPER_CURSOR_FRAME_HOVER_A;

        const DWORD loopFrame = ((hoverElapsed - 500) / 500) & 1;
        return loopFrame ? SUPER_CURSOR_FRAME_HOVER_A : SUPER_CURSOR_FRAME_HOVER_B;
    }

    return SUPER_CURSOR_FRAME_NORMAL;
}

static IDirect3DTexture9* GetSuperCursorTextureForFrameKind(SuperCursorFrameKind kind)
{
    switch (kind) {
    case SUPER_CURSOR_FRAME_PRESSED:
        return g_texCursorPressed ? g_texCursorPressed : g_texCursorNormal;
    case SUPER_CURSOR_FRAME_HOVER_A:
        return g_texCursorHoverA ? g_texCursorHoverA : g_texCursorNormal;
    case SUPER_CURSOR_FRAME_HOVER_B:
        return g_texCursorHoverB ? g_texCursorHoverB :
               (g_texCursorHoverA ? g_texCursorHoverA : g_texCursorNormal);
    case SUPER_CURSOR_FRAME_NORMAL:
    default:
        return g_texCursorNormal;
    }
}

static D3D8Texture* GetSuperCursorTextureForFrameKindD3D8(SuperCursorFrameKind kind)
{
    switch (kind) {
    case SUPER_CURSOR_FRAME_PRESSED:
        return g_d3d8TexCursorPressed.pTexture8 ? &g_d3d8TexCursorPressed :
               (g_d3d8TexCursorNormal.pTexture8 ? &g_d3d8TexCursorNormal : nullptr);
    case SUPER_CURSOR_FRAME_HOVER_A:
        return g_d3d8TexCursorHoverA.pTexture8 ? &g_d3d8TexCursorHoverA :
               (g_d3d8TexCursorNormal.pTexture8 ? &g_d3d8TexCursorNormal : nullptr);
    case SUPER_CURSOR_FRAME_HOVER_B:
        return g_d3d8TexCursorHoverB.pTexture8 ? &g_d3d8TexCursorHoverB :
               (g_d3d8TexCursorHoverA.pTexture8 ? &g_d3d8TexCursorHoverA :
                (g_d3d8TexCursorNormal.pTexture8 ? &g_d3d8TexCursorNormal : nullptr));
    case SUPER_CURSOR_FRAME_NORMAL:
    default:
        return g_d3d8TexCursorNormal.pTexture8 ? &g_d3d8TexCursorNormal : nullptr;
    }
}

static IDirect3DTexture9* GetSuperCursorTextureForCurrentState()
{
    return GetSuperCursorTextureForFrameKind(GetSuperCursorFrameKindForCurrentState());
}

static ImVec2 GetBottomRightAlignedCursorOffset(
    const ImVec2& baselineOffset,
    float baselineWidth,
    float baselineHeight,
    float currentWidth,
    float currentHeight)
{
    if (baselineWidth <= 0.0f || baselineHeight <= 0.0f ||
        currentWidth <= 0.0f || currentHeight <= 0.0f)
    {
        return baselineOffset;
    }

    return ImVec2(
        baselineOffset.x + (baselineWidth - currentWidth),
        baselineOffset.y + (baselineHeight - currentHeight));
}

static ImVec2 GetSuperCursorFixedOffset(
    SuperCursorFrameKind kind,
    float currentWidth,
    float currentHeight,
    float normalWidth,
    float normalHeight,
    float hoverAWidth,
    float hoverAHeight)
{
    const ImVec2 normalBaselineOffset(0.0f, -4.0f);
    const ImVec2 hoverABaselineOffset(0.0f, -2.0f);

    if (kind == SUPER_CURSOR_FRAME_HOVER_A)
        return hoverABaselineOffset;

    if (kind == SUPER_CURSOR_FRAME_HOVER_B)
    {
        return GetBottomRightAlignedCursorOffset(
            hoverABaselineOffset,
            hoverAWidth,
            hoverAHeight,
            currentWidth,
            currentHeight);
    }

    if (kind == SUPER_CURSOR_FRAME_PRESSED)
    {
        const ImVec2 bottomRightAligned = GetBottomRightAlignedCursorOffset(
            normalBaselineOffset,
            normalWidth,
            normalHeight,
            currentWidth,
            currentHeight);
        return ImVec2(bottomRightAligned.x - 3.0f, bottomRightAligned.y);
    }

    return normalBaselineOffset;
}

static void DrawSuperButtonCursorInPresent(IDirect3DDevice9* pDevice)
{
    if (!pDevice || !g_GameHwnd)
        return;
    if (!ShouldSuppressGameMouseForSuperBtnD3D())
        return;
    if (SuperImGuiOverlayShouldSuppressGameMouse())
        return;

    const SuperCursorFrameKind frameKind = GetSuperCursorFrameKindForCurrentState();
    IDirect3DTexture9* tex = GetSuperCursorTextureForFrameKind(frameKind);
    if (!tex)
        return;

    POINT pt = {};
    if (!GetCursorPos(&pt) || !ScreenToClient(g_GameHwnd, &pt))
        return;

    D3DSURFACE_DESC desc = {};
    if (FAILED(tex->GetLevelDesc(0, &desc)))
        return;

    float normalWidth = (float)desc.Width;
    float normalHeight = (float)desc.Height;
    if (g_texCursorNormal)
    {
        D3DSURFACE_DESC normalDesc = {};
        if (SUCCEEDED(g_texCursorNormal->GetLevelDesc(0, &normalDesc)) && normalDesc.Width > 0 && normalDesc.Height > 0)
        {
            normalWidth = (float)normalDesc.Width;
            normalHeight = (float)normalDesc.Height;
        }
    }

    float hoverAWidth = normalWidth;
    float hoverAHeight = normalHeight;
    if (g_texCursorHoverA)
    {
        D3DSURFACE_DESC hoverADesc = {};
        if (SUCCEEDED(g_texCursorHoverA->GetLevelDesc(0, &hoverADesc)) && hoverADesc.Width > 0 && hoverADesc.Height > 0)
        {
            hoverAWidth = (float)hoverADesc.Width;
            hoverAHeight = (float)hoverADesc.Height;
        }
    }

    const ImVec2 extraOffset = GetSuperCursorFixedOffset(
        frameKind,
        (float)desc.Width,
        (float)desc.Height,
        normalWidth,
        normalHeight,
        hoverAWidth,
        hoverAHeight);
    const float drawX = floorf((float)pt.x + extraOffset.x);
    const float drawY = floorf((float)pt.y + extraOffset.y);
    DrawTexturedQuad(pDevice, tex, drawX, drawY, (float)desc.Width, (float)desc.Height);

    LONG after = InterlockedDecrement(&g_PresentCursorDrawLogBudget);
    if (after >= 0) {
        WriteLogFmt("[PresentCursorDraw] hover=%d pressed=%d pos=(%d,%d) draw=(%.1f,%.1f) tex=0x%08X size=%ux%u extra=(%.1f,%.1f)",
            g_SuperBtnD3DHover ? 1 : 0,
            g_SuperBtnD3DPressed ? 1 : 0,
            pt.x, pt.y,
            drawX, drawY,
            (DWORD)(uintptr_t)tex,
            (unsigned)desc.Width,
            (unsigned)desc.Height,
            extraOffset.x,
            extraOffset.y);
    }
}

static void DrawSuperButtonTextureInPresentD3D8(void* pDevice8)
{
    if (ENABLE_POST_B9F6E0_NATIVE_TIMING_TEST)
        return;
    if (!pDevice8 || !g_NativeBtnCreated || !g_SuperBtnObj)
        return;

    if (ENABLE_SUPERBTN_D3D_BUTTON_MODE) {
        if (!g_D3D8TexturesLoaded)
            return;
        if (!UpdateSuperBtnVisiblePieces("d3d8_present_draw"))
            return;

        D3D8Texture* tex = GetSuperBtnTextureForStateD3D8(g_SuperBtnD3DVisualState);
        if (!tex || !tex->pTexture8)
            return;

        D3D8SavedState saved = {};
        D3D8_SaveRenderState(pDevice8, saved);
        D3D8_SetOverlayRenderState(pDevice8);

        bool ok = false;
        for (size_t i = 0; i < g_SuperBtnVisiblePieces.size(); ++i) {
            const SuperBtnVisiblePiece& piece = g_SuperBtnVisiblePieces[i];
            const RECT& rc = piece.screen;
            const int w = rc.right - rc.left;
            const int h = rc.bottom - rc.top;
            if (w <= 0 || h <= 0)
                continue;

            D3D8_DrawTexturedQuadUV(
                pDevice8,
                tex,
                (float)rc.left,
                (float)rc.top,
                (float)w,
                (float)h,
                piece.u0,
                piece.v0,
                piece.u1,
                piece.v1,
                0xFFFFFFFF);
            ok = true;
        }

        D3D8_RestoreRenderState(pDevice8, saved);

        if (ok)
            g_SuperBtnLastDrawTick = GetTickCount();

        LONG after = InterlockedDecrement(&g_PresentBtnDrawLogBudget);
        if (after >= 0) {
            WriteLogFmt("[D3D8PresentBtnDraw] ok=%d state=%u pieces=%d tex=0x%08X btn=0x%08X",
                ok ? 1 : 0,
                (unsigned)g_SuperBtnD3DVisualState,
                (int)g_SuperBtnVisiblePieces.size(),
                (DWORD)(uintptr_t)tex->pTexture8,
                (DWORD)g_SuperBtnObj);
        }
        return;
    }

    if (!ENABLE_PRESENT_SUPERBTN_DRAW || !g_D3D8TexturesLoaded)
        return;

    DWORD state = NormalizeSuperBtnVisualState(GetCurrentSuperBtnState());
    D3D8Texture* tex = GetSuperBtnTextureForStateD3D8(state);
    int screenX = 0;
    int screenY = 0;
    int screenW = 0;
    int screenH = 0;
    if (!tex || !tex->pTexture8 || !GetButtonScreenRectByObj(g_SuperBtnObj, &screenX, &screenY, &screenW, &screenH))
        return;

    D3D8SavedState saved = {};
    D3D8_SaveRenderState(pDevice8, saved);
    D3D8_SetOverlayRenderState(pDevice8);
    D3D8_DrawTexturedQuad(pDevice8, tex, (float)screenX, (float)screenY, (float)screenW, (float)screenH, 0xFFFFFFFF);
    D3D8_RestoreRenderState(pDevice8, saved);

    LONG after = InterlockedDecrement(&g_PresentBtnDrawLogBudget);
    if (after >= 0) {
        WriteLogFmt("[D3D8PresentBtnDraw] state=%u rect=(%d,%d,%d,%d) tex=0x%08X btn=0x%08X",
            state,
            screenX, screenY, screenW, screenH,
            (DWORD)(uintptr_t)tex->pTexture8,
            (DWORD)g_SuperBtnObj);
    }
}

static void DrawSuperButtonCursorInPresentD3D8(void* pDevice8)
{
    HWND hwnd = g_D3D8GameHwnd ? g_D3D8GameHwnd : g_GameHwnd;
    if (!pDevice8 || !hwnd)
        return;
    if (!ShouldSuppressGameMouseForSuperBtnD3D())
        return;
    if (SuperD3D8OverlayShouldSuppressGameMouse())
        return;
    if (!g_D3D8TexturesLoaded)
        return;

    const SuperCursorFrameKind frameKind = GetSuperCursorFrameKindForCurrentState();
    D3D8Texture* tex = GetSuperCursorTextureForFrameKindD3D8(frameKind);
    if (!tex || !tex->pTexture8)
        return;

    POINT pt = {};
    if (!GetCursorPos(&pt) || !ScreenToClient(hwnd, &pt))
        return;

    float normalWidth = (tex->width > 0) ? (float)tex->width : 0.0f;
    float normalHeight = (tex->height > 0) ? (float)tex->height : 0.0f;
    if (g_d3d8TexCursorNormal.width > 0 && g_d3d8TexCursorNormal.height > 0)
    {
        normalWidth = (float)g_d3d8TexCursorNormal.width;
        normalHeight = (float)g_d3d8TexCursorNormal.height;
    }
    float hoverAWidth = normalWidth;
    float hoverAHeight = normalHeight;
    if (g_d3d8TexCursorHoverA.width > 0 && g_d3d8TexCursorHoverA.height > 0)
    {
        hoverAWidth = (float)g_d3d8TexCursorHoverA.width;
        hoverAHeight = (float)g_d3d8TexCursorHoverA.height;
    }

    const ImVec2 extraOffset = GetSuperCursorFixedOffset(
        frameKind,
        (float)tex->width,
        (float)tex->height,
        normalWidth,
        normalHeight,
        hoverAWidth,
        hoverAHeight);
    const float drawX = floorf((float)pt.x + extraOffset.x);
    const float drawY = floorf((float)pt.y + extraOffset.y);

    D3D8SavedState saved = {};
    D3D8_SaveRenderState(pDevice8, saved);
    D3D8_SetOverlayRenderState(pDevice8);
    D3D8_DrawTexturedQuad(pDevice8, tex, drawX, drawY, (float)tex->width, (float)tex->height, 0xFFFFFFFF);
    D3D8_RestoreRenderState(pDevice8, saved);

    LONG after = InterlockedDecrement(&g_PresentCursorDrawLogBudget);
    if (after >= 0) {
        WriteLogFmt("[D3D8PresentCursorDraw] hover=%d pressed=%d pos=(%d,%d) draw=(%.1f,%.1f) tex=0x%08X size=%dx%d extra=(%.1f,%.1f)",
            g_SuperBtnD3DHover ? 1 : 0,
            g_SuperBtnD3DPressed ? 1 : 0,
            pt.x, pt.y,
            drawX, drawY,
            (DWORD)(uintptr_t)tex->pTexture8,
            tex->width,
            tex->height,
            extraOffset.x,
            extraOffset.y);
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
    if (ENABLE_HOTPATH_DIAGNOSTIC_LOGS && after >= 0) {
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
        if (ENABLE_HOTPATH_DIAGNOSTIC_LOGS && after >= 0) {
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
    if (ENABLE_HOTPATH_DIAGNOSTIC_LOGS && after >= 0) {
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
    if (ENABLE_HOTPATH_DIAGNOSTIC_LOGS && tag && strcmp(tag, "SuperBtn") == 0) {
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
    if (!ENABLE_HOTPATH_DIAGNOSTIC_LOGS)
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
    if (!ENABLE_HOTPATH_DIAGNOSTIC_LOGS)
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
                if (ENABLE_HOTPATH_DIAGNOSTIC_LOGS && mAfter >= 0)
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
                if (ENABLE_HOTPATH_DIAGNOSTIC_LOGS && mAfter >= 0)
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
