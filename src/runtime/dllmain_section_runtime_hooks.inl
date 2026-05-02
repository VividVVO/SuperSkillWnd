// 获取游戏主窗口
// ============================================================================
static HWND GetRealGameWindow()
{
    struct Param
    {
        HWND hwnd;
        DWORD pid;
    };
    Param p = {NULL, GetCurrentProcessId()};

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL
                {
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
        return TRUE; }, (LPARAM)&p);
    return p.hwnd;
}

// ============================================================================
// PNG纹理从DLL资源加载（面板背景用，后续迁移到原生后可移除）
// ============================================================================
static IDirect3DTexture9 *LoadTextureFromResource(IDirect3DDevice9 *dev, int resID)
{
    if (!dev)
        return nullptr;

    HRSRC hRes = FindResourceA(g_hModule, MAKEINTRESOURCEA(resID), RT_RCDATA);
    if (!hRes)
    {
        WriteLogFmt("[Tex] FindResource(%d) failed", resID);
        return nullptr;
    }

    HGLOBAL hMem = LoadResource(g_hModule, hRes);
    DWORD sz = SizeofResource(g_hModule, hRes);
    if (!hMem || !sz)
    {
        WriteLogFmt("[Tex] LoadResource(%d) failed", resID);
        return nullptr;
    }

    void *pData = LockResource(hMem);
    if (!pData)
        return nullptr;

    int w, h, ch;
    unsigned char *pixels = stbi_load_from_memory((const unsigned char *)pData, (int)sz, &w, &h, &ch, 4);
    if (!pixels)
    {
        WriteLogFmt("[Tex] stbi_load(%d) failed", resID);
        return nullptr;
    }

    const bool hardenSuperBtnEdges =
        resID == IDR_BTN_NORMAL ||
        resID == IDR_BTN_HOVER ||
        resID == IDR_BTN_PRESSED ||
        resID == IDR_BTN_DISABLED;
    if (hardenSuperBtnEdges)
    {
        HardenPixelArtAlphaEdgesRgba(pixels, w, h);
    }

    IDirect3DTexture9 *tex = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)))
    {
        stbi_image_free(pixels);
        return nullptr;
    }

    D3DLOCKED_RECT lr;
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0)))
    {
        for (int y2 = 0; y2 < h; y2++)
        {
            unsigned char *src = pixels + y2 * w * 4;
            unsigned char *dst = (unsigned char *)lr.pBits + y2 * lr.Pitch;
            for (int x2 = 0; x2 < w; x2++)
            {
                dst[x2 * 4 + 0] = src[x2 * 4 + 2]; // B
                dst[x2 * 4 + 1] = src[x2 * 4 + 1]; // G
                dst[x2 * 4 + 2] = src[x2 * 4 + 0]; // R
                dst[x2 * 4 + 3] = src[x2 * 4 + 3]; // A
            }
        }
        tex->UnlockRect(0);
    }

    stbi_image_free(pixels);
    WriteLogFmt("[Tex] Loaded #%d: %dx%d", resID, w, h);
    return tex;
}

static IDirect3DTexture9 *LoadTextureFromFilePath(IDirect3DDevice9 *dev, const char *path)
{
    if (!dev || !path || !path[0])
        return nullptr;

    int w = 0, h = 0, ch = 0;
    unsigned char *pixels = stbi_load(path, &w, &h, &ch, 4);
    if (!pixels)
    {
        WriteLogFmt("[Tex] stbi_load(file) failed path=%s", path);
        return nullptr;
    }

    IDirect3DTexture9 *tex = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)))
    {
        stbi_image_free(pixels);
        return nullptr;
    }

    D3DLOCKED_RECT lr = {};
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0)))
    {
        for (int y2 = 0; y2 < h; y2++)
        {
            unsigned char *src = pixels + y2 * w * 4;
            unsigned char *dst = (unsigned char *)lr.pBits + y2 * lr.Pitch;
            for (int x2 = 0; x2 < w; x2++)
            {
                dst[x2 * 4 + 0] = src[x2 * 4 + 2];
                dst[x2 * 4 + 1] = src[x2 * 4 + 1];
                dst[x2 * 4 + 2] = src[x2 * 4 + 0];
                dst[x2 * 4 + 3] = src[x2 * 4 + 3];
            }
        }
        tex->UnlockRect(0);
    }

    stbi_image_free(pixels);
    WriteLogFmt("[Tex] Loaded file: %s -> %dx%d", path, w, h);
    return tex;
}

static D3D8Texture LoadTextureFromResourceD3D8(void *pDevice8, int resID)
{
    D3D8Texture tex = {};
    if (!pDevice8)
        return tex;

    HRSRC hRes = FindResourceA(g_hModule, MAKEINTRESOURCEA(resID), RT_RCDATA);
    if (!hRes)
    {
        WriteLogFmt("[D3D8Tex] FindResource(%d) failed", resID);
        return tex;
    }

    HGLOBAL hMem = LoadResource(g_hModule, hRes);
    DWORD sz = SizeofResource(g_hModule, hRes);
    if (!hMem || !sz)
    {
        WriteLogFmt("[D3D8Tex] LoadResource(%d) failed", resID);
        return tex;
    }

    void *pData = LockResource(hMem);
    if (!pData)
    {
        WriteLogFmt("[D3D8Tex] LockResource(%d) failed", resID);
        return tex;
    }

    int w = 0;
    int h = 0;
    int ch = 0;
    unsigned char *rgba = stbi_load_from_memory((const unsigned char *)pData, (int)sz, &w, &h, &ch, 4);
    if (!rgba)
    {
        WriteLogFmt("[D3D8Tex] stbi_load(%d) failed", resID);
        return tex;
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

    tex = D3D8_CreateTextureFromRGBA(pDevice8, rgba, w, h);
    stbi_image_free(rgba);
    if (tex.pTexture8)
    {
        WriteLogFmt("[D3D8Tex] Loaded #%d: %dx%d", resID, tex.width, tex.height);
    }
    return tex;
}

static void LoadAllTextures(IDirect3DDevice9 *dev)
{
    if (g_TexturesLoaded || !dev)
        return;
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

static void ReleaseAllD3D8Textures(const char *reason)
{
    if (g_D3D8TexturesLoaded)
    {
        WriteLogFmt("[D3D8Tex] release reason=%s", reason ? reason : "unknown");
    }
    D3D8_ReleaseTexture(g_d3d8TexBtnNormal);
    D3D8_ReleaseTexture(g_d3d8TexBtnHover);
    D3D8_ReleaseTexture(g_d3d8TexBtnPressed);
    D3D8_ReleaseTexture(g_d3d8TexBtnDisabled);
    D3D8_ReleaseTexture(g_d3d8TexCursorNormal);
    D3D8_ReleaseTexture(g_d3d8TexCursorHoverA);
    D3D8_ReleaseTexture(g_d3d8TexCursorHoverB);
    D3D8_ReleaseTexture(g_d3d8TexCursorPressed);
    g_D3D8TextureDevice = nullptr;
    g_D3D8TexturesLoaded = false;
}

static void ReleaseAllD3D9Textures(const char *reason)
{
    if (g_texPanelBg)
    {
        WriteLogFmt("[Tex] release panel=%p reason=%s", g_texPanelBg, reason ? reason : "unknown");
        g_texPanelBg->Release();
        g_texPanelBg = nullptr;
    }
    if (g_texBtnNormal)
    {
        g_texBtnNormal->Release();
        g_texBtnNormal = nullptr;
    }
    if (g_texBtnHover)
    {
        g_texBtnHover->Release();
        g_texBtnHover = nullptr;
    }
    if (g_texBtnPressed)
    {
        g_texBtnPressed->Release();
        g_texBtnPressed = nullptr;
    }
    if (g_texBtnDisabled)
    {
        g_texBtnDisabled->Release();
        g_texBtnDisabled = nullptr;
    }
    if (g_texCursorNormal)
    {
        g_texCursorNormal->Release();
        g_texCursorNormal = nullptr;
    }
    if (g_texCursorHoverA)
    {
        g_texCursorHoverA->Release();
        g_texCursorHoverA = nullptr;
    }
    if (g_texCursorHoverB)
    {
        g_texCursorHoverB->Release();
        g_texCursorHoverB = nullptr;
    }
    if (g_texCursorPressed)
    {
        g_texCursorPressed->Release();
        g_texCursorPressed = nullptr;
    }
    g_TexturesLoaded = false;
}

static void EnsureD3D8SuperTexturesLoaded(void *pDevice8)
{
    if (!pDevice8)
        return;

    if (g_D3D8TexturesLoaded && g_D3D8TextureDevice == pDevice8)
        return;

    if (g_D3D8TexturesLoaded && g_D3D8TextureDevice != pDevice8)
        ReleaseAllD3D8Textures("device_changed");

    g_d3d8TexBtnNormal = LoadTextureFromResourceD3D8(pDevice8, IDR_BTN_NORMAL);
    g_d3d8TexBtnHover = LoadTextureFromResourceD3D8(pDevice8, IDR_BTN_HOVER);
    g_d3d8TexBtnPressed = LoadTextureFromResourceD3D8(pDevice8, IDR_BTN_PRESSED);
    g_d3d8TexBtnDisabled = LoadTextureFromResourceD3D8(pDevice8, IDR_BTN_DISABLED);
    g_d3d8TexCursorNormal = LoadTextureFromResourceD3D8(pDevice8, IDR_CURSOR_NORMAL);
    g_d3d8TexCursorHoverA = LoadTextureFromResourceD3D8(pDevice8, IDR_CURSOR_HOVER_A);
    g_d3d8TexCursorHoverB = LoadTextureFromResourceD3D8(pDevice8, IDR_CURSOR_HOVER_B);
    g_d3d8TexCursorPressed = LoadTextureFromResourceD3D8(pDevice8, IDR_CURSOR_PRESSED);
    g_D3D8TextureDevice = pDevice8;
    g_D3D8TexturesLoaded =
        g_d3d8TexBtnNormal.pTexture8 &&
        g_d3d8TexBtnHover.pTexture8 &&
        g_d3d8TexBtnPressed.pTexture8 &&
        g_d3d8TexCursorNormal.pTexture8;

    WriteLogFmt("[D3D8Tex] loaded=%d btn=[0x%08X,0x%08X,0x%08X,0x%08X] cursor=[0x%08X,0x%08X,0x%08X,0x%08X]",
                g_D3D8TexturesLoaded ? 1 : 0,
                (DWORD)(uintptr_t)g_d3d8TexBtnNormal.pTexture8,
                (DWORD)(uintptr_t)g_d3d8TexBtnHover.pTexture8,
                (DWORD)(uintptr_t)g_d3d8TexBtnPressed.pTexture8,
                (DWORD)(uintptr_t)g_d3d8TexBtnDisabled.pTexture8,
                (DWORD)(uintptr_t)g_d3d8TexCursorNormal.pTexture8,
                (DWORD)(uintptr_t)g_d3d8TexCursorHoverA.pTexture8,
                (DWORD)(uintptr_t)g_d3d8TexCursorHoverB.pTexture8,
                (DWORD)(uintptr_t)g_d3d8TexCursorPressed.pTexture8);
}

static void PrepareForD3DDeviceReset(const char *reason)
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

static void __fastcall SuperCWndDraw(uintptr_t thisPtr, void * /*edx_unused*/, int *clipRegion)
{
    if (!thisPtr)
        return;

    int drawX = g_PanelDrawX;
    int drawY = g_PanelDrawY;
    if (drawX <= -9000 || drawY <= -9000)
    {
        drawX = CWnd_GetX(thisPtr);
        drawY = CWnd_GetY(thisPtr);
    }

    if (!g_SuperExpanded)
        return;

    if (g_DrawCallCount < 20)
    {
        int cx = CWnd_GetX(thisPtr);
        int cy = CWnd_GetY(thisPtr);
        int rx = CWnd_GetRenderX(thisPtr);
        int ry = CWnd_GetRenderY(thisPtr);
        int w = *(int *)(thisPtr + 10 * 4);
        int h = *(int *)(thisPtr + 11 * 4);
        if (IsOfficialSecondChildObject(thisPtr, true, true))
        {
            int refCount = *(int *)(thisPtr + CWND_OFF_REFCNT * 4);
            WriteLogFmt("[Draw] #%d officialSecond=1 ref=%d com=(%d,%d) render=(%d,%d) size=%dx%d",
                        g_DrawCallCount, refCount, cx, cy, rx, ry, w, h);
        }
        else
        {
            int hx = CWnd_GetHomeX(thisPtr);
            int hy = CWnd_GetHomeY(thisPtr);
            WriteLogFmt("[Draw] #%d officialSecond=0 home=(%d,%d) com=(%d,%d) render=(%d,%d) size=%dx%d",
                        g_DrawCallCount, hx, hy, cx, cy, rx, ry, w, h);
        }
        g_DrawCallCount++;
    }

    DWORD *surface = nullptr;
    __try
    {
        ((tGetSurface)ADDR_435A50)(thisPtr, &surface);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        static int s_surfaceExceptLog = 0;
        if (s_surfaceExceptLog < 12)
        {
            WriteLogFmt("[SuperWndDraw] EXCEPTION: sub_435A50 0x%08X", GetExceptionCode());
            s_surfaceExceptLog++;
        }
        return;
    }

    if (!surface)
    {
        static int s_surfaceNullLog = 0;
        if (s_surfaceNullLog < 12)
        {
            WriteLog("[SuperWndDraw] FAIL: surface null");
            s_surfaceNullLog++;
        }
        return;
    }

    static int s_superNativeDrawLogCount = 0;
    bool emitLog = (s_superNativeDrawLogCount < 40);
    if (DrawNativePanelOnSurface(surface, 0, 0, "SuperWndDraw", emitLog))
    {
        if (emitLog)
            s_superNativeDrawLogCount++;
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
    if (!skillWndThis)
        return false;

    for (int i = 0; i < 5; ++i)
    {
        InterlockedExchange(&g_SuperBtnMetricOverrideX[i], LONG_MIN);
        InterlockedExchange(&g_SuperBtnMetricOverrideY[i], LONG_MIN);
    }

    // 控件容器 = SkillWndEx + 0xBEC（lea，不是解引用）
    uintptr_t ctrlContainer = skillWndThis + 0xBEC;

    // 安全检查：控件容器的前3个DWORD应该已被 sub_6688B0 初始化
    if (SafeIsBadReadPtr((void *)ctrlContainer, 12))
    {
        WriteLog("[NativeBtn] FAIL: ctrl container unreadable");
        return false;
    }
    // 检查 ctrlContainer[0] 应该是父窗口指针（即 SkillWndThis 本身）
    DWORD parentPtr = *(DWORD *)ctrlContainer;
    if (parentPtr != (DWORD)skillWndThis)
    {
        WriteLogFmt("[NativeBtn] WARNING: container[0]=%08X != this=%08X, continue anyway",
                    parentPtr, (DWORD)skillWndThis);
    }

    DWORD createdObj = 0;
    const unsigned short *usedPath = reinterpret_cast<const unsigned short *>(SUPER_BTN_RES_PATH);
    bool createCallOk = CreateNativeButtonInstance(
        skillWndThis,
        usedPath,
        SUPER_BTN_ID,
        BTN_X_OFFSET,
        BTN_Y_OFFSET,
        false,
        &createdObj);

    if (createCallOk)
    {
        WriteLogFmt("[NativeBtn] primary create OK path=%S obj=0x%08X",
                    reinterpret_cast<const wchar_t *>(usedPath),
                    createdObj);
    }
    else
    {
        usedPath = reinterpret_cast<const unsigned short *>(SUPER_BTN_RES_PATH_ALT);
        createCallOk = CreateNativeButtonInstance(
            skillWndThis,
            usedPath,
            SUPER_BTN_ID,
            BTN_X_OFFSET,
            BTN_Y_OFFSET,
            false,
            &createdObj);
        if (createCallOk)
        {
            WriteLogFmt("[NativeBtn] primary alt create OK path=%S obj=0x%08X",
                        reinterpret_cast<const wchar_t *>(usedPath),
                        createdObj);
        }
    }

    if (!createCallOk)
    {
        usedPath = reinterpret_cast<const unsigned short *>(ADDR_OFF_SkillEx_BtMacro);
        createCallOk = CreateNativeButtonInstance(
            skillWndThis,
            usedPath,
            SUPER_BTN_ID,
            BTN_X_OFFSET,
            BTN_Y_OFFSET,
            true,
            &createdObj);
        if (!createCallOk)
        {
            WriteLogFmt("[NativeBtn] create returned null obj path=%S",
                        reinterpret_cast<const wchar_t *>(usedPath));
            return false;
        }
        WriteLogFmt("[NativeBtn] fallback BtMacro create OK path=%S obj=0x%08X",
                    reinterpret_cast<const wchar_t *>(usedPath),
                    createdObj);
    }

    g_SuperBtnObj = createdObj;
    if (!g_SuperBtnObj)
    {
        WriteLog("[NativeBtn] FAIL: resultBuf[1] is NULL");
        return false;
    }

    WriteLogFmt("[NativeBtn] OK: obj=0x%08X", (DWORD)g_SuperBtnObj);
    WriteLogFmt("[NativeBtn] basePath=%S", reinterpret_cast<const wchar_t *>(usedPath));
    LogNativeButtonCoreFields(g_SuperBtnObj, "BtnCoreCreate");

    __try
    {
        ((tRefreshButtonState)ADDR_5095A0)(reinterpret_cast<DWORD *>(g_SuperBtnObj), nullptr);
        WriteLogFmt("[NativeBtn] state refresh OK obj=0x%08X", (DWORD)g_SuperBtnObj);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        WriteLogFmt("[NativeBtn] state refresh EXCEPTION obj=0x%08X code=0x%08X",
                    (DWORD)g_SuperBtnObj, GetExceptionCode());
    }

    if (ENABLE_SUPERBTN_D3D_BUTTON_MODE)
    {
        MoveNativeButtonRaw(g_SuperBtnObj, -4096, -4096, "BtnHideD3D");
        ResetSuperBtnD3DInteractionState();
    }
    else
    {
        MoveNativeButtonRaw(g_SuperBtnObj, BTN_X_OFFSET, BTN_Y_OFFSET, "BtnMoveCreate");
    }
    SeedSuperBtnMetricOverridesIfEmpty(BTN_METRIC_FALLBACK_X, BTN_METRIC_FALLBACK_Y, "BtnMetricSeedCreate");
    if (ENABLE_SUPERBTN_SELF_DRAWOBJ_PATCH)
    {
        PatchSuperBtnOwnDrawObjectsFromResources(g_SuperBtnObj, "create");
    }
    if (ENABLE_SUPERBTN_RUNTIME_WRAPPER_PATCH)
    {
        PatchSuperBtnCurrentWrapperFromResources(g_SuperBtnObj, "create");
    }

    if (!ENABLE_SUPERBTN_D3D_BUTTON_MODE && (ENABLE_SUPERBTN_STATE_DRAWOBJ_OVERRIDE || ENABLE_SUPERBTN_DRAWOBJ_AB_FALLBACK || ENABLE_SUPERBTN_NATIVE_DONOR_DRAWOBJ) &&
        (usedPath == reinterpret_cast<const unsigned short *>(SUPER_BTN_RES_PATH) ||
         usedPath == reinterpret_cast<const unsigned short *>(SUPER_BTN_RES_PATH_ALT)))
    {
        DWORD compareObj = 0;
        if (CreateNativeButtonInstance(
                skillWndThis,
                reinterpret_cast<const unsigned short *>(ADDR_OFF_SkillEx_BtMacro),
                SUPER_BTN_ID + 1,
                BTN_X_OFFSET,
                BTN_Y_OFFSET,
                false,
                &compareObj) &&
            compareObj)
        {
            g_SuperBtnSkinDonorObj = compareObj;
            for (int i = 0; i < 5; ++i)
            {
                g_SuperBtnStateDonorObj[i] = 0;
                g_SuperBtnStateDonorPatched[i] = false;
            }
            DWORD compareState = 0;
            if (!SafeIsBadReadPtr((void *)(compareObj + 0x34), 4))
                compareState = *(DWORD *)(compareObj + 0x34);
            if (compareState < 5 && oButtonMetric507DF0 && oButtonMetric507ED0)
            {
                int mx = 0;
                int my = 0;
                __try
                {
                    mx = oButtonMetric507DF0(compareObj);
                    my = oButtonMetric507ED0(compareObj);
                    if (IsReasonableButtonMetric(mx) && IsReasonableButtonMetric(my))
                    {
                        InterlockedExchange(&g_SuperBtnMetricOverrideX[compareState], mx);
                        InterlockedExchange(&g_SuperBtnMetricOverrideY[compareState], my);
                        WriteLogFmt("[BtnMetricPrime] state=%u x=%d y=%d", compareState, mx, my);
                    }
                    else
                    {
                        WriteLogFmt("[BtnMetricPrimeSkip] state=%u x=%d y=%d", compareState, mx, my);
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    WriteLogFmt("[BtnMetricPrime] EXCEPTION state=%u code=0x%08X", compareState, GetExceptionCode());
                }
            }
            g_SuperBtnCompareObj = compareObj;
            if (ENABLE_DEBUG_VISIBLE_COMPARE_BUTTON)
            {
                MoveNativeButtonRaw(compareObj, BTN_X_OFFSET + BTN_COMPARE_DEBUG_DX, BTN_Y_OFFSET, "BtnCompareDebug");
            }
            else
            {
                MoveNativeButtonRaw(compareObj, -4096, -4096, "BtnCompareHide");
                if (!ENABLE_SUPERBTN_NATIVE_DONOR_DRAWOBJ)
                {
                    // compare按钮只用于取一次原版metric，隐藏后不再参与跟踪，避免离屏值持续污染缓存。
                    g_SuperBtnSkinDonorObj = 0;
                }
            }
            LogNativeButtonCoreFields(compareObj, "BtnCoreCompareBtMacro");
            if (ENABLE_SUPERBTN_NATIVE_DONOR_DRAWOBJ)
            {
                for (DWORD donorState = 0; donorState <= 4; ++donorState)
                {
                    DWORD donorObj = 0;
                    if (CreateNativeButtonInstance(
                            skillWndThis,
                            reinterpret_cast<const unsigned short *>(ADDR_OFF_SkillEx_BtMacro),
                            SUPER_BTN_ID + 1 + donorState,
                            BTN_X_OFFSET,
                            BTN_Y_OFFSET,
                            false,
                            &donorObj) &&
                        donorObj)
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
                if (!g_SuperBtnStateDonorObj[0])
                {
                    g_SuperBtnStateDonorObj[0] = compareObj;
                    WriteLogFmt("[BtnDonorCreate] state=0 FALLBACK compare=0x%08X", compareObj);
                }
                PatchSuperBtnDonorDrawObjectsFromResources();
                // v17.6: 把所有 5 个 slot 钉成 normal donor，然后启用 stableNormal 模式。
                // 这样 hkButtonRefreshState5095A0 会拦截后续所有状态刷新（包括 hover），
                // 按钮保持 state=0，draw 链不会被破坏。
                if (g_SuperBtnObj)
                {
                    ForceSuperButtonAllStatesToNormalDonor(g_SuperBtnObj);
                }
                // v17.7b: slot 值已经复制到 SuperBtn，现在把 donor/compare 宽高清零
                // 防止 sub_529640 在 UI tree 遍历时为 donor 生成 -4096 矩形覆盖 SuperBtn
                for (int di = 0; di < 5; ++di)
                {
                    uintptr_t dObj = g_SuperBtnStateDonorObj[di];
                    if (dObj)
                    {
                        __try
                        {
                            if (!SafeIsBadReadPtr((void *)(dObj + 0x1C), 8))
                            {
                                *(DWORD *)(dObj + 0x1C) = 0;
                                *(DWORD *)(dObj + 0x20) = 0;
                            }
                        }
                        __except (EXCEPTION_EXECUTE_HANDLER)
                        {
                        }
                    }
                }
                if (g_SuperBtnCompareObj)
                {
                    __try
                    {
                        uintptr_t cObj = g_SuperBtnCompareObj;
                        if (!SafeIsBadReadPtr((void *)(cObj + 0x1C), 8))
                        {
                            *(DWORD *)(cObj + 0x1C) = 0;
                            *(DWORD *)(cObj + 0x20) = 0;
                        }
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                    }
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
    if (!skillWndThis || SafeIsBadReadPtr((void *)(skillWndThis + 3048), 4))
        return 0;
    return *(DWORD *)(skillWndThis + 3048);
}

static bool ReleaseSkillWndSecondChild(uintptr_t skillWndThis, const char *reason)
{
    if (!skillWndThis)
        return false;

    uintptr_t child = GetSkillWndSecondChildPtr(skillWndThis);
    if (!child)
        return false;

    RestoreSuperChildCustomMouseGuardVTable(child, reason ? reason : "release");
    LogOfficialSecondChildState(child, "Lifecycle:BeforeSecondChildRelease");

    WriteLogFmt("[Lifecycle] releasing second child (reason=%s) ptr=0x%08X",
                reason ? reason : "unknown", (DWORD)child);

    DWORD fnClose = ADDR_B9E880;
    __try
    {
        __asm {
            mov ecx, [child]
            call [fnClose]
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        WriteLogFmt("[Lifecycle] EXCEPTION in second-child close: 0x%08X", GetExceptionCode());
    }

    uintptr_t childAfterClose = GetSkillWndSecondChildPtr(skillWndThis);
    if (childAfterClose)
    {
        DWORD fnRelease = ADDR_9D93A0;
        uintptr_t wrapPtr = skillWndThis + 3044;
        int zero = 0;
        __try
        {
            __asm {
                push [zero]
                mov ecx, [wrapPtr]
                call [fnRelease]
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            WriteLogFmt("[Lifecycle] EXCEPTION in second-child release: 0x%08X", GetExceptionCode());
        }
        if (!SafeIsBadReadPtr((void *)(skillWndThis + 3048), 4))
        {
            *(DWORD *)(skillWndThis + 3048) = 0;
        }
    }

    return true;
}

static bool CreateSuperWnd(uintptr_t skillWndThis)
{
    if (!skillWndThis)
        return false;

    if (ENABLE_IMGUI_OVERLAY_PANEL)
    {
        if (!g_GameHwnd || !g_pDevice)
        {
            WriteLogFmt("[ImGuiOverlay] FAIL: hwnd/device not ready (hwnd=0x%08X device=0x%08X)", (DWORD)(uintptr_t)g_GameHwnd, (DWORD)(uintptr_t)g_pDevice);
            return false;
        }

        if (!SuperImGuiOverlayEnsureInitialized(g_GameHwnd, g_pDevice, 1.0f, IMGUI_PANEL_ASSET_PATH))
        {
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

    if (!g_SuperChildHooksReady)
    {
        WriteLog("[NativeWnd] FAIL: route-B child hooks not ready");
        return false;
    }

    uintptr_t existingSecond = GetSkillWndSecondChildPtr(skillWndThis);
    if (existingSecond)
    {
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
    __try
    {
        __asm {
            push [mode]
            mov ecx, [skillWndThis]
            call [fnCreateSlot]
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        WriteLogFmt("[NativeWnd] EXCEPTION in second-child wrapper 0x%08X", GetExceptionCode());
        return false;
    }

    uintptr_t wndObj = GetSkillWndSecondChildPtr(skillWndThis);
    if (!wndObj)
    {
        WriteLog("[NativeWnd] FAIL: second-child wrapper returned null slot");
        return false;
    }

    if (SafeIsBadReadPtr((void *)wndObj, 0x84))
    {
        WriteLog("[NativeWnd] FAIL: second-child slot object unreadable");
        return false;
    }

    WriteLogFmt("[NativeWnd] official second-child OK: slot=0x%08X mode=%d", (DWORD)wndObj, mode);
    LogNativeChildSurfaceShape(wndObj, "NativeWndCtor");
    LogOfficialSecondChildState(wndObj, "NativeWndCtorState");

    // Step 2: 算初始锚点，并尝试把官方 second-child 的壳收缩到我们的目标尺寸。
    int swX = 0, swY = 0;
    bool fromVTable = GetSkillWndAnchorPos(skillWndThis, &swX, &swY);
    if (!fromVTable)
    {
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
    if (!ApplySuperChildCustomDrawVTable(wndObj))
    {
        WriteLog("[NativeWnd] FAIL: custom draw vtable install failed");
        ReleaseSkillWndSecondChild(skillWndThis, "custom_vt_fail");
        return false;
    }
    if (!ApplySuperChildCustomMouseGuardVTable(wndObj))
    {
        WriteLog("[NativeWnd] WARN: custom VT2 mouse guard install failed");
    }

    // Step 5: 再补一次 move，确保初始化后逻辑位置与我们的锚点一致
    MoveNativeChildWnd(wndObj, xPos, yPos, "NativeWndInitMove");

    // Step 6: official second-child 没有可靠 show/hide 槽位；收起时走 close+release。
    MarkSuperWndDirty(wndObj, "NativeWndInit");
    LogOfficialSecondChildState(wndObj, "NativeWndAfterInit");

    g_SuperCWnd = wndObj;
    g_NativeWndCreated = true;
    g_SuperUsesSkillWndSecondSlot = true;
    WriteLogFmt("[NativeWnd] === SUCCESS(second_child_slot): 0x%08X ===", (DWORD)wndObj);
    return true;
}

static void SetSuperWndVisible(uintptr_t wndObj, int showVal)
{
    if (ENABLE_IMGUI_OVERLAY_PANEL)
    {
        SuperImGuiOverlaySetVisible(showVal != 0);
        return;
    }

    if (!wndObj)
        return;

    if (IsOfficialSecondChildObject(wndObj, true, true))
    {
        static int s_officialSecondVisibleNoopLogCount = 0;
        if (s_officialSecondVisibleNoopLogCount < 8)
        {
            WriteLogFmt("[Visible] official second-child has no reliable show/hide slot; no-op show=%d wnd=0x%08X",
                        showVal, (DWORD)wndObj);
            s_officialSecondVisibleNoopLogCount++;
        }
        return;
    }

    uintptr_t thisForVT2 = wndObj + 4; // 与sub_9E9B50一致
    if (SafeIsBadReadPtr((void *)thisForVT2, 4))
        return;

    DWORD vtable2 = *(DWORD *)thisForVT2;
    if (!vtable2 || SafeIsBadReadPtr((void *)(vtable2 + 0x28), 4))
        return;

    DWORD fnShow = *(DWORD *)(vtable2 + 0x28);
    DWORD fnVis = *(DWORD *)(vtable2 + 0x20);
    DWORD ecxVal = (DWORD)thisForVT2;

    if (!fnShow || !fnVis)
        return;

    __try
    {
        __asm {
            push [showVal]
            mov ecx, [ecxVal]
            call [fnShow]
        }
        __asm
        {
            push [showVal]
            mov ecx, [ecxVal]
            call [fnVis]
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        WriteLogFmt("[Visible] EXCEPTION: 0x%08X", GetExceptionCode());
    }
}

static void SafeCloseSuperWnd(const char *reason)
{
    if (ENABLE_IMGUI_OVERLAY_PANEL)
    {
        WriteLogFmt("[Lifecycle] hiding imgui overlay (reason=%s)", reason ? reason : "unknown");
        SuperImGuiOverlaySetVisible(false);
        return;
    }

    if (!g_SuperCWnd)
        return;

    uintptr_t oldWnd = g_SuperCWnd;
    WriteLogFmt("[Lifecycle] closing super wnd (reason=%s) ptr=0x%08X",
                reason ? reason : "unknown", (DWORD)oldWnd);

    SetSuperWndVisible(oldWnd, 0);

    if (g_SuperUsesSkillWndSecondSlot && g_SkillWndThis)
    {
        uintptr_t slotChild = GetSkillWndSecondChildPtr(g_SkillWndThis);
        if (slotChild == oldWnd)
        {
            ReleaseSkillWndSecondChild(g_SkillWndThis, reason ? reason : "unknown");
            return;
        }
    }

    if (!SafeIsBadReadPtr((void *)oldWnd, 4))
    {
        DWORD fnClose = ADDR_B9E880;
        __try
        {
            __asm {
                mov ecx, [oldWnd]
                call [fnClose]
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            WriteLogFmt("[Lifecycle] EXCEPTION in close: 0x%08X", GetExceptionCode());
        }
    }
}

static void DestroySuperWndOnly(const char *reason)
{
    if (ENABLE_IMGUI_OVERLAY_PANEL)
    {
        SetSuperWndVisible(g_SuperCWnd, 0);
        g_PanelDrawX = -9999;
        g_PanelDrawY = -9999;
        return;
    }

    if (!g_SuperCWnd)
        return;
    SafeCloseSuperWnd(reason);
    g_SuperCWnd = 0;
    g_NativeWndCreated = false;
    g_SuperUsesSkillWndSecondSlot = false;
    g_PanelDrawX = -9999;
    g_PanelDrawY = -9999;
}

static void ResetSuperRuntimeState(bool closeWnd, const char *reason)
{
    if (ENABLE_IMGUI_OVERLAY_PANEL)
    {
        if (g_IsD3D8Mode)
        {
            SuperD3D8OverlaySetVisible(false);
            SuperD3D8OverlayResetPanelState();
        }
        else
        {
            SuperImGuiOverlaySetVisible(false);
            SuperImGuiOverlayResetPanelState();
        }
    }

    if (closeWnd && g_SuperCWnd)
    {
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
    for (int i = 0; i < 5; ++i)
    {
        g_SuperBtnSelfStatePatched[i] = false;
    }
    for (int i = 0; i < 5; ++i)
    {
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

static void OnSkillWndPointerObserved(uintptr_t observed, const char *srcTag)
{
    DWORD now = GetTickCount();
    if (observed && SafeIsBadReadPtr((void *)observed, 0x20))
    {
        observed = 0;
    }

    if (observed == g_SkillWndThis)
    {
        if (observed)
            g_LastSkillWndSeenTick = now;
        return;
    }

    if (!observed)
    {
        if (g_SkillWndThis && g_LastSkillWndSeenTick && (now - g_LastSkillWndSeenTick) < SKILLWND_GONE_DEBOUNCE_MS)
        {
            return;
        }
        if (g_SkillWndThis)
        {
            WriteLogFmt("[Lifecycle] SkillWnd gone (src=%s old=0x%08X)",
                        srcTag ? srcTag : "unknown", (DWORD)g_SkillWndThis);
            ResetSuperRuntimeState(g_SuperCWnd != 0, "skillwnd_gone");
        }
        g_SkillWndThis = 0;
        SkillOverlayBridgeSetSkillWnd(0);
        g_Ready = false;
        return;
    }

    if (g_SkillWndThis && g_SkillWndThis != observed)
    {
        WriteLogFmt("[Lifecycle] SkillWnd switched (src=%s old=0x%08X new=0x%08X)",
                    srcTag ? srcTag : "unknown", (DWORD)g_SkillWndThis, (DWORD)observed);
        ResetSuperRuntimeState(g_SuperCWnd != 0, "skillwnd_switched");
    }

    g_SkillWndThis = observed;
    SkillOverlayBridgeSetSkillWnd(g_SkillWndThis);
    g_Ready = true;
    g_LastSkillWndSeenTick = now;

    if (g_IsD3D8Mode && ENABLE_IMGUI_OVERLAY_PANEL)
        EnsureDeferredInteractionHooks("skillwnd_ready");
}

// ============================================================================
// 切换超级技能栏窗口显示/隐藏
// 复刻 sub_9E9B50 的逻辑
// ============================================================================
static void ToggleSuperWnd(const char *srcTag)
{
    if (!g_SkillWndThis)
        return;

    DWORD now = GetTickCount();
    if (now - g_LastToggleTick < 120)
    {
        return;
    }
    g_LastToggleTick = now;

    g_SuperExpanded = !g_SuperExpanded;
    WriteLogFmt("[Toggle:%s] expanded=%d", srcTag ? srcTag : "unknown", g_SuperExpanded);

    if (ENABLE_IMGUI_OVERLAY_PANEL)
    {
        if (g_IsD3D8Mode)
            SuperD3D8OverlaySetPanelExpanded(g_SuperExpanded);
        else
            SuperImGuiOverlaySetPanelExpanded(g_SuperExpanded);
    }

    // D3D8 mode owns the shared ImGui panel from hkD3D8Present.
    // It does not need the D3D9 CreateSuperWnd route or a native child window.
    if (g_IsD3D8Mode && ENABLE_IMGUI_OVERLAY_PANEL)
    {
        WriteLogFmt("[Toggle] D3D8 mode: panel %s", g_SuperExpanded ? "ON" : "OFF");
        return;
    }

    if (g_SuperExpanded && !g_NativeWndCreated)
    {
        WriteLog(ENABLE_IMGUI_OVERLAY_PANEL ? "[Toggle] creating imgui overlay panel..." : "[Toggle] creating official second-slot super child...");
        if (CreateSuperWnd(g_SkillWndThis))
        {
            WriteLog(ENABLE_IMGUI_OVERLAY_PANEL ? "[Toggle] imgui overlay panel ready" : "[Toggle] official second-slot super child created OK");
        }
        else
        {
            WriteLog(ENABLE_IMGUI_OVERLAY_PANEL ? "[Toggle] imgui overlay panel create FAILED" : "[Toggle] official second-slot super child create FAILED");
            g_SuperExpanded = false;
            g_PanelDrawX = -9999;
            g_PanelDrawY = -9999;
            WriteLog("[Toggle] rollback: create failed, expanded reset to 0");
            return;
        }
    }
    if (g_SuperCWnd)
    {
        SetSuperWndVisible(g_SuperCWnd, g_SuperExpanded ? 1 : 0);
    }
    if (g_SuperExpanded)
    {
        UpdateSuperCWnd();
        if (ENABLE_TOGGLE_FOCUS_SYNC)
        {
            SyncSkillWndActiveFocus("ToggleFocusSync", true);
        }
    }
    if (!g_SuperExpanded)
    {
        DestroySuperWndOnly("toggle_hide");
    }
}

#if defined(SSW_ENABLE_SECOND_CHILD_CARRIER_PROBE_RUNTIME)
static void RunSecondChildCarrierProbeHotkey()
{
    if (!g_SkillWndThis)
    {
        WriteLog("[CarrierProbe] F10 ignored: SkillWnd not ready");
        return;
    }

    WriteLogFmt("[CarrierProbe] F10 run-once skillWnd=0x%08X flags=0x%08X",
                (DWORD)g_SkillWndThis,
                SECOND_CHILD_CARRIER_PROBE_FLAGS);
    SSW_SecondChildCarrierProbe_RunOnce((DWORD)g_SkillWndThis, SECOND_CHILD_CARRIER_PROBE_FLAGS, -9999, -9999);
}

static void PollSecondChildCarrierProbeTick(DWORD reasonCode, bool force)
{
    if (!g_SkillWndThis)
        return;

    DWORD now = GetTickCount();
    if (!force && (now - g_LastCarrierProbePollTick) < 250)
        return;

    g_LastCarrierProbePollTick = now;
    SSW_SecondChildCarrierProbe_Poll((DWORD)g_SkillWndThis, reasonCode);
}

static void ReleaseSecondChildCarrierProbeHotkey()
{
    if (!g_SkillWndThis)
    {
        WriteLog("[CarrierProbe] F12 ignored: SkillWnd not ready");
        return;
    }

    WriteLogFmt("[CarrierProbe] F12 release skillWnd=0x%08X", (DWORD)g_SkillWndThis);
    SSW_SecondChildCarrierProbe_Release((DWORD)g_SkillWndThis);
}
#endif

static bool IsPointInRectPad(int mx, int my, int x, int y, int w, int h, int pad)
{
    return (mx >= (x - pad) && mx < (x + w + pad) &&
            my >= (y - pad) && my < (y + h + pad));
}

static bool TryToggleByMousePoint(int mx, int my, const char *srcTag)
{
    if (ENABLE_SUPERBTN_D3D_BUTTON_MODE)
        return false;
    if (!g_Ready || !g_NativeBtnCreated || !g_SkillWndThis)
        return false;

    DWORD now = GetTickCount();
    if (now - g_LastNativeMsgToggleTick < 180)
    {
        return false;
    }

    int bxObj = 0, byObj = 0, bwObj = 0, bhObj = 0;
    int bxCom = 0, byCom = 0, bwCom = 0, bhCom = 0;
    int bxVt = 0, byVt = 0, bwVt = 0, bhVt = 0;
    bool hasObj = GetSuperButtonScreenRect(&bxObj, &byObj, &bwObj, &bhObj);
    bool hasCom = GetExpectedButtonRectCom(&bxCom, &byCom, &bwCom, &bhCom);
    bool hasVt = GetExpectedButtonRectVt(&bxVt, &byVt, &bwVt, &bhVt);

    // resultBuf对象上的宽高有时不可信（曾出现高度=1），这里做一次兜底过滤
    if (hasObj && (bwObj < 20 || bhObj < 10 || bwObj > 256 || bhObj > 64))
    {
        hasObj = false;
    }

    const int kPad = 0;
    bool hitObj = hasObj && IsPointInRectPad(mx, my, bxObj, byObj, bwObj, bhObj, kPad);
    bool hitCom = !hitObj && hasCom && IsPointInRectPad(mx, my, bxCom, byCom, bwCom, bhCom, kPad);
    bool hitVt = !hitObj && !hitCom && hasVt && IsPointInRectPad(mx, my, bxVt, byVt, bwVt, bhVt, kPad);
    if (!(hitObj || hitCom || hitVt))
    {
        static DWORD s_lastMissLogTick = 0;
        if (now - s_lastMissLogTick > 200)
        {
            s_lastMissLogTick = now;
            WriteLogFmt("[BtnMiss:%s] mx=%d my=%d obj=%s(%d,%d,%d,%d) com=%s(%d,%d,%d,%d) vt=%s(%d,%d,%d,%d)",
                        srcTag ? srcTag : "unknown", mx, my,
                        hasObj ? "Y" : "N", bxObj, byObj, bxObj + bwObj, byObj + bhObj,
                        hasCom ? "Y" : "N", bxCom, byCom, bxCom + bwCom, byCom + bhCom,
                        hasVt ? "Y" : "N", bxVt, byVt, bxVt + bwVt, byVt + bhVt);
        }
        return false;
    }

    if (now - g_LastFallbackHitLogTick > 120)
    {
        g_LastFallbackHitLogTick = now;
        WriteLogFmt("[BtnHit:%s] mx=%d my=%d obj=%s(%d,%d,%d,%d) com=%s(%d,%d,%d,%d) vt=%s(%d,%d,%d,%d)",
                    srcTag ? srcTag : "unknown", mx, my,
                    hitObj ? "HIT" : "no", bxObj, byObj, bxObj + bwObj, byObj + bhObj,
                    hitCom ? "HIT" : "no", bxCom, byCom, bxCom + bwCom, byCom + bhCom,
                    hitVt ? "HIT" : "no", bxVt, byVt, bxVt + bwVt, byVt + bhVt);
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
typedef void(__thiscall *tSkillWndInitChildren)(uintptr_t thisptr, int **a2);
static tSkillWndInitChildren oSkillWndInitChildren = nullptr;

static void __cdecl hkSkillWndPostInit(uintptr_t skillWndThis)
{
    OnSkillWndPointerObserved(skillWndThis, "hook_postinit");
    WriteLogFmt("[Hook] SkillWndEx captured: 0x%08X", (DWORD)g_SkillWndThis);

    // 创建原生按钮
    if (!g_NativeBtnCreated)
    {
        WriteLog("[Hook] Creating native button...");
        if (CreateSuperButton(g_SkillWndThis))
        {
            WriteLog("[Hook] Native button created OK");
        }
        else
        {
            WriteLog("[Hook] Native button creation FAILED");
        }
    }

    if (!g_NativeWndCreated)
    {
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
        mov esi, ecx // esi = SkillWndEx this

            // 调用原函数：__thiscall(ecx=this, push a2), retn 4
            // a2 在 [ebp+8] (因为我们push了ebp，原来的[esp+4]变成[ebp+8])
        mov eax, [ebp + 8] // a2
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
typedef void(__thiscall *tSkillWndMsg)(uintptr_t thisptr, int ctrlID);
static tSkillWndMsg oSkillWndMsg = nullptr;

static void __cdecl hkMsgHandler(uintptr_t thisPtr, int ctrlID)
{
    DWORD now = GetTickCount();
    if (ctrlID != g_LastMsgID || (now - g_LastMsgTick) > 400)
    {
        WriteLogFmt("[Msg] ctrlID=0x%X this=0x%08X", ctrlID, (DWORD)thisPtr);
        g_LastMsgID = ctrlID;
        g_LastMsgTick = now;
    }

    if ((DWORD)ctrlID == SUPER_BTN_ID)
    {
        WriteLogFmt("[Msg] Super button clicked (ID=0x%X)", ctrlID);
        // WndProc fallback 可能已经在同一点击里先切过一次；这里直接复用节流时间戳，避免二次翻转
        bool skipToggle = ((now - g_LastNativeMsgToggleTick) < 180) || ((now - g_LastToggleTick) < 120);
        if (skipToggle)
        {
            if (now - g_LastNativeMsgSkipTick > 200)
            {
                g_LastNativeMsgSkipTick = now;
                WriteLog("[Msg] Super button native toggle skipped (already handled by fallback)");
            }
        }
        else
        {
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
        mov eax, [esp + 4] // ctrlID
        push eax
        push ecx // this
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
typedef LONG(__thiscall *tSkillWndMove)(uintptr_t thisptr, int a2, int a3);
static tSkillWndMove oSkillWndMove = nullptr;

static LONG __cdecl hkSkillWndMoveHandler(uintptr_t thisPtr, int a2, int a3)
{
    if (thisPtr == g_SkillWndThis && g_SuperExpanded && g_SuperCWnd && g_SuperUsesSkillWndSecondSlot)
    {
        LogOfficialSecondChildState(g_SuperCWnd, "MoveHook:BeforeOrig");
    }

    LONG ret = oSkillWndMove ? oSkillWndMove(thisPtr, a2, a3) : 0;
    if (thisPtr == g_SkillWndThis && g_SuperExpanded && g_SuperCWnd)
    {
        if (g_SuperUsesSkillWndSecondSlot)
        {
            LogOfficialSecondChildState(g_SuperCWnd, "MoveHook:AfterOrig");
        }
        MoveSuperChildBySkillAnchor("MoveHookDirect", false);
        if (g_SuperUsesSkillWndSecondSlot)
        {
            LogOfficialSecondChildState(g_SuperCWnd, "MoveHook:AfterRetarget");
        }
        if (ENABLE_MOVE_FOCUS_SYNC)
        {
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
typedef int(__thiscall *tSkillWndRefresh)(uintptr_t thisptr);
static tSkillWndRefresh oSkillWndRefresh = nullptr;

static int __cdecl hkSkillWndRefreshHandler(uintptr_t thisPtr)
{
    if (thisPtr == g_SkillWndThis && g_SuperExpanded && g_SuperCWnd && g_SuperUsesSkillWndSecondSlot)
    {
        LogOfficialSecondChildState(g_SuperCWnd, "RefreshHook:BeforeOrig");
    }

    int ret = oSkillWndRefresh ? oSkillWndRefresh(thisPtr) : 0;
    if (thisPtr == g_SkillWndThis && g_SuperExpanded && g_SuperCWnd)
    {
        if (g_SuperUsesSkillWndSecondSlot)
        {
            LogOfficialSecondChildState(g_SuperCWnd, "RefreshHook:AfterOrig");
        }
        if (ENABLE_REFRESH_NATIVE_CHILD_UPDATE)
        {
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
typedef int(__thiscall *tSkillWndDraw)(uintptr_t thisptr, int clipRegion);
static tSkillWndDraw oSkillWndDraw = nullptr;

static bool DrawSuperPanelNativeBackgrnd(uintptr_t skillWndThis)
{
    if (!skillWndThis || !g_SuperExpanded)
        return false;
    if (g_SuperChildHooksReady)
        return false; // v11.1: route-B 是主路线，创建失败时 fail-closed，不再偷偷退回 fallback surface draw
    if (g_NativeWndCreated && g_SuperCWnd)
        return false;

    UpdateSuperCWnd();
    if (g_PanelDrawX <= -9000 || g_PanelDrawY <= -9000)
        return false;

    DWORD *surface = nullptr;
    __try
    {
        ((tGetSurface)ADDR_435A50)(skillWndThis, &surface);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        WriteLog("[SkillDrawNative] EXCEPTION: sub_435A50");
        return false;
    }
    if (!surface)
    {
        WriteLog("[SkillDrawNative] FAIL: surface null");
        return false;
    }

    int skillComX = 0, skillComY = 0;
    bool hasSkillCom = GetSkillWndComPos(skillWndThis, &skillComX, &skillComY);
    int localX = g_PanelDrawX;
    int localY = g_PanelDrawY;
    if (hasSkillCom)
    {
        localX = g_PanelDrawX - skillComX;
        localY = g_PanelDrawY - skillComY;
    }

    bool ok = DrawNativePanelOnSurface(surface, localX, localY, "SkillDrawNative", false);

    static int s_nativeDrawLogCount = 0;
    if (s_nativeDrawLogCount < 60)
    {
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
    if (ENABLE_IMGUI_OVERLAY_PANEL)
        return;
    if (!skillWndThis || !g_SuperExpanded)
        return;

    bool nativeOk = DrawSuperPanelNativeBackgrnd(skillWndThis);

    static int s_drawHookLogCount = 0;
    if (s_drawHookLogCount < 40)
    {
        WriteLogFmt("[SkillDraw] native=%d panel=(%d,%d) skill=0x%08X",
                    nativeOk ? 1 : 0, g_PanelDrawX, g_PanelDrawY, (DWORD)skillWndThis);
        s_drawHookLogCount++;
    }
}

static int __cdecl hkSkillWndDrawHandler(uintptr_t thisPtr, int clipRegion)
{
    int ret = 0;
    SkillOverlayBridgeFilterNativeSkillWindow(thisPtr);
    if (oSkillWndDraw)
    {
        ret = oSkillWndDraw(thisPtr, clipRegion);
    }

    OnSkillWndPointerObserved(thisPtr, "skill_draw");

    // v17.6b diag: 确认 draw handler 是否持续被调，以及 thisPtr 是否匹配
    {
        static LONG s_drawHandlerCallCount = 0;
        LONG count = InterlockedIncrement(&s_drawHandlerCallCount);
        if (ENABLE_HOTPATH_DIAGNOSTIC_LOGS && (count <= 20 || (count % 500 == 0)))
        {
            WriteLogFmt("[DrawHandler] #%d this=0x%08X g_this=0x%08X match=%d btn=0x%08X created=%d",
                        (int)count, (DWORD)thisPtr, (DWORD)g_SkillWndThis,
                        thisPtr == g_SkillWndThis ? 1 : 0,
                        (DWORD)g_SuperBtnObj, g_NativeBtnCreated ? 1 : 0);
        }
    }

    if (thisPtr == g_SkillWndThis)
    {
        if (g_NativeBtnCreated && g_SuperBtnObj && !ENABLE_SUPERBTN_D3D_BUTTON_MODE)
        {
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
static void *oSkillListBuildContinue = nullptr; // trampoline (原 7 字节: mov eax,[esp+20h]; mov esi,[ebx+8])

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
        (BYTE *)ADDR_7DD67D, (void *)hkSkillListBuildFilterNaked, 7);
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
typedef int(__thiscall *tSkillWndDtor)(uintptr_t thisptr);
static tSkillWndDtor oSkillWndDtor = nullptr;

static int __cdecl hkSkillWndDtorHandler(uintptr_t thisPtr)
{
    if (thisPtr && thisPtr == g_SkillWndThis)
    {
        WriteLogFmt("[Lifecycle] SkillWnd dtor: this=0x%08X", (DWORD)thisPtr);
        ResetSuperRuntimeState(g_SuperCWnd != 0, "skillwnd_dtor");
        g_SkillWndThis = 0;
        g_Ready = false;
    }

    if (oSkillWndDtor)
    {
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
static void *oSendPacket = nullptr;
static DWORD g_SendPacketOriginalCallTarget = 0;
static void *oRecvPacket = nullptr;
static void *oExternalPotentialWritePatch = nullptr;
static void *oExternalPotentialClearPatch = nullptr;
static uintptr_t g_ExternalPotentialIncreaseAddressRuntime = 0;
static DWORD g_ExternalPotentialWriteContinue = 0;
static DWORD g_ExternalPotentialWriteLoopTarget = 0;
static DWORD g_ExternalPotentialClearContinue = 0;
static void *oLocalIndependentPotentialPrimaryFlatStats = nullptr;
static DWORD g_LocalIndependentPotentialPrimaryContinueNonZero = 0;
static DWORD g_LocalIndependentPotentialPrimaryContinueZero = 0;
static void *oLocalIndependentPotentialPrimaryPercentStats = nullptr;
static DWORD g_LocalIndependentPotentialPrimaryPercentContinueNonZero = 0;
static DWORD g_LocalIndependentPotentialPrimaryPercentContinueZero = 0;
static void *oLocalIndependentPotentialFlatStats = nullptr;
static uintptr_t g_LocalIndependentPotentialPreparedPtr = 0;
static DWORD g_LocalIndependentPotentialContinueNonZero = 0;
static DWORD g_LocalIndependentPotentialContinueZero = 0;
static void *oLocalIndependentPotentialSkillLevelDisplay = nullptr;
static void *oLocalIndependentPotentialDamageDisplay = nullptr;
static void *oAbilityRedHashLookupHook = nullptr;
static void *oAbilityRedHashInsertHook = nullptr;
typedef int (__fastcall *tAbilityRedDisplayCandidateFn)(void *thisPtr, void *edxUnused, DWORD arg1, DWORD arg2, DWORD arg3, DWORD arg4, DWORD arg5, DWORD arg6, DWORD arg7);
static tAbilityRedDisplayCandidateFn oAbilityRedDisplayCandidateFn = nullptr;
typedef int (__fastcall *tAbilityRedExtendedAggregateFn)(void *thisPtr, void *edxUnused, DWORD arg1, DWORD arg2, DWORD arg3);
static tAbilityRedExtendedAggregateFn oAbilityRedExtendedAggregateFn = nullptr;
typedef int (__fastcall *tAbilityRedMasterAggregateFn)(void *thisPtr, void *edxUnused, DWORD arg1, DWORD arg2, DWORD arg3, DWORD arg4, DWORD arg5, DWORD arg6, DWORD arg7);
static tAbilityRedMasterAggregateFn oAbilityRedMasterAggregateFn = nullptr;
typedef int (__cdecl *tAbilityRedMovementSpeedSourceFn)(int playerObj);
typedef int (__cdecl *tAbilityRedMovementSpeedCapBaseFn)(int playerObj);
typedef int (__thiscall *tAbilityRedMovementCapOverrideFn)(void *capObj);
typedef int (__thiscall *tAbilityRedMovementValueFn)(void *thisPtr);
typedef int (__fastcall *tAbilityRedMovementSetterFn)(void *thisPtr, int value);
typedef int (__thiscall *tMovementSpeedTransformFn)(void *thisPtr, int value);
typedef LONG (__cdecl *tMovementOutputClampComputeFn)(
    DWORD *a1,
    int a2,
    int a3,
    int a4,
    int a5,
    int a6,
    int a7,
    int *a8,
    int *a9,
    double *a10,
    DWORD *a11);
static tAbilityRedMovementSetterFn oAbilityRedMovementSpeedSetter831F00Fn = nullptr;
static tAbilityRedMovementSetterFn oAbilityRedMovementJumpSetter832000Fn = nullptr;
static tMovementOutputClampComputeFn oMovementOutputClampComputeB93B80Fn = nullptr;
typedef int (__fastcall *tAbilityRedSiblingCalcFn)(void *thisPtr, void *edxUnused);
static tAbilityRedSiblingCalcFn oAbilityRedSiblingCalc82F780Fn = nullptr;
static tAbilityRedSiblingCalcFn oAbilityRedSiblingCalc82F870Fn = nullptr;
static tAbilityRedSiblingCalcFn oAbilityRedSiblingCalc82F960Fn = nullptr;
static tAbilityRedSiblingCalcFn oAbilityRedSiblingCalc82FA50Fn = nullptr;
typedef int (__fastcall *tAbilityRedFinalCalc6Fn)(void *thisPtr, void *edxUnused, DWORD arg1, DWORD arg2, DWORD arg3, DWORD arg4, DWORD arg5, DWORD arg6);
typedef int (__fastcall *tAbilityRedFinalCalc5Fn)(void *thisPtr, void *edxUnused, DWORD arg1, DWORD arg2, DWORD arg3, DWORD arg4, DWORD arg5);
static tAbilityRedFinalCalc6Fn oAbilityRedFinalCalc84C470Fn = nullptr;
static tAbilityRedFinalCalc5Fn oAbilityRedFinalCalc84CA90Fn = nullptr;
static tAbilityRedFinalCalc5Fn oAbilityRedFinalCalc84CBD0Fn = nullptr;
static DWORD g_AbilityRedDisplayCallsiteOriginalTarget = 0;
static DWORD g_AbilityRedDiff84C470PreSubContinue = 0;
static DWORD g_AbilityRedDiff84BE40PreSubContinue = 0;
static DWORD g_AbilityRedDiff84CA90AccPreSubContinue = 0;
static DWORD g_AbilityRedDiff84CA90MagicAccPreSubContinue = 0;
static DWORD g_AbilityRedDiff84CBD0AvoidPreSubContinue = 0;
static DWORD g_AbilityRedDiff84CBD0MagicAvoidPreSubContinue = 0;
static DWORD g_AbilityRedAttackRangeStyleContinue = 0;
static DWORD g_AbilityRedCriticalRateStyleContinue = 0;
static DWORD g_AbilityRedSpeedStyleContinue = 0;
static DWORD g_AbilityRedJumpStyleContinue = 0;
static void *oAbilityRedBake857BB6Hook = nullptr;
static void *oAbilityRedBake857C29Hook = nullptr;
static void *oAbilityRedBake857C9CHook = nullptr;
static void *oAbilityRedBake857D0FHook = nullptr;
static void *oAbilityRedBake1988569C3Hook = nullptr;
static void *oAbilityRedBake198856D57Hook = nullptr;
static void *oAbilityRedBake19885725FHook = nullptr;
static void *oAbilityRedBake198857C3BHook = nullptr;
static void *oAbilityRedBake198858AEDHook = nullptr;
static void *oAbilityRedBake198831A50Hook = nullptr;
static void *oAbilityRedBake19883AF02Hook = nullptr;
static void *oAbilityRedLevelReadHook = nullptr;
static void *oAbilityRedSkillWrite52FE14Hook = nullptr;
static void *oAbilityRedSkillWrite6226CEHook = nullptr;
static void *oAbilityRedSkillWrite49CA01Hook = nullptr;
typedef int (__stdcall *tLocalIndependentPotentialSkillLevelDisplayFn)(int a1, int a2, DWORD *a3);
typedef LONG (__cdecl *tLocalIndependentPotentialPercentQuadDisplayFn)(int a1, int a2, DWORD *a3, DWORD *a4, DWORD *a5, DWORD *a6);
typedef LONG (__thiscall *tLocalIndependentPotentialPercentFullDisplayFn)(DWORD *thisPtr, int pExceptionObject, int a3, DWORD *a4);
typedef LONG (__thiscall *tLocalIndependentPotentialFlatBasicDisplayFn)(DWORD *thisPtr, int pExceptionObject, int a3);
typedef LONG (__thiscall *tLocalIndependentPotentialFlatExtendedDisplayFn)(DWORD *thisPtr, int pExceptionObject, int a3);
static tLocalIndependentPotentialSkillLevelDisplayFn oLocalIndependentPotentialSkillLevelDisplayFn = nullptr;
static tLocalIndependentPotentialPercentQuadDisplayFn oLocalIndependentPotentialPercentQuadDisplayFn = nullptr;
static tLocalIndependentPotentialPercentFullDisplayFn oLocalIndependentPotentialPercentFullDisplayFn = nullptr;
static tLocalIndependentPotentialFlatBasicDisplayFn oLocalIndependentPotentialFlatBasicDisplayFn = nullptr;
static tLocalIndependentPotentialFlatExtendedDisplayFn oLocalIndependentPotentialFlatExtendedDisplayFn = nullptr;
typedef BYTE* (__stdcall *tPotentialTextFormatFn)(int* src, BYTE* statsPtr);
static tPotentialTextFormatFn oPotentialTextFormat = nullptr;
static void *oSkillReleaseClassifierRoot = nullptr;
static void *oSkillReleaseClassifier = nullptr;
static void *oSkillReleaseClassifierB2F370 = nullptr;
typedef BOOL(__cdecl *tSkillNativeIdGateFn)(int skillId);
static tSkillNativeIdGateFn oSkillNativeIdGate7CE790 = nullptr;
static tSkillNativeIdGateFn oSkillNativeIdGate7D0000 = nullptr;
static tSkillNativeIdGateFn oMountedSkillWhitelist7CF270 = nullptr;
typedef int(__thiscall *tMountedStateGateFn)(void *thisPtr);
static tMountedStateGateFn oMountedStateGate42DE20 = nullptr;
typedef int(__thiscall *tMountedUseFailPromptFn)(void *thisPtr, int a2);
static tMountedUseFailPromptFn oMountedUseFailPromptAE6260 = nullptr;
typedef int(__thiscall *tMountedDemonJumpContextClearFn)(void *contextPtr);
static tMountedDemonJumpContextClearFn oMountedDemonJumpContextClear433380 = nullptr;
typedef void(__thiscall *tMountedSkillPacketDispatchFn)(void *thisPtr, int skillId, char level);
static tMountedSkillPacketDispatchFn oMountedSkillPacketDispatchB26760 = nullptr;
typedef int(__thiscall *tMountedSkillAttackPacketFn)(
    void *thisPtr,
    int *skillIdPtr,
    int a3,
    int a4,
    int a5,
    int a6,
    unsigned int a7,
    int a8);
static tMountedSkillAttackPacketFn oMountedSkillAttackPacketB28A00 = nullptr;
typedef int(__cdecl *tMountActionGateFn)(int mountItemId);
static tMountActionGateFn oMountActionGate4069E0 = nullptr;
static tMountActionGateFn oMountActionGate406AB0 = nullptr;
typedef int(__cdecl *tMountNativeFlightSkillMapFn)(int mountItemId);
static tMountNativeFlightSkillMapFn oMountNativeFlightSkillMap7CF370 = nullptr;
typedef int(__thiscall *tMountedSkillContextGateFn)(void *mountContext);
static tMountedSkillContextGateFn oMountedSkillContextGateA9BF40 = nullptr;
static DWORD g_MountedSkillContextGateCallsiteOriginalTarget = 0;
static DWORD g_MountedUnknownSkillReleaseBranchOriginalTarget = 0;
typedef void(__thiscall *tMountedDemonJumpContextPrimeFn)(
    void *userLocal,
    unsigned int **skillEntryOrNull);
typedef int(__thiscall *tMountedDemonJumpContextCurrentSkillFn)(void *contextPtr);
typedef signed int(__stdcall *tMountedDemonJumpSkillEntryLookupFn)(
    int skillId,
    unsigned int **skillEntryOut);
typedef void(__thiscall *tMountedDemonJumpContextSeedFn)(
    void *userLocal,
    int a2,
    int a3,
    int a4,
    int a5,
    int a6,
    int a7,
    int a8,
    int a9,
    int a10,
    int a11,
    int a12);
static const DWORD ADDR_MountedDemonJumpContextPrimeB00AD0 = 0x00B00AD0;
static const DWORD ADDR_MountedDemonJumpContextCurrentSkill4300A0 = 0x004300A0;
static const DWORD ADDR_MountedDemonJumpSkillEntryLookupAE0420 = 0x00AE0420;
static const DWORD ADDR_MountedDemonJumpContextSeedAC6B00 = 0x00AC6B00;
static const DWORD ADDR_MountedDemonJumpContextClear433380 = 0x00433380;
static const DWORD ADDR_MountedDemonJumpContextMountedClearReturn433F48 = 0x00433F48;
typedef int(__thiscall *tMountedDemonJumpLateRouteFn)(
    void *thisPtr,
    int a2,
    int a3);
typedef void(__thiscall *tMountedDemonJumpLateVoidRouteFn)(
    void *thisPtr,
    int a2,
    int a3);
typedef int(__thiscall *tMountedDemonJumpLateBranchFn)(
    void *thisPtr,
    int a2);
typedef UINT(__thiscall *tMountedDemonJumpContextInputFn)(
    void *thisPtr,
    UINT a2,
    int a3);
typedef BOOL(__thiscall *tMountedDemonJumpLateFilterFn)(
    void *thisPtr,
    int a2,
    int a3);
typedef void(__stdcall *tMountedDemonJumpLateTickFn)(
    unsigned int a1,
    int a2);
static tMountedDemonJumpLateRouteFn oMountedDemonJumpLateRoute575D60 = nullptr;
static tMountedDemonJumpLateTickFn oMountedDemonJumpLateTick576020 = nullptr;
static tMountedDemonJumpContextInputFn oMountedDemonJumpContextInputB22630 = nullptr;
static tMountedDemonJumpLateVoidRouteFn oMountedDemonJumpMoveB1DB10 = nullptr;
static tMountedDemonJumpLateVoidRouteFn oMountedDemonJumpMoveB1C9E0 = nullptr;
static tMountedDemonJumpLateBranchFn oMountedDemonJumpBranchADEDA0 = nullptr;
static tMountedDemonJumpLateFilterFn oMountedDemonJumpFilterBDBFD0 = nullptr;
static const DWORD ADDR_MountedDemonJumpLateRoute575D60 = 0x00575D60;
static const DWORD ADDR_MountedDemonJumpLateTick576020 = 0x00576020;
static const DWORD ADDR_MountedDemonJumpContextInputB22630 = 0x00B22630;
static const DWORD ADDR_MountedDemonJumpMoveB1DB10 = 0x00B1DB10;
static const DWORD ADDR_MountedDemonJumpMoveB1C9E0 = 0x00B1C9E0;
static const DWORD ADDR_MountedDemonJumpBranchADEDA0 = 0x00ADEDA0;
static const DWORD ADDR_MountedDemonJumpFilterBDBFD0 = 0x00BDBFD0;
static const DWORD ADDR_MountedSkillPacketDispatchB26760 = 0x00B26760;
static const DWORD ADDR_MountedSkillAttackPacketB28A00 = 0x00B28A00;
static const size_t kMountedDemonJumpContextOffset = 24200;
static const size_t kMountedDemonJumpContextRootSkillOffset = 24204;
static const size_t kMountedDemonJumpReadyFlagOffset = 24197;
typedef int(__thiscall *tMountedCrashTrace2ArgFn)(void *thisPtr, DWORD arg1, DWORD arg2);
typedef int(__thiscall *tMountedCrashTraceNoArgFn)(void *thisPtr);
typedef int(__thiscall *tMountedCrashTrace1ArgFn)(void *thisPtr, int arg1);
typedef int(__thiscall *tMountedCrashTrace6ArgFn)(
    void *thisPtr,
    DWORD arg1,
    DWORD arg2,
    DWORD arg3,
    DWORD arg4,
    DWORD arg5,
    DWORD arg6);
typedef int(__cdecl *tMountedCrashTraceCdecl1ArgFn)(int arg1);
static tMountedCrashTrace2ArgFn oMountedDemonJumpTrace8057F0 = nullptr;
static tMountedCrashTraceCdecl1ArgFn oMountedDemonJumpTrace550FF0 = nullptr;
static tMountedCrashTraceNoArgFn oMountedDemonJumpTrace829EC0 = nullptr;
static tMountedCrashTraceNoArgFn oMountedDemonJumpTrace829F10 = nullptr;
static tMountedCrashTraceNoArgFn oMountedDemonJumpTrace551170 = nullptr;
static tMountedCrashTrace1ArgFn oMountedDemonJumpTraceA01BF0 = nullptr;
static tMountedCrashTraceNoArgFn oMountedDemonJumpTrace4C1720 = nullptr;
static tMountedCrashTraceNoArgFn oMountedDemonJumpTrace52BCB0 = nullptr;
static tMountedCrashTrace6ArgFn oMountedDemonJumpTrace805850 = nullptr;
typedef int(__thiscall *tMountSoaringGateFn)(void *thisPtr, int levelContext, void *mountContext, int skillId, unsigned int **skillEntryOut);
static tMountSoaringGateFn oMountSoaringGate7DC1B0 = nullptr;
typedef int(__thiscall *tMountNativeSoaringReleaseFn)(void *thisPtr, int skillId);
static tMountNativeSoaringReleaseFn oMountNativeSoaringReleaseB26290 = nullptr;
typedef int(__thiscall *tMountFamilyGateFn)(void *mountContext);
static tMountFamilyGateFn oMountFamilyGateA9AAA0 = nullptr;
typedef int(__thiscall *tMountContextGetItemIdFn)(void *mountContext);
typedef BOOL(__thiscall *tMountContextIsFlyingFamilyFn)(void *mountContext);
static tMountContextIsFlyingFamilyFn oMountContextIsFlyingFamily7D4CD0 = nullptr;
typedef void *(__thiscall *tMountItemInfoLookupFn)(void *thisPtr, int mountItemId);
typedef int(__thiscall *tMountItemInfoDataKeyFn)(void *thisPtr);
typedef int(__cdecl *tMountMovementDataLookupFn)(int dataKey);
static tMountMovementDataLookupFn oMountMovementDataLookup888B30 = nullptr;
typedef __int16 (__thiscall *tEncodedDoubleWriteFn)(void *slot, int lowDword, int highDword);
typedef double (__thiscall *tEncodedDoubleReadFn)(void *slot);
typedef int (__thiscall *tMountedFlightPhysicsDispatchFn)(void *thisPtr, int deltaMs);
typedef void (__thiscall *tMountedFlightPhysicsStepFn)(void *thisPtr, int deltaMs);
typedef int (__thiscall *tMountedFlightPhysicsStateFn)(void *thisPtr);
typedef int (__thiscall *tMountedFlightPhysicsVerticalFn)(void *thisPtr);
typedef int (__thiscall *tMountedFlightPhysicsFinalizeFn)(void *thisPtr, int deltaMs);
static tMountedFlightPhysicsDispatchFn oMountedFlightPhysicsDispatchB87E60 = nullptr;
static tMountedFlightPhysicsStepFn oMountedFlightPhysicsStepB83C90 = nullptr;
static tMountedFlightPhysicsStepFn oMountedFlightPhysicsStepB844D0 = nullptr;
static tMountedFlightPhysicsStepFn oMountedFlightPhysicsStepB88090 = nullptr;
static tMountedFlightPhysicsStateFn oMountedFlightPhysicsStateB84D70 = nullptr;
static tMountedFlightPhysicsVerticalFn oMountedFlightPhysicsVerticalB8FE30 = nullptr;
static tMountedFlightPhysicsVerticalFn oMountedFlightPhysicsVerticalB92990 = nullptr;
static tMountedFlightPhysicsFinalizeFn oMountedFlightPhysicsFinalizeB851F0 = nullptr;
typedef int(__thiscall *tNativeGlyphLookupFn)(void *fontCache, unsigned int codepoint, RECT *outRectOrNull);
static tNativeGlyphLookupFn oNativeGlyphLookup = nullptr;
typedef int(__thiscall *tSkillLevelBaseFn)(void *thisPtr, DWORD playerObj, int skillId, void *cachePtr);
typedef int(__thiscall *tSkillLevelCurrentFn)(void *thisPtr, DWORD playerObj, int skillId, void *cachePtr, int flags);
static tSkillLevelBaseFn oSkillLevelBase = nullptr;
static tSkillLevelCurrentFn oSkillLevelCurrent = nullptr;
typedef int(__thiscall *tSkillEffectFn)(void *thisPtr, int level);
static tSkillEffectFn oSkillEffect800260 = nullptr;
static tSkillEffectFn oSkillEffect800580 = nullptr;
typedef int(__thiscall *tPassiveEffectGetterFn)(void *effectPtr);
static tPassiveEffectGetterFn oPassiveEffectDamage43DE00 = nullptr;
static tPassiveEffectGetterFn oPassiveEffectDamage43DE50 = nullptr;
static tPassiveEffectGetterFn oPassiveEffectAttackCount5E9EE0 = nullptr;
static tPassiveEffectGetterFn oPassiveEffectMobCount7D1990 = nullptr;
static tPassiveEffectGetterFn oPassiveEffectAttackCount7D19E0 = nullptr;
static tPassiveEffectGetterFn oPassiveEffectIgnore7D28E0 = nullptr;
typedef void(__thiscall *tSkillPresentationDispatch)(void *thisPtr, int *skillData, int a3, int a4, int a5, int a6, int a7);
static tSkillPresentationDispatch oSkillPresentationDispatch = nullptr;
typedef void(__thiscall *tStatusBarInternalRefreshFn)(uintptr_t thisPtr);
static tStatusBarInternalRefreshFn oStatusBarRefreshSlotsPrimary = nullptr;
static tStatusBarInternalRefreshFn oStatusBarRefreshSlotsSecondary = nullptr;
static tStatusBarInternalRefreshFn oStatusBarRefreshInternal = nullptr;
static tStatusBarInternalRefreshFn oStatusBarCleanupTransient = nullptr;
typedef int(__thiscall *tSurfaceDrawImageFn)(void *surface, int x, int y, int imageObj, DWORD *variantLikeAlpha);
static tSurfaceDrawImageFn oSurfaceDrawImageFn = nullptr;
typedef char(__thiscall *tNativeCursorStateSetFn)(uintptr_t thisPtr, unsigned int state);
static tNativeCursorStateSetFn oNativeCursorStateSetFn = nullptr;
typedef int(__thiscall *tStatusBarTransientRefreshFn)(uintptr_t thisPtr, int a2);
typedef void(__thiscall *tStatusBarTransientDispatchFn)(uintptr_t thisPtr, int a2);
typedef LONG* (__stdcall *tStatusBarTransientToggleFn)(int a1);
static tStatusBarTransientRefreshFn oStatusBarTransientRefresh = nullptr;
static tStatusBarTransientDispatchFn oStatusBarTransientDispatch = nullptr;
static tStatusBarTransientToggleFn oStatusBarTransientToggle = nullptr;
struct StatusBarObservedBuffSlot
{
    uintptr_t wrapper = 0;
    uintptr_t child = 0;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int renderX = 0;
    int renderY = 0;
};
static StatusBarObservedBuffSlot g_StatusBarObservedBuffSlots[9] = {};
static bool g_StatusBarBuffSlotHooksInstalled = false;
static volatile DWORD g_ClassifierOverrideSkillId = 0;
static volatile DWORD g_ForcedNativeReleaseJump = 0;
static void hkExternalPotentialWriteNaked();
static void hkExternalPotentialClearNaked();
static bool PatchExternalPotentialIncreaseStub(BYTE* stubTarget);
static bool SetupLocalIndependentPotentialPrimaryFlatStatHook();
static bool SetupLocalIndependentPotentialPrimaryPercentStatHook();
static bool SetupLocalIndependentPotentialFlatStatHook();
static bool SetupLocalIndependentPotentialDisplayFunctionHooks();
static bool SetupAbilityRedHashContainerHooks();
static bool SetupAbilityRedDisplayCandidateHook();
static bool SetupAbilityRedDisplayCallsiteHook();
static bool SetupAbilityRedLevelReadHook();
static bool SetupAbilityRedSkillWriteHooks();
static bool SetupAbilityRedExtendedAggregateHook();
static bool SetupAbilityRedMasterAggregateHook();
static bool SetupAbilityRedMovementSetterHooks();
static bool SetupMovementOutputClampHook();
static bool SetupAbilityRedSiblingCalcHooks();
static bool SetupAbilityRedDiff84C470PreSubHook();
static bool SetupAbilityRedAdditionalDiffHooks();
static bool SetupAbilityRedPositiveStyleHooks();
static bool SetupAbilityRedBakeWriteHooks();
static bool SetupAbilityRedBake198Hooks();
static bool SetupAbilityRedFinalValueHooks();
static bool SetupLocalIndependentPotentialSkillLevelDisplayHook();
static bool SetupLocalIndependentPotentialDamageDisplayHook();
static bool SetupPotentialTextDisplayHook();
static bool SetupStatusBarBuffSlotHooks();
static bool SetupSurfaceDrawImageObservationHook();
static bool SetupNativeCursorStateHook();
static bool SetupSkillEffectPassiveBonusHooks();
static bool SetupMountedUnknownSkillReleaseBranchHook();
static bool SetupMountedUseFailPromptSuppressHook();
static bool SetupMountMovementObservationHooks();
static bool SetupMountedFlightPhysicsSpeedHooks();
static bool SetupMountedDemonJumpCrashTraceHooks();
static bool SetupMountedDemonJumpPacketObserveHooks();
static bool SetupMountedDemonJumpLatePathHooks();
enum MountedRuntimeSkillKind
{
    MountedRuntimeSkillKind_DoubleJump = 0,
    MountedRuntimeSkillKind_DemonJump = 1,
    MountedRuntimeSkillKind_Count = 2
};
static bool HasRecentMountedDoubleJumpIntent(int mountItemId, DWORD maxAgeMs = 400);
static bool HasRecentMountedDemonJumpIntent(int mountItemId, DWORD maxAgeMs = 400);
static bool IsMountedDemonJumpRelatedSkillId(int skillId);
static void ArmMountedDemonJumpCrashTrace(int runtimeSkillId, int mountItemId);
static bool IsMountedDemonJumpCrashTraceFresh(
    int *runtimeSkillIdOut = nullptr,
    int *mountItemIdOut = nullptr,
    DWORD maxAgeMs = 2500);
static bool IsExtendedMountSoaringContextMount(int mountItemId);
static bool TryGetMountedSoaringFlightTiming(
    int mountItemId,
    DWORD *ageMsOut,
    DWORD *activeDurationMsOut);
static const bool kEnableMountedDoubleJumpRuntimeHooks = true;
static const bool kEnableMountMovementAbilityRedHooks = true;
static const bool kEnableGlobalMovementSetterProtectionHooks = false;
// Despite the historical name, this now covers both the mounted data loader
// cap in 888DF0 and the later shared speed/jump clamp chain.
static const bool kEnableMountMovementCapPatches = true;
static const bool kEnableMountMovementObservationHooks = true;
static const bool kEnableMountedFlightPhysicsSpeedHooks = true;
static bool TryReadCurrentUserLocalPtr(void **userLocalOut);
static bool TryReadMountedDemonJumpContextState(
    int *rootSkillIdOut,
    int *currentSkillIdOut = nullptr,
    DWORD *userLocalOut = nullptr);
static bool PrimeMountedDemonJumpContextIfNeeded(
    int mountItemId,
    const char *reason = nullptr,
    int *currentSkillIdOut = nullptr);
static bool HasMountedDemonJumpContextPrimedForMount(
    int mountItemId,
    int *currentSkillIdOut = nullptr,
    int *rootSkillIdOut = nullptr);
static bool TryReadCurrentUserMountItemId(int *mountItemIdOut);
static bool IsMountedDemonJumpRuntimeChildSkillId(int skillId);
static void RememberMountedDemonJumpNativeChildSkill(
    int mountItemId,
    int skillId,
    const char *source = nullptr);
static bool TryGetRecentMountedDemonJumpNativeChildSkill(
    int mountItemId,
    int *skillIdOut,
    const char **sourceOut = nullptr,
    DWORD maxAgeMs = 0);
static bool SendMountedDemonJumpSyntheticSpecialMovePacket(
    int skillId,
    int level,
    DWORD *tickOut = nullptr);
static bool ArmMountedDemonJumpPendingSpecialMoveRewrite(
    int mountItemId,
    int expectedSkillId,
    int packetSkillId,
    int packetLevel,
    int runtimeChildSkillId,
    const char *source = nullptr,
    DWORD *tickOut = nullptr);
static bool TryRewriteMountedDemonJumpOutgoingPacket(
    void **packetDataSlot,
    int *packetLenSlot,
    uintptr_t callerRetAddr);
static bool ShouldSuppressMountedDemonJumpMountedContextClear(
    void *contextPtr,
    DWORD callerRet,
    int *mountItemIdOut = nullptr,
    int *rootSkillIdOut = nullptr,
    int *currentSkillIdOut = nullptr);
static bool TryResolveMountedMovementDataKeyFromMountItemId(int mountItemId, int *dataKeyOut);
static bool TryReadMountItemIdFromPlayerObjectRaw(void *playerObj, int *mountItemIdOut);
static bool TryReadMountItemIdFromPlayerObject(void *playerObj, int *mountItemIdOut);
static bool TryGetRecentMountedMovementRawSample(
    int *mountItemIdOut,
    int *dataKeyOut,
    int *speedOut,
    int *jumpOut,
    DWORD maxAgeMs);
static void ClearRecentMountedMovementRawSample();
static bool TryResolveCurrentUserMountItemIdWithFallback(
    int *mountItemIdOut,
    const char **sourceOut = nullptr);
static bool TryResolveMountedDoubleJumpMountItemIdWithFallback(
    void *playerObj,
    int *mountItemIdOut,
    const char **sourceOut = nullptr,
    DWORD maxAgeMs = 1200);
static void ObserveMountedDoubleJumpNativeRelease(int mountItemId, int skillId);
static bool TryResolveMountedDemonJumpMountItemIdWithFallback(
    void *playerObj,
    int *mountItemIdOut,
    const char **sourceOut = nullptr,
    DWORD maxAgeMs = 1200);
static bool TryGetRecentMountedDemonJumpIntentItemId(
    int *mountItemIdOut,
    DWORD maxAgeMs = 400);
static void ObserveMountedDemonJumpNativeRelease(int mountItemId, int skillId);

static uintptr_t g_LocalIndependentPotentialSkillLevelLastTarget = 0;
static DWORD g_LocalIndependentPotentialSkillLevelLastTick = 0;
static uintptr_t g_LocalIndependentPotentialDamageLastKey = 0;
static DWORD g_LocalIndependentPotentialDamageLastTick = 0;
static uintptr_t g_LocalIndependentPotentialPercentQuadLastKey = 0;
static DWORD g_LocalIndependentPotentialPercentQuadLastTick = 0;
static uintptr_t g_LocalIndependentPotentialPercentFullLastKey = 0;
static DWORD g_LocalIndependentPotentialPercentFullLastTick = 0;
static uintptr_t g_LocalIndependentPotentialFlatBasicLastKey = 0;
static DWORD g_LocalIndependentPotentialFlatBasicLastTick = 0;
static uintptr_t g_LocalIndependentPotentialFlatExtendedLastKey = 0;
static DWORD g_LocalIndependentPotentialFlatExtendedLastTick = 0;
static const bool kLocalIndependentPotentialDisplayObserveOnly = true;
static uintptr_t g_AbilityRedDisplayCandidateLastThis = 0;
static DWORD g_AbilityRedDisplayCandidateLastTick = 0;
static DWORD g_AbilityRedDisplayCallsiteLastTick = 0;
static DWORD g_AbilityRedLevelReadLastTick = 0;
static DWORD g_AbilityRedSnapshotLastTick = 0;
static DWORD g_AbilityRedSkillWriteLastTick = 0;
static DWORD g_AbilityRedHashLookupLastCaller = 0;
static uintptr_t g_AbilityRedHashLookupLastThis = 0;
static DWORD g_AbilityRedHashLookupLastKey = 0;
static DWORD g_AbilityRedHashLookupLastTick = 0;
static DWORD g_AbilityRedHashInsertLastCaller = 0;
static uintptr_t g_AbilityRedHashInsertLastThis = 0;
static DWORD g_AbilityRedHashInsertLastKey = 0;
static DWORD g_AbilityRedHashInsertLastValue = 0;
static DWORD g_AbilityRedHashInsertLastTick = 0;
static DWORD g_AbilityRedExtendedAggregateLastCaller = 0;
static DWORD g_AbilityRedExtendedAggregateLastTick = 0;
static DWORD g_AbilityRedMasterAggregateLastCaller = 0;
static DWORD g_AbilityRedMasterAggregateLastTick = 0;
static DWORD g_AbilityRedSibling82F780LastCaller = 0;
static uintptr_t g_AbilityRedSibling82F780LastThis = 0;
static DWORD g_AbilityRedSibling82F780LastTick = 0;
static int g_AbilityRedSibling82F780LastActive = -1;
static DWORD g_AbilityRedSibling82F870LastCaller = 0;
static uintptr_t g_AbilityRedSibling82F870LastThis = 0;
static DWORD g_AbilityRedSibling82F870LastTick = 0;
static int g_AbilityRedSibling82F870LastActive = -1;
static DWORD g_AbilityRedSibling82F960LastCaller = 0;
static uintptr_t g_AbilityRedSibling82F960LastThis = 0;
static DWORD g_AbilityRedSibling82F960LastTick = 0;
static int g_AbilityRedSibling82F960LastActive = -1;
static DWORD g_AbilityRedSibling82FA50LastCaller = 0;
static uintptr_t g_AbilityRedSibling82FA50LastThis = 0;
static DWORD g_AbilityRedSibling82FA50LastTick = 0;
static int g_AbilityRedSibling82FA50LastActive = -1;
static LONG g_MountMovementObserveLogBudget = 32;
static LONG g_MountMovementOverrideMissLogBudget = 16;
static volatile LONG g_RecentMountedMovementRawMountItemId = 0;
static volatile LONG g_RecentMountedMovementRawDataKey = 0;
static volatile LONG g_RecentMountedMovementRawSpeed = 0;
static volatile LONG g_RecentMountedMovementRawJump = 0;
static volatile LONG g_RecentMountedMovementRawTick = 0;
static LONG g_MountedFlightPhysicsScaleLogBudget = 96;
static LONG g_MountedFlightPhysicsEntryLogBudget = 96;
static LONG g_MountedFlightPhysicsDeltaLogBudget = 24;
static LONG g_MountMovementNativeCacheRebuildLogBudget = 24;
static volatile LONG g_MountMovementNativeCacheRebuildInProgress = 0;
static volatile LONG g_LastMountMovementNativeCacheRebuildDataKey = 0;
static volatile LONG g_LastMountMovementNativeCacheRebuildTick = 0;
static DWORD g_AbilityRedDiff84C470LastCaller = 0;
static uintptr_t g_AbilityRedDiff84C470LastThis = 0;
static DWORD g_AbilityRedDiff84C470LastTick = 0;
static int g_AbilityRedDiff84C470LastActive = -1;
static DWORD g_AbilityRedBaseSumInactive9F7241 = 0;
static DWORD g_AbilityRedBaseSumInactive9F7546 = 0;
static DWORD g_AbilityRedBaseSumInactive9F7893 = 0;
static DWORD g_AbilityRedBaseSumInactive9F7C7F = 0;
static DWORD g_AbilityRedBaseSumInactive9F8048 = 0;
static DWORD g_AbilityRedBaseSumInactive9F82A8 = 0;
static DWORD g_AbilityRedBake857BB6LastSig = 0;
static DWORD g_AbilityRedBake857BB6LastTick = 0;
static DWORD g_AbilityRedBake857C29LastSig = 0;
static DWORD g_AbilityRedBake857C29LastTick = 0;
static DWORD g_AbilityRedBake857C9CLastSig = 0;
static DWORD g_AbilityRedBake857C9CLastTick = 0;
static DWORD g_AbilityRedBake857D0FLastSig = 0;
static DWORD g_AbilityRedBake857D0FLastTick = 0;
static DWORD g_AbilityRedBake1988569C3LastSig = 0;
static DWORD g_AbilityRedBake1988569C3LastTick = 0;
static DWORD g_AbilityRedBake198856D57LastSig = 0;
static DWORD g_AbilityRedBake198856D57LastTick = 0;
static DWORD g_AbilityRedBake19885725FLastSig = 0;
static DWORD g_AbilityRedBake19885725FLastTick = 0;
static DWORD g_AbilityRedBake198857C3BLastSig = 0;
static DWORD g_AbilityRedBake198857C3BLastTick = 0;
static DWORD g_AbilityRedBake198858AEDLastSig = 0;
static DWORD g_AbilityRedBake198858AEDLastTick = 0;
static DWORD g_AbilityRedBake198831A50LastSig = 0;
static DWORD g_AbilityRedBake198831A50LastTick = 0;
static volatile LONG g_activeMountedSoaringFlightItemId = 0;
static volatile LONG g_activeMountedSoaringFlightTick = 0;
static volatile LONG g_activeMountedSoaringFlightStartTick = 0;
static const DWORD kMountedSoaringFallbackGraceMs = 1500;
static bool HasMeaningfulMountedFlightPhysicsScaleBaseline(
    double nativeFs,
    double nativeSwim,
    double overrideFs,
    double overrideSwim);
static double NormalizeMountedFlightPhysicsHumanScalar(double rawValue);
static void RememberMountedFlightPhysicsScaleSample(
    int mountItemId,
    int dataKey,
    double nativeFs,
    double nativeSwim,
    const MountedMovementOverride &appliedOverride);
struct MountedFlightPhysicsScaleSample
{
    int mountItemId;
    int dataKey;
    double nativeFs;
    double nativeSwim;
    double overrideFs;
    double overrideSwim;
    DWORD tick;
};
static MountedFlightPhysicsScaleSample g_MountedFlightPhysicsScaleSample = {};
static const size_t kMountedFlightPhysicsScaleSampleHistoryCapacity = 8;
static MountedFlightPhysicsScaleSample
    g_MountedFlightPhysicsScaleSampleHistory[kMountedFlightPhysicsScaleSampleHistoryCapacity] = {};

static bool IsMountedFlightPhysicsScaleSampleUseful(
    const MountedFlightPhysicsScaleSample &sample)
{
    return sample.mountItemId > 0 &&
           sample.dataKey > 0 &&
           sample.tick != 0 &&
           HasMeaningfulMountedFlightPhysicsScaleBaseline(
               sample.nativeFs,
               sample.nativeSwim,
               sample.overrideFs,
               sample.overrideSwim);
}

static bool TryFindMountedFlightPhysicsScaleHistorySample(
    int mountItemId,
    int dataKey,
    MountedFlightPhysicsScaleSample *sampleOut)
{
    if (sampleOut)
    {
        ZeroMemory(sampleOut, sizeof(*sampleOut));
    }
    if (mountItemId <= 0 || dataKey <= 0 || !sampleOut)
    {
        return false;
    }

    int bestIndex = -1;
    DWORD bestTick = 0;
    for (size_t i = 0; i < kMountedFlightPhysicsScaleSampleHistoryCapacity; ++i)
    {
        const MountedFlightPhysicsScaleSample &candidate =
            g_MountedFlightPhysicsScaleSampleHistory[i];
        if (candidate.mountItemId != mountItemId ||
            candidate.dataKey != dataKey ||
            !IsMountedFlightPhysicsScaleSampleUseful(candidate))
        {
            continue;
        }

        if (bestIndex < 0 || candidate.tick >= bestTick)
        {
            bestIndex = static_cast<int>(i);
            bestTick = candidate.tick;
        }
    }

    if (bestIndex < 0)
    {
        return false;
    }

    *sampleOut = g_MountedFlightPhysicsScaleSampleHistory[bestIndex];
    return true;
}

static bool TryGetMountedFlightPhysicsScaleSampleForRoute(
    int mountItemId,
    int dataKey,
    MountedFlightPhysicsScaleSample *sampleOut)
{
    if (sampleOut)
    {
        ZeroMemory(sampleOut, sizeof(*sampleOut));
    }
    if (mountItemId <= 0 || dataKey <= 0 || !sampleOut)
    {
        return false;
    }

    const MountedFlightPhysicsScaleSample liveSample =
        g_MountedFlightPhysicsScaleSample;
    if (liveSample.mountItemId == mountItemId &&
        liveSample.dataKey == dataKey &&
        liveSample.tick != 0)
    {
        *sampleOut = liveSample;
        return true;
    }

    return TryFindMountedFlightPhysicsScaleHistorySample(
        mountItemId,
        dataKey,
        sampleOut);
}

static bool TryPrimeMountedFlightPhysicsScaleSampleForMount(
    int mountItemId,
    MountedFlightPhysicsScaleSample *sampleOut)
{
    if (sampleOut)
    {
        ZeroMemory(sampleOut, sizeof(*sampleOut));
    }
    if (mountItemId <= 0 || !sampleOut || !oMountMovementDataLookup888B30)
    {
        return false;
    }

    int recentMountItemId = 0;
    int dataKey = 0;
    const bool hasRecentDataKey =
        TryGetRecentMountedMovementRawSample(
            &recentMountItemId,
            &dataKey,
            nullptr,
            nullptr,
            30000) &&
        recentMountItemId == mountItemId &&
        dataKey > 0;
    if (!hasRecentDataKey &&
        !TryResolveMountedMovementDataKeyFromMountItemId(
            mountItemId,
            &dataKey))
    {
        return false;
    }

    if (TryGetMountedFlightPhysicsScaleSampleForRoute(
            mountItemId,
            dataKey,
            sampleOut))
    {
        return true;
    }

    MountedMovementOverride mountedOverride = {};
    if (!SkillOverlayBridgeResolveMountedSoaringOverride(
            mountItemId,
            dataKey,
            mountedOverride) &&
        !SkillOverlayBridgeResolveMountedMovementOverride(
            mountItemId,
            dataKey,
            mountedOverride))
    {
        return false;
    }

    const int dataPtr = oMountMovementDataLookup888B30(dataKey);
    if (dataPtr <= 0 ||
        SafeIsBadReadPtr(reinterpret_cast<void *>(static_cast<uintptr_t>(dataPtr)), 0x2C))
    {
        return false;
    }

    double rawFs = 0.0;
    DWORD rawSwim = 0;
    __try
    {
        rawFs = *reinterpret_cast<double *>(static_cast<uintptr_t>(dataPtr) + 0x20);
        rawSwim = *reinterpret_cast<DWORD *>(static_cast<uintptr_t>(dataPtr) + 0x28);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    MountedMovementOverride primedOverride = mountedOverride;
    // Flight priming must follow the runtime lookup key actually observed by
    // 888B30. The config-side mountTamingMobId is only a selector hint and can
    // differ from the real mounted movement table key (for example 1932064 can
    // resolve to runtime key 18). Priming with the config key poisons the
    // sample cache and blocks the later real flight sample from taking over.
    if (mountedOverride.useNativeMovement)
    {
        const double normalizedFs =
            NormalizeMountedFlightPhysicsHumanScalar(rawFs);
        const double normalizedSwim =
            NormalizeMountedFlightPhysicsHumanScalar(static_cast<double>(rawSwim));
        primedOverride.hasFs = normalizedFs > 0.0;
        primedOverride.fs = normalizedFs;
        primedOverride.hasSwim = normalizedSwim > 0.0;
        primedOverride.swim = normalizedSwim;
        RememberMountedFlightPhysicsScaleSample(
            mountItemId,
            dataKey,
            100.0,
            100.0,
            primedOverride);
    }
    else
    {
        RememberMountedFlightPhysicsScaleSample(
            mountItemId,
            dataKey,
            rawFs,
            static_cast<double>(rawSwim),
            primedOverride);
    }

    const bool primed =
        TryGetMountedFlightPhysicsScaleSampleForRoute(
            mountItemId,
            dataKey,
            sampleOut);
    if (primed)
    {
        static LONG s_mountFlightPrimeLogBudget = 24;
        if (InterlockedDecrement(&s_mountFlightPrimeLogBudget) >= 0)
        {
            WriteLogFmt(
                "[MountFlightPrime] mount=%d key=%d raw=[fs=%.3f swim=%u] sample=[nativeFs=%.3f nativeSwim=%.3f overrideFs=%.3f overrideSwim=%.3f] nativeMode=%d",
                mountItemId,
                dataKey,
                rawFs,
                rawSwim,
                sampleOut->nativeFs,
                sampleOut->nativeSwim,
                sampleOut->overrideFs,
                sampleOut->overrideSwim,
                mountedOverride.useNativeMovement ? 1 : 0);
        }
    }
    return primed;
}

static void RememberMountedFlightPhysicsScaleHistorySample(
    const MountedFlightPhysicsScaleSample &sample)
{
    if (!IsMountedFlightPhysicsScaleSampleUseful(sample))
    {
        return;
    }

    int replaceIndex = -1;
    DWORD oldestTick = 0xFFFFFFFFu;
    for (size_t i = 0; i < kMountedFlightPhysicsScaleSampleHistoryCapacity; ++i)
    {
        MountedFlightPhysicsScaleSample &entry =
            g_MountedFlightPhysicsScaleSampleHistory[i];
        if (entry.mountItemId == sample.mountItemId &&
            entry.dataKey == sample.dataKey)
        {
            replaceIndex = static_cast<int>(i);
            break;
        }
        if (!IsMountedFlightPhysicsScaleSampleUseful(entry))
        {
            replaceIndex = static_cast<int>(i);
            break;
        }
        if (entry.tick < oldestTick)
        {
            oldestTick = entry.tick;
            replaceIndex = static_cast<int>(i);
        }
    }

    if (replaceIndex < 0)
    {
        replaceIndex = 0;
    }
    g_MountedFlightPhysicsScaleSampleHistory[replaceIndex] = sample;
}

static void ClearMountedFlightPhysicsScaleSample()
{
    ZeroMemory(&g_MountedFlightPhysicsScaleSample, sizeof(g_MountedFlightPhysicsScaleSample));
    ZeroMemory(
        g_MountedFlightPhysicsScaleSampleHistory,
        sizeof(g_MountedFlightPhysicsScaleSampleHistory));
}

static bool HasMeaningfulMountedFlightPhysicsScaleBaseline(
    double nativeFs,
    double nativeSwim,
    double overrideFs,
    double overrideSwim)
{
    const bool hasFsBaseline =
        nativeFs > 0.0 &&
        overrideFs > 0.0 &&
        fabs(nativeFs - overrideFs) > 0.001;
    const bool hasSwimBaseline =
        nativeSwim > 0.0 &&
        overrideSwim > 0.0 &&
        fabs(nativeSwim - overrideSwim) > 0.5;
    return hasFsBaseline || hasSwimBaseline;
}

static double NormalizeMountedFlightPhysicsHumanScalar(double rawValue)
{
    if (!(rawValue > 0.0))
    {
        return 0.0;
    }

    double normalizedValue = rawValue;
    while (normalizedValue > 999.0)
    {
        normalizedValue /= 10.0;
    }
    return normalizedValue;
}

static bool IsMountedFlightPhysicsHumanPercentBaselineValue(
    double nativeValue,
    double overrideValue)
{
    return nativeValue >= 95.0 &&
           nativeValue <= 105.0 &&
           overrideValue > nativeValue + 0.001 &&
           overrideValue <= 999.0;
}

static bool IsMountedFlightPhysicsHumanPercentScaleSample(
    const MountedFlightPhysicsScaleSample &sample)
{
    if (sample.mountItemId <= 0 || sample.dataKey <= 0 || sample.tick == 0)
    {
        return false;
    }

    return IsMountedFlightPhysicsHumanPercentBaselineValue(
               sample.nativeFs,
               sample.overrideFs) ||
           IsMountedFlightPhysicsHumanPercentBaselineValue(
               sample.nativeSwim,
               sample.overrideSwim);
}

static bool IsMountedFlightPhysicsHumanPercentRouteSample(
    int mountItemId,
    int dataKey)
{
    MountedFlightPhysicsScaleSample sample = {};
    return TryGetMountedFlightPhysicsScaleSampleForRoute(
               mountItemId,
               dataKey,
               &sample) &&
           IsMountedFlightPhysicsHumanPercentScaleSample(sample);
}

static void RememberMountedFlightPhysicsScaleSample(
    int mountItemId,
    int dataKey,
    double nativeFs,
    double nativeSwim,
    const MountedMovementOverride &appliedOverride)
{
    const double overrideFs = appliedOverride.hasFs ? appliedOverride.fs : 0.0;
    const double overrideSwim = appliedOverride.hasSwim ? appliedOverride.swim : 0.0;
    const DWORD nowTick = GetTickCount();
    const MountedFlightPhysicsScaleSample existingSample =
        g_MountedFlightPhysicsScaleSample;
    const bool hasFreshBaseline =
        HasMeaningfulMountedFlightPhysicsScaleBaseline(
            nativeFs,
            nativeSwim,
            overrideFs,
            overrideSwim);
    const bool hasExistingUsefulBaseline =
        HasMeaningfulMountedFlightPhysicsScaleBaseline(
            existingSample.nativeFs,
            existingSample.nativeSwim,
            existingSample.overrideFs,
            existingSample.overrideSwim);
    const bool canReuseExistingBaseline =
        existingSample.mountItemId == mountItemId &&
        existingSample.dataKey == dataKey &&
        existingSample.tick != 0;
    MountedFlightPhysicsScaleSample historicalSample = {};
    const bool hasHistoricalUsefulBaseline =
        TryFindMountedFlightPhysicsScaleHistorySample(
            mountItemId,
            dataKey,
            &historicalSample);

    if (!hasFreshBaseline && canReuseExistingBaseline && hasExistingUsefulBaseline)
    {
        g_MountedFlightPhysicsScaleSample.overrideFs =
            overrideFs > existingSample.overrideFs ? overrideFs : existingSample.overrideFs;
        g_MountedFlightPhysicsScaleSample.overrideSwim =
            overrideSwim > existingSample.overrideSwim ? overrideSwim : existingSample.overrideSwim;
        g_MountedFlightPhysicsScaleSample.tick = nowTick;
        RememberMountedFlightPhysicsScaleHistorySample(g_MountedFlightPhysicsScaleSample);
        return;
    }

    if (!hasFreshBaseline && hasHistoricalUsefulBaseline)
    {
        g_MountedFlightPhysicsScaleSample = historicalSample;
        if (overrideFs > g_MountedFlightPhysicsScaleSample.overrideFs)
        {
            g_MountedFlightPhysicsScaleSample.overrideFs = overrideFs;
        }
        if (overrideSwim > g_MountedFlightPhysicsScaleSample.overrideSwim)
        {
            g_MountedFlightPhysicsScaleSample.overrideSwim = overrideSwim;
        }
        g_MountedFlightPhysicsScaleSample.tick = nowTick;
        RememberMountedFlightPhysicsScaleHistorySample(g_MountedFlightPhysicsScaleSample);
        return;
    }

    g_MountedFlightPhysicsScaleSample.mountItemId = mountItemId;
    g_MountedFlightPhysicsScaleSample.dataKey = dataKey;
    g_MountedFlightPhysicsScaleSample.nativeFs = nativeFs;
    g_MountedFlightPhysicsScaleSample.nativeSwim = nativeSwim;
    g_MountedFlightPhysicsScaleSample.overrideFs = overrideFs;
    g_MountedFlightPhysicsScaleSample.overrideSwim = overrideSwim;
    g_MountedFlightPhysicsScaleSample.tick = nowTick;
    RememberMountedFlightPhysicsScaleHistorySample(g_MountedFlightPhysicsScaleSample);
}
static DWORD g_AbilityRedBake19883AF02LastSig = 0;
static DWORD g_AbilityRedBake19883AF02LastTick = 0;
static DWORD g_AbilityRedFinal84C470LastCaller = 0;
static uintptr_t g_AbilityRedFinal84C470LastThis = 0;
static DWORD g_AbilityRedFinal84C470LastTick = 0;
static int g_AbilityRedFinal84C470LastActive = -1;
static DWORD g_AbilityRedFinal84CA90LastCaller = 0;
static uintptr_t g_AbilityRedFinal84CA90LastThis = 0;
static DWORD g_AbilityRedFinal84CA90LastTick = 0;
static int g_AbilityRedFinal84CA90LastActive = -1;
static DWORD g_AbilityRedFinal84CBD0LastCaller = 0;
static uintptr_t g_AbilityRedFinal84CBD0LastThis = 0;
static DWORD g_AbilityRedFinal84CBD0LastTick = 0;
static int g_AbilityRedFinal84CBD0LastActive = -1;

static DWORD SeedAbilityRedInactiveBaselineFromPrimary(DWORD siteId, DWORD currentSum);

static bool ShouldApplyLocalIndependentPotentialBurst(uintptr_t key, uintptr_t* lastKey, DWORD* lastTick)
{
    if (!key || !lastKey || !lastTick)
        return false;

    const DWORD now = GetTickCount();
    if (*lastKey == key && now - *lastTick <= 15)
        return false;

    *lastKey = key;
    *lastTick = now;
    return true;
}

static bool TryGetObservedDrawObjectSize(int imageObj, int* outW, int* outH)
{
    if (outW)
        *outW = 0;
    if (outH)
        *outH = 0;
    if (imageObj <= 0 || SafeIsBadReadPtr(reinterpret_cast<void*>(imageObj), 4))
        return false;

    __try
    {
        DWORD vtable = *reinterpret_cast<DWORD*>(imageObj);
        if (!vtable ||
            SafeIsBadReadPtr(reinterpret_cast<void*>(vtable + 64), 4) ||
            SafeIsBadReadPtr(reinterpret_cast<void*>(vtable + 72), 4))
        {
            return false;
        }

        typedef int (__stdcall *tGetDrawObjMetric)(int obj, LONG* outValue);
        tGetDrawObjMetric fnGetWidth = *reinterpret_cast<tGetDrawObjMetric*>(vtable + 64);
        tGetDrawObjMetric fnGetHeight = *reinterpret_cast<tGetDrawObjMetric*>(vtable + 72);
        if (!fnGetWidth || !fnGetHeight)
            return false;

        LONG w = 0;
        LONG h = 0;
        if (fnGetWidth(imageObj, &w) < 0 || fnGetHeight(imageObj, &h) < 0)
            return false;
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
            return false;

        if (outW)
            *outW = (int)w;
        if (outH)
            *outH = (int)h;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static int ExtractObservedDrawAlpha(const DWORD* variantLikeAlpha, WORD* outVariantType)
{
    if (outVariantType)
        *outVariantType = VT_EMPTY;
    if (!variantLikeAlpha)
        return 255;

    const WORD variantType = static_cast<WORD>(variantLikeAlpha[0] & 0xFFFFu);
    if (outVariantType)
        *outVariantType = variantType;

    switch (variantType)
    {
    case VT_I4:
    case VT_INT:
    case VT_UI4:
    case VT_UINT:
        return static_cast<int>(variantLikeAlpha[2]);
    case VT_I2:
    case VT_UI2:
        return static_cast<short>(variantLikeAlpha[2] & 0xFFFFu);
    case VT_EMPTY:
    default:
        return 255;
    }
}

static void ObserveSurfaceDrawImageCall(void *surface, int x, int y, int imageObj, DWORD *variantLikeAlpha)
{
    UNREFERENCED_PARAMETER(surface);

    HWND hwnd = g_GameHwnd ? g_GameHwnd : g_D3D8GameHwnd;
    RECT clientRect = {};
    if (!hwnd || !::GetClientRect(hwnd, &clientRect))
        return;

    int w = 0;
    int h = 0;
    if (!TryGetObservedDrawObjectSize(imageObj, &w, &h))
        return;

    const int clientW = clientRect.right - clientRect.left;
    const int clientH = clientRect.bottom - clientRect.top;
    WORD variantType = VT_EMPTY;
    const int alpha = ExtractObservedDrawAlpha(variantLikeAlpha, &variantType);
    const bool coversViewport =
        x <= 16 &&
        y <= 16 &&
        x + w >= clientW - 16 &&
        y + h >= clientH - 16 &&
        w >= clientW - 32 &&
        h >= clientH - 32;
    const bool nearFullscreen =
        x <= 64 &&
        y <= 64 &&
        x + w >= clientW - 64 &&
        y + h >= clientH - 64 &&
        w >= ((clientW * 3) / 4) &&
        h >= ((clientH * 3) / 4);
    if (nearFullscreen)
    {
        static DWORD s_lastNearFullscreenDrawLogTick = 0;
        const DWORD nowTick = GetTickCount();
        if (nowTick - s_lastNearFullscreenDrawLogTick > 250)
        {
            s_lastNearFullscreenDrawLogTick = nowTick;
            WriteLogFmt(
                "[ObservedSceneFadeNearFullscreen] imageObj=0x%08X rect=(%d,%d,%d,%d) size=%dx%d alpha=%d vt=0x%04X raw=[0x%08X,0x%08X,0x%08X,0x%08X] client=%dx%d",
                imageObj,
                x,
                y,
                x + w,
                y + h,
                w,
                h,
                alpha,
                static_cast<unsigned int>(variantType),
                variantLikeAlpha ? variantLikeAlpha[0] : 0u,
                variantLikeAlpha ? variantLikeAlpha[1] : 0u,
                variantLikeAlpha ? variantLikeAlpha[2] : 0u,
                variantLikeAlpha ? variantLikeAlpha[3] : 0u,
                clientW,
                clientH);
        }
    }
    if (coversViewport)
    {
        static DWORD s_lastFullscreenDrawLogTick = 0;
        const DWORD nowTick = GetTickCount();
        if (nowTick - s_lastFullscreenDrawLogTick > 250)
        {
            s_lastFullscreenDrawLogTick = nowTick;
            WriteLogFmt("[ObservedSceneFadeCandidate] imageObj=0x%08X rect=(%d,%d,%d,%d) size=%dx%d alpha=%d vt=0x%04X client=%dx%d",
                imageObj,
                x,
                y,
                x + w,
                y + h,
                w,
                h,
                alpha,
                static_cast<unsigned int>(variantType),
                clientW,
                clientH);
        }
    }

    SkillOverlayBridgeObserveSceneFadeCandidate(imageObj, x, y, w, h, alpha, clientW, clientH);

    POINT mousePt = {};
    if (::GetCursorPos(&mousePt) && ::ScreenToClient(hwnd, &mousePt))
    {
        const bool nearMouse =
            w <= 96 &&
            h <= 96 &&
            abs(x - mousePt.x) <= 48 &&
            abs(y - mousePt.y) <= 48;
        if (nearMouse)
        {
            static DWORD s_lastMouseDrawLogTick = 0;
            const DWORD nowTick = GetTickCount();
            if (nowTick - s_lastMouseDrawLogTick > 100)
            {
                s_lastMouseDrawLogTick = nowTick;
                WriteLogFmt("[ObservedCursorDraw] imageObj=0x%08X pos=(%d,%d) size=%dx%d alpha=%d mouse=(%d,%d)",
                    imageObj,
                    x,
                    y,
                    w,
                    h,
                    alpha,
                    mousePt.x,
                    mousePt.y);
            }
        }
    }
}

static int __fastcall hkSurfaceDrawImage(void *surface, void * /*edxUnused*/, int x, int y, int imageObj, DWORD *variantLikeAlpha)
{
    ObserveSurfaceDrawImageCall(surface, x, y, imageObj, variantLikeAlpha);
    return oSurfaceDrawImageFn
        ? oSurfaceDrawImageFn(surface, x, y, imageObj, variantLikeAlpha)
        : 0;
}

static char __cdecl hkNativeCursorStateSetHandler(uintptr_t thisPtr, unsigned int requestedState)
{
    const char result = oNativeCursorStateSetFn
        ? oNativeCursorStateSetFn(thisPtr, requestedState)
        : 0;

    int currentState = -1;
    uintptr_t currentHandle = 0;
    if (thisPtr && !SafeIsBadReadPtr((void*)(thisPtr + 0x9C8), 4))
    {
        currentState = *(int*)(thisPtr + 0x9C4);
        currentHandle = *(uintptr_t*)(thisPtr + 0x978);
    }

    SkillOverlayBridgeSetObservedNativeCursorState(currentState);

    static int s_lastLoggedState = -9999;
    static uintptr_t s_lastLoggedHandle = 0;
    static DWORD s_lastCursorStateLogTick = 0;
    const DWORD nowTick = GetTickCount();
    if (currentState != s_lastLoggedState ||
        currentHandle != s_lastLoggedHandle ||
        nowTick - s_lastCursorStateLogTick > 1000)
    {
        s_lastLoggedState = currentState;
        s_lastLoggedHandle = currentHandle;
        s_lastCursorStateLogTick = nowTick;
        WriteLogFmt("[ObservedCursorState] req=%u current=%d manager=0x%08X handle=0x%08X result=%d",
            requestedState,
            currentState,
            (DWORD)thisPtr,
            (DWORD)currentHandle,
            (int)result);
    }

    return result;
}

__declspec(naked) static void hkNativeCursorStateSetNaked()
{
    __asm {
        mov eax, [esp + 4]
        push eax
        push ecx
        call hkNativeCursorStateSetHandler
        add esp, 8
        ret 4
    }
}

static bool IsAbilityRedHashReturnAddressOfInterest(DWORD returnAddr)
{
    return (returnAddr >= 0x009F5000 && returnAddr < 0x009F5600) ||
           (returnAddr >= 0x00AE4000 && returnAddr < 0x00AE7800);
}

static void ReadAbilityRedHashContainerMeta(
    uintptr_t thisPtr,
    DWORD *bucketBase,
    DWORD *bucketCount,
    DWORD *entryCount)
{
    if (bucketBase)
        *bucketBase = 0;
    if (bucketCount)
        *bucketCount = 0;
    if (entryCount)
        *entryCount = 0;
    if (!thisPtr || SafeIsBadReadPtr(reinterpret_cast<void*>(thisPtr + 0x04), 0x0C))
        return;

    if (bucketBase)
        *bucketBase = *reinterpret_cast<DWORD*>(thisPtr + 0x04);
    if (bucketCount)
        *bucketCount = *reinterpret_cast<DWORD*>(thisPtr + 0x08);
    if (entryCount)
        *entryCount = *reinterpret_cast<DWORD*>(thisPtr + 0x0C);
}

static bool ShouldLogAbilityRedHashLookup(DWORD returnAddr, uintptr_t thisPtr, DWORD key)
{
    if (!EnableAbilityRedDiagnosticLogs())
        return false;
    if (!IsAbilityRedHashReturnAddressOfInterest(returnAddr))
        return false;

    const DWORD now = GetTickCount();
    if (g_AbilityRedHashLookupLastCaller == returnAddr &&
        g_AbilityRedHashLookupLastThis == thisPtr &&
        g_AbilityRedHashLookupLastKey == key &&
        now - g_AbilityRedHashLookupLastTick <= 1000)
    {
        return false;
    }

    g_AbilityRedHashLookupLastCaller = returnAddr;
    g_AbilityRedHashLookupLastThis = thisPtr;
    g_AbilityRedHashLookupLastKey = key;
    g_AbilityRedHashLookupLastTick = now;
    return true;
}

static bool ShouldLogAbilityRedHashInsert(DWORD returnAddr, uintptr_t thisPtr, DWORD key, DWORD value)
{
    if (!EnableAbilityRedDiagnosticLogs())
        return false;
    if (!IsAbilityRedHashReturnAddressOfInterest(returnAddr))
        return false;

    const DWORD now = GetTickCount();
    if (g_AbilityRedHashInsertLastCaller == returnAddr &&
        g_AbilityRedHashInsertLastThis == thisPtr &&
        g_AbilityRedHashInsertLastKey == key &&
        g_AbilityRedHashInsertLastValue == value &&
        now - g_AbilityRedHashInsertLastTick <= 1000)
    {
        return false;
    }

    g_AbilityRedHashInsertLastCaller = returnAddr;
    g_AbilityRedHashInsertLastThis = thisPtr;
    g_AbilityRedHashInsertLastKey = key;
    g_AbilityRedHashInsertLastValue = value;
    g_AbilityRedHashInsertLastTick = now;
    return true;
}

static bool ShouldLogAbilityRedExtendedAggregate(DWORD returnAddr)
{
    if (!EnableAbilityRedDiagnosticLogs())
        return false;
    const DWORD now = GetTickCount();
    if (g_AbilityRedExtendedAggregateLastCaller == returnAddr &&
        now - g_AbilityRedExtendedAggregateLastTick <= 1000)
    {
        return false;
    }

    g_AbilityRedExtendedAggregateLastCaller = returnAddr;
    g_AbilityRedExtendedAggregateLastTick = now;
    return true;
}

static bool ShouldLogAbilityRedMasterAggregate(DWORD returnAddr)
{
    if (!EnableAbilityRedDiagnosticLogs())
        return false;
    const DWORD now = GetTickCount();
    if (g_AbilityRedMasterAggregateLastCaller == returnAddr &&
        now - g_AbilityRedMasterAggregateLastTick <= 1000)
    {
        return false;
    }

    g_AbilityRedMasterAggregateLastCaller = returnAddr;
    g_AbilityRedMasterAggregateLastTick = now;
    return true;
}

static unsigned int RotL32(unsigned int value, unsigned int count)
{
    count &= 31u;
    return (value << count) | (value >> ((32u - count) & 31u));
}

static unsigned int RotR32(unsigned int value, unsigned int count)
{
    count &= 31u;
    return (value >> count) | (value << ((32u - count) & 31u));
}

static int GenerateLocalIndependentPotentialCipherKey()
{
    typedef int (__fastcall *tGenerateCipherKeyFn)(void *seedPtr, void *edxUnused);
    tGenerateCipherKeyFn generateCipherKey = reinterpret_cast<tGenerateCipherKeyFn>(ADDR_4098C0);
    if (!generateCipherKey)
        return 0;
    return generateCipherKey(reinterpret_cast<void*>(ADDR_F631B8), nullptr);
}

static bool ReadEncryptedTripletValue(DWORD *base, size_t keyIndex, int *outValue)
{
    if (!base || !outValue)
        return false;

    const size_t maxIndex = keyIndex + 2;
    if (SafeIsBadReadPtr(base, (maxIndex + 1) * sizeof(DWORD)))
        return false;

    const unsigned int key = static_cast<unsigned int>(base[keyIndex]);
    const unsigned int enc = static_cast<unsigned int>(base[keyIndex + 1]);
    const unsigned int check = static_cast<unsigned int>(base[keyIndex + 2]);
    if (enc + RotR32(key ^ 0xBAADF00Du, 5) != check)
        return false;

    *outValue = static_cast<int>(key ^ RotL32(enc, 5));
    return true;
}

static bool WriteEncryptedTripletValue(DWORD *base, size_t keyIndex, int plainValue)
{
    if (!base)
        return false;

    const size_t maxIndex = keyIndex + 2;
    if (SafeIsBadWritePtr(base, (maxIndex + 1) * sizeof(DWORD)))
        return false;

    const int generatedKey = GenerateLocalIndependentPotentialCipherKey();
    if (generatedKey == 0)
        return false;

    const unsigned int key = static_cast<unsigned int>(generatedKey);
    const unsigned int enc = RotR32(static_cast<unsigned int>(plainValue) ^ key, 5);
    const unsigned int check = enc + RotR32(key ^ 0xBAADF00Du, 5);
    base[keyIndex] = key;
    base[keyIndex + 1] = enc;
    base[keyIndex + 2] = check;
    return true;
}

static void LogAbilityRedDecodedSnapshot(const char *tag)
{
    if (!EnableAbilityRedDiagnosticLogs())
        return;
    const DWORD now = GetTickCount();
    if (now - g_AbilityRedSnapshotLastTick <= 1000)
        return;
    g_AbilityRedSnapshotLastTick = now;

    DWORD *basePrimary = reinterpret_cast<DWORD*>(0x00F6D134);
    DWORD *baseExtended = reinterpret_cast<DWORD*>(0x00F6D200);

    int strVal = 0, dexVal = 0, intVal = 0, lukVal = 0, hpVal = 0, mpVal = 0;
    int watkVal = 0, matkVal = 0, wdefVal = 0, mdefVal = 0, accVal = 0, avoidVal = 0, speedVal = 0, jumpVal = 0;

    ReadEncryptedTripletValue(basePrimary, 9, &strVal);
    ReadEncryptedTripletValue(basePrimary, 12, &dexVal);
    ReadEncryptedTripletValue(basePrimary, 15, &intVal);
    ReadEncryptedTripletValue(basePrimary, 18, &lukVal);
    ReadEncryptedTripletValue(basePrimary, 24, &hpVal);
    ReadEncryptedTripletValue(basePrimary, 27, &mpVal);

    ReadEncryptedTripletValue(baseExtended, 57, &watkVal);
    ReadEncryptedTripletValue(baseExtended, 87, &matkVal);
    ReadEncryptedTripletValue(baseExtended, 72, &wdefVal);
    ReadEncryptedTripletValue(baseExtended, 102, &mdefVal);
    ReadEncryptedTripletValue(baseExtended, 117, &accVal);
    ReadEncryptedTripletValue(baseExtended, 132, &avoidVal);
    ReadEncryptedTripletValue(baseExtended, 159, &speedVal);
    ReadEncryptedTripletValue(baseExtended, 171, &jumpVal);

    WriteLogFmt(
        "[AbilityRedSnapshot] %s primary[str=%d dex=%d int=%d luk=%d hp=%d mp=%d] extended[watk=%d matk=%d wdef=%d mdef=%d acc=%d avoid=%d speed=%d jump=%d] active=%d",
        tag ? tag : "unknown",
        strVal,
        dexVal,
        intVal,
        lukVal,
        hpVal,
        mpVal,
        watkVal,
        matkVal,
        wdefVal,
        mdefVal,
        accVal,
        avoidVal,
        speedVal,
        jumpVal,
        SkillOverlayBridgeHasLocalIndependentPotentialBonuses() ? 1 : 0);
}

static void __cdecl hkApplyLocalIndependentPotentialSkillLevelDisplay(uintptr_t targetPtr)
{
    if (!targetPtr || SafeIsBadWritePtr(reinterpret_cast<void*>(targetPtr), sizeof(int)))
        return;
    if (!SkillOverlayBridgeHasLocalIndependentPotentialBonuses())
        return;
    if (!ShouldApplyLocalIndependentPotentialBurst(targetPtr, &g_LocalIndependentPotentialSkillLevelLastTarget, &g_LocalIndependentPotentialSkillLevelLastTick))
        return;

    const int delta = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x88);
    if (delta == 0)
        return;

    if (kLocalIndependentPotentialDisplayObserveOnly)
    {
        WriteLogFmt("[IndependentBuffLocalDisplay] observe AE0A70 target=0x%08X delta=%d", (DWORD)targetPtr, delta);
        return;
    }

    *reinterpret_cast<int*>(targetPtr) += delta;
    WriteLogFmt("[IndependentBuffLocalDisplay] AE0B23 target=0x%08X delta=%d", (DWORD)targetPtr, delta);
}

static void __cdecl hkApplyLocalIndependentPotentialDamageDisplay(
    uintptr_t critRatePtr,
    uintptr_t option31Ptr,
    uintptr_t damagePtr,
    uintptr_t bossDamagePtr,
    uintptr_t ignoreDefensePtr)
{
    (void)option31Ptr;

    if (!SkillOverlayBridgeHasLocalIndependentPotentialBonuses())
        return;

    const uintptr_t key = critRatePtr ^ (option31Ptr << 1) ^ (damagePtr << 2) ^ (bossDamagePtr << 3) ^ (ignoreDefensePtr << 4);
    if (!ShouldApplyLocalIndependentPotentialBurst(key, &g_LocalIndependentPotentialDamageLastKey, &g_LocalIndependentPotentialDamageLastTick))
        return;

    const struct
    {
        uintptr_t targetPtr;
        int offset;
    } targets[] = {
        { critRatePtr, 0x78 },
        { damagePtr, 0xAC },
        { bossDamagePtr, 0xC4 },
        { ignoreDefensePtr, 0xA0 },
    };

    int appliedValues[4] = {};
    for (int i = 0; i < 4; ++i)
    {
        const uintptr_t targetPtr = targets[i].targetPtr;
        if (!targetPtr || SafeIsBadWritePtr(reinterpret_cast<void*>(targetPtr), sizeof(int)))
            continue;

        const int delta = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(targets[i].offset);
        if (delta == 0)
            continue;

        if (kLocalIndependentPotentialDisplayObserveOnly)
        {
            appliedValues[i] = delta;
            continue;
        }

        *reinterpret_cast<int*>(targetPtr) += delta;
        appliedValues[i] = delta;
    }

    if (appliedValues[0] || appliedValues[1] || appliedValues[2] || appliedValues[3])
    {
        WriteLogFmt(kLocalIndependentPotentialDisplayObserveOnly
                ? "[IndependentBuffLocalDisplay] observe AE0FDC crit=%d damage=%d boss=%d ignore=%d"
                : "[IndependentBuffLocalDisplay] AE0FDC crit=%d damage=%d boss=%d ignore=%d",
            appliedValues[0],
            appliedValues[1],
            appliedValues[2],
            appliedValues[3]);
    }
}

static void __cdecl hkApplyLocalIndependentPotentialPercentQuadDisplay(
    uintptr_t strPtr,
    uintptr_t intPtr,
    uintptr_t dexPtr,
    uintptr_t lukPtr)
{
    if (!SkillOverlayBridgeHasLocalIndependentPotentialBonuses())
        return;

    const uintptr_t key = strPtr ^ (intPtr << 1) ^ (dexPtr << 2) ^ (lukPtr << 3);
    if (!ShouldApplyLocalIndependentPotentialBurst(key, &g_LocalIndependentPotentialPercentQuadLastKey, &g_LocalIndependentPotentialPercentQuadLastTick))
        return;

    const struct
    {
        uintptr_t targetPtr;
        int offset;
    } targets[] = {
        { strPtr, 0x48 },
        { intPtr, 0x50 },
        { dexPtr, 0x4C },
        { lukPtr, 0x54 },
    };

    int appliedValues[4] = {};
    for (int i = 0; i < 4; ++i)
    {
        if (!targets[i].targetPtr || SafeIsBadWritePtr(reinterpret_cast<void*>(targets[i].targetPtr), sizeof(DWORD)))
            continue;

        const int delta = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(targets[i].offset);
        if (delta == 0)
            continue;

        if (kLocalIndependentPotentialDisplayObserveOnly)
        {
            appliedValues[i] = delta;
            continue;
        }

        *reinterpret_cast<DWORD*>(targets[i].targetPtr) += delta;
        appliedValues[i] = delta;
    }

    if (appliedValues[0] || appliedValues[1] || appliedValues[2] || appliedValues[3])
    {
        WriteLogFmt(kLocalIndependentPotentialDisplayObserveOnly
                ? "[IndependentBuffLocalDisplay] observe 8538C0 str=%d int=%d dex=%d luk=%d"
                : "[IndependentBuffLocalDisplay] 8538C0 str=%d int=%d dex=%d luk=%d",
            appliedValues[0],
            appliedValues[1],
            appliedValues[2],
            appliedValues[3]);
    }
}

static void __cdecl hkApplyLocalIndependentPotentialPercentFullDisplay(uintptr_t valuesPtr)
{
    if (!valuesPtr || SafeIsBadWritePtr(reinterpret_cast<void*>(valuesPtr), 6 * sizeof(DWORD)))
        return;
    if (!SkillOverlayBridgeHasLocalIndependentPotentialBonuses())
        return;
    if (!ShouldApplyLocalIndependentPotentialBurst(valuesPtr, &g_LocalIndependentPotentialPercentFullLastKey, &g_LocalIndependentPotentialPercentFullLastTick))
        return;

    DWORD *values = reinterpret_cast<DWORD*>(valuesPtr);
    const int offsets[] = { 0x48, 0x4C, 0x50, 0x54, 0x58, 0x5C };
    int appliedValues[6] = {};
    for (int i = 0; i < 6; ++i)
    {
        const int delta = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(offsets[i]);
        if (delta == 0)
            continue;
        if (kLocalIndependentPotentialDisplayObserveOnly)
        {
            appliedValues[i] = delta;
            continue;
        }
        values[i] += delta;
        appliedValues[i] = delta;
    }

    if (appliedValues[0] || appliedValues[1] || appliedValues[2] || appliedValues[3] || appliedValues[4] || appliedValues[5])
    {
        WriteLogFmt(kLocalIndependentPotentialDisplayObserveOnly
                ? "[IndependentBuffLocalDisplay] observe 853E10 str=%d dex=%d int=%d luk=%d hp=%d mp=%d"
                : "[IndependentBuffLocalDisplay] 853E10 str=%d dex=%d int=%d luk=%d hp=%d mp=%d",
            appliedValues[0],
            appliedValues[1],
            appliedValues[2],
            appliedValues[3],
            appliedValues[4],
            appliedValues[5]);
    }
}

static void __cdecl hkApplyLocalIndependentPotentialFlatBasicDisplay(uintptr_t thisPtr)
{
    if (!thisPtr)
        return;
    if (!SkillOverlayBridgeHasLocalIndependentPotentialBonuses())
        return;
    if (!ShouldApplyLocalIndependentPotentialBurst(thisPtr, &g_LocalIndependentPotentialFlatBasicLastKey, &g_LocalIndependentPotentialFlatBasicLastTick))
        return;

    DWORD *values = reinterpret_cast<DWORD*>(thisPtr);
    const struct
    {
        size_t keyIndex;
        int offset;
    } targets[] = {
        { 9,  0x08 }, // STR
        { 12, 0x0C }, // DEX
        { 15, 0x10 }, // INT
        { 18, 0x14 }, // LUK
        { 24, 0x20 }, // MAXHP
        { 27, 0x24 }, // MAXMP
    };

    int appliedCount = 0;
    for (int i = 0; i < (int)ARRAYSIZE(targets); ++i)
    {
        const int delta = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(targets[i].offset);
        if (delta == 0)
            continue;

        if (kLocalIndependentPotentialDisplayObserveOnly)
        {
            ++appliedCount;
            continue;
        }

        int currentValue = 0;
        if (!ReadEncryptedTripletValue(values, targets[i].keyIndex, &currentValue))
            continue;
        if (!WriteEncryptedTripletValue(values, targets[i].keyIndex, currentValue + delta))
            continue;
        ++appliedCount;
    }

    if (appliedCount > 0)
        WriteLogFmt(kLocalIndependentPotentialDisplayObserveOnly
                ? "[IndependentBuffLocalDisplay] observe 853B00 applied=%d this=0x%08X"
                : "[IndependentBuffLocalDisplay] 853B00 applied=%d this=0x%08X",
            appliedCount,
            (DWORD)thisPtr);
}

static void __cdecl hkApplyLocalIndependentPotentialFlatExtendedDisplay(uintptr_t thisPtr)
{
    if (!thisPtr)
        return;
    if (!SkillOverlayBridgeHasLocalIndependentPotentialBonuses())
        return;
    if (!ShouldApplyLocalIndependentPotentialBurst(thisPtr, &g_LocalIndependentPotentialFlatExtendedLastKey, &g_LocalIndependentPotentialFlatExtendedLastTick))
        return;

    DWORD *values = reinterpret_cast<DWORD*>(thisPtr);
    const struct
    {
        size_t keyIndex;
        int offset;
    } targets[] = {
        { 117, 0x28 }, // ACC
        { 132, 0x2C }, // AVOID
        { 159, 0x30 }, // SPEED
        { 171, 0x34 }, // JUMP
        { 57,  0x38 }, // WATK
        { 87,  0x3C }, // MATK
        { 72,  0x40 }, // WDEF
        { 102, 0x44 }, // MDEF
        { 1712,0xC8 }, // CRIT MIN
        { 1724,0xCC }, // CRIT MAX
        { 1736,0xD0 }, // TER
        { 1748,0xD4 }, // ASR
    };

    int appliedCount = 0;
    for (int i = 0; i < (int)ARRAYSIZE(targets); ++i)
    {
        const int delta = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(targets[i].offset);
        if (delta == 0)
            continue;

        if (kLocalIndependentPotentialDisplayObserveOnly)
        {
            ++appliedCount;
            continue;
        }

        int currentValue = 0;
        if (!ReadEncryptedTripletValue(values, targets[i].keyIndex, &currentValue))
            continue;
        if (!WriteEncryptedTripletValue(values, targets[i].keyIndex, currentValue + delta))
            continue;
        ++appliedCount;
    }

    if (appliedCount > 0)
        WriteLogFmt(kLocalIndependentPotentialDisplayObserveOnly
                ? "[IndependentBuffLocalDisplay] observe 856830 applied=%d this=0x%08X"
                : "[IndependentBuffLocalDisplay] 856830 applied=%d this=0x%08X",
            appliedCount,
            (DWORD)thisPtr);
}

static void __cdecl hkObserveAbilityRedDisplayCandidate(
    uintptr_t thisPtr,
    DWORD callerRet,
    int resultValue,
    DWORD arg1,
    DWORD arg2,
    DWORD arg3,
    DWORD arg4,
    DWORD arg5,
    DWORD arg6,
    DWORD arg7,
    DWORD ptrMaskBefore,
    const DWORD *ptrValuesBefore,
    DWORD ptrMaskAfter,
    const DWORD *ptrValuesAfter)
{
    if (!EnableAbilityRedDiagnosticLogs())
        return;
    const DWORD now = GetTickCount();
    if (g_AbilityRedDisplayCandidateLastThis == thisPtr &&
        now - g_AbilityRedDisplayCandidateLastTick <= 1000)
    {
        return;
    }

    g_AbilityRedDisplayCandidateLastThis = thisPtr;
    g_AbilityRedDisplayCandidateLastTick = now;

    DWORD vtable = 0;
    DWORD vtD8 = 0;
    DWORD field04 = 0;
    DWORD field08 = 0;
    DWORD field0C = 0;
    DWORD field10 = 0;
    DWORD field14 = 0;
    DWORD field18 = 0;

    if (thisPtr && !SafeIsBadReadPtr(reinterpret_cast<void*>(thisPtr), 0x1C))
    {
        vtable = *reinterpret_cast<DWORD*>(thisPtr + 0x00);
        field04 = *reinterpret_cast<DWORD*>(thisPtr + 0x04);
        field08 = *reinterpret_cast<DWORD*>(thisPtr + 0x08);
        field0C = *reinterpret_cast<DWORD*>(thisPtr + 0x0C);
        field10 = *reinterpret_cast<DWORD*>(thisPtr + 0x10);
        field14 = *reinterpret_cast<DWORD*>(thisPtr + 0x14);
        field18 = *reinterpret_cast<DWORD*>(thisPtr + 0x18);
        if (vtable && !SafeIsBadReadPtr(reinterpret_cast<void*>(vtable + 0xD8), 4))
            vtD8 = *reinterpret_cast<DWORD*>(vtable + 0xD8);
    }

    WriteLogFmt(
        "[AbilityRedDisplay] AE0E60 caller=0x%08X this=0x%08X vt=0x%08X vtD8=0x%08X result=%d args=[0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X] ptrBefore(mask=0x%02X)=[0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X] ptrAfter(mask=0x%02X)=[0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X] fields=[0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X]",
        callerRet,
        (DWORD)thisPtr,
        vtable,
        vtD8,
        resultValue,
        arg1,
        arg2,
        arg3,
        arg4,
        arg5,
        arg6,
        arg7,
        ptrMaskBefore,
        ptrValuesBefore ? ptrValuesBefore[0] : 0,
        ptrValuesBefore ? ptrValuesBefore[1] : 0,
        ptrValuesBefore ? ptrValuesBefore[2] : 0,
        ptrValuesBefore ? ptrValuesBefore[3] : 0,
        ptrValuesBefore ? ptrValuesBefore[4] : 0,
        ptrValuesBefore ? ptrValuesBefore[5] : 0,
        ptrValuesBefore ? ptrValuesBefore[6] : 0,
        ptrMaskAfter,
        ptrValuesAfter ? ptrValuesAfter[0] : 0,
        ptrValuesAfter ? ptrValuesAfter[1] : 0,
        ptrValuesAfter ? ptrValuesAfter[2] : 0,
        ptrValuesAfter ? ptrValuesAfter[3] : 0,
        ptrValuesAfter ? ptrValuesAfter[4] : 0,
        ptrValuesAfter ? ptrValuesAfter[5] : 0,
        ptrValuesAfter ? ptrValuesAfter[6] : 0,
        field04,
        field08,
        field0C,
        field10,
        field14,
        field18);
}

static DWORD ReadAbilityRedDisplayPointerValue(DWORD candidatePtr, DWORD bitMask, DWORD *maskOut)
{
    if (maskOut && candidatePtr && !SafeIsBadReadPtr(reinterpret_cast<void*>(candidatePtr), sizeof(DWORD)))
    {
        *maskOut |= bitMask;
        return *reinterpret_cast<DWORD*>(candidatePtr);
    }
    return 0;
}

static bool ShouldLogAbilityRedFinalCalculator(
    DWORD *lastCaller,
    uintptr_t *lastThis,
    DWORD *lastTick,
    int *lastActive,
    DWORD returnAddr,
    uintptr_t thisPtr,
    int activeState)
{
    if (!EnableAbilityRedDiagnosticLogs())
        return false;
    if (!lastCaller || !lastThis || !lastTick || !lastActive)
        return true;

    const DWORD now = GetTickCount();
    if (*lastCaller == returnAddr &&
        *lastThis == thisPtr &&
        *lastActive == activeState &&
        now - *lastTick <= 1000)
    {
        return false;
    }

    *lastCaller = returnAddr;
    *lastThis = thisPtr;
    *lastActive = activeState;
    *lastTick = now;
    return true;
}

static int __fastcall hkAbilityRedDisplayCandidateFunction(
    void *thisPtr,
    void *edxUnused,
    DWORD arg1,
    DWORD arg2,
    DWORD arg3,
    DWORD arg4,
    DWORD arg5,
    DWORD arg6,
    DWORD arg7)
{
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    const DWORD args[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    DWORD ptrMaskBefore = 0;
    DWORD ptrValuesBefore[7] = {};
    for (int i = 0; i < 7; ++i)
        ptrValuesBefore[i] = ReadAbilityRedDisplayPointerValue(args[i], (1u << i), &ptrMaskBefore);

    const int resultValue = oAbilityRedDisplayCandidateFn
        ? oAbilityRedDisplayCandidateFn(thisPtr, edxUnused, arg1, arg2, arg3, arg4, arg5, arg6, arg7)
        : 0;

    DWORD ptrMaskAfter = 0;
    DWORD ptrValuesAfter[7] = {};
    for (int i = 0; i < 7; ++i)
        ptrValuesAfter[i] = ReadAbilityRedDisplayPointerValue(args[i], (1u << i), &ptrMaskAfter);

    hkObserveAbilityRedDisplayCandidate(
        reinterpret_cast<uintptr_t>(thisPtr),
        callerRet,
        resultValue,
        arg1,
        arg2,
        arg3,
        arg4,
        arg5,
        arg6,
        arg7,
        ptrMaskBefore,
        ptrValuesBefore,
        ptrMaskAfter,
        ptrValuesAfter);
    return resultValue;
}

static int __fastcall hkAbilityRedExtendedAggregateFunction(
    void *thisPtr,
    void *edxUnused,
    DWORD arg1,
    DWORD arg2,
    DWORD arg3)
{
    (void)edxUnused;
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    DWORD before[6] = {};
    DWORD after[6] = {};
    if (arg3 && !SafeIsBadReadPtr(reinterpret_cast<void*>(arg3), sizeof(before)))
        memcpy(before, reinterpret_cast<void*>(arg3), sizeof(before));

    const int resultValue = oAbilityRedExtendedAggregateFn
        ? oAbilityRedExtendedAggregateFn(thisPtr, edxUnused, arg1, arg2, arg3)
        : 0;

    if (arg3 && !SafeIsBadReadPtr(reinterpret_cast<void*>(arg3), sizeof(after)))
        memcpy(after, reinterpret_cast<void*>(arg3), sizeof(after));

    if (ShouldLogAbilityRedExtendedAggregate(callerRet))
    {
        WriteLogFmt(
            "[AbilityRedAggregate] 856BA0 caller=0x%08X this=0x%08X arg1=0x%08X arg2=0x%08X out=0x%08X result=%d before=[%u,%u,%u,%u,%u,%u] after=[%u,%u,%u,%u,%u,%u] active=%d",
            callerRet,
            (DWORD)(uintptr_t)thisPtr,
            arg1,
            arg2,
            arg3,
            resultValue,
            before[0], before[1], before[2], before[3], before[4], before[5],
            after[0], after[1], after[2], after[3], after[4], after[5],
            SkillOverlayBridgeHasLocalIndependentPotentialBonuses() ? 1 : 0);
    }

    return resultValue;
}

static void ReadAbilityRedMasterAggregateBuffer(DWORD ptr, DWORD *outValues, size_t count)
{
    if (!outValues || count == 0)
        return;
    for (size_t i = 0; i < count; ++i)
        outValues[i] = 0;
    if (!ptr || SafeIsBadReadPtr(reinterpret_cast<void*>(ptr), count * sizeof(DWORD)))
        return;
    memcpy(outValues, reinterpret_cast<void*>(ptr), count * sizeof(DWORD));
}

static void DecodeAbilityRedMasterTripletPair(
    DWORD ptr,
    DWORD *rawValues,
    int *decodedA,
    int *decodedB,
    bool *okA,
    bool *okB)
{
    if (decodedA)
        *decodedA = 0;
    if (decodedB)
        *decodedB = 0;
    if (okA)
        *okA = false;
    if (okB)
        *okB = false;

    DWORD localRaw[6] = {};
    if (rawValues)
        memcpy(localRaw, rawValues, sizeof(localRaw));
    else if (ptr && !SafeIsBadReadPtr(reinterpret_cast<void*>(ptr), sizeof(localRaw)))
        memcpy(localRaw, reinterpret_cast<void*>(ptr), sizeof(localRaw));
    else
        return;

    int valueA = 0;
    int valueB = 0;
    const bool localOkA = ReadEncryptedTripletValue(localRaw, 0, &valueA);
    const bool localOkB = ReadEncryptedTripletValue(localRaw, 3, &valueB);

    if (decodedA)
        *decodedA = valueA;
    if (decodedB)
        *decodedB = valueB;
    if (okA)
        *okA = localOkA;
    if (okB)
        *okB = localOkB;
}

static void DecodeAbilityRedMasterDefenseValues(
    DWORD ptr,
    int *wdefValue,
    int *mdefValue,
    bool *wdefOk,
    bool *mdefOk)
{
    if (wdefValue)
        *wdefValue = 0;
    if (mdefValue)
        *mdefValue = 0;
    if (wdefOk)
        *wdefOk = false;
    if (mdefOk)
        *mdefOk = false;
    if (!ptr)
        return;

    int localWdef = 0;
    int localMdef = 0;
    const bool localWdefOk = ReadEncryptedTripletValue(reinterpret_cast<DWORD*>(ptr), 72, &localWdef);
    const bool localMdefOk = ReadEncryptedTripletValue(reinterpret_cast<DWORD*>(ptr), 102, &localMdef);

    if (wdefValue)
        *wdefValue = localWdef;
    if (mdefValue)
        *mdefValue = localMdef;
    if (wdefOk)
        *wdefOk = localWdefOk;
    if (mdefOk)
        *mdefOk = localMdefOk;
}

static void DecodeAbilityRedTripletAtOffset(
    uintptr_t thisPtr,
    size_t byteOffset,
    int *outValue,
    bool *outOk)
{
    if (outValue)
        *outValue = 0;
    if (outOk)
        *outOk = false;
    if (!thisPtr)
        return;

    DWORD *base = reinterpret_cast<DWORD*>(thisPtr + byteOffset);
    int localValue = 0;
    const bool localOk = ReadEncryptedTripletValue(base, 0, &localValue);
    if (outValue)
        *outValue = localValue;
    if (outOk)
        *outOk = localOk;
}

struct AbilityRedMovementDiagnosis
{
    int mountItemIdFromA4 = 0;
    int mountItemIdFromUser = 0;
    int speedSourceAdd = 0;
    int speedCapBase = 0;
    int speedCapOverride = 0;
    int currentSpeed = 0;
    int currentJump = 0;
    bool mountItemIdFromA4Ok = false;
    bool mountItemIdFromUserOk = false;
    bool speedSourceAddOk = false;
    bool speedCapBaseOk = false;
    bool speedCapOverrideOk = false;
    bool currentSpeedOk = false;
    bool currentJumpOk = false;
};

static int ComputeAbilityRedMovementFinalCap(
    const AbilityRedMovementDiagnosis *diag,
    bool *outOk)
{
    if (outOk)
        *outOk = false;
    if (!diag)
        return 0;

    if (diag->speedCapOverrideOk && diag->speedCapOverride != 0)
    {
        if (outOk)
            *outOk = true;
        return diag->speedCapOverride;
    }

    if (diag->speedCapBaseOk)
    {
        if (outOk)
            *outOk = true;
        return diag->speedCapBase + 140;
    }

    return 0;
}

static void CollectAbilityRedMovementDiagnosis(
    void *thisPtr,
    DWORD playerObjArg,
    DWORD capArg,
    AbilityRedMovementDiagnosis *outDiag)
{
    if (!outDiag)
        return;
    *outDiag = AbilityRedMovementDiagnosis();

    int mountItemId = 0;
    if (TryReadMountItemIdFromPlayerObject(
            reinterpret_cast<void *>(static_cast<uintptr_t>(playerObjArg)),
            &mountItemId))
    {
        outDiag->mountItemIdFromA4 = mountItemId;
        outDiag->mountItemIdFromA4Ok = true;
    }

    mountItemId = 0;
    if (TryReadCurrentUserMountItemId(&mountItemId))
    {
        outDiag->mountItemIdFromUser = mountItemId;
        outDiag->mountItemIdFromUserOk = true;
    }

    const tAbilityRedMovementSpeedSourceFn speedSourceFn =
        reinterpret_cast<tAbilityRedMovementSpeedSourceFn>(ADDR_804550);
    const tAbilityRedMovementSpeedCapBaseFn speedCapBaseFn =
        reinterpret_cast<tAbilityRedMovementSpeedCapBaseFn>(ADDR_82C700);
    const tAbilityRedMovementCapOverrideFn speedCapOverrideFn =
        reinterpret_cast<tAbilityRedMovementCapOverrideFn>(ADDR_8213D0);
    const tAbilityRedMovementValueFn currentSpeedFn =
        reinterpret_cast<tAbilityRedMovementValueFn>(ADDR_8222B0);
    const tAbilityRedMovementValueFn currentJumpFn =
        reinterpret_cast<tAbilityRedMovementValueFn>(ADDR_8223F0);

    __try
    {
        if (playerObjArg && speedSourceFn)
        {
            outDiag->speedSourceAdd = speedSourceFn(static_cast<int>(playerObjArg));
            outDiag->speedSourceAddOk = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    __try
    {
        if (playerObjArg && speedCapBaseFn)
        {
            outDiag->speedCapBase = speedCapBaseFn(static_cast<int>(playerObjArg));
            outDiag->speedCapBaseOk = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    __try
    {
        if (capArg && speedCapOverrideFn)
        {
            outDiag->speedCapOverride =
                speedCapOverrideFn(reinterpret_cast<void *>(static_cast<uintptr_t>(capArg)));
            outDiag->speedCapOverrideOk = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    __try
    {
        if (thisPtr && currentSpeedFn)
        {
            outDiag->currentSpeed = currentSpeedFn(thisPtr);
            outDiag->currentSpeedOk = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    __try
    {
        if (thisPtr && currentJumpFn)
        {
            outDiag->currentJump = currentJumpFn(thisPtr);
            outDiag->currentJumpOk = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static const DWORD kMovementSetterProtectWindowMs = 1500;
static const int kMovementSpeedProtectHighValueThreshold = 160;
static const int kMovementJumpProtectHighValueThreshold = 123;
static const int kMovementSetterPreserveMinValue = 100;
static const int kMovementSpeedProtectLegacyRewriteThreshold = 220;
static const int kMovementJumpProtectLegacyRewriteThreshold = 140;
static const bool kEnableGlobalMovementOutputClampHook = true;
static const DWORD kMountedFlightCruiseMinActiveMs = 120;
static const DWORD kMountedSoaringFlightActiveRefreshGapMs = 3000;
static const DWORD kMountedSoaringFlightActiveTimeoutMs = 10000;
static volatile LONG g_MovementSetterLastHighSpeedValue = 0;
static volatile LONG g_MovementSetterLastHighJumpValue = 0;
static volatile LONG g_MovementSetterLastHighSpeedTick = 0;
static volatile LONG g_MovementSetterLastHighJumpTick = 0;
static volatile LONG g_MovementSetterLastHighSpeedThisPtr = 0;
static volatile LONG g_MovementSetterLastHighJumpThisPtr = 0;
static volatile LONG g_MovementSetterLastHighSpeedCaller = 0;
static volatile LONG g_MovementSetterLastHighJumpCaller = 0;
static volatile LONG g_MovementSetterSpeedLogBudget = 24;
static volatile LONG g_MovementSetterJumpLogBudget = 24;
static volatile LONG g_MovementOutputClampLogBudget = 24;

static bool ShouldUseMountedFlightMovementSetterProtection()
{
    int mountItemId = 0;
    if (!TryResolveCurrentUserMountItemIdWithFallback(&mountItemId, nullptr) ||
        !IsExtendedMountSoaringContextMount(mountItemId))
    {
        return false;
    }

    const MountedFlightPhysicsScaleSample sample = g_MountedFlightPhysicsScaleSample;
    if (sample.mountItemId <= 0 ||
        sample.mountItemId != mountItemId ||
        sample.tick == 0)
    {
        return false;
    }

    return (sample.overrideSwim > sample.nativeSwim && sample.nativeSwim > 0.0) ||
           (sample.overrideFs > sample.nativeFs && sample.nativeFs > 0.0);
}

static int MaybePreserveMovementSetterHighValue(
    const char *tag,
    void *thisPtr,
    int value,
    int highValueThreshold,
    int legacyRewriteThreshold,
    volatile LONG *lastHighValue,
    volatile LONG *lastHighTick,
    volatile LONG *lastHighThisPtr,
    volatile LONG *lastHighCaller,
    volatile LONG *logBudget)
{
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    const DWORD nowTick = GetTickCount();
    const DWORD thisAddr = (DWORD)(uintptr_t)thisPtr;
    if (!ShouldUseMountedFlightMovementSetterProtection())
    {
        return value;
    }

    if (value > highValueThreshold)
    {
        InterlockedExchange(lastHighValue, value);
        InterlockedExchange(lastHighTick, static_cast<LONG>(nowTick));
        InterlockedExchange(lastHighThisPtr, static_cast<LONG>(thisAddr));
        InterlockedExchange(lastHighCaller, static_cast<LONG>(callerRet));
        if (logBudget && InterlockedDecrement(logBudget) >= 0)
        {
            WriteLogFmt(
                "[MoveSetter] capture %s this=0x%08X high=%d caller=0x%08X",
                tag ? tag : "?",
                thisAddr,
                value,
                callerRet);
        }
        return value;
    }

    const int lastHigh = InterlockedCompareExchange(lastHighValue, 0, 0);
    const DWORD lastTick = static_cast<DWORD>(InterlockedCompareExchange(lastHighTick, 0, 0));
    const DWORD lastThis = static_cast<DWORD>(InterlockedCompareExchange(lastHighThisPtr, 0, 0));
    const DWORD lastCaller = static_cast<DWORD>(InterlockedCompareExchange(lastHighCaller, 0, 0));
    if (lastHigh <= 0 || lastTick == 0 || lastThis == 0)
    {
        return value;
    }

    if (nowTick - lastTick > kMovementSetterProtectWindowMs || lastThis != thisAddr)
    {
        return value;
    }

    if (value < kMovementSetterPreserveMinValue || value > legacyRewriteThreshold || lastHigh <= value)
    {
        return value;
    }

    if (logBudget && InterlockedDecrement(logBudget) >= 0)
    {
        WriteLogFmt(
            "[MoveSetter] preserve %s this=0x%08X low=%d caller=0x%08X recentHigh=%d highCaller=0x%08X age=%u",
            tag ? tag : "?",
            thisAddr,
            value,
            callerRet,
            lastHigh,
            lastCaller,
            nowTick - lastTick);
    }
    return lastHigh;
}

static int __fastcall hkAbilityRedMovementSpeedSetter831F00(void *thisPtr, int value)
{
    const int patchedValue = MaybePreserveMovementSetterHighValue(
        "speed",
        thisPtr,
        value,
        kMovementSpeedProtectHighValueThreshold,
        kMovementSpeedProtectLegacyRewriteThreshold,
        &g_MovementSetterLastHighSpeedValue,
        &g_MovementSetterLastHighSpeedTick,
        &g_MovementSetterLastHighSpeedThisPtr,
        &g_MovementSetterLastHighSpeedCaller,
        &g_MovementSetterSpeedLogBudget);
    return oAbilityRedMovementSpeedSetter831F00Fn
        ? oAbilityRedMovementSpeedSetter831F00Fn(thisPtr, patchedValue)
        : patchedValue;
}

static int __fastcall hkAbilityRedMovementJumpSetter832000(void *thisPtr, int value)
{
    const int patchedValue = MaybePreserveMovementSetterHighValue(
        "jump",
        thisPtr,
        value,
        kMovementJumpProtectHighValueThreshold,
        kMovementJumpProtectLegacyRewriteThreshold,
        &g_MovementSetterLastHighJumpValue,
        &g_MovementSetterLastHighJumpTick,
        &g_MovementSetterLastHighJumpThisPtr,
        &g_MovementSetterLastHighJumpCaller,
        &g_MovementSetterJumpLogBudget);
    return oAbilityRedMovementJumpSetter832000Fn
        ? oAbilityRedMovementJumpSetter832000Fn(thisPtr, patchedValue)
        : patchedValue;
}

static LONG __cdecl hkMovementOutputClampComputeB93B80(
    DWORD *a1,
    int a2,
    int a3,
    int a4,
    int a5,
    int a6,
    int a7,
    int *a8,
    int *a9,
    double *a10,
    DWORD *a11)
{
    const LONG result = oMovementOutputClampComputeB93B80Fn
        ? oMovementOutputClampComputeB93B80Fn(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11)
        : 0;
    if (!a8 || SafeIsBadWritePtr(a8, sizeof(int)))
        return result;

    int contextualPlayerMountItemId = 0;
    const bool contextualPlayerMountReadable =
        TryReadMountItemIdFromPlayerObjectRaw(
            reinterpret_cast<void *>(static_cast<uintptr_t>(a4)),
            &contextualPlayerMountItemId);

    int mountedRawMountItemId = 0;
    int mountedRawDataKey = 0;
    int mountedRawSpeed = 0;
    int mountedRawJump = 0;
    bool hasMountedRawSample =
        TryGetRecentMountedMovementRawSample(
            &mountedRawMountItemId,
            &mountedRawDataKey,
            &mountedRawSpeed,
            &mountedRawJump,
            1200);
    if (contextualPlayerMountReadable &&
        (contextualPlayerMountItemId <= 0 ||
         (hasMountedRawSample && contextualPlayerMountItemId != mountedRawMountItemId)))
    {
        // B93B80 is still hit for a few frames after dismount. Use the current
        // player-object mount field as the earliest "we are on foot now"
        // signal and drop the cached mounted raw sample immediately so the
        // clamp hook stops reapplying mounted speed/jump on foot.
        ClearRecentMountedMovementRawSample();
        hasMountedRawSample = false;
        mountedRawMountItemId = 0;
        mountedRawDataKey = 0;
        mountedRawSpeed = 0;
        mountedRawJump = 0;
    }
    MountedMovementOverride mountedRawOverride = {};
    const bool shouldRaiseFromMountedRaw =
        hasMountedRawSample &&
        SkillOverlayBridgeResolveMountedMovementOverride(
            mountedRawMountItemId,
            mountedRawDataKey,
            mountedRawOverride) &&
        mountedRawOverride.matched;

    DWORD *baseExtended = reinterpret_cast<DWORD *>(0x00F6D200);
    int decodedSpeed = 0;
    int decodedJump = 0;
    const bool hasSpeed = ReadEncryptedTripletValue(baseExtended, 159, &decodedSpeed);
    const bool hasJump = ReadEncryptedTripletValue(baseExtended, 171, &decodedJump);
    if ((!hasSpeed || decodedSpeed <= kMovementSpeedProtectHighValueThreshold) &&
        (!hasJump || decodedJump <= kMovementJumpProtectHighValueThreshold) &&
        (!shouldRaiseFromMountedRaw ||
         (mountedRawSpeed <= kMovementSpeedProtectHighValueThreshold &&
          mountedRawJump <= kMovementJumpProtectHighValueThreshold)))
    {
        return result;
    }

    const int originalSpeedOut = *a8;
    int originalJumpOut = 0;
    if (a9 && !SafeIsBadReadPtr(a9, sizeof(int)))
        originalJumpOut = *a9;

    int metricExtra = 0;
    if (a11 && !SafeIsBadReadPtr(a11, sizeof(DWORD)))
    {
        const int originalMetricOut = static_cast<int>(*a11);
        if (originalMetricOut > originalSpeedOut)
            metricExtra = originalMetricOut - originalSpeedOut;
    }

    int raisedSpeedOut = originalSpeedOut;
    if (hasSpeed && decodedSpeed > kMovementSpeedProtectHighValueThreshold)
    {
        int transformedSpeed = decodedSpeed;
        __try
        {
            tMovementSpeedTransformFn transformFn =
                reinterpret_cast<tMovementSpeedTransformFn>(ADDR_82C810);
            if (transformFn && a3)
                transformedSpeed = transformFn(reinterpret_cast<void *>(static_cast<uintptr_t>(a3)), decodedSpeed);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            transformedSpeed = decodedSpeed;
        }

        if (transformedSpeed > 0)
        {
            const int finalSpeedOut = transformedSpeed + 20;
            if (finalSpeedOut > raisedSpeedOut)
                raisedSpeedOut = finalSpeedOut;
        }
    }

    int raisedJumpOut = originalJumpOut;
    if (a9 && !SafeIsBadWritePtr(a9, sizeof(int)) &&
        hasJump && decodedJump > kMovementJumpProtectHighValueThreshold &&
        decodedJump > raisedJumpOut)
    {
        raisedJumpOut = decodedJump;
    }

    if (shouldRaiseFromMountedRaw)
    {
        if (mountedRawSpeed > raisedSpeedOut)
        {
            raisedSpeedOut = mountedRawSpeed;
        }
        if (a9 &&
            !SafeIsBadWritePtr(a9, sizeof(int)) &&
            mountedRawJump > raisedJumpOut)
        {
            raisedJumpOut = mountedRawJump;
        }
    }

    if (raisedSpeedOut != originalSpeedOut)
    {
        *a8 = raisedSpeedOut;
        if (a11 && !SafeIsBadWritePtr(a11, sizeof(DWORD)))
            *a11 = static_cast<DWORD>(raisedSpeedOut + metricExtra);
    }

    if (a9 && !SafeIsBadWritePtr(a9, sizeof(int)) && raisedJumpOut != originalJumpOut)
        *a9 = raisedJumpOut;

    if ((raisedSpeedOut != originalSpeedOut || raisedJumpOut != originalJumpOut) &&
        InterlockedDecrement(&g_MovementOutputClampLogBudget) >= 0)
    {
        WriteLogFmt(
            "[MoveClamp] B93B80 raise speed=%d->%d jump=%d->%d decoded=(%d,%d) mountedRaw=[mount=%d key=%d speed=%d jump=%d use=%d] a3=0x%08X a4=0x%08X metricExtra=%d",
            originalSpeedOut,
            raisedSpeedOut,
            originalJumpOut,
            raisedJumpOut,
            decodedSpeed,
            decodedJump,
            mountedRawMountItemId,
            mountedRawDataKey,
            mountedRawSpeed,
            mountedRawJump,
            shouldRaiseFromMountedRaw ? 1 : 0,
            a3,
            a4,
            metricExtra);
    }

    return result;
}

static BYTE ReadAbilityRedSiblingByte(uintptr_t thisPtr, size_t byteOffset, bool *outOk)
{
    if (outOk)
        *outOk = false;
    if (!thisPtr)
        return 0;

    BYTE *ptr = reinterpret_cast<BYTE*>(thisPtr + byteOffset);
    if (SafeIsBadReadPtr(ptr, sizeof(BYTE)))
        return 0;

    if (outOk)
        *outOk = true;
    return *ptr;
}

static DWORD ReadAbilityRedSiblingDword(uintptr_t thisPtr, size_t byteOffset, bool *outOk)
{
    if (outOk)
        *outOk = false;
    if (!thisPtr)
        return 0;

    DWORD *ptr = reinterpret_cast<DWORD*>(thisPtr + byteOffset);
    if (SafeIsBadReadPtr(ptr, sizeof(DWORD)))
        return 0;

    if (outOk)
        *outOk = true;
    return *ptr;
}

static void ObserveAbilityRedSiblingCalculator(
    const char *label,
    DWORD *lastCaller,
    uintptr_t *lastThis,
    DWORD *lastTick,
    int *lastActive,
    DWORD callerRet,
    uintptr_t thisValue,
    int resultValue)
{
    const int activeState = SkillOverlayBridgeHasLocalIndependentPotentialBonuses() ? 1 : 0;
    if (!ShouldLogAbilityRedFinalCalculator(
            lastCaller,
            lastThis,
            lastTick,
            lastActive,
            callerRet,
            thisValue,
            activeState))
    {
        return;
    }

    int slot24 = 0, slot30 = 0, slot3C = 0, slot48 = 0, slotB4 = 0;
    bool slot24Ok = false, slot30Ok = false, slot3COk = false, slot48Ok = false, slotB4Ok = false;
    DecodeAbilityRedTripletAtOffset(thisValue, 0x24, &slot24, &slot24Ok);
    DecodeAbilityRedTripletAtOffset(thisValue, 0x30, &slot30, &slot30Ok);
    DecodeAbilityRedTripletAtOffset(thisValue, 0x3C, &slot3C, &slot3COk);
    DecodeAbilityRedTripletAtOffset(thisValue, 0x48, &slot48, &slot48Ok);
    DecodeAbilityRedTripletAtOffset(thisValue, 0xB4, &slotB4, &slotB4Ok);

    bool meta8COk = false, meta8DOk = false, meta90Ok = false;
    bool meta94Ok = false, meta95Ok = false, meta98Ok = false;
    const BYTE meta8C = ReadAbilityRedSiblingByte(thisValue, 0x8C, &meta8COk);
    const BYTE meta8D = ReadAbilityRedSiblingByte(thisValue, 0x8D, &meta8DOk);
    const DWORD meta90 = ReadAbilityRedSiblingDword(thisValue, 0x90, &meta90Ok);
    const BYTE meta94 = ReadAbilityRedSiblingByte(thisValue, 0x94, &meta94Ok);
    const BYTE meta95 = ReadAbilityRedSiblingByte(thisValue, 0x95, &meta95Ok);
    const DWORD meta98 = ReadAbilityRedSiblingDword(thisValue, 0x98, &meta98Ok);

    WriteLogFmt(
        "[AbilityRedSibling] %s caller=0x%08X this=0x%08X result=%d active=%d metaA=[8C=0x%02X/%d 8D=0x%02X/%d 90=0x%08X/%d] metaB=[94=0x%02X/%d 95=0x%02X/%d 98=0x%08X/%d]",
        label ? label : "unknown",
        callerRet,
        (DWORD)thisValue,
        resultValue,
        activeState,
        meta8C, meta8COk ? 1 : 0,
        meta8D, meta8DOk ? 1 : 0,
        meta90, meta90Ok ? 1 : 0,
        meta94, meta94Ok ? 1 : 0,
        meta95, meta95Ok ? 1 : 0,
        meta98, meta98Ok ? 1 : 0);
    WriteLogFmt(
        "[AbilityRedSibling] %s slots 24=%d/%d 30=%d/%d 3C=%d/%d 48=%d/%d B4=%d/%d",
        label ? label : "unknown",
        slot24, slot24Ok ? 1 : 0,
        slot30, slot30Ok ? 1 : 0,
        slot3C, slot3COk ? 1 : 0,
        slot48, slot48Ok ? 1 : 0,
        slotB4, slotB4Ok ? 1 : 0);
}

static int __fastcall hkAbilityRedSiblingCalc82F780(void *thisPtr, void *edxUnused)
{
    (void)edxUnused;
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    const int resultValue = oAbilityRedSiblingCalc82F780Fn
        ? oAbilityRedSiblingCalc82F780Fn(thisPtr, edxUnused)
        : 0;
    ObserveAbilityRedSiblingCalculator(
        "82F780",
        &g_AbilityRedSibling82F780LastCaller,
        &g_AbilityRedSibling82F780LastThis,
        &g_AbilityRedSibling82F780LastTick,
        &g_AbilityRedSibling82F780LastActive,
        callerRet,
        reinterpret_cast<uintptr_t>(thisPtr),
        resultValue);
    return resultValue;
}

static int __fastcall hkAbilityRedSiblingCalc82F870(void *thisPtr, void *edxUnused)
{
    (void)edxUnused;
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    const int resultValue = oAbilityRedSiblingCalc82F870Fn
        ? oAbilityRedSiblingCalc82F870Fn(thisPtr, edxUnused)
        : 0;
    ObserveAbilityRedSiblingCalculator(
        "82F870",
        &g_AbilityRedSibling82F870LastCaller,
        &g_AbilityRedSibling82F870LastThis,
        &g_AbilityRedSibling82F870LastTick,
        &g_AbilityRedSibling82F870LastActive,
        callerRet,
        reinterpret_cast<uintptr_t>(thisPtr),
        resultValue);
    return resultValue;
}

static int __fastcall hkAbilityRedSiblingCalc82F960(void *thisPtr, void *edxUnused)
{
    (void)edxUnused;
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    const int resultValue = oAbilityRedSiblingCalc82F960Fn
        ? oAbilityRedSiblingCalc82F960Fn(thisPtr, edxUnused)
        : 0;
    ObserveAbilityRedSiblingCalculator(
        "82F960",
        &g_AbilityRedSibling82F960LastCaller,
        &g_AbilityRedSibling82F960LastThis,
        &g_AbilityRedSibling82F960LastTick,
        &g_AbilityRedSibling82F960LastActive,
        callerRet,
        reinterpret_cast<uintptr_t>(thisPtr),
        resultValue);
    return resultValue;
}

static int __fastcall hkAbilityRedSiblingCalc82FA50(void *thisPtr, void *edxUnused)
{
    (void)edxUnused;
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    const int resultValue = oAbilityRedSiblingCalc82FA50Fn
        ? oAbilityRedSiblingCalc82FA50Fn(thisPtr, edxUnused)
        : 0;
    ObserveAbilityRedSiblingCalculator(
        "82FA50",
        &g_AbilityRedSibling82FA50LastCaller,
        &g_AbilityRedSibling82FA50LastThis,
        &g_AbilityRedSibling82FA50LastTick,
        &g_AbilityRedSibling82FA50LastActive,
        callerRet,
        reinterpret_cast<uintptr_t>(thisPtr),
        resultValue);
    return resultValue;
}

static DWORD __cdecl hkAdjustAbilityRedDiff84C470PreSub(
    DWORD finalThisValue,
    DWORD frameEbp,
    DWORD mainSlotValue,
    DWORD sumAfterAdds)
{
    DWORD adjustedSum = sumAfterAdds;
    const int activeState = SkillOverlayBridgeHasLocalIndependentPotentialBonuses() ? 1 : 0;
    if (!activeState)
        g_AbilityRedBaseSumInactive9F7546 = sumAfterAdds;

    DWORD inactiveBaseline = g_AbilityRedBaseSumInactive9F7546;
    if (activeState && inactiveBaseline == 0)
    {
        const DWORD seededBaseline = SeedAbilityRedInactiveBaselineFromPrimary(ADDR_9F7546, sumAfterAdds);
        if (seededBaseline > 0)
        {
            g_AbilityRedBaseSumInactive9F7546 = seededBaseline;
            inactiveBaseline = seededBaseline;
            WriteLogFmt("[AbilityRedDiff] seed baseline site=0x%08X current=%u seeded=%u",
                ADDR_9F7546,
                sumAfterAdds,
                seededBaseline);
        }
    }
    if (!ShouldLogAbilityRedFinalCalculator(
            &g_AbilityRedDiff84C470LastCaller,
            &g_AbilityRedDiff84C470LastThis,
            &g_AbilityRedDiff84C470LastTick,
            &g_AbilityRedDiff84C470LastActive,
            ADDR_9F7546,
            finalThisValue,
            activeState))
    {
        const int localDeltaQuiet = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x44);
        if (activeState && inactiveBaseline > 0 && sumAfterAdds > inactiveBaseline)
            adjustedSum = inactiveBaseline;
        else if (activeState && localDeltaQuiet > 0 && (int)mainSlotValue >= localDeltaQuiet)
            adjustedSum = (DWORD)((int)sumAfterAdds - localDeltaQuiet);
        return adjustedSum;
    }

    DWORD local48 = 0;
    DWORD local50 = 0;
    DWORD local60 = 0;
    if (frameEbp && !SafeIsBadReadPtr(reinterpret_cast<void*>(frameEbp + 0x48), sizeof(DWORD)))
        local48 = *reinterpret_cast<DWORD*>(frameEbp + 0x48);
    if (frameEbp && !SafeIsBadReadPtr(reinterpret_cast<void*>(frameEbp + 0x50), sizeof(DWORD)))
        local50 = *reinterpret_cast<DWORD*>(frameEbp + 0x50);
    if (frameEbp && !SafeIsBadReadPtr(reinterpret_cast<void*>(frameEbp + 0x60), sizeof(DWORD)))
        local60 = *reinterpret_cast<DWORD*>(frameEbp + 0x60);

    int decoded198 = 0;
    bool decoded198Ok = false;
    DecodeAbilityRedTripletAtOffset(finalThisValue, 0x198, &decoded198, &decoded198Ok);

    const int helperValue = static_cast<int>(sumAfterAdds) - static_cast<int>(mainSlotValue) - static_cast<int>(local48);
    const int localDelta = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x44);
    if (activeState && inactiveBaseline > 0 && sumAfterAdds > inactiveBaseline)
        adjustedSum = inactiveBaseline;
    else if (activeState && localDelta > 0 && (int)mainSlotValue >= localDelta)
        adjustedSum = (DWORD)((int)sumAfterAdds - localDelta);
    const int predictedDiffRaw = static_cast<int>(local60) - static_cast<int>(sumAfterAdds);
    const int predictedDiffAdjusted = static_cast<int>(local60) - static_cast<int>(adjustedSum);

    WriteLogFmt(
        "[AbilityRedDiff] 84C470 this=0x%08X ebp=0x%08X final=%u sumRaw=%u sumAdj=%u helper=%d mainReg=%u slot198=%d/%d local48=%u local50=0x%08X localDelta=%d inactiveBase=%u diffRaw=%d diffAdj=%d active=%d",
        finalThisValue,
        frameEbp,
        local60,
        sumAfterAdds,
        adjustedSum,
        helperValue,
        mainSlotValue,
        decoded198,
        decoded198Ok ? 1 : 0,
        local48,
        local50,
        localDelta,
        inactiveBaseline,
        predictedDiffRaw,
        predictedDiffAdjusted,
        activeState);
    return adjustedSum;
}

__declspec(naked) static void hkAbilityRedDiff84C470PreSubNaked()
{
    __asm {
        add esi, ebx
        add esi, dword ptr [ebp + 0x48]
        pushfd
        pushad
        push esi
        push ebx
        push ebp
        push edi
        call hkAdjustAbilityRedDiff84C470PreSub
        add esp, 16
        mov dword ptr [esp + 4], eax
        popad
        popfd
        jmp dword ptr [g_AbilityRedDiff84C470PreSubContinue]
    }
}

static DWORD __cdecl hkAdjustAbilityRedBaseSumByLocalDelta(
    DWORD frameEbp,
    DWORD sumAfterAdds,
    int deltaOffset)
{
    (void)frameEbp;

    if (!SkillOverlayBridgeHasLocalIndependentPotentialBonuses())
        return sumAfterAdds;

    const int localDelta = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(deltaOffset);
    if (localDelta <= 0)
        return sumAfterAdds;

    const int rawSum = static_cast<int>(sumAfterAdds);
    if (rawSum < localDelta)
        return sumAfterAdds;

    return static_cast<DWORD>(rawSum - localDelta);
}

static DWORD* GetAbilityRedInactiveBaselineSlot(DWORD siteId)
{
    switch (siteId)
    {
    case ADDR_9F7241: return &g_AbilityRedBaseSumInactive9F7241;
    case ADDR_9F7546: return &g_AbilityRedBaseSumInactive9F7546;
    case ADDR_9F7893: return &g_AbilityRedBaseSumInactive9F7893;
    case ADDR_9F7C7F: return &g_AbilityRedBaseSumInactive9F7C7F;
    case ADDR_9F8048: return &g_AbilityRedBaseSumInactive9F8048;
    case ADDR_9F82A8: return &g_AbilityRedBaseSumInactive9F82A8;
    default:
        return nullptr;
    }
}

static bool HasPositiveLocalIndependentPotentialDeltaAny(const int* offsets, size_t count);

static bool HasPositiveLocalIndependentPotentialPrimaryDelta()
{
    const int offsets[] = { 0x08, 0x0C, 0x10, 0x14 };
    return HasPositiveLocalIndependentPotentialDeltaAny(offsets, ARRAYSIZE(offsets));
}

struct AbilityRedPrimaryDeltaBackup
{
    int originalValues[4];
    bool valid[4];
    bool applied;
};

static LONG g_AbilityRedBaselineSeedGuard = 0;

static bool BeginTemporaryAbilityRedPrimaryBaseline(AbilityRedPrimaryDeltaBackup* backup)
{
    if (!backup)
        return false;

    ZeroMemory(backup, sizeof(*backup));

    DWORD* const basePrimary = reinterpret_cast<DWORD*>(ADDR_AbilityPrimaryCache);
    if (!basePrimary)
        return false;

    const struct
    {
        size_t keyIndex;
        int offset;
    } targets[] = {
        { 9,  0x08 },
        { 12, 0x0C },
        { 15, 0x10 },
        { 18, 0x14 },
    };

    for (int i = 0; i < (int)ARRAYSIZE(targets); ++i)
    {
        const int delta = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(targets[i].offset);
        if (delta <= 0)
            continue;

        int currentValue = 0;
        if (!ReadEncryptedTripletValue(basePrimary, targets[i].keyIndex, &currentValue))
            return false;

        backup->originalValues[i] = currentValue;
        backup->valid[i] = true;
        backup->applied = true;

        if (!WriteEncryptedTripletValue(basePrimary, targets[i].keyIndex, currentValue - delta))
            return false;
    }

    return backup->applied;
}

static void EndTemporaryAbilityRedPrimaryBaseline(const AbilityRedPrimaryDeltaBackup* backup)
{
    if (!backup)
        return;

    DWORD* const basePrimary = reinterpret_cast<DWORD*>(ADDR_AbilityPrimaryCache);
    if (!basePrimary)
        return;

    const size_t keyIndices[] = { 9, 12, 15, 18 };
    for (int i = 0; i < (int)ARRAYSIZE(keyIndices); ++i)
    {
        if (!backup->valid[i])
            continue;
        WriteEncryptedTripletValue(basePrimary, keyIndices[i], backup->originalValues[i]);
    }
}

static tAbilityRedSiblingCalcFn SelectAbilityRedSiblingBaselineFn(DWORD siteId)
{
    switch (siteId)
    {
    case ADDR_9F7241:
    case ADDR_9F7546:
        return oAbilityRedSiblingCalc82F780Fn;
    case ADDR_9F7893:
        return oAbilityRedSiblingCalc82F870Fn;
    case ADDR_9F7C7F:
        return oAbilityRedSiblingCalc82F960Fn;
    case ADDR_9F8048:
    case ADDR_9F82A8:
        return oAbilityRedSiblingCalc82FA50Fn;
    default:
        return nullptr;
    }
}

static DWORD SeedAbilityRedInactiveBaselineFromPrimary(DWORD siteId, DWORD currentSum)
{
    if (!HasPositiveLocalIndependentPotentialPrimaryDelta())
        return 0;

    tAbilityRedSiblingCalcFn siblingFn = SelectAbilityRedSiblingBaselineFn(siteId);
    if (!siblingFn)
        return 0;

    void* const primaryThis = reinterpret_cast<void*>(ADDR_AbilityPrimaryCache);
    if (!primaryThis || SafeIsBadReadPtr(primaryThis, 0x40))
        return 0;

    if (InterlockedExchange(&g_AbilityRedBaselineSeedGuard, 1) != 0)
        return 0;

    DWORD seededBaseline = 0;
    AbilityRedPrimaryDeltaBackup backup = {};
    const int activeComparable = siblingFn(primaryThis, nullptr);
    if (BeginTemporaryAbilityRedPrimaryBaseline(&backup))
    {
        const int inactiveComparable = siblingFn(primaryThis, nullptr);
        const int effectDelta = activeComparable - inactiveComparable;
        if (effectDelta > 0 && static_cast<int>(currentSum) >= effectDelta)
            seededBaseline = static_cast<DWORD>(static_cast<int>(currentSum) - effectDelta);
    }
    EndTemporaryAbilityRedPrimaryBaseline(&backup);

    InterlockedExchange(&g_AbilityRedBaselineSeedGuard, 0);
    return seededBaseline;
}

static DWORD __cdecl hkAdjustAbilityRedBaseSumBySiteBaseline(
    DWORD frameEbp,
    DWORD sumAfterAdds,
    int deltaOffset,
    DWORD siteId)
{
    (void)frameEbp;

    const bool active = SkillOverlayBridgeHasLocalIndependentPotentialBonuses();
    DWORD* inactiveBaselineSlot = GetAbilityRedInactiveBaselineSlot(siteId);
    if (!active)
    {
        if (inactiveBaselineSlot)
            *inactiveBaselineSlot = sumAfterAdds;
        return sumAfterAdds;
    }

    if (inactiveBaselineSlot && *inactiveBaselineSlot == 0)
    {
        const DWORD seededBaseline = SeedAbilityRedInactiveBaselineFromPrimary(siteId, sumAfterAdds);
        if (seededBaseline > 0)
        {
            *inactiveBaselineSlot = seededBaseline;
            WriteLogFmt("[AbilityRedDiff] seed baseline site=0x%08X current=%u seeded=%u",
                siteId,
                sumAfterAdds,
                seededBaseline);
            return seededBaseline;
        }
    }

    if (inactiveBaselineSlot && *inactiveBaselineSlot > 0 && sumAfterAdds > *inactiveBaselineSlot)
    {
        const int directDelta = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(deltaOffset);
        if (directDelta > 0 || HasPositiveLocalIndependentPotentialPrimaryDelta())
            return *inactiveBaselineSlot;
    }

    return hkAdjustAbilityRedBaseSumByLocalDelta(frameEbp, sumAfterAdds, deltaOffset);
}

static bool HasPositiveLocalIndependentPotentialDeltaAny(const int* offsets, size_t count)
{
    if (!offsets || count == 0)
        return false;
    if (!SkillOverlayBridgeHasLocalIndependentPotentialBonuses())
        return false;

    for (size_t i = 0; i < count; ++i)
    {
        if (SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(offsets[i]) > 0)
            return true;
    }
    return false;
}

enum AbilityRedPositiveStyleMode
{
    AbilityRedPositiveStyle_AttackRange = 1,
    AbilityRedPositiveStyle_CriticalRate = 2,
    AbilityRedPositiveStyle_Speed = 3,
    AbilityRedPositiveStyle_Jump = 4,
};

static DWORD __cdecl hkSelectAbilityRedPositiveStyle(
    DWORD frameEbp,
    DWORD currentStyle,
    int mode)
{
    const int attackOffsets[] = { 0x38, 0x3C };
    const int criticalOffsets[] = { 0x78 };
    const int speedOffsets[] = { 0x30 };
    const int jumpOffsets[] = { 0x34 };

    bool shouldUseRedStyle = false;
    switch (mode)
    {
    case AbilityRedPositiveStyle_AttackRange:
        shouldUseRedStyle = HasPositiveLocalIndependentPotentialDeltaAny(attackOffsets, ARRAYSIZE(attackOffsets));
        if (!shouldUseRedStyle)
            shouldUseRedStyle = HasPositiveLocalIndependentPotentialPrimaryDelta();
        break;
    case AbilityRedPositiveStyle_CriticalRate:
        shouldUseRedStyle = HasPositiveLocalIndependentPotentialDeltaAny(criticalOffsets, ARRAYSIZE(criticalOffsets));
        break;
    case AbilityRedPositiveStyle_Speed:
        shouldUseRedStyle = HasPositiveLocalIndependentPotentialDeltaAny(speedOffsets, ARRAYSIZE(speedOffsets));
        break;
    case AbilityRedPositiveStyle_Jump:
        shouldUseRedStyle = HasPositiveLocalIndependentPotentialDeltaAny(jumpOffsets, ARRAYSIZE(jumpOffsets));
        break;
    default:
        break;
    }

    if (!shouldUseRedStyle)
        return currentStyle;

    if (!frameEbp || SafeIsBadReadPtr(reinterpret_cast<void*>(frameEbp + 0x24), sizeof(DWORD)))
        return currentStyle;

    const DWORD redStyle = *reinterpret_cast<DWORD*>(frameEbp + 0x24);
    return redStyle ? redStyle : currentStyle;
}

__declspec(naked) static void hkAbilityRedDiff84BE40PreSubNaked()
{
    __asm {
        add esi, ebx
        add esi, dword ptr [ebp + 0x48]
        pushfd
        pushad
        push ADDR_9F7241
        push 0x40
        push esi
        push ebp
        call hkAdjustAbilityRedBaseSumBySiteBaseline
        add esp, 16
        mov dword ptr [esp + 4], eax
        popad
        popfd
        jmp dword ptr [g_AbilityRedDiff84BE40PreSubContinue]
    }
}

__declspec(naked) static void hkAbilityRedDiff84CA90AccPreSubNaked()
{
    __asm {
        add esi, edi
        add esi, dword ptr [ebp + 0x48]
        pushfd
        pushad
        push ADDR_9F7893
        push 0x28
        push esi
        push ebp
        call hkAdjustAbilityRedBaseSumBySiteBaseline
        add esp, 16
        mov dword ptr [esp + 4], eax
        popad
        popfd
        jmp dword ptr [g_AbilityRedDiff84CA90AccPreSubContinue]
    }
}

__declspec(naked) static void hkAbilityRedDiff84CA90MagicAccPreSubNaked()
{
    __asm {
        add esi, ebx
        add esi, dword ptr [ebp + 0x48]
        pushfd
        pushad
        push ADDR_9F7C7F
        push 0x28
        push esi
        push ebp
        call hkAdjustAbilityRedBaseSumBySiteBaseline
        add esp, 16
        mov dword ptr [esp + 4], eax
        popad
        popfd
        jmp dword ptr [g_AbilityRedDiff84CA90MagicAccPreSubContinue]
    }
}

__declspec(naked) static void hkAbilityRedDiff84CBD0AvoidPreSubNaked()
{
    __asm {
        add esi, ebx
        add esi, dword ptr [ebp + 0x48]
        pushfd
        pushad
        push ADDR_9F8048
        push 0x2C
        push esi
        push ebp
        call hkAdjustAbilityRedBaseSumBySiteBaseline
        add esp, 16
        mov dword ptr [esp + 4], eax
        popad
        popfd
        jmp dword ptr [g_AbilityRedDiff84CBD0AvoidPreSubContinue]
    }
}

__declspec(naked) static void hkAbilityRedDiff84CBD0MagicAvoidPreSubNaked()
{
    __asm {
        add eax, dword ptr [ebp + 0x48]
        add esi, eax
        pushfd
        pushad
        push ADDR_9F82A8
        push 0x2C
        push esi
        push ebp
        call hkAdjustAbilityRedBaseSumBySiteBaseline
        add esp, 16
        mov dword ptr [esp + 4], eax
        popad
        popfd
        jmp dword ptr [g_AbilityRedDiff84CBD0MagicAvoidPreSubContinue]
    }
}

__declspec(naked) static void hkAbilityRedAttackRangeStyleNaked()
{
    __asm {
        pushfd
        pushad
        push AbilityRedPositiveStyle_AttackRange
        push dword ptr [ebp + 0x68]
        push ebp
        call hkSelectAbilityRedPositiveStyle
        add esp, 12
        mov dword ptr [esp + 28], eax
        popad
        popfd
        lea ecx, [ebp + 0x34]
        push ecx
        jmp dword ptr [g_AbilityRedAttackRangeStyleContinue]
    }
}

__declspec(naked) static void hkAbilityRedCriticalRateStyleNaked()
{
    __asm {
        pushfd
        pushad
        push AbilityRedPositiveStyle_CriticalRate
        push dword ptr [ebp + 0x68]
        push ebp
        call hkSelectAbilityRedPositiveStyle
        add esp, 12
        mov dword ptr [esp + 28], eax
        popad
        popfd
        lea ecx, [ebp + 0x34]
        push ecx
        jmp dword ptr [g_AbilityRedCriticalRateStyleContinue]
    }
}

__declspec(naked) static void hkAbilityRedSpeedStyleNaked()
{
    __asm {
        pushfd
        pushad
        push AbilityRedPositiveStyle_Speed
        push dword ptr [ebp + 0x30]
        push ebp
        call hkSelectAbilityRedPositiveStyle
        add esp, 12
        mov dword ptr [esp + 28], eax
        popad
        popfd
        lea ecx, [ebp + 0x94]
        push ecx
        jmp dword ptr [g_AbilityRedSpeedStyleContinue]
    }
}

__declspec(naked) static void hkAbilityRedJumpStyleNaked()
{
    __asm {
        pushfd
        pushad
        push AbilityRedPositiveStyle_Jump
        push dword ptr [ebp + 0x30]
        push ebp
        call hkSelectAbilityRedPositiveStyle
        add esp, 12
        mov dword ptr [esp + 24], eax
        popad
        popfd
        mov esi, dword ptr [ebp + 0x64]
        jmp dword ptr [g_AbilityRedJumpStyleContinue]
    }
}

static bool ShouldLogAbilityRedBakeWrite(
    DWORD *lastSig,
    DWORD *lastTick,
    uintptr_t thisValue,
    uintptr_t srcValue,
    int rawDelta,
    int priorValue,
    int activeState)
{
    if (!EnableAbilityRedDiagnosticLogs())
        return false;
    if (!lastSig || !lastTick)
        return true;

    const DWORD signature =
        (DWORD)thisValue ^
        ((DWORD)srcValue << 1) ^
        (DWORD)(rawDelta * 131) ^
        (DWORD)(priorValue * 17) ^
        (DWORD)(activeState << 30);

    const DWORD now = GetTickCount();
    if (*lastSig == signature && now - *lastTick <= 1000)
        return false;

    *lastSig = signature;
    *lastTick = now;
    return true;
}

static void ObserveAbilityRedBakeWrite(
    const char *label,
    DWORD *lastSig,
    DWORD *lastTick,
    DWORD thisValue,
    DWORD srcValue,
    DWORD rawEax,
    DWORD oldValue,
    size_t destOffset,
    size_t srcStartOffset,
    size_t srcEndOffset)
{
    const int activeState = SkillOverlayBridgeHasLocalIndependentPotentialBonuses() ? 1 : 0;
    const short rawShort = (short)(rawEax & 0xFFFF);
    const int rawDelta = (int)rawShort;
    const int priorValue = (int)oldValue;

    if (!(activeState || rawDelta != 0 || destOffset == 0x198))
        return;

    if (!ShouldLogAbilityRedBakeWrite(
            lastSig,
            lastTick,
            thisValue,
            srcValue,
            rawDelta,
            priorValue,
            activeState))
    {
        return;
    }

    int slotBefore = 0;
    bool slotBeforeOk = false;
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisValue, destOffset, &slotBefore, &slotBeforeOk);

    DWORD srcTail = 0;
    bool srcTailOk = false;
    if (srcValue && !SafeIsBadReadPtr(reinterpret_cast<void*>(srcValue + srcEndOffset), sizeof(DWORD)))
    {
        srcTail = *reinterpret_cast<DWORD*>(srcValue + srcEndOffset);
        srcTailOk = true;
    }

    WriteLogFmt(
        "[AbilityRedBake] %s this=0x%08X src=0x%08X dest=0x%03X srcField=[0x%02X..0x%02X] raw=%d prior=%d slotBefore=%d/%d predicted=%d tail=0x%08X/%d active=%d",
        label ? label : "unknown",
        thisValue,
        srcValue,
        (unsigned int)destOffset,
        (unsigned int)srcStartOffset,
        (unsigned int)srcEndOffset,
        rawDelta,
        priorValue,
        slotBefore,
        slotBeforeOk ? 1 : 0,
        priorValue + rawDelta,
        srcTail,
        srcTailOk ? 1 : 0,
        activeState);
}

static void __cdecl hkObserveAbilityRedBake857BB6(DWORD thisValue, DWORD srcValue, DWORD rawEax, DWORD oldValue)
{
    ObserveAbilityRedBakeWrite("857BB6", &g_AbilityRedBake857BB6LastSig, &g_AbilityRedBake857BB6LastTick,
        thisValue, srcValue, rawEax, oldValue, 0x15C, 0x91, 0x95);
}

static void __cdecl hkObserveAbilityRedBake857C29(DWORD thisValue, DWORD srcValue, DWORD rawEax, DWORD oldValue)
{
    ObserveAbilityRedBakeWrite("857C29", &g_AbilityRedBake857C29LastSig, &g_AbilityRedBake857C29LastTick,
        thisValue, srcValue, rawEax, oldValue, 0x198, 0xA1, 0xA5);
}

static void __cdecl hkObserveAbilityRedBake857C9C(DWORD thisValue, DWORD srcValue, DWORD rawEax, DWORD oldValue)
{
    ObserveAbilityRedBakeWrite("857C9C", &g_AbilityRedBake857C9CLastSig, &g_AbilityRedBake857C9CLastTick,
        thisValue, srcValue, rawEax, oldValue, 0x1D4, 0xA9, 0xAD);
}

static void __cdecl hkObserveAbilityRedBake857D0F(DWORD thisValue, DWORD srcValue, DWORD rawEax, DWORD oldValue)
{
    ObserveAbilityRedBakeWrite("857D0F", &g_AbilityRedBake857D0FLastSig, &g_AbilityRedBake857D0FLastTick,
        thisValue, srcValue, rawEax, oldValue, 0x210, 0xB1, 0xB5);
}

static void ObserveAbilityRedBake198Site(
    const char *label,
    DWORD *lastSig,
    DWORD *lastTick,
    DWORD thisValue,
    DWORD auxPtr,
    int auxValue,
    int sumValue)
{
    const int activeState = SkillOverlayBridgeHasLocalIndependentPotentialBonuses() ? 1 : 0;

    int prior198 = 0;
    bool prior198Ok = false;
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisValue, 0x198, &prior198, &prior198Ok);

    if (!(activeState || sumValue != prior198))
        return;

    if (!ShouldLogAbilityRedBakeWrite(
            lastSig,
            lastTick,
            thisValue,
            auxPtr,
            sumValue - prior198,
            prior198,
            activeState))
    {
        return;
    }

    WriteLogFmt(
        "[AbilityRedBake198] %s this=0x%08X prior198=%d/%d sum=%d delta=%d auxPtr=0x%08X auxValue=%d active=%d",
        label ? label : "unknown",
        thisValue,
        prior198,
        prior198Ok ? 1 : 0,
        sumValue,
        sumValue - prior198,
        auxPtr,
        auxValue,
        activeState);
}

static void __cdecl hkObserveAbilityRedBake1988569C3(DWORD thisValue, DWORD ediValue, DWORD ebxValue)
{
    int auxValue = 0;
    if (ediValue && !SafeIsBadReadPtr(reinterpret_cast<void*>(ediValue + 0x44), sizeof(DWORD)))
        auxValue = (int)(*reinterpret_cast<DWORD*>(ediValue + 0x44));

    ObserveAbilityRedBake198Site(
        "8569C3",
        &g_AbilityRedBake1988569C3LastSig,
        &g_AbilityRedBake1988569C3LastTick,
        thisValue,
        ediValue ? (ediValue + 0x44) : 0,
        auxValue,
        (int)ebxValue);
}

static void __cdecl hkObserveAbilityRedBake198856D57(DWORD thisValue)
{
    ObserveAbilityRedBake198Site(
        "856D57",
        &g_AbilityRedBake198856D57LastSig,
        &g_AbilityRedBake198856D57LastTick,
        thisValue,
        0,
        0,
        0);
}

static void __cdecl hkObserveAbilityRedBake19885725F(DWORD thisValue, DWORD ediValue, DWORD ebxValue)
{
    DWORD sourceNode = 0;
    int auxValue = 0;
    if (ediValue && !SafeIsBadReadPtr(reinterpret_cast<void*>(ediValue), sizeof(DWORD)))
    {
        sourceNode = *reinterpret_cast<DWORD*>(ediValue);
        if (sourceNode && !SafeIsBadReadPtr(reinterpret_cast<void*>(sourceNode + 0x18), sizeof(short)))
            auxValue = (int)(*(short*)(sourceNode + 0x18));
    }

    ObserveAbilityRedBake198Site(
        "85725F",
        &g_AbilityRedBake19885725FLastSig,
        &g_AbilityRedBake19885725FLastTick,
        thisValue,
        sourceNode ? (sourceNode + 0x18) : ediValue,
        auxValue,
        (int)ebxValue);
}

static void __cdecl hkObserveAbilityRedBake198857C3B(DWORD thisValue, DWORD ediValue, DWORD ebxValue)
{
    int auxValue = 0;
    if (ediValue && !SafeIsBadReadPtr(reinterpret_cast<void*>(ediValue + 0xA5), sizeof(DWORD)))
        auxValue = (int)(*(DWORD*)(ediValue + 0xA5));

    ObserveAbilityRedBake198Site(
        "857C3B",
        &g_AbilityRedBake198857C3BLastSig,
        &g_AbilityRedBake198857C3BLastTick,
        thisValue,
        ediValue ? (ediValue + 0xA1) : 0,
        auxValue,
        (int)ebxValue);
}

static void __cdecl hkObserveAbilityRedBake198858AED(DWORD thisValue, DWORD ediValue)
{
    ObserveAbilityRedBake198Site(
        "858AED",
        &g_AbilityRedBake198858AEDLastSig,
        &g_AbilityRedBake198858AEDLastTick,
        thisValue,
        0,
        0,
        (int)ediValue);
}

static void __cdecl hkObserveAbilityRedBake198831A50(DWORD thisValue, DWORD ediValue)
{
    ObserveAbilityRedBake198Site(
        "831A50",
        &g_AbilityRedBake198831A50LastSig,
        &g_AbilityRedBake198831A50LastTick,
        thisValue,
        0,
        0,
        (int)ediValue);
}

static void __cdecl hkObserveAbilityRedBake19883AF02(DWORD thisValue, DWORD ediValue, DWORD ebxValue)
{
    int auxValue = 0;
    if (ediValue && !SafeIsBadReadPtr(reinterpret_cast<void*>(ediValue + 0x7C5), sizeof(DWORD)))
        auxValue = (int)(*(DWORD*)(ediValue + 0x7C5));

    ObserveAbilityRedBake198Site(
        "83AF02",
        &g_AbilityRedBake19883AF02LastSig,
        &g_AbilityRedBake19883AF02LastTick,
        thisValue,
        ediValue ? (ediValue + 0x7C5) : 0,
        auxValue,
        (int)ebxValue);
}

__declspec(naked) static void hkAbilityRedBake857BB6Naked()
{
    __asm {
        pushfd
        pushad
        push ebp
        push eax
        push edi
        push esi
        call hkObserveAbilityRedBake857BB6
        add esp, 16
        popad
        popfd
        jmp [oAbilityRedBake857BB6Hook]
    }
}

__declspec(naked) static void hkAbilityRedBake857C29Naked()
{
    __asm {
        pushfd
        pushad
        push ebp
        push eax
        push edi
        push esi
        call hkObserveAbilityRedBake857C29
        add esp, 16
        popad
        popfd
        jmp [oAbilityRedBake857C29Hook]
    }
}

__declspec(naked) static void hkAbilityRedBake857C9CNaked()
{
    __asm {
        pushfd
        pushad
        push ebp
        push eax
        push edi
        push esi
        call hkObserveAbilityRedBake857C9C
        add esp, 16
        popad
        popfd
        jmp [oAbilityRedBake857C9CHook]
    }
}

__declspec(naked) static void hkAbilityRedBake857D0FNaked()
{
    __asm {
        pushfd
        pushad
        push ebp
        push eax
        push edi
        push esi
        call hkObserveAbilityRedBake857D0F
        add esp, 16
        popad
        popfd
        jmp [oAbilityRedBake857D0FHook]
    }
}

__declspec(naked) static void hkAbilityRedBake1988569C3Naked()
{
    __asm {
        pushfd
        pushad
        push ebx
        push edi
        push esi
        call hkObserveAbilityRedBake1988569C3
        add esp, 12
        popad
        popfd
        jmp [oAbilityRedBake1988569C3Hook]
    }
}

__declspec(naked) static void hkAbilityRedBake198856D57Naked()
{
    __asm {
        pushfd
        pushad
        push esi
        call hkObserveAbilityRedBake198856D57
        add esp, 4
        popad
        popfd
        jmp [oAbilityRedBake198856D57Hook]
    }
}

__declspec(naked) static void hkAbilityRedBake19885725FNaked()
{
    __asm {
        pushfd
        pushad
        push ebx
        push edi
        push esi
        call hkObserveAbilityRedBake19885725F
        add esp, 12
        popad
        popfd
        jmp [oAbilityRedBake19885725FHook]
    }
}

__declspec(naked) static void hkAbilityRedBake198857C3BNaked()
{
    __asm {
        pushfd
        pushad
        push ebx
        push edi
        push esi
        call hkObserveAbilityRedBake198857C3B
        add esp, 12
        popad
        popfd
        jmp [oAbilityRedBake198857C3BHook]
    }
}

__declspec(naked) static void hkAbilityRedBake198858AEDNaked()
{
    __asm {
        pushfd
        pushad
        push edi
        push esi
        call hkObserveAbilityRedBake198858AED
        add esp, 8
        popad
        popfd
        jmp [oAbilityRedBake198858AEDHook]
    }
}

__declspec(naked) static void hkAbilityRedBake198831A50Naked()
{
    __asm {
        pushfd
        pushad
        push edi
        push esi
        call hkObserveAbilityRedBake198831A50
        add esp, 8
        popad
        popfd
        jmp [oAbilityRedBake198831A50Hook]
    }
}

__declspec(naked) static void hkAbilityRedBake19883AF02Naked()
{
    __asm {
        pushfd
        pushad
        push ebx
        push edi
        push esi
        call hkObserveAbilityRedBake19883AF02
        add esp, 12
        popad
        popfd
        jmp [oAbilityRedBake19883AF02Hook]
    }
}

static int __fastcall hkAbilityRedMasterAggregateFunction(
    void *thisPtr,
    void *edxUnused,
    DWORD arg1,
    DWORD arg2,
    DWORD arg3,
    DWORD arg4,
    DWORD arg5,
    DWORD arg6,
    DWORD arg7)
{
    (void)edxUnused;
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    DWORD before3[6] = {};
    DWORD before4[6] = {};
    DWORD before5[6] = {};
    DWORD before6[6] = {};
    DWORD before7[6] = {};
    int before3A = 0, before3B = 0, before4A = 0, before4B = 0, before5A = 0, before5B = 0, before6A = 0, before6B = 0, before7A = 0, before7B = 0;
    int after3A = 0, after3B = 0, after4A = 0, after4B = 0, after5A = 0, after5B = 0, after6A = 0, after6B = 0, after7A = 0, after7B = 0;
    bool before3OkA = false, before3OkB = false, before4OkA = false, before4OkB = false, before5OkA = false, before5OkB = false, before6OkA = false, before6OkB = false, before7OkA = false, before7OkB = false;
    bool after3OkA = false, after3OkB = false, after4OkA = false, after4OkB = false, after5OkA = false, after5OkB = false, after6OkA = false, after6OkB = false, after7OkA = false, after7OkB = false;
    int before3Wdef = 0, before3Mdef = 0, before4Wdef = 0, before4Mdef = 0, before5Wdef = 0, before5Mdef = 0, before6Wdef = 0, before6Mdef = 0, before7Wdef = 0, before7Mdef = 0;
    int after3Wdef = 0, after3Mdef = 0, after4Wdef = 0, after4Mdef = 0, after5Wdef = 0, after5Mdef = 0, after6Wdef = 0, after6Mdef = 0, after7Wdef = 0, after7Mdef = 0;
    bool before3WdefOk = false, before3MdefOk = false, before4WdefOk = false, before4MdefOk = false, before5WdefOk = false, before5MdefOk = false, before6WdefOk = false, before6MdefOk = false, before7WdefOk = false, before7MdefOk = false;
    bool after3WdefOk = false, after3MdefOk = false, after4WdefOk = false, after4MdefOk = false, after5WdefOk = false, after5MdefOk = false, after6WdefOk = false, after6MdefOk = false, after7WdefOk = false, after7MdefOk = false;
    const size_t tripletOffsets[] = { 0x90, 0xE4, 0x114, 0x120, 0x150, 0x15C, 0x18C, 0x198, 0x1C8, 0x1D4, 0x204, 0x210, 0x240 };
    int tripletBefore[(sizeof(tripletOffsets) / sizeof(tripletOffsets[0]))] = {};
    int tripletAfter[(sizeof(tripletOffsets) / sizeof(tripletOffsets[0]))] = {};
    bool tripletBeforeOk[(sizeof(tripletOffsets) / sizeof(tripletOffsets[0]))] = {};
    bool tripletAfterOk[(sizeof(tripletOffsets) / sizeof(tripletOffsets[0]))] = {};
    AbilityRedMovementDiagnosis movementBefore = {};
    AbilityRedMovementDiagnosis movementAfter = {};
    ReadAbilityRedMasterAggregateBuffer(arg3, before3, ARRAYSIZE(before3));
    ReadAbilityRedMasterAggregateBuffer(arg4, before4, ARRAYSIZE(before4));
    ReadAbilityRedMasterAggregateBuffer(arg5, before5, ARRAYSIZE(before5));
    ReadAbilityRedMasterAggregateBuffer(arg6, before6, ARRAYSIZE(before6));
    ReadAbilityRedMasterAggregateBuffer(arg7, before7, ARRAYSIZE(before7));
    DecodeAbilityRedMasterTripletPair(arg3, before3, &before3A, &before3B, &before3OkA, &before3OkB);
    DecodeAbilityRedMasterTripletPair(arg4, before4, &before4A, &before4B, &before4OkA, &before4OkB);
    DecodeAbilityRedMasterTripletPair(arg5, before5, &before5A, &before5B, &before5OkA, &before5OkB);
    DecodeAbilityRedMasterTripletPair(arg6, before6, &before6A, &before6B, &before6OkA, &before6OkB);
    DecodeAbilityRedMasterTripletPair(arg7, before7, &before7A, &before7B, &before7OkA, &before7OkB);
    DecodeAbilityRedMasterDefenseValues(arg3, &before3Wdef, &before3Mdef, &before3WdefOk, &before3MdefOk);
    DecodeAbilityRedMasterDefenseValues(arg4, &before4Wdef, &before4Mdef, &before4WdefOk, &before4MdefOk);
    DecodeAbilityRedMasterDefenseValues(arg5, &before5Wdef, &before5Mdef, &before5WdefOk, &before5MdefOk);
    DecodeAbilityRedMasterDefenseValues(arg6, &before6Wdef, &before6Mdef, &before6WdefOk, &before6MdefOk);
    DecodeAbilityRedMasterDefenseValues(arg7, &before7Wdef, &before7Mdef, &before7WdefOk, &before7MdefOk);
    for (size_t i = 0; i < ARRAYSIZE(tripletOffsets); ++i)
        DecodeAbilityRedTripletAtOffset(reinterpret_cast<uintptr_t>(thisPtr), tripletOffsets[i], &tripletBefore[i], &tripletBeforeOk[i]);
    CollectAbilityRedMovementDiagnosis(thisPtr, arg2, arg4, &movementBefore);

    const int resultValue = oAbilityRedMasterAggregateFn
        ? oAbilityRedMasterAggregateFn(thisPtr, edxUnused, arg1, arg2, arg3, arg4, arg5, arg6, arg7)
        : 0;

    DWORD after3[6] = {};
    DWORD after4[6] = {};
    DWORD after5[6] = {};
    DWORD after6[6] = {};
    DWORD after7[6] = {};
    ReadAbilityRedMasterAggregateBuffer(arg3, after3, ARRAYSIZE(after3));
    ReadAbilityRedMasterAggregateBuffer(arg4, after4, ARRAYSIZE(after4));
    ReadAbilityRedMasterAggregateBuffer(arg5, after5, ARRAYSIZE(after5));
    ReadAbilityRedMasterAggregateBuffer(arg6, after6, ARRAYSIZE(after6));
    ReadAbilityRedMasterAggregateBuffer(arg7, after7, ARRAYSIZE(after7));
    DecodeAbilityRedMasterTripletPair(arg3, after3, &after3A, &after3B, &after3OkA, &after3OkB);
    DecodeAbilityRedMasterTripletPair(arg4, after4, &after4A, &after4B, &after4OkA, &after4OkB);
    DecodeAbilityRedMasterTripletPair(arg5, after5, &after5A, &after5B, &after5OkA, &after5OkB);
    DecodeAbilityRedMasterTripletPair(arg6, after6, &after6A, &after6B, &after6OkA, &after6OkB);
    DecodeAbilityRedMasterTripletPair(arg7, after7, &after7A, &after7B, &after7OkA, &after7OkB);
    DecodeAbilityRedMasterDefenseValues(arg3, &after3Wdef, &after3Mdef, &after3WdefOk, &after3MdefOk);
    DecodeAbilityRedMasterDefenseValues(arg4, &after4Wdef, &after4Mdef, &after4WdefOk, &after4MdefOk);
    DecodeAbilityRedMasterDefenseValues(arg5, &after5Wdef, &after5Mdef, &after5WdefOk, &after5MdefOk);
    DecodeAbilityRedMasterDefenseValues(arg6, &after6Wdef, &after6Mdef, &after6WdefOk, &after6MdefOk);
    DecodeAbilityRedMasterDefenseValues(arg7, &after7Wdef, &after7Mdef, &after7WdefOk, &after7MdefOk);
    for (size_t i = 0; i < ARRAYSIZE(tripletOffsets); ++i)
        DecodeAbilityRedTripletAtOffset(reinterpret_cast<uintptr_t>(thisPtr), tripletOffsets[i], &tripletAfter[i], &tripletAfterOk[i]);
    CollectAbilityRedMovementDiagnosis(thisPtr, arg2, arg4, &movementAfter);

    if (ShouldLogAbilityRedMasterAggregate(callerRet))
    {
        bool movementFinalCapOk = false;
        const int movementFinalCap = ComputeAbilityRedMovementFinalCap(&movementAfter, &movementFinalCapOk);

        WriteLogFmt(
            "[AbilityRedMaster] 856C60 caller=0x%08X this=0x%08X args=[0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X] result=%d active=%d",
            callerRet,
            (DWORD)(uintptr_t)thisPtr,
            arg1, arg2, arg3, arg4, arg5, arg6, arg7,
            resultValue,
            SkillOverlayBridgeHasLocalIndependentPotentialBonuses() ? 1 : 0);

        WriteLogFmt(
            "[AbilityRedMaster] 856C60 decA a3=%d/%d ok=%d/%d -> %d/%d ok=%d/%d a4=%d/%d ok=%d/%d -> %d/%d ok=%d/%d a5=%d/%d ok=%d/%d -> %d/%d ok=%d/%d",
            before3A, before3B, before3OkA ? 1 : 0, before3OkB ? 1 : 0, after3A, after3B, after3OkA ? 1 : 0, after3OkB ? 1 : 0,
            before4A, before4B, before4OkA ? 1 : 0, before4OkB ? 1 : 0, after4A, after4B, after4OkA ? 1 : 0, after4OkB ? 1 : 0,
            before5A, before5B, before5OkA ? 1 : 0, before5OkB ? 1 : 0, after5A, after5B, after5OkA ? 1 : 0, after5OkB ? 1 : 0);

        WriteLogFmt(
            "[AbilityRedMaster] 856C60 decB a6=%d/%d ok=%d/%d -> %d/%d ok=%d/%d a7=%d/%d ok=%d/%d -> %d/%d ok=%d/%d",
            before6A, before6B, before6OkA ? 1 : 0, before6OkB ? 1 : 0, after6A, after6B, after6OkA ? 1 : 0, after6OkB ? 1 : 0,
            before7A, before7B, before7OkA ? 1 : 0, before7OkB ? 1 : 0, after7A, after7B, after7OkA ? 1 : 0, after7OkB ? 1 : 0);

        WriteLogFmt(
            "[AbilityRedMaster] 856C60 def a3=%d/%d ok=%d/%d -> %d/%d ok=%d/%d a4=%d/%d ok=%d/%d -> %d/%d ok=%d/%d",
            before3Wdef, before3Mdef, before3WdefOk ? 1 : 0, before3MdefOk ? 1 : 0, after3Wdef, after3Mdef, after3WdefOk ? 1 : 0, after3MdefOk ? 1 : 0,
            before4Wdef, before4Mdef, before4WdefOk ? 1 : 0, before4MdefOk ? 1 : 0, after4Wdef, after4Mdef, after4WdefOk ? 1 : 0, after4MdefOk ? 1 : 0);

        WriteLogFmt(
            "[AbilityRedMaster] 856C60 def2 a5=%d/%d ok=%d/%d -> %d/%d ok=%d/%d a6=%d/%d ok=%d/%d -> %d/%d ok=%d/%d a7=%d/%d ok=%d/%d -> %d/%d ok=%d/%d",
            before5Wdef, before5Mdef, before5WdefOk ? 1 : 0, before5MdefOk ? 1 : 0, after5Wdef, after5Mdef, after5WdefOk ? 1 : 0, after5MdefOk ? 1 : 0,
            before6Wdef, before6Mdef, before6WdefOk ? 1 : 0, before6MdefOk ? 1 : 0, after6Wdef, after6Mdef, after6WdefOk ? 1 : 0, after6MdefOk ? 1 : 0,
            before7Wdef, before7Mdef, before7WdefOk ? 1 : 0, before7MdefOk ? 1 : 0, after7Wdef, after7Mdef, after7WdefOk ? 1 : 0, after7MdefOk ? 1 : 0);

        WriteLogFmt(
            "[AbilityRedMaster] 856C60 thisdecA 90=%d/%d->%d/%d E4=%d/%d->%d/%d 114=%d/%d->%d/%d 120=%d/%d->%d/%d",
            tripletBefore[0], tripletBeforeOk[0] ? 1 : 0, tripletAfter[0], tripletAfterOk[0] ? 1 : 0,
            tripletBefore[1], tripletBeforeOk[1] ? 1 : 0, tripletAfter[1], tripletAfterOk[1] ? 1 : 0,
            tripletBefore[2], tripletBeforeOk[2] ? 1 : 0, tripletAfter[2], tripletAfterOk[2] ? 1 : 0,
            tripletBefore[3], tripletBeforeOk[3] ? 1 : 0, tripletAfter[3], tripletAfterOk[3] ? 1 : 0);

        WriteLogFmt(
            "[AbilityRedMaster] 856C60 thisdecB 150=%d/%d->%d/%d 15C=%d/%d->%d/%d 18C=%d/%d->%d/%d 198=%d/%d->%d/%d",
            tripletBefore[4], tripletBeforeOk[4] ? 1 : 0, tripletAfter[4], tripletAfterOk[4] ? 1 : 0,
            tripletBefore[5], tripletBeforeOk[5] ? 1 : 0, tripletAfter[5], tripletAfterOk[5] ? 1 : 0,
            tripletBefore[6], tripletBeforeOk[6] ? 1 : 0, tripletAfter[6], tripletAfterOk[6] ? 1 : 0,
            tripletBefore[7], tripletBeforeOk[7] ? 1 : 0, tripletAfter[7], tripletAfterOk[7] ? 1 : 0);

        WriteLogFmt(
            "[AbilityRedMaster] 856C60 thisdecC 1C8=%d/%d->%d/%d 1D4=%d/%d->%d/%d 204=%d/%d->%d/%d 210=%d/%d->%d/%d 240=%d/%d->%d/%d",
            tripletBefore[8], tripletBeforeOk[8] ? 1 : 0, tripletAfter[8], tripletAfterOk[8] ? 1 : 0,
            tripletBefore[9], tripletBeforeOk[9] ? 1 : 0, tripletAfter[9], tripletAfterOk[9] ? 1 : 0,
            tripletBefore[10], tripletBeforeOk[10] ? 1 : 0, tripletAfter[10], tripletAfterOk[10] ? 1 : 0,
            tripletBefore[11], tripletBeforeOk[11] ? 1 : 0, tripletAfter[11], tripletAfterOk[11] ? 1 : 0,
            tripletBefore[12], tripletBeforeOk[12] ? 1 : 0, tripletAfter[12], tripletAfterOk[12] ? 1 : 0);

        WriteLogFmt(
            "[AbilityRedMaster] 856C60 move a4=0x%08X a6=0x%08X mount[a4=%d/%d user=%d/%d] speedAdd=%d/%d capBase=%d/%d override=%d/%d finalCap=%d/%d speed=%d/%d->%d/%d jump=%d/%d->%d/%d",
            arg2,
            arg4,
            movementAfter.mountItemIdFromA4, movementAfter.mountItemIdFromA4Ok ? 1 : 0,
            movementAfter.mountItemIdFromUser, movementAfter.mountItemIdFromUserOk ? 1 : 0,
            movementAfter.speedSourceAdd, movementAfter.speedSourceAddOk ? 1 : 0,
            movementAfter.speedCapBase, movementAfter.speedCapBaseOk ? 1 : 0,
            movementAfter.speedCapOverride, movementAfter.speedCapOverrideOk ? 1 : 0,
            movementFinalCap, movementFinalCapOk ? 1 : 0,
            movementBefore.currentSpeed, movementBefore.currentSpeedOk ? 1 : 0,
            movementAfter.currentSpeed, movementAfter.currentSpeedOk ? 1 : 0,
            movementBefore.currentJump, movementBefore.currentJumpOk ? 1 : 0,
            movementAfter.currentJump, movementAfter.currentJumpOk ? 1 : 0);
    }

    return resultValue;
}

static int __fastcall hkAbilityRedFinalCalc84C470(
    void *thisPtr,
    void *edxUnused,
    DWORD arg1,
    DWORD arg2,
    DWORD arg3,
    DWORD arg4,
    DWORD arg5,
    DWORD arg6)
{
    (void)edxUnused;
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    const DWORD args[6] = { arg1, arg2, arg3, arg4, arg5, arg6 };
    DWORD ptrMaskBefore = 0;
    DWORD ptrBefore[6] = {};
    for (int i = 0; i < 6; ++i)
        ptrBefore[i] = ReadAbilityRedDisplayPointerValue(args[i], (1u << i), &ptrMaskBefore);

    int before198 = 0, before1C8 = 0, before1D4 = 0, before210 = 0;
    bool before198Ok = false, before1C8Ok = false, before1D4Ok = false, before210Ok = false;
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x198, &before198, &before198Ok);
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x1C8, &before1C8, &before1C8Ok);
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x1D4, &before1D4, &before1D4Ok);
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x210, &before210, &before210Ok);

    const int resultValue = oAbilityRedFinalCalc84C470Fn
        ? oAbilityRedFinalCalc84C470Fn(thisPtr, edxUnused, arg1, arg2, arg3, arg4, arg5, arg6)
        : 0;

    DWORD ptrMaskAfter = 0;
    DWORD ptrAfter[6] = {};
    for (int i = 0; i < 6; ++i)
        ptrAfter[i] = ReadAbilityRedDisplayPointerValue(args[i], (1u << i), &ptrMaskAfter);

    int after198 = 0, after1C8 = 0, after1D4 = 0, after210 = 0;
    bool after198Ok = false, after1C8Ok = false, after1D4Ok = false, after210Ok = false;
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x198, &after198, &after198Ok);
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x1C8, &after1C8, &after1C8Ok);
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x1D4, &after1D4, &after1D4Ok);
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x210, &after210, &after210Ok);

    const int activeState = SkillOverlayBridgeHasLocalIndependentPotentialBonuses() ? 1 : 0;
    if (ShouldLogAbilityRedFinalCalculator(
            &g_AbilityRedFinal84C470LastCaller,
            &g_AbilityRedFinal84C470LastThis,
            &g_AbilityRedFinal84C470LastTick,
            &g_AbilityRedFinal84C470LastActive,
            callerRet,
            (uintptr_t)thisPtr,
            activeState))
    {
        WriteLogFmt(
            "[AbilityRedFinal] 84C470 caller=0x%08X this=0x%08X result=%d active=%d args=[0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X]",
            callerRet,
            (DWORD)(uintptr_t)thisPtr,
            resultValue,
            activeState,
            arg1, arg2, arg3, arg4, arg5, arg6);
        WriteLogFmt(
            "[AbilityRedFinal] 84C470 ptrB(mask=0x%02X)=[0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X] ptrA(mask=0x%02X)=[0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X]",
            ptrMaskBefore,
            ptrBefore[0], ptrBefore[1], ptrBefore[2], ptrBefore[3], ptrBefore[4], ptrBefore[5],
            ptrMaskAfter,
            ptrAfter[0], ptrAfter[1], ptrAfter[2], ptrAfter[3], ptrAfter[4], ptrAfter[5]);
        WriteLogFmt(
            "[AbilityRedFinal] 84C470 slots 198=%d/%d->%d/%d 1C8=%d/%d->%d/%d 1D4=%d/%d->%d/%d 210=%d/%d->%d/%d",
            before198, before198Ok ? 1 : 0, after198, after198Ok ? 1 : 0,
            before1C8, before1C8Ok ? 1 : 0, after1C8, after1C8Ok ? 1 : 0,
            before1D4, before1D4Ok ? 1 : 0, after1D4, after1D4Ok ? 1 : 0,
            before210, before210Ok ? 1 : 0, after210, after210Ok ? 1 : 0);
    }

    return resultValue;
}

static int __fastcall hkAbilityRedFinalCalc84CA90(
    void *thisPtr,
    void *edxUnused,
    DWORD arg1,
    DWORD arg2,
    DWORD arg3,
    DWORD arg4,
    DWORD arg5)
{
    (void)edxUnused;
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    const DWORD args[5] = { arg1, arg2, arg3, arg4, arg5 };
    DWORD ptrMaskBefore = 0;
    DWORD ptrBefore[5] = {};
    for (int i = 0; i < 5; ++i)
        ptrBefore[i] = ReadAbilityRedDisplayPointerValue(args[i], (1u << i), &ptrMaskBefore);

    int before198 = 0, before1D4 = 0, before204 = 0;
    bool before198Ok = false, before1D4Ok = false, before204Ok = false;
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x198, &before198, &before198Ok);
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x1D4, &before1D4, &before1D4Ok);
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x204, &before204, &before204Ok);

    const int resultValue = oAbilityRedFinalCalc84CA90Fn
        ? oAbilityRedFinalCalc84CA90Fn(thisPtr, edxUnused, arg1, arg2, arg3, arg4, arg5)
        : 0;

    DWORD ptrMaskAfter = 0;
    DWORD ptrAfter[5] = {};
    for (int i = 0; i < 5; ++i)
        ptrAfter[i] = ReadAbilityRedDisplayPointerValue(args[i], (1u << i), &ptrMaskAfter);

    int after198 = 0, after1D4 = 0, after204 = 0;
    bool after198Ok = false, after1D4Ok = false, after204Ok = false;
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x198, &after198, &after198Ok);
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x1D4, &after1D4, &after1D4Ok);
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x204, &after204, &after204Ok);

    const int activeState = SkillOverlayBridgeHasLocalIndependentPotentialBonuses() ? 1 : 0;
    if (ShouldLogAbilityRedFinalCalculator(
            &g_AbilityRedFinal84CA90LastCaller,
            &g_AbilityRedFinal84CA90LastThis,
            &g_AbilityRedFinal84CA90LastTick,
            &g_AbilityRedFinal84CA90LastActive,
            callerRet,
            (uintptr_t)thisPtr,
            activeState))
    {
        WriteLogFmt(
            "[AbilityRedFinal] 84CA90 caller=0x%08X this=0x%08X result=%d active=%d args=[0x%08X,0x%08X,0x%08X,0x%08X,0x%08X]",
            callerRet,
            (DWORD)(uintptr_t)thisPtr,
            resultValue,
            activeState,
            arg1, arg2, arg3, arg4, arg5);
        WriteLogFmt(
            "[AbilityRedFinal] 84CA90 ptrB(mask=0x%02X)=[0x%08X,0x%08X,0x%08X,0x%08X,0x%08X] ptrA(mask=0x%02X)=[0x%08X,0x%08X,0x%08X,0x%08X,0x%08X]",
            ptrMaskBefore,
            ptrBefore[0], ptrBefore[1], ptrBefore[2], ptrBefore[3], ptrBefore[4],
            ptrMaskAfter,
            ptrAfter[0], ptrAfter[1], ptrAfter[2], ptrAfter[3], ptrAfter[4]);
        WriteLogFmt(
            "[AbilityRedFinal] 84CA90 slots 198=%d/%d->%d/%d 1D4=%d/%d->%d/%d 204=%d/%d->%d/%d",
            before198, before198Ok ? 1 : 0, after198, after198Ok ? 1 : 0,
            before1D4, before1D4Ok ? 1 : 0, after1D4, after1D4Ok ? 1 : 0,
            before204, before204Ok ? 1 : 0, after204, after204Ok ? 1 : 0);
    }

    return resultValue;
}

static int __fastcall hkAbilityRedFinalCalc84CBD0(
    void *thisPtr,
    void *edxUnused,
    DWORD arg1,
    DWORD arg2,
    DWORD arg3,
    DWORD arg4,
    DWORD arg5)
{
    (void)edxUnused;
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    const DWORD args[5] = { arg1, arg2, arg3, arg4, arg5 };
    DWORD ptrMaskBefore = 0;
    DWORD ptrBefore[5] = {};
    for (int i = 0; i < 5; ++i)
        ptrBefore[i] = ReadAbilityRedDisplayPointerValue(args[i], (1u << i), &ptrMaskBefore);

    int before198 = 0, before210 = 0, before240 = 0;
    bool before198Ok = false, before210Ok = false, before240Ok = false;
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x198, &before198, &before198Ok);
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x210, &before210, &before210Ok);
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x240, &before240, &before240Ok);

    const int resultValue = oAbilityRedFinalCalc84CBD0Fn
        ? oAbilityRedFinalCalc84CBD0Fn(thisPtr, edxUnused, arg1, arg2, arg3, arg4, arg5)
        : 0;

    DWORD ptrMaskAfter = 0;
    DWORD ptrAfter[5] = {};
    for (int i = 0; i < 5; ++i)
        ptrAfter[i] = ReadAbilityRedDisplayPointerValue(args[i], (1u << i), &ptrMaskAfter);

    int after198 = 0, after210 = 0, after240 = 0;
    bool after198Ok = false, after210Ok = false, after240Ok = false;
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x198, &after198, &after198Ok);
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x210, &after210, &after210Ok);
    DecodeAbilityRedTripletAtOffset((uintptr_t)thisPtr, 0x240, &after240, &after240Ok);

    const int activeState = SkillOverlayBridgeHasLocalIndependentPotentialBonuses() ? 1 : 0;
    if (ShouldLogAbilityRedFinalCalculator(
            &g_AbilityRedFinal84CBD0LastCaller,
            &g_AbilityRedFinal84CBD0LastThis,
            &g_AbilityRedFinal84CBD0LastTick,
            &g_AbilityRedFinal84CBD0LastActive,
            callerRet,
            (uintptr_t)thisPtr,
            activeState))
    {
        WriteLogFmt(
            "[AbilityRedFinal] 84CBD0 caller=0x%08X this=0x%08X result=%d active=%d args=[0x%08X,0x%08X,0x%08X,0x%08X,0x%08X]",
            callerRet,
            (DWORD)(uintptr_t)thisPtr,
            resultValue,
            activeState,
            arg1, arg2, arg3, arg4, arg5);
        WriteLogFmt(
            "[AbilityRedFinal] 84CBD0 ptrB(mask=0x%02X)=[0x%08X,0x%08X,0x%08X,0x%08X,0x%08X] ptrA(mask=0x%02X)=[0x%08X,0x%08X,0x%08X,0x%08X,0x%08X]",
            ptrMaskBefore,
            ptrBefore[0], ptrBefore[1], ptrBefore[2], ptrBefore[3], ptrBefore[4],
            ptrMaskAfter,
            ptrAfter[0], ptrAfter[1], ptrAfter[2], ptrAfter[3], ptrAfter[4]);
        WriteLogFmt(
            "[AbilityRedFinal] 84CBD0 slots 198=%d/%d->%d/%d 210=%d/%d->%d/%d 240=%d/%d->%d/%d",
            before198, before198Ok ? 1 : 0, after198, after198Ok ? 1 : 0,
            before210, before210Ok ? 1 : 0, after210, after210Ok ? 1 : 0,
            before240, before240Ok ? 1 : 0, after240, after240Ok ? 1 : 0);
    }

    return resultValue;
}

static void __cdecl hkObserveAbilityRedDisplayCallsite(
    DWORD esiValue,
    DWORD ecxValue,
    DWORD ebpValue,
    DWORD critPtr,
    DWORD option31Ptr,
    DWORD damagePtr,
    DWORD bossDamagePtr,
    DWORD ignoreDefensePtr,
    DWORD retAddr)
{
    if (!EnableAbilityRedDiagnosticLogs())
        return;
    const DWORD now = GetTickCount();
    if (now - g_AbilityRedDisplayCallsiteLastTick <= 1000)
        return;
    g_AbilityRedDisplayCallsiteLastTick = now;

    DWORD critValue = 0;
    DWORD option31Value = 0;
    DWORD damageValue = 0;
    DWORD bossDamageValue = 0;
    DWORD ignoreDefenseValue = 0;

    if (critPtr && !SafeIsBadReadPtr(reinterpret_cast<void*>(critPtr), 4))
        critValue = *reinterpret_cast<DWORD*>(critPtr);
    if (option31Ptr && !SafeIsBadReadPtr(reinterpret_cast<void*>(option31Ptr), 4))
        option31Value = *reinterpret_cast<DWORD*>(option31Ptr);
    if (damagePtr && !SafeIsBadReadPtr(reinterpret_cast<void*>(damagePtr), 4))
        damageValue = *reinterpret_cast<DWORD*>(damagePtr);
    if (bossDamagePtr && !SafeIsBadReadPtr(reinterpret_cast<void*>(bossDamagePtr), 4))
        bossDamageValue = *reinterpret_cast<DWORD*>(bossDamagePtr);
    if (ignoreDefensePtr && !SafeIsBadReadPtr(reinterpret_cast<void*>(ignoreDefensePtr), 4))
        ignoreDefenseValue = *reinterpret_cast<DWORD*>(ignoreDefensePtr);

    WriteLogFmt(
        "[AbilityRedDisplayCall] AE6C21 esi=0x%08X ecx=0x%08X ebp=0x%08X ptrs=[0x%08X,0x%08X,0x%08X,0x%08X,0x%08X] vals=[%u,%u,%u,%u,%u] ret=0x%08X",
        esiValue,
        ecxValue,
        ebpValue,
        critPtr,
        option31Ptr,
        damagePtr,
        bossDamagePtr,
        ignoreDefensePtr,
        critValue,
        option31Value,
        damageValue,
        bossDamageValue,
        ignoreDefenseValue,
        retAddr);
}

__declspec(naked) static void hkAbilityRedDisplayCallsiteNaked()
{
    __asm {
        pushfd
        pushad
        push dword ptr [esp + 0x24] // retAddr
        push dword ptr [esp + 0x68] // ignoreDefensePtr
        push dword ptr [esp + 0x68] // bossDamagePtr
        push dword ptr [esp + 0x68] // damagePtr
        push dword ptr [esp + 0x68] // option31Ptr
        push dword ptr [esp + 0x68] // critPtr
        push dword ptr [esp + 0x20] // ebp
        push dword ptr [esp + 0x34] // ecx
        push dword ptr [esp + 0x24] // esi
        call hkObserveAbilityRedDisplayCallsite
        add esp, 36
        popad
        popfd
        jmp dword ptr [g_AbilityRedDisplayCallsiteOriginalTarget]
    }
}

static void __cdecl hkObserveAbilityRedLevelReadFrame(DWORD *frame)
{
    if (!EnableAbilityRedDiagnosticLogs())
        return;
    if (!frame)
        return;

    const DWORD ediValue = frame[0];
    const DWORD esiValue = frame[1];
    const DWORD ebpValue = frame[2];
    const DWORD pushfdEsp = frame[3];
    const DWORD ebxValue = frame[4];
    const DWORD edxValue = frame[5];
    const DWORD ecxValue = frame[6];
    const DWORD eaxValue = frame[7];
    const DWORD originalEsp = pushfdEsp + 4;

    const DWORD now = GetTickCount();
    if (now - g_AbilityRedLevelReadLastTick <= 1000)
        return;
    g_AbilityRedLevelReadLastTick = now;

    DWORD stack18 = 0;
    DWORD stack1C = 0;
    DWORD stack20 = 0;
    DWORD stack24 = 0;
    DWORD stack24v0 = 0;
    DWORD stack24v4 = 0;
    DWORD stack24v8 = 0;
    DWORD stack24vC = 0;
    if (originalEsp && !SafeIsBadReadPtr(reinterpret_cast<void*>(originalEsp + 0x18), 16))
    {
        stack18 = *reinterpret_cast<DWORD*>(originalEsp + 0x18);
        stack1C = *reinterpret_cast<DWORD*>(originalEsp + 0x1C);
        stack20 = *reinterpret_cast<DWORD*>(originalEsp + 0x20);
        stack24 = *reinterpret_cast<DWORD*>(originalEsp + 0x24);
    }
    if (stack24 && !SafeIsBadReadPtr(reinterpret_cast<void*>(stack24), 16))
    {
        stack24v0 = *reinterpret_cast<DWORD*>(stack24 + 0x0);
        stack24v4 = *reinterpret_cast<DWORD*>(stack24 + 0x4);
        stack24v8 = *reinterpret_cast<DWORD*>(stack24 + 0x8);
        stack24vC = *reinterpret_cast<DWORD*>(stack24 + 0xC);
    }

    WriteLogFmt(
        "[AbilityRedLevelRead] AE43D5 esp=0x%08X regs=[eax=0x%08X ecx=0x%08X edx=0x%08X ebx=0x%08X ebp=0x%08X esi=0x%08X edi=0x%08X] stack=[+18=0x%08X +1C=0x%08X +20=0x%08X +24=0x%08X] ptr24=[0x%08X,0x%08X,0x%08X,0x%08X]",
        originalEsp,
        eaxValue,
        ecxValue,
        edxValue,
        ebxValue,
        ebpValue,
        esiValue,
        ediValue,
        stack18,
        stack1C,
        stack20,
        stack24,
        stack24v0,
        stack24v4,
        stack24v8,
        stack24vC);

    LogAbilityRedDecodedSnapshot("AE43D5");
}

__declspec(naked) static void hkAbilityRedLevelReadNaked()
{
    __asm {
        pushfd
        pushad
        mov eax, esp
        push eax
        call hkObserveAbilityRedLevelReadFrame
        add esp, 4
        popad
        popfd
        jmp [oAbilityRedLevelReadHook]
    }
}

static void __cdecl hkObserveAbilityRedHashLookupFrame(DWORD *frame)
{
    if (!frame)
        return;

    const DWORD ecxValue = frame[6];
    const DWORD pushfdEsp = frame[3];
    const DWORD originalEsp = pushfdEsp + 4;
    DWORD returnAddr = 0;
    DWORD keyPtr = 0;
    DWORD outValuePtr = 0;
    if (originalEsp && !SafeIsBadReadPtr(reinterpret_cast<void*>(originalEsp), 12))
    {
        returnAddr = *reinterpret_cast<DWORD*>(originalEsp + 0x0);
        keyPtr = *reinterpret_cast<DWORD*>(originalEsp + 0x4);
        outValuePtr = *reinterpret_cast<DWORD*>(originalEsp + 0x8);
    }

    DWORD keyValue = 0;
    DWORD outBefore = 0;
    DWORD bucketBase = 0;
    DWORD bucketCount = 0;
    DWORD entryCount = 0;
    if (keyPtr && !SafeIsBadReadPtr(reinterpret_cast<void*>(keyPtr), sizeof(DWORD)))
        keyValue = *reinterpret_cast<DWORD*>(keyPtr);
    if (outValuePtr && !SafeIsBadReadPtr(reinterpret_cast<void*>(outValuePtr), sizeof(DWORD)))
        outBefore = *reinterpret_cast<DWORD*>(outValuePtr);

    if (!ShouldLogAbilityRedHashLookup(returnAddr, (uintptr_t)ecxValue, keyValue))
        return;

    ReadAbilityRedHashContainerMeta((uintptr_t)ecxValue, &bucketBase, &bucketCount, &entryCount);
    WriteLogFmt(
        "[AbilityRedHashLookup] caller=0x%08X this=0x%08X keyPtr=0x%08X key=%u outPtr=0x%08X outBefore=%u buckets=0x%08X bucketCount=%u entryCount=%u active=%d",
        returnAddr,
        ecxValue,
        keyPtr,
        keyValue,
        outValuePtr,
        outBefore,
        bucketBase,
        bucketCount,
        entryCount,
        SkillOverlayBridgeHasLocalIndependentPotentialBonuses() ? 1 : 0);
}

__declspec(naked) static void hkAbilityRedHashLookupNaked()
{
    __asm {
        pushfd
        pushad
        mov eax, esp
        push eax
        call hkObserveAbilityRedHashLookupFrame
        add esp, 4
        popad
        popfd
        jmp [oAbilityRedHashLookupHook]
    }
}

static void __cdecl hkObserveAbilityRedHashInsertFrame(DWORD *frame)
{
    if (!frame)
        return;

    const DWORD ecxValue = frame[6];
    const DWORD pushfdEsp = frame[3];
    const DWORD originalEsp = pushfdEsp + 4;
    DWORD returnAddr = 0;
    DWORD keyPtr = 0;
    DWORD valuePtr = 0;
    if (originalEsp && !SafeIsBadReadPtr(reinterpret_cast<void*>(originalEsp), 12))
    {
        returnAddr = *reinterpret_cast<DWORD*>(originalEsp + 0x0);
        keyPtr = *reinterpret_cast<DWORD*>(originalEsp + 0x4);
        valuePtr = *reinterpret_cast<DWORD*>(originalEsp + 0x8);
    }

    DWORD keyValue = 0;
    DWORD valueValue = 0;
    DWORD bucketBase = 0;
    DWORD bucketCount = 0;
    DWORD entryCount = 0;
    if (keyPtr && !SafeIsBadReadPtr(reinterpret_cast<void*>(keyPtr), sizeof(DWORD)))
        keyValue = *reinterpret_cast<DWORD*>(keyPtr);
    if (valuePtr && !SafeIsBadReadPtr(reinterpret_cast<void*>(valuePtr), sizeof(DWORD)))
        valueValue = *reinterpret_cast<DWORD*>(valuePtr);

    if (!ShouldLogAbilityRedHashInsert(returnAddr, (uintptr_t)ecxValue, keyValue, valueValue))
        return;

    ReadAbilityRedHashContainerMeta((uintptr_t)ecxValue, &bucketBase, &bucketCount, &entryCount);
    WriteLogFmt(
        "[AbilityRedHashInsert] caller=0x%08X this=0x%08X keyPtr=0x%08X valuePtr=0x%08X key=%u value=%u buckets=0x%08X bucketCount=%u entryCount=%u active=%d",
        returnAddr,
        ecxValue,
        keyPtr,
        valuePtr,
        keyValue,
        valueValue,
        bucketBase,
        bucketCount,
        entryCount,
        SkillOverlayBridgeHasLocalIndependentPotentialBonuses() ? 1 : 0);
}

__declspec(naked) static void hkAbilityRedHashInsertNaked()
{
    __asm {
        pushfd
        pushad
        mov eax, esp
        push eax
        call hkObserveAbilityRedHashInsertFrame
        add esp, 4
        popad
        popfd
        jmp [oAbilityRedHashInsertHook]
    }
}

static void __cdecl hkObserveAbilityRedSkillWrite(
    const char *tag,
    DWORD destPtr,
    DWORD sourcePtr,
    DWORD sourceValue,
    DWORD carrierA,
    DWORD carrierB)
{
    if (!EnableAbilityRedDiagnosticLogs())
        return;
    const DWORD now = GetTickCount();
    if (now - g_AbilityRedSkillWriteLastTick <= 1000)
        return;
    g_AbilityRedSkillWriteLastTick = now;

    DWORD destValue = 0;
    DWORD destVtable = 0;
    DWORD dest04 = 0;
    DWORD dest08 = 0;
    DWORD dest0C = 0;
    DWORD sourceBase = sourcePtr >= 0x0C ? (sourcePtr - 0x0C) : 0;
    DWORD sourceBase0 = 0;
    DWORD sourceBase4 = 0;
    DWORD sourceBase8 = 0;
    DWORD sourceBaseC = 0;
    if (destPtr && !SafeIsBadReadPtr(reinterpret_cast<void*>(destPtr), 4))
        destValue = *reinterpret_cast<DWORD*>(destPtr);
    if (destPtr && !SafeIsBadReadPtr(reinterpret_cast<void*>(destPtr), 16))
    {
        destVtable = *reinterpret_cast<DWORD*>(destPtr + 0x0);
        dest04 = *reinterpret_cast<DWORD*>(destPtr + 0x4);
        dest08 = *reinterpret_cast<DWORD*>(destPtr + 0x8);
        dest0C = *reinterpret_cast<DWORD*>(destPtr + 0xC);
    }
    if (sourceBase && !SafeIsBadReadPtr(reinterpret_cast<void*>(sourceBase), 16))
    {
        sourceBase0 = *reinterpret_cast<DWORD*>(sourceBase + 0x0);
        sourceBase4 = *reinterpret_cast<DWORD*>(sourceBase + 0x4);
        sourceBase8 = *reinterpret_cast<DWORD*>(sourceBase + 0x8);
        sourceBaseC = *reinterpret_cast<DWORD*>(sourceBase + 0xC);
    }

    WriteLogFmt(
        "[AbilityRedSkillWrite] %s dest=0x%08X destVal=0x%08X destFields=[0x%08X,0x%08X,0x%08X,0x%08X] src=0x%08X srcVal=0x%08X srcBase=0x%08X srcFields=[0x%08X,0x%08X,0x%08X,0x%08X] carry=[0x%08X,0x%08X] active=%d",
        tag ? tag : "unknown",
        destPtr,
        destValue,
        destVtable,
        dest04,
        dest08,
        dest0C,
        sourcePtr,
        sourceValue,
        sourceBase,
        sourceBase0,
        sourceBase4,
        sourceBase8,
        sourceBaseC,
        carrierA,
        carrierB,
        SkillOverlayBridgeHasLocalIndependentPotentialBonuses() ? 1 : 0);
}

static void __cdecl hkObserveAbilityRedSkillWrite52FE14Frame(DWORD *frame)
{
    if (!frame)
        return;

    const DWORD ecxValue = frame[6];
    const DWORD eaxValue = frame[7];
    DWORD sourceValue = 0;
    if (ecxValue && !SafeIsBadReadPtr(reinterpret_cast<void*>(ecxValue), 4))
        sourceValue = *reinterpret_cast<DWORD*>(ecxValue);
    hkObserveAbilityRedSkillWrite("52FE14", eaxValue + 0x0C, ecxValue, sourceValue, eaxValue, ecxValue);
}

static void __cdecl hkObserveAbilityRedSkillWrite6226CEFrame(DWORD *frame)
{
    if (!frame)
        return;

    const DWORD esiValue = frame[1];
    const DWORD eaxValue = frame[7];
    DWORD sourceValue = 0;
    if (esiValue && !SafeIsBadReadPtr(reinterpret_cast<void*>(esiValue + 0x0C), 4))
        sourceValue = *reinterpret_cast<DWORD*>(esiValue + 0x0C);
    hkObserveAbilityRedSkillWrite("6226CE", eaxValue + 0x0C, esiValue + 0x0C, sourceValue, eaxValue, esiValue);
}

static void __cdecl hkObserveAbilityRedSkillWrite49CA01Frame(DWORD *frame)
{
    if (!frame)
        return;

    const DWORD edxValue = frame[5];
    const DWORD eaxValue = frame[7];
    DWORD sourceValue = 0;
    if (edxValue && !SafeIsBadReadPtr(reinterpret_cast<void*>(edxValue + 0x0C), 4))
        sourceValue = *reinterpret_cast<DWORD*>(edxValue + 0x0C);
    hkObserveAbilityRedSkillWrite("49CA01", eaxValue, edxValue + 0x0C, sourceValue, eaxValue, edxValue);
}

static void __cdecl hkPatchAbilityRedSkillWrite49CA01Frame(DWORD *frame)
{
    if (!frame)
        return;
    if (!SkillOverlayBridgeHasLocalIndependentPotentialBonuses())
        return;

    const int mdefDelta = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x44);
    if (mdefDelta <= 0)
        return;

    const DWORD pushfdEsp = frame[3];
    const DWORD originalEsp = pushfdEsp + 4;
    DWORD callerRet = 0;
    if (originalEsp && !SafeIsBadReadPtr(reinterpret_cast<void*>(originalEsp + 0x08), sizeof(DWORD)))
        callerRet = *reinterpret_cast<DWORD*>(originalEsp + 0x08);

    const DWORD edxValue = frame[5];
    if (!edxValue || SafeIsBadWritePtr(reinterpret_cast<void*>(edxValue + 0x0C), sizeof(DWORD)))
        return;
    if (SafeIsBadReadPtr(reinterpret_cast<void*>(edxValue + 0x08), sizeof(DWORD)))
        return;

    DWORD *const sourceBase = reinterpret_cast<DWORD*>(edxValue);
    const DWORD nodeKey = sourceBase[2];
    const DWORD currentValue = sourceBase[3];
    if (nodeKey == 0 || nodeKey == currentValue)
        return;

    static DWORD s_lastAbilityRed49PatchLogTick = 0;
    const DWORD now = GetTickCount();
    if (now - s_lastAbilityRed49PatchLogTick > 1000)
    {
        s_lastAbilityRed49PatchLogTick = now;
        WriteLogFmt("[AbilityRedSkillWrite] 49CA01 observe nodeKey caller=0x%08X srcBase=0x%08X key=0x%08X value=0x%08X mdefDelta=%d",
            callerRet,
            edxValue,
            nodeKey,
            currentValue,
            mdefDelta);
    }
}

__declspec(naked) static void hkAbilityRedSkillWrite52FE14Naked()
{
    __asm {
        pushfd
        pushad
        mov eax, esp
        push eax
        call hkObserveAbilityRedSkillWrite52FE14Frame
        add esp, 4
        popad
        popfd
        jmp [oAbilityRedSkillWrite52FE14Hook]
    }
}

__declspec(naked) static void hkAbilityRedSkillWrite6226CENaked()
{
    __asm {
        pushfd
        pushad
        mov eax, esp
        push eax
        call hkObserveAbilityRedSkillWrite6226CEFrame
        add esp, 4
        popad
        popfd
        jmp [oAbilityRedSkillWrite6226CEHook]
    }
}

__declspec(naked) static void hkAbilityRedSkillWrite49CA01Naked()
{
    __asm {
        pushfd
        pushad
        mov eax, esp
        push eax
        call hkObserveAbilityRedSkillWrite49CA01Frame
        add esp, 4
        mov eax, esp
        push eax
        call hkPatchAbilityRedSkillWrite49CA01Frame
        add esp, 4
        popad
        popfd
        jmp [oAbilityRedSkillWrite49CA01Hook]
    }
}

static BYTE* __stdcall hkPotentialTextFormat(int* src, BYTE* statsPtr)
{
    BYTE* displayPtr = statsPtr;
    if (statsPtr && SkillOverlayBridgeHasLocalIndependentPotentialBonuses())
    {
        uintptr_t prepared = SkillOverlayBridgePrepareLocalIndependentPotentialDisplayBuffer((uintptr_t)statsPtr);
        if (prepared && !SafeIsBadReadPtr(reinterpret_cast<void*>(prepared), 0xD8))
        {
            displayPtr = reinterpret_cast<BYTE*>(prepared);

            static DWORD s_lastPotentialDisplayHookLogTick = 0;
            const DWORD now = GetTickCount();
            if (now - s_lastPotentialDisplayHookLogTick > 1000)
            {
                s_lastPotentialDisplayHookLogTick = now;
                WriteLogFmt("[PotentialTextHook] A4CA60 src=0x%08X stats=0x%08X prepared=0x%08X",
                    (DWORD)(uintptr_t)src,
                    (DWORD)(uintptr_t)statsPtr,
                    (DWORD)prepared);
            }
        }
    }

    return oPotentialTextFormat ? oPotentialTextFormat(src, displayPtr) : nullptr;
}

__declspec(naked) static void hkLocalIndependentPotentialSkillLevelDisplayNaked()
{
    __asm {
        pushad
        push dword ptr [esp + 0x4C]
        call hkApplyLocalIndependentPotentialSkillLevelDisplay
        add esp, 4
        popad
        jmp [oLocalIndependentPotentialSkillLevelDisplay]
    }
}

__declspec(naked) static void hkLocalIndependentPotentialDamageDisplayNaked()
{
    __asm {
        pushad
        mov eax, [esp + 0x5C]
        mov ecx, [esp + 0x58]
        mov edx, [esp + 0x54]
        mov ebx, [esp + 0x50]
        mov esi, [esp + 0x4C]
        push eax
        push ecx
        push edx
        push ebx
        push esi
        call hkApplyLocalIndependentPotentialDamageDisplay
        add esp, 20
        popad
        jmp [oLocalIndependentPotentialDamageDisplay]
    }
}

static int __stdcall hkLocalIndependentPotentialSkillLevelDisplayFunction(int a1, int a2, DWORD *a3)
{
    const int result = oLocalIndependentPotentialSkillLevelDisplayFn
        ? oLocalIndependentPotentialSkillLevelDisplayFn(a1, a2, a3)
        : 0;
    static DWORD s_lastSkillLevelCallLogTick = 0;
    const DWORD now = GetTickCount();
    const int delta88 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x88);
    if (EnableIndependentBuffOverlayDiagnosticLogs() &&
        now - s_lastSkillLevelCallLogTick > 1000)
    {
        s_lastSkillLevelCallLogTick = now;
        WriteLogFmt("[IndependentBuffLocalDisplayCall] AE0A70 a1=0x%08X a2=%d out=0x%08X delta88=%d active=%d",
            (DWORD)a1,
            a2,
            (DWORD)(uintptr_t)a3,
            delta88,
            SkillOverlayBridgeHasLocalIndependentPotentialBonuses() ? 1 : 0);
    }
    LogAbilityRedDecodedSnapshot("AE0A70");
    hkApplyLocalIndependentPotentialSkillLevelDisplay(reinterpret_cast<uintptr_t>(a3));
    return result;
}

static LONG __cdecl hkLocalIndependentPotentialPercentQuadDisplayFunction(
    int a1,
    int a2,
    DWORD *a3,
    DWORD *a4,
    DWORD *a5,
    DWORD *a6)
{
    const LONG result = oLocalIndependentPotentialPercentQuadDisplayFn
        ? oLocalIndependentPotentialPercentQuadDisplayFn(a1, a2, a3, a4, a5, a6)
        : 0;
    static DWORD s_lastPercentQuadCallLogTick = 0;
    const DWORD now = GetTickCount();
    const int d48 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x48);
    const int d4C = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x4C);
    const int d50 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x50);
    const int d54 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x54);
    if (EnableIndependentBuffOverlayDiagnosticLogs() &&
        now - s_lastPercentQuadCallLogTick > 1000)
    {
        s_lastPercentQuadCallLogTick = now;
        WriteLogFmt("[IndependentBuffLocalDisplayCall] 8538C0 a1=0x%08X a2=%d outs=[0x%08X,0x%08X,0x%08X,0x%08X] deltas=[%d,%d,%d,%d] active=%d",
            (DWORD)a1,
            a2,
            (DWORD)(uintptr_t)a3,
            (DWORD)(uintptr_t)a4,
            (DWORD)(uintptr_t)a5,
            (DWORD)(uintptr_t)a6,
            d48,
            d4C,
            d50,
            d54,
            SkillOverlayBridgeHasLocalIndependentPotentialBonuses() ? 1 : 0);
    }
    LogAbilityRedDecodedSnapshot("8538C0");
    hkApplyLocalIndependentPotentialPercentQuadDisplay(
        reinterpret_cast<uintptr_t>(a3),
        reinterpret_cast<uintptr_t>(a4),
        reinterpret_cast<uintptr_t>(a5),
        reinterpret_cast<uintptr_t>(a6));
    return result;
}

static LONG __fastcall hkLocalIndependentPotentialPercentFullDisplayFunction(
    DWORD *thisPtr,
    void * /*edxUnused*/,
    int pExceptionObject,
    int a3,
    DWORD *a4)
{
    const LONG result = oLocalIndependentPotentialPercentFullDisplayFn
        ? oLocalIndependentPotentialPercentFullDisplayFn(thisPtr, pExceptionObject, a3, a4)
        : 0;
    static DWORD s_lastPercentFullCallLogTick = 0;
    const DWORD now = GetTickCount();
    const int d48 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x48);
    const int d4C = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x4C);
    const int d50 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x50);
    const int d54 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x54);
    const int d58 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x58);
    const int d5C = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x5C);
    if (EnableIndependentBuffOverlayDiagnosticLogs() &&
        now - s_lastPercentFullCallLogTick > 1000)
    {
        s_lastPercentFullCallLogTick = now;
        WriteLogFmt("[IndependentBuffLocalDisplayCall] 853E10 this=0x%08X exc=0x%08X a3=%d out=0x%08X deltas=[%d,%d,%d,%d,%d,%d] active=%d",
            (DWORD)(uintptr_t)thisPtr,
            (DWORD)pExceptionObject,
            a3,
            (DWORD)(uintptr_t)a4,
            d48,
            d4C,
            d50,
            d54,
            d58,
            d5C,
            SkillOverlayBridgeHasLocalIndependentPotentialBonuses() ? 1 : 0);
    }
    LogAbilityRedDecodedSnapshot("853E10");
    hkApplyLocalIndependentPotentialPercentFullDisplay(reinterpret_cast<uintptr_t>(a4));
    return result;
}

static LONG __fastcall hkLocalIndependentPotentialFlatBasicDisplayFunction(
    DWORD *thisPtr,
    void * /*edxUnused*/,
    int pExceptionObject,
    int a3)
{
    const LONG result = oLocalIndependentPotentialFlatBasicDisplayFn
        ? oLocalIndependentPotentialFlatBasicDisplayFn(thisPtr, pExceptionObject, a3)
        : 0;
    static DWORD s_lastFlatBasicCallLogTick = 0;
    const DWORD now = GetTickCount();
    const int d08 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x08);
    const int d0C = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x0C);
    const int d10 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x10);
    const int d14 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x14);
    const int d20 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x20);
    const int d24 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x24);
    if (EnableIndependentBuffOverlayDiagnosticLogs() &&
        now - s_lastFlatBasicCallLogTick > 1000)
    {
        s_lastFlatBasicCallLogTick = now;
        WriteLogFmt("[IndependentBuffLocalDisplayCall] 853B00 this=0x%08X exc=0x%08X a3=%d deltas=[%d,%d,%d,%d,%d,%d] active=%d",
            (DWORD)(uintptr_t)thisPtr,
            (DWORD)pExceptionObject,
            a3,
            d08,
            d0C,
            d10,
            d14,
            d20,
            d24,
            SkillOverlayBridgeHasLocalIndependentPotentialBonuses() ? 1 : 0);
    }
    LogAbilityRedDecodedSnapshot("853B00");
    hkApplyLocalIndependentPotentialFlatBasicDisplay(reinterpret_cast<uintptr_t>(thisPtr));
    return result;
}

static LONG __fastcall hkLocalIndependentPotentialFlatExtendedDisplayFunction(
    DWORD *thisPtr,
    void * /*edxUnused*/,
    int pExceptionObject,
    int a3)
{
    const LONG result = oLocalIndependentPotentialFlatExtendedDisplayFn
        ? oLocalIndependentPotentialFlatExtendedDisplayFn(thisPtr, pExceptionObject, a3)
        : 0;
    static DWORD s_lastFlatExtendedCallLogTick = 0;
    const DWORD now = GetTickCount();
    const int d28 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x28);
    const int d2C = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x2C);
    const int d30 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x30);
    const int d34 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x34);
    const int d38 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x38);
    const int d3C = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x3C);
    const int d40 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x40);
    const int d44 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0x44);
    const int dC8 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0xC8);
    const int dCC = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0xCC);
    const int dD0 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0xD0);
    const int dD4 = SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(0xD4);
    if (EnableIndependentBuffOverlayDiagnosticLogs() &&
        now - s_lastFlatExtendedCallLogTick > 1000)
    {
        s_lastFlatExtendedCallLogTick = now;
        WriteLogFmt("[IndependentBuffLocalDisplayCall] 856830 this=0x%08X exc=0x%08X a3=%d deltas=[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d] active=%d",
            (DWORD)(uintptr_t)thisPtr,
            (DWORD)pExceptionObject,
            a3,
            d28, d2C, d30, d34, d38, d3C, d40, d44, dC8, dCC, dD0, dD4,
            SkillOverlayBridgeHasLocalIndependentPotentialBonuses() ? 1 : 0);
    }
    LogAbilityRedDecodedSnapshot("856830");
    hkApplyLocalIndependentPotentialFlatExtendedDisplay(reinterpret_cast<uintptr_t>(thisPtr));
    return result;
}

static void ApplyVirtualStatusBarBuffShift(uintptr_t statusBar, StatusBarObservedBuffSlot* slots, const char* reason)
{
    if (!statusBar || !slots)
        return;

    std::vector<IndependentBuffOverlayEntry> entries;
    SkillOverlayBridgeGetIndependentBuffOverlayEntries(entries);
    const int virtualCount = (int)entries.size();
    if (virtualCount <= 0 || virtualCount >= 6)
        return;

    struct VisibleSlotRef
    {
        int slotIndex = 0;
        int x = 0;
        int y = 0;
        int renderX = 0;
        int renderY = 0;
        int w = 0;
        int h = 0;
        uintptr_t child = 0;
    };

    std::vector<VisibleSlotRef> visibleSlots;
    visibleSlots.reserve(6);
    for (int i = 0; i < 6; ++i)
    {
        if (!slots[i].child || slots[i].w <= 0 || slots[i].h <= 0)
            continue;

        VisibleSlotRef slot = {};
        slot.slotIndex = i;
        slot.x = slots[i].x;
        slot.y = slots[i].y;
        slot.renderX = slots[i].renderX;
        slot.renderY = slots[i].renderY;
        slot.w = slots[i].w;
        slot.h = slots[i].h;
        slot.child = slots[i].child;
        visibleSlots.push_back(slot);
    }

    if (visibleSlots.empty() || (int)visibleSlots.size() + virtualCount > 6)
        return;

    std::sort(visibleSlots.begin(), visibleSlots.end(),
        [](const VisibleSlotRef& left, const VisibleSlotRef& right)
        {
            return left.slotIndex < right.slotIndex;
        });

    int stepX = 0;
    int stepRenderX = 0;
    for (size_t i = 1; i < visibleSlots.size(); ++i)
    {
        const int deltaIndex = visibleSlots[i].slotIndex - visibleSlots[i - 1].slotIndex;
        const int deltaX = visibleSlots[i].x - visibleSlots[i - 1].x;
        const int deltaRenderX = visibleSlots[i].renderX - visibleSlots[i - 1].renderX;
        if (deltaIndex > 0 && deltaX > 0)
        {
            const int candidateX = deltaX / deltaIndex;
            if (candidateX > 0 && (stepX == 0 || candidateX < stepX))
                stepX = candidateX;
        }
        if (deltaIndex > 0 && deltaRenderX > 0)
        {
            const int candidateRenderX = deltaRenderX / deltaIndex;
            if (candidateRenderX > 0 && (stepRenderX == 0 || candidateRenderX < stepRenderX))
                stepRenderX = candidateRenderX;
        }
    }

    if (stepX <= 0)
        stepX = visibleSlots[0].w + 2;
    if (stepX <= 0)
        return;

    if (stepRenderX <= 0)
        stepRenderX = stepX;

    const int baseX = visibleSlots[0].x - visibleSlots[0].slotIndex * stepX;
    const int baseRenderX = visibleSlots[0].renderX - visibleSlots[0].slotIndex * stepRenderX;
    const int baseY = visibleSlots[0].y;
    const int baseRenderY = visibleSlots[0].renderY;

    for (int order = (int)visibleSlots.size() - 1; order >= 0; --order)
    {
        const VisibleSlotRef& slot = visibleSlots[(size_t)order];
        const uintptr_t child = slot.child;
        if (!child || SafeIsBadReadPtr((void*)child, 0x4C))
            continue;

        const int targetIndex = order + virtualCount;
        const int targetX = baseX + targetIndex * stepX;
        const int targetRenderX = baseRenderX + targetIndex * stepRenderX;
        if (slot.x == targetX && slot.renderX == targetRenderX)
            continue;

        CWnd_SetComPos(child, targetX, baseY);
        CWnd_SetRenderPos(child, targetRenderX, baseRenderY);
        WriteLogFmt("[StatusBarBuffSlotShift] reason=%s child=0x%08X fromSlot=%d toSlot=%d x=%d->%d renderX=%d->%d virtualCount=%d",
            reason ? reason : "unknown",
            (DWORD)child,
            slot.slotIndex,
            targetIndex,
            slot.x,
            targetX,
            slot.renderX,
            targetRenderX,
            virtualCount);

        slots[slot.slotIndex].x = targetX;
        slots[slot.slotIndex].renderX = targetRenderX;
        slots[slot.slotIndex].y = baseY;
        slots[slot.slotIndex].renderY = baseRenderY;
    }
}

static void ObserveStatusBarBuffSlots(uintptr_t statusBar, const char* reason)
{
    if (!statusBar || SafeIsBadReadPtr((void*)statusBar, 0xB30 + 4))
        return;

    SkillOverlayBridgeSetObservedStatusBarPtr(statusBar);

    StatusBarObservedBuffSlot current[9] = {};
    for (int i = 0; i < 9; ++i)
    {
        const uintptr_t slotAddr = statusBar + (i < 6 ? (0xAE8 + i * 8) : (0xB18 + (i - 6) * 8));
        if (SafeIsBadReadPtr((void*)slotAddr, 4))
            continue;

        const uintptr_t wrapper = *(uintptr_t*)slotAddr;
        current[i].wrapper = wrapper;
        if (!wrapper || SafeIsBadReadPtr((void*)wrapper, 8))
            continue;

        const uintptr_t child = wrapper + 4;
        current[i].child = child;
        if (!child || SafeIsBadReadPtr((void*)child, 0x4C))
            continue;

        current[i].x = CWnd_GetX(child);
        current[i].y = CWnd_GetY(child);
        current[i].w = CWnd_GetWidth(child);
        current[i].h = CWnd_GetHeight(child);
        current[i].renderX = CWnd_GetRenderX(child);
        current[i].renderY = CWnd_GetRenderY(child);
    }

    int activeVisibleCount = 0;
    int firstVisibleX = -1;
    for (int i = 0; i < 9; ++i)
    {
        const bool slotLooksVisible =
            current[i].child &&
            current[i].w >= 16 &&
            current[i].h >= 16 &&
            (current[i].x != 0 || current[i].y != 0 || current[i].renderX != 0 || current[i].renderY != 0);
        if (slotLooksVisible)
        {
            ++activeVisibleCount;
            if (firstVisibleX < 0 || current[i].x < firstVisibleX)
                firstVisibleX = current[i].x;
        }
    }
    if (activeVisibleCount > 0)
        SkillOverlayBridgeSetObservedNativeVisibleBuffVisualCount(activeVisibleCount);
    else
        SkillOverlayBridgeSetObservedNativeVisibleBuffVisualCount(-1);
    if (activeVisibleCount > 0 && firstVisibleX >= 0)
        SkillOverlayBridgeSetObservedNativeVisibleBuffAnchorX(firstVisibleX);
    else
        SkillOverlayBridgeSetObservedNativeVisibleBuffAnchorX(-1);

    static DWORD s_lastStatusBarBuffSummaryLogTick = 0;
    const DWORD nowTick = GetTickCount();
    if (nowTick - s_lastStatusBarBuffSummaryLogTick > 1000)
    {
        s_lastStatusBarBuffSummaryLogTick = nowTick;
        unsigned int wrapperMask = 0;
        unsigned int childMask = 0;
        unsigned int visibleMask = 0;
        for (int i = 0; i < 6; ++i)
        {
            if (current[i].wrapper)
                wrapperMask |= (1u << i);
            if (current[i].child)
                childMask |= (1u << i);
            if (current[i].child &&
                current[i].w >= 16 &&
                current[i].h >= 16 &&
                (current[i].x != 0 || current[i].y != 0 || current[i].renderX != 0 || current[i].renderY != 0))
                visibleMask |= (1u << i);
        }
        WriteLogFmt("[StatusBarBuffSlotSummary] reason=%s statusBar=0x%08X topWr=0x%02X topChild=0x%02X topVisible=0x%02X activeVisible=%d firstVisibleX=%d B30=0x%08X",
            reason ? reason : "unknown",
            (DWORD)statusBar,
            wrapperMask,
            childMask,
            visibleMask,
            activeVisibleCount,
            firstVisibleX,
            *(DWORD*)(statusBar + 0xB30));
    }

    bool changed = false;
    for (int i = 0; i < 9; ++i)
    {
        const StatusBarObservedBuffSlot& prev = g_StatusBarObservedBuffSlots[i];
        const StatusBarObservedBuffSlot& now = current[i];
        if (prev.wrapper != now.wrapper ||
            prev.child != now.child ||
            prev.x != now.x || prev.y != now.y ||
            prev.w != now.w || prev.h != now.h ||
            prev.renderX != now.renderX || prev.renderY != now.renderY)
        {
            changed = true;
            WriteLogFmt("[StatusBarBuffSlot] reason=%s slot=%d wrapper=0x%08X child=0x%08X rect=(%d,%d,%d,%d) render=(%d,%d)",
                reason ? reason : "unknown",
                i,
                (DWORD)now.wrapper,
                (DWORD)now.child,
                now.x,
                now.y,
                now.w,
                now.h,
                now.renderX,
                now.renderY);
        }
    }

    if (changed)
    {
        for (int i = 0; i < 9; ++i)
            g_StatusBarObservedBuffSlots[i] = current[i];
    }

    // Keep native buff icons in their original slots. Overlay icons render in a
    // separate block on the left, so shifting native children here only scrambles
    // the observed order during status-bar refreshes.
}

static void LogStatusBarHookSeen(const char* reason, uintptr_t thisPtr)
{
    static DWORD s_lastStatusBarHookSeenLogTick = 0;
    const DWORD nowTick = GetTickCount();
    if (nowTick - s_lastStatusBarHookSeenLogTick < 1000)
        return;
    s_lastStatusBarHookSeenLogTick = nowTick;
    WriteLogFmt("[StatusBarHookSeen] reason=%s this=0x%08X readable=%d",
        reason ? reason : "unknown",
        (DWORD)thisPtr,
        (!thisPtr || SafeIsBadReadPtr((void*)thisPtr, 0xB30 + 4)) ? 0 : 1);
}

static void __cdecl hkStatusBarRefreshSlotsPrimaryHandler(uintptr_t thisPtr)
{
    LogStatusBarHookSeen("9F4F00", thisPtr);
    SkillOverlayBridgeSetObservedStatusBarPtr(thisPtr);
    if (oStatusBarRefreshSlotsPrimary)
        oStatusBarRefreshSlotsPrimary(thisPtr);
    ObserveStatusBarBuffSlots(thisPtr, "9F4F00");
}

static void __cdecl hkStatusBarRefreshSlotsSecondaryHandler(uintptr_t thisPtr)
{
    LogStatusBarHookSeen("9F4C30", thisPtr);
    SkillOverlayBridgeSetObservedStatusBarPtr(thisPtr);
    if (oStatusBarRefreshSlotsSecondary)
        oStatusBarRefreshSlotsSecondary(thisPtr);
    ObserveStatusBarBuffSlots(thisPtr, "9F4C30");
}

static void __cdecl hkStatusBarRefreshInternalHandler(uintptr_t thisPtr)
{
    LogStatusBarHookSeen("9F5FE0", thisPtr);
    SkillOverlayBridgeSetObservedStatusBarPtr(thisPtr);
    if (oStatusBarRefreshInternal)
        oStatusBarRefreshInternal(thisPtr);
    ObserveStatusBarBuffSlots(thisPtr, "9F5FE0");
}

static void __cdecl hkStatusBarCleanupTransientHandler(uintptr_t thisPtr)
{
    LogStatusBarHookSeen("9FCAE0", thisPtr);
    SkillOverlayBridgeSetObservedStatusBarPtr(thisPtr);
    if (oStatusBarCleanupTransient)
        oStatusBarCleanupTransient(thisPtr);
    ObserveStatusBarBuffSlots(thisPtr, "9FCAE0");
}

static void LogStatusBarTransientState(uintptr_t thisPtr, const char* reason, int extra)
{
    if (!thisPtr || SafeIsBadReadPtr((void*)thisPtr, 0xB70))
        return;

    const DWORD slotAD8 = *(DWORD*)(thisPtr + 0xAD8);
    const DWORD ptrB2C0 = *(DWORD*)(thisPtr + 0xB2C);
    const DWORD ptrB2C4 = *(DWORD*)(thisPtr + 0xB30);
    const DWORD ptrB20 = *(DWORD*)(thisPtr + 0xB20);
    const DWORD ptrB24 = *(DWORD*)(thisPtr + 0xB24);
    const DWORD ptrB28 = *(DWORD*)(thisPtr + 0xB28);
    const DWORD ptrB5C = *(DWORD*)(thisPtr + 0xB5C);

    int childX = 0, childY = 0, childW = 0, childH = 0, childRenderX = 0, childRenderY = 0;
    if (ptrB2C4 && !SafeIsBadReadPtr((void*)ptrB2C4, 0x4C))
    {
        childX = CWnd_GetX(ptrB2C4);
        childY = CWnd_GetY(ptrB2C4);
        childW = CWnd_GetWidth(ptrB2C4);
        childH = CWnd_GetHeight(ptrB2C4);
        childRenderX = CWnd_GetRenderX(ptrB2C4);
        childRenderY = CWnd_GetRenderY(ptrB2C4);
    }

    WriteLogFmt(
        "[StatusBarTransient] %s this=0x%08X extra=%d AD8=%u B20=0x%08X B24=0x%08X B28=0x%08X B2C=0x%08X B30=0x%08X B5C=0x%08X childRect=(%d,%d,%d,%d) render=(%d,%d)",
        reason ? reason : "unknown",
        (DWORD)thisPtr,
        extra,
        slotAD8,
        ptrB20,
        ptrB24,
        ptrB28,
        ptrB2C0,
        ptrB2C4,
        ptrB5C,
        childX,
        childY,
        childW,
        childH,
        childRenderX,
        childRenderY);
}

static int __cdecl hkStatusBarTransientRefreshHandler(uintptr_t thisPtr, int a2)
{
    LogStatusBarHookSeen("9FC110", thisPtr);
    SkillOverlayBridgeSetObservedStatusBarPtr(thisPtr);
    const int result = oStatusBarTransientRefresh ? oStatusBarTransientRefresh(thisPtr, a2) : 0;
    LogStatusBarTransientState(thisPtr, "9FC110", result);
    ObserveStatusBarBuffSlots(thisPtr, "9FC110");
    return result;
}

static void __cdecl hkStatusBarTransientDispatchHandler(uintptr_t thisPtr, int a2)
{
    LogStatusBarHookSeen("9FCC10", thisPtr);
    SkillOverlayBridgeSetObservedStatusBarPtr(thisPtr);
    if (oStatusBarTransientDispatch)
        oStatusBarTransientDispatch(thisPtr, a2);
    LogStatusBarTransientState(thisPtr, "9FCC10", a2);
    ObserveStatusBarBuffSlots(thisPtr, "9FCC10");
}

static LONG* __cdecl hkStatusBarTransientToggleHandler(int a1)
{
    LONG* result = oStatusBarTransientToggle ? oStatusBarTransientToggle(a1) : nullptr;
    DWORD statusBar = 0;
    if (!SafeIsBadReadPtr((void*)ADDR_StatusBar, 4))
        statusBar = *(DWORD*)ADDR_StatusBar;
    if (statusBar)
    {
        LogStatusBarHookSeen("9FCBD0", statusBar);
        SkillOverlayBridgeSetObservedStatusBarPtr(statusBar);
        LogStatusBarTransientState(statusBar, "9FCBD0", a1);
        ObserveStatusBarBuffSlots(statusBar, "9FCBD0");
    }
    return result;
}

__declspec(naked) static void hkStatusBarRefreshSlotsPrimaryNaked()
{
    __asm {
        push ecx
        call hkStatusBarRefreshSlotsPrimaryHandler
        add esp, 4
        ret
    }
}

__declspec(naked) static void hkStatusBarRefreshSlotsSecondaryNaked()
{
    __asm {
        push ecx
        call hkStatusBarRefreshSlotsSecondaryHandler
        add esp, 4
        ret
    }
}

__declspec(naked) static void hkStatusBarRefreshInternalNaked()
{
    __asm {
        push ecx
        call hkStatusBarRefreshInternalHandler
        add esp, 4
        ret
    }
}

__declspec(naked) static void hkStatusBarCleanupTransientNaked()
{
    __asm {
        push ecx
        call hkStatusBarCleanupTransientHandler
        add esp, 4
        ret
    }
}

__declspec(naked) static void hkStatusBarTransientRefreshNaked()
{
    __asm {
        mov eax, [esp + 4]
        push eax
        push ecx
        call hkStatusBarTransientRefreshHandler
        add esp, 8
        ret 4
    }
}

__declspec(naked) static void hkStatusBarTransientDispatchNaked()
{
    __asm {
        mov eax, [esp + 4]
        push eax
        push ecx
        call hkStatusBarTransientDispatchHandler
        add esp, 8
        ret 4
    }
}

__declspec(naked) static void hkStatusBarTransientToggleNaked()
{
    __asm {
        mov eax, [esp + 4]
        push eax
        call hkStatusBarTransientToggleHandler
        add esp, 4
        ret 4
    }
}

static bool PatchBytesIfExpected(DWORD address, const BYTE *expected, const BYTE *patch, size_t length, const char *label)
{
    if (!address || !expected || !patch || !length)
        return false;

    BYTE *target = (BYTE *)(uintptr_t)address;
    for (size_t i = 0; i < length; ++i)
    {
        if (target[i] != expected[i])
        {
            WriteLogFmt("[RuntimePatch] skip %s at 0x%08X: byte[%u]=0x%02X expected=0x%02X",
                        label ? label : "unknown",
                        address,
                        (unsigned int)i,
                        (unsigned int)target[i],
                        (unsigned int)expected[i]);
            return false;
        }
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        WriteLogFmt("[RuntimePatch] FAIL %s at 0x%08X: VirtualProtect", label ? label : "unknown", address);
        return false;
    }

    for (size_t i = 0; i < length; ++i)
        target[i] = patch[i];

    VirtualProtect(target, length, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, length);
    WriteLogFmt("[RuntimePatch] OK %s at 0x%08X len=%u",
                label ? label : "unknown",
                address,
                (unsigned int)length);
    return true;
}

static bool PatchNopsIfExpected(DWORD address, const BYTE *expected, size_t length, const char *label)
{
    BYTE nops[16] = {};
    if (length > sizeof(nops))
        return false;

    for (size_t i = 0; i < length; ++i)
        nops[i] = 0x90;

    return PatchBytesIfExpected(address, expected, nops, length, label);
}

static bool ApplyMountMovementCapPatches()
{
    bool ok = false;

    // Root cause for mounted 190/123: 888DF0 clamps the raw TamingMob data while
    // building the cached mount movement object. Keep the original 80 floor for
    // speed, but remove the upper caps so boosted data survives into later paths.
    static const BYTE kMountLoaderSpeedUpperClamp[] = {
        0x3D, 0xBE, 0x00, 0x00, 0x00,
        0x7C, 0x05,
        0xB8, 0xBE, 0x00, 0x00, 0x00};
    if (PatchNopsIfExpected(
            ADDR_889079,
            kMountLoaderSpeedUpperClamp,
            sizeof(kMountLoaderSpeedUpperClamp),
            "mount loader speed upper cap"))
    {
        ok = true;
    }

    static const BYTE kMountLoaderJumpUpperClamp[] = {
        0x83, 0xF8, 0x7B,
        0x7C, 0x05,
        0xB8, 0x7B, 0x00, 0x00, 0x00};
    if (PatchNopsIfExpected(
            ADDR_8890F7,
            kMountLoaderJumpUpperClamp,
            sizeof(kMountLoaderJumpUpperClamp),
            "mount loader jump upper cap"))
    {
        ok = true;
    }

    // B92D10 is the normal mounted data path. It writes the mount data img's raw
    // speed/jump first, then optionally overwrites both with legacy recompute
    // helpers (84CD10 / B92260). For custom mounts with boosted data this sends
    // the values straight back to the old 190/123 family. Keep the raw data.
    static const BYTE kMountedRawSpeedOverrideGate[] = {0x74, 0x13};
    static const BYTE kMountedRawSpeedOverrideGatePatch[] = {0xEB, 0x13};
    if (PatchBytesIfExpected(
            ADDR_B9301F,
            kMountedRawSpeedOverrideGate,
            kMountedRawSpeedOverrideGatePatch,
            sizeof(kMountedRawSpeedOverrideGate),
            "mounted raw speed override gate"))
    {
        ok = true;
    }

    static const BYTE kMountedRawJumpOverrideGate[] = {0x74, 0x0F};
    static const BYTE kMountedRawJumpOverrideGatePatch[] = {0xEB, 0x0F};
    if (PatchBytesIfExpected(
            ADDR_B93038,
            kMountedRawJumpOverrideGate,
            kMountedRawJumpOverrideGatePatch,
            sizeof(kMountedRawJumpOverrideGate),
            "mounted raw jump override gate"))
    {
        ok = true;
    }

    // AA0B90 is the mounted-only movement path.
    // Keep its original additive logic, but remove the two final upper-cap paths:
    // 1) special branch: [150,170] -> [150,+inf)
    // 2) ground branch: min(max(speed+30,130), mountBase+150) -> max(speed+30,130)
    static const BYTE kMountedSpecialUpperCapGate[] = {0x7C, 0x22};
    static const BYTE kMountedSpecialUpperCapGatePatch[] = {0xEB, 0x22};
    if (PatchBytesIfExpected(
            ADDR_AA0CFB,
            kMountedSpecialUpperCapGate,
            kMountedSpecialUpperCapGatePatch,
            sizeof(kMountedSpecialUpperCapGate),
            "mounted special speed upper cap gate"))
    {
        ok = true;
    }

    static const BYTE kMountedGroundUpperClamp[] = {
        0x8D, 0xB3, 0x96, 0x00, 0x00, 0x00,
        0x3B, 0xC6,
        0x7D, 0x02,
        0x8B, 0xF0
    };
    static const BYTE kMountedGroundUpperClampPatch[] = {
        0x8B, 0xF0,
        0x90, 0x90, 0x90, 0x90,
        0x90, 0x90,
        0x90, 0x90,
        0x90, 0x90
    };
    if (PatchBytesIfExpected(
            ADDR_AA0D13,
            kMountedGroundUpperClamp,
            kMountedGroundUpperClampPatch,
            sizeof(kMountedGroundUpperClamp),
            "mounted ground speed upper clamp"))
    {
        ok = true;
    }

    // B93B80 is the shared movement stat clamp path that feeds the mounted
    // speed/jump numbers seen in panel/output. Remove only the upper caps so
    // high mounted values can propagate without breaking the original floors.
    static const BYTE kMountedOutputSpeedUpperClamp[] = {0x3B, 0xC6, 0x7C, 0x02, 0x8B, 0xC6};
    if (PatchNopsIfExpected(
            ADDR_B93D0F,
            kMountedOutputSpeedUpperClamp,
            sizeof(kMountedOutputSpeedUpperClamp),
            "mounted output speed upper cap"))
    {
        ok = true;
    }

    static const BYTE kMountedOutputJumpUpperClamp[] = {0x3B, 0xFD, 0x7D, 0x02, 0x8B, 0xEF};
    static const BYTE kMountedOutputJumpUpperClampPatch[] = {0x8B, 0xEF, 0x90, 0x90, 0x90, 0x90};
    if (PatchBytesIfExpected(
            ADDR_B93D23,
            kMountedOutputJumpUpperClamp,
            kMountedOutputJumpUpperClampPatch,
            sizeof(kMountedOutputJumpUpperClamp),
            "mounted output jump upper cap"))
    {
        ok = true;
    }

    static const BYTE kMountedOutputMode2SpeedUpperClamp[] = {0x3B, 0xDE, 0x7D, 0x02, 0x8B, 0xF3};
    static const BYTE kMountedOutputMode2SpeedUpperClampPatch[] = {0x8B, 0xF3, 0x90, 0x90, 0x90, 0x90};
    if (PatchBytesIfExpected(
            ADDR_B93D4A,
            kMountedOutputMode2SpeedUpperClamp,
            kMountedOutputMode2SpeedUpperClampPatch,
            sizeof(kMountedOutputMode2SpeedUpperClamp),
            "mounted output mode2 speed upper cap"))
    {
        ok = true;
    }

    // Keep the lower bound (100) intact, but remove the final upper clamp.
    static const BYTE kSpeedUpperClamp[] = {0x3B, 0xD7, 0x7C, 0x02, 0x8B, 0xD7};
    if (PatchNopsIfExpected(ADDR_858D30, kSpeedUpperClamp, sizeof(kSpeedUpperClamp), "movement speed upper cap"))
        ok = true;

    static const BYTE kJumpUpperCompare[] = {0x83, 0xF8, 0x7B};
    if (PatchNopsIfExpected(ADDR_858D49, kJumpUpperCompare, sizeof(kJumpUpperCompare), "movement jump cap cmp"))
        ok = true;

    static const BYTE kJumpUpperClamp[] = {0x7C, 0x05, 0xBA, 0x7B, 0x00, 0x00, 0x00};
    if (PatchNopsIfExpected(ADDR_858D4E, kJumpUpperClamp, sizeof(kJumpUpperClamp), "movement jump upper cap"))
        ok = true;

    return ok;
}

static void __cdecl hkSendPacketInspect(void **packetDataSlot, int *packetLenSlot, uintptr_t callerRetAddr)
{
    TryRewriteMountedDemonJumpOutgoingPacket(
        packetDataSlot,
        packetLenSlot,
        callerRetAddr);
    SkillOverlayBridgeInspectOutgoingPacketMutable(packetDataSlot, packetLenSlot, callerRetAddr);
}

__declspec(naked) static void hkSendPacketNaked()
{
    __asm {
        pushad
        mov edx, [esp + 32]
        push edx
        lea eax, [esp + 44]
        push eax
        lea ecx, [esp + 44]
        push ecx
        call hkSendPacketInspect
        add esp, 12
        popad
        call dword ptr [g_SendPacketOriginalCallTarget]
        jmp [oSendPacket]
    }
}

static void __cdecl hkRecvPacketInspect(void *inPacket, int opcode, uintptr_t callerRetAddr)
{
    SkillOverlayBridgeInspectIncomingPacket(inPacket, opcode, callerRetAddr);
}

__declspec(naked) static void hkRecvPacketNaked()
{
    __asm {
        movzx eax, ax
        lea ecx, dword ptr [eax - 0x10]
        pushad
        push 0
        push eax
        push esi
        call hkRecvPacketInspect
        add esp, 12
        popad
        cmp ecx, 0xA
        jmp [oRecvPacket]
    }
}

__declspec(naked) static void hkRecvPacketNakedFallback()
{
    __asm {
        pushfd
        pushad
        movzx eax, ax
        push 0
        push eax
        push esi
        call hkRecvPacketInspect
        add esp, 12
        popad
        popfd
        jmp [oRecvPacket]
    }
}

static bool MatchesRecvPacketDirectPrologue(const BYTE *code)
{
    if (!code)
        return false;

    return code[0] == 0x0F && code[1] == 0xB7 && code[2] == 0xC0 &&
           code[3] == 0x8D && code[4] == 0x48 && code[5] == 0xF0 &&
           code[6] == 0x83 && code[7] == 0xF9 && code[8] == 0x0A;
}

static void __cdecl hkLocalIndependentPotentialFlatStatsPrepare(uintptr_t sourcePtr)
{
    // 853B49 / 853E5A / 856879 feed the main ability display objects.
    // If we substitute a boosted buffer here, the client bakes the delta into
    // the primary shown value instead of keeping it as a red bonus delta.
    g_LocalIndependentPotentialPreparedPtr = sourcePtr;
}

__declspec(naked) static void hkLocalIndependentPotentialFlatStatsNaked()
{
    __asm {
        test eax, eax
        je no_source
        mov edi, [esp + 0x2C]
        imul edi, edi, 0xF0
        add edi, [eax + 0x18]
        jmp push_prepare
no_source:
        xor edi, edi
push_prepare:
        pushad
        push edi
        call hkLocalIndependentPotentialFlatStatsPrepare
        add esp, 4
        popad
        mov edi, dword ptr [g_LocalIndependentPotentialPreparedPtr]
        test edi, edi
        jne continue_nonzero
        jmp dword ptr [g_LocalIndependentPotentialContinueZero]
continue_nonzero:
        jmp dword ptr [g_LocalIndependentPotentialContinueNonZero]
    }
}

__declspec(naked) static void hkLocalIndependentPotentialPrimaryFlatStatsNaked()
{
    __asm {
        test eax, eax
        je no_source
        mov edi, [esp + 0x2C]
        mov edx, [esi + 0x28]
        imul edi, edi, 0xF0
        add edi, [eax + 0x18]
        jmp push_prepare
no_source:
        xor edi, edi
push_prepare:
        pushad
        push edi
        call hkLocalIndependentPotentialFlatStatsPrepare
        add esp, 4
        popad
        mov edi, dword ptr [g_LocalIndependentPotentialPreparedPtr]
        test edi, edi
        jne prepared_nonzero
        test eax, eax
        jne original_nonzero
        jmp dword ptr [g_LocalIndependentPotentialPrimaryContinueZero]
original_nonzero:
        mov edi, [esp + 0x2C]
        mov edx, [esi + 0x28]
        imul edi, edi, 0xF0
        add edi, [eax + 0x18]
        jmp dword ptr [g_LocalIndependentPotentialPrimaryContinueNonZero]
prepared_nonzero:
        mov edx, [esi + 0x28]
        jmp dword ptr [g_LocalIndependentPotentialPrimaryContinueNonZero]
    }
}

__declspec(naked) static void hkLocalIndependentPotentialPrimaryPercentStatsNaked()
{
    __asm {
        test ebp, ebp
        je no_source
        mov esi, [esp + 0x30]
        mov ecx, [esp + 0x34]
        imul esi, esi, 0xF0
        add esi, [ebp + 0x18]
        jmp push_prepare
no_source:
        xor esi, esi
push_prepare:
        pushad
        push esi
        call hkLocalIndependentPotentialFlatStatsPrepare
        add esp, 4
        popad
        mov esi, dword ptr [g_LocalIndependentPotentialPreparedPtr]
        test esi, esi
        jne prepared_nonzero
        test ebp, ebp
        jne original_nonzero
        jmp dword ptr [g_LocalIndependentPotentialPrimaryPercentContinueZero]
original_nonzero:
        mov esi, [esp + 0x30]
        mov ecx, [esp + 0x34]
        imul esi, esi, 0xF0
        add esi, [ebp + 0x18]
        jmp dword ptr [g_LocalIndependentPotentialPrimaryPercentContinueNonZero]
prepared_nonzero:
        mov ecx, [esp + 0x34]
        jmp dword ptr [g_LocalIndependentPotentialPrimaryPercentContinueNonZero]
    }
}

static BYTE *TryFollowAbsoluteRegisterJumpStub(BYTE *code)
{
    if (!code)
        return nullptr;

    const BYTE movOpcode = code[0];
    if (movOpcode < 0xB8 || movOpcode > 0xBF)
        return nullptr;

    const BYTE regIndex = (BYTE)(movOpcode - 0xB8);
    if (code[5] != 0xFF || code[6] != (BYTE)(0xE0 + regIndex))
        return nullptr;

    const DWORD target = *(DWORD *)(code + 1);
    if (target == 0 || target == (DWORD)(uintptr_t)code)
        return nullptr;

    return FollowJmpChain((void *)(uintptr_t)target);
}

static uintptr_t TryExtractPotentialIncreaseAddressFromRecvStub(BYTE* stubTarget)
{
    if (!stubTarget)
        return 0;

    for (size_t i = 0; i + 10 < 0x200; ++i)
    {
        BYTE* p = stubTarget + i;
        if (p[0] == 0x6B && p[1] == 0xC0 && p[2] == 0x04 &&
            p[3] == 0x05 && p[8] == 0x89 && p[9] == 0x08)
        {
            return *(DWORD*)(p + 4);
        }
    }

    return 0;
}

static bool PatchExternalPotentialIncreaseStub(BYTE* stubTarget)
{
    if (!stubTarget || !g_ExternalPotentialIncreaseAddressRuntime)
        return false;

    BYTE* writeSite = nullptr;
    BYTE* clearSite = nullptr;

    for (size_t i = 0; i + 5 < 0x200; ++i)
    {
        BYTE* p = stubTarget + i;
        if (!writeSite &&
            p[0] == 0x89 && p[1] == 0x08 &&
            p[2] == 0x90 &&
            p[3] == 0xEB)
        {
            writeSite = p;
        }

        if (!clearSite &&
            p[0] == 0xBF &&
            *(DWORD*)(p + 1) == (DWORD)g_ExternalPotentialIncreaseAddressRuntime &&
            p[5] == 0x31 && p[6] == 0xC0 &&
            p[7] == 0xB9 && *(DWORD*)(p + 8) == 0x128 &&
            p[12] == 0xC1 && p[13] == 0xE9 && p[14] == 0x02 &&
            p[15] == 0xF3 && p[16] == 0xAB)
        {
            clearSite = p;
        }

        if (writeSite && clearSite)
            break;
    }

    if (!writeSite)
    {
        WriteLog("[PacketHook] external potential write site not found");
        return false;
    }

    if (!clearSite)
    {
        WriteLog("[PacketHook] external potential clear site not found");
        return false;
    }

    g_ExternalPotentialWriteContinue = (DWORD)(uintptr_t)(writeSite + 5);
    g_ExternalPotentialWriteLoopTarget = (DWORD)(uintptr_t)(writeSite + 5 + (signed char)writeSite[4]);
    g_ExternalPotentialClearContinue = (DWORD)(uintptr_t)(clearSite + 17);

    DWORD oldProtect = 0;
    if (!VirtualProtect(writeSite, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        WriteLog("[PacketHook] external potential write patch protect failed");
        return false;
    }
    writeSite[0] = 0xE9;
    *(int*)(writeSite + 1) = (int)((uintptr_t)hkExternalPotentialWriteNaked - (uintptr_t)writeSite - 5);
    VirtualProtect(writeSite, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), writeSite, 5);
    oExternalPotentialWritePatch = writeSite;

    if (!VirtualProtect(clearSite, 17, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        WriteLog("[PacketHook] external potential clear patch protect failed");
        return false;
    }
    clearSite[0] = 0xE9;
    *(int*)(clearSite + 1) = (int)((uintptr_t)hkExternalPotentialClearNaked - (uintptr_t)clearSite - 5);
    for (int i = 5; i < 17; ++i)
        clearSite[i] = 0x90;
    VirtualProtect(clearSite, 17, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), clearSite, 17);
    oExternalPotentialClearPatch = clearSite;

    WriteLogFmt("[PacketHook] external potential stub patched write=0x%08X loop=0x%08X clear=0x%08X continue=0x%08X",
        (DWORD)(uintptr_t)writeSite,
        g_ExternalPotentialWriteLoopTarget,
        (DWORD)(uintptr_t)clearSite,
        g_ExternalPotentialClearContinue);
    return true;
}

static void __cdecl hkExternalPotentialWriteApplied(uintptr_t writeAddress, int baseValue)
{
    SkillOverlayBridgeApplyPotentialBaseValue(writeAddress, baseValue);
}

static void __cdecl hkExternalPotentialClearApplied()
{
    SkillOverlayBridgeClearPotentialBaseValues();
}

__declspec(naked) static void hkExternalPotentialWriteNaked()
{
    __asm {
        mov dword ptr [eax], ecx
        pushad
        push ecx
        push eax
        call hkExternalPotentialWriteApplied
        add esp, 8
        popad
        jmp dword ptr [g_ExternalPotentialWriteLoopTarget]
    }
}

__declspec(naked) static void hkExternalPotentialClearNaked()
{
    __asm {
        mov edi, dword ptr [g_ExternalPotentialIncreaseAddressRuntime]
        xor eax, eax
        mov ecx, 0x128
        shr ecx, 2
        rep stosd
        pushad
        call hkExternalPotentialClearApplied
        add esp, 0
        popad
        jmp dword ptr [g_ExternalPotentialClearContinue]
    }
}

static void __cdecl hkSkillReleaseClassifierDispatch(int skillId)
{
    const DWORD forcedJump =
        SkillOverlayBridgeResolveNativeReleaseJumpTarget(skillId);
    if (IsMountedDemonJumpRelatedSkillId(skillId))
    {
        static LONG s_mountedDemonJumpReleaseClassifierLogBudget = 24;
        const LONG budgetAfterDecrement =
            InterlockedDecrement(&s_mountedDemonJumpReleaseClassifierLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[MountDemonJump] B3144D classifier enter skill=%d jump=0x%08X",
                        skillId,
                        forcedJump);
        }
    }
    g_ForcedNativeReleaseJump = forcedJump;
}

static void __cdecl hkSkillReleaseClassifierRootDispatch(int skillId)
{
    DWORD overrideSkillId =
        (DWORD)SkillOverlayBridgeResolveNativeClassifierOverrideSkillId(skillId);
    if (overrideSkillId > 0 &&
        overrideSkillId != (DWORD)skillId &&
        (skillId == 30010183 ||
         skillId == 30010184 ||
         skillId == 30010186))
    {
        int mountItemId = 0;
        if (TryResolveMountedDemonJumpMountItemIdWithFallback(
                nullptr,
                &mountItemId,
                nullptr,
                1200) &&
            HasRecentMountedDemonJumpIntent(mountItemId) &&
            SkillOverlayBridgeCanUseMountedDemonJumpRuntimeSkill(mountItemId, skillId))
        {
            static LONG s_mountedDemonJumpRootChildOverrideSuppressLogBudget = 24;
            const LONG budgetAfterDecrement =
                InterlockedDecrement(&s_mountedDemonJumpRootChildOverrideSuppressLogBudget);
            if (budgetAfterDecrement >= 0)
            {
                WriteLogFmt(
                    "[MountDemonJump] B31349 keep native child skill=%d mount=%d suppressOverride=%d",
                    skillId,
                    mountItemId,
                    (int)overrideSkillId);
            }
            overrideSkillId = 0;
        }
    }
    if (IsMountedDemonJumpRelatedSkillId(skillId))
    {
        static LONG s_mountedDemonJumpReleaseRootLogBudget = 24;
        const LONG budgetAfterDecrement =
            InterlockedDecrement(&s_mountedDemonJumpReleaseRootLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[MountDemonJump] B31349 root enter skill=%d override=%d",
                        skillId,
                        (int)overrideSkillId);
        }
    }
    g_ClassifierOverrideSkillId = overrideSkillId;
}

static void __cdecl hkSkillReleaseClassifierB2F370Dispatch(int skillId)
{
    int overrideSkillId = SkillOverlayBridgeResolveNativeClassifierOverrideSkillId(skillId);
    if (overrideSkillId > 0 &&
        overrideSkillId != skillId &&
        (skillId == 30010183 ||
         skillId == 30010184 ||
         skillId == 30010186))
    {
        int mountItemId = 0;
        if (TryResolveMountedDemonJumpMountItemIdWithFallback(
                nullptr,
                &mountItemId,
                nullptr,
                1200) &&
            HasRecentMountedDemonJumpIntent(mountItemId) &&
            SkillOverlayBridgeCanUseMountedDemonJumpRuntimeSkill(mountItemId, skillId))
        {
            static LONG s_mountedDemonJumpChildOverrideSuppressLogBudget = 24;
            const LONG budgetAfterDecrement =
                InterlockedDecrement(&s_mountedDemonJumpChildOverrideSuppressLogBudget);
            if (budgetAfterDecrement >= 0)
            {
                WriteLogFmt(
                    "[MountDemonJump] B2F370 keep native child skill=%d mount=%d suppressOverride=%d",
                    skillId,
                    mountItemId,
                    overrideSkillId);
            }
            overrideSkillId = 0;
        }
    }
    g_ClassifierOverrideSkillId = (DWORD)overrideSkillId;
    if (overrideSkillId > 0 && overrideSkillId != skillId)
    {
        WriteLogFmt("[SkillReleaseHook] B2F370 override skillId=%d -> %d", skillId, overrideSkillId);
    }

    if (!kEnableMountedDoubleJumpRuntimeHooks)
    {
        return;
    }

    int mountItemId = 0;
    if (skillId > 0 &&
        TryResolveMountedDoubleJumpMountItemIdWithFallback(
            nullptr,
            &mountItemId,
            nullptr,
            1200) &&
        HasRecentMountedDoubleJumpIntent(mountItemId) &&
        SkillOverlayBridgeCanUseMountedDoubleJumpRuntimeSkill(mountItemId, skillId))
    {
        ObserveMountedDoubleJumpNativeRelease(mountItemId, skillId);
        static LONG s_mountedDoubleJumpNativeReleaseObserveLogBudget = 24;
        const LONG budgetAfterDecrement =
            InterlockedDecrement(&s_mountedDoubleJumpNativeReleaseObserveLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[MountDoubleJump] B2F370 observe native release mount=%d skill=%d",
                        mountItemId,
                        skillId);
        }
    }

    mountItemId = 0;
    if (skillId > 0 &&
        TryResolveMountedDemonJumpMountItemIdWithFallback(
            nullptr,
            &mountItemId,
            nullptr,
            1200) &&
        HasRecentMountedDemonJumpIntent(mountItemId) &&
        SkillOverlayBridgeCanUseMountedDemonJumpRuntimeSkill(mountItemId, skillId))
    {
        ObserveMountedDemonJumpNativeRelease(mountItemId, skillId);
        static LONG s_mountedDemonJumpNativeReleaseObserveLogBudget = 24;
        const LONG budgetAfterDecrement =
            InterlockedDecrement(&s_mountedDemonJumpNativeReleaseObserveLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[MountDemonJump] B2F370 observe native release mount=%d skill=%d",
                        mountItemId,
                        skillId);
        }
    }
}

static bool IsExtendedMountActionGateMount(int mountItemId)
{
    // 客户端 sub_4069E0 仍只硬编码放行到 1992015，导致 1999xxx 自定义坐骑
    // 即使 WZ 带 ladder/rope 资源、服务端也认可攀爬，case 51/52 仍会直接回退。
    if (mountItemId >= 1932016 && mountItemId <= 1999999)
    {
        return true;
    }
    return false;
}

static bool IsExtendedMountServerValidatedSoaringMount(int mountItemId)
{
    // 扩展 193x 坐骑统一放进 80001089 / family gate 链，最终能否飞行交给服务端判定。
    // 原生 1992xxx 飞行家族保持客户端原行为，避免回归已稳定的原生骑宠链。
    return mountItemId >= 1932016 && mountItemId < 1992000;
}

static int ResolveExtendedMountNativeFlightSkillId(int mountItemId)
{
    // 客户端原生只为 1992000..1992015 建了飞行技能映射。
    // 对 1999xxx 自定义坐骑统一复用一条稳定 donor 飞行链，避免“服务端允许飞行，
    // 但本地 jump+up / 二次起飞 / 80001089 链仍查不到 skillId”。
    if (IsExtendedMountServerValidatedSoaringMount(mountItemId))
    {
        // 扩展 193x 坐骑统一走 80001089；即使最终服务端不允许飞，也要先把客户端
        // 的原生 Soaring gate / 发包链接通，避免在本地提前死在 80001077 donor 分叉。
        return 80001089;
    }
    if (mountItemId >= 1932016 && mountItemId <= 1999999)
    {
        return 80001077;
    }
    return 0;
}

static bool IsExtendedMountFamilyGateMount(int mountItemId)
{
    return IsExtendedMountServerValidatedSoaringMount(mountItemId);
}

static bool IsExtendedMountSoaringContextMount(int mountItemId)
{
    return IsExtendedMountServerValidatedSoaringMount(mountItemId) ||
           mountItemId == 1992014 ||
           mountItemId == 1992018;
}

static const char *GetMountedRuntimeSkillLogTag(MountedRuntimeSkillKind kind)
{
    return kind == MountedRuntimeSkillKind_DemonJump
               ? "MountDemonJump"
               : "MountDoubleJump";
}

static int ResolveMountedRuntimeSkillIdForKind(
    MountedRuntimeSkillKind kind,
    int mountItemId)
{
    return kind == MountedRuntimeSkillKind_DemonJump
               ? SkillOverlayBridgeResolveMountedDemonJumpSkillId(mountItemId)
               : SkillOverlayBridgeResolveMountedDoubleJumpSkillId(mountItemId);
}

static bool CanUseMountedRuntimeSkillForKind(
    MountedRuntimeSkillKind kind,
    int mountItemId,
    int skillId)
{
    return kind == MountedRuntimeSkillKind_DemonJump
               ? SkillOverlayBridgeCanUseMountedDemonJumpSkill(mountItemId, skillId)
               : SkillOverlayBridgeCanUseMountedDoubleJumpSkill(mountItemId, skillId);
}

static bool CanUseMountedRuntimeSkillRuntimeForKind(
    MountedRuntimeSkillKind kind,
    int mountItemId,
    int skillId)
{
    return kind == MountedRuntimeSkillKind_DemonJump
               ? SkillOverlayBridgeCanUseMountedDemonJumpRuntimeSkill(mountItemId, skillId)
               : SkillOverlayBridgeCanUseMountedDoubleJumpRuntimeSkill(mountItemId, skillId);
}

static bool HasRecentMountedRuntimeRouteArmForKind(
    MountedRuntimeSkillKind kind,
    int mountItemId,
    DWORD maxAgeMs)
{
    return kind == MountedRuntimeSkillKind_DemonJump
               ? SkillOverlayBridgeHasRecentMountedDemonJumpRouteArm(mountItemId, maxAgeMs)
               : SkillOverlayBridgeHasRecentMountedDoubleJumpRouteArm(mountItemId, maxAgeMs);
}

static bool TryGetRecentMountedRuntimeRouteArmMountItemIdForKind(
    MountedRuntimeSkillKind kind,
    int *mountItemIdOut,
    DWORD maxAgeMs)
{
    return kind == MountedRuntimeSkillKind_DemonJump
               ? SkillOverlayBridgeTryGetRecentMountedDemonJumpRouteArmMountItemId(mountItemIdOut, maxAgeMs)
               : SkillOverlayBridgeTryGetRecentMountedDoubleJumpRouteArmMountItemId(mountItemIdOut, maxAgeMs);
}

static volatile LONG g_recentExtendedMountContextItemId = 0;
static volatile LONG g_recentExtendedMountContextTick = 0;
static volatile LONG g_recentMountedRuntimeSkillIntentItemId[MountedRuntimeSkillKind_Count] = {0};
static volatile LONG g_recentMountedRuntimeSkillIntentTick[MountedRuntimeSkillKind_Count] = {0};
static volatile LONG g_recentMountedRuntimeSkillNativeReleaseItemId[MountedRuntimeSkillKind_Count] = {0};
static volatile LONG g_recentMountedRuntimeSkillNativeReleaseSkillId[MountedRuntimeSkillKind_Count] = {0};
static volatile LONG g_recentMountedRuntimeSkillNativeReleaseTick[MountedRuntimeSkillKind_Count] = {0};

static void ObserveExtendedMountContext(int mountItemId)
{
    if (!IsExtendedMountSoaringContextMount(mountItemId))
    {
        return;
    }
    InterlockedExchange(&g_recentExtendedMountContextItemId, mountItemId);
    InterlockedExchange(&g_recentExtendedMountContextTick, static_cast<LONG>(GetTickCount()));
}

static void ClearExtendedMountContext()
{
    InterlockedExchange(&g_recentExtendedMountContextItemId, 0);
    InterlockedExchange(&g_recentExtendedMountContextTick, 0);
}

static void ObserveMountedRuntimeSkillIntent(
    MountedRuntimeSkillKind kind,
    int mountItemId)
{
    if (!kEnableMountedDoubleJumpRuntimeHooks)
    {
        return;
    }

    if (mountItemId <= 0)
    {
        return;
    }
    InterlockedExchange(&g_recentMountedRuntimeSkillIntentItemId[kind], mountItemId);
    InterlockedExchange(
        &g_recentMountedRuntimeSkillIntentTick[kind],
        static_cast<LONG>(GetTickCount()));
}

static void ObserveMountedDoubleJumpIntent(int mountItemId)
{
    ObserveMountedRuntimeSkillIntent(MountedRuntimeSkillKind_DoubleJump, mountItemId);
}

static void ObserveMountedDemonJumpIntent(int mountItemId)
{
    ObserveMountedRuntimeSkillIntent(MountedRuntimeSkillKind_DemonJump, mountItemId);
    PrimeMountedDemonJumpContextIfNeeded(mountItemId, "intent");
}

static void ObserveMountedRuntimeSkillNativeRelease(
    MountedRuntimeSkillKind kind,
    int mountItemId,
    int skillId)
{
    if (!kEnableMountedDoubleJumpRuntimeHooks)
    {
        return;
    }

    if (mountItemId <= 0 || skillId <= 0)
    {
        return;
    }

    InterlockedExchange(&g_recentMountedRuntimeSkillNativeReleaseItemId[kind], mountItemId);
    InterlockedExchange(&g_recentMountedRuntimeSkillNativeReleaseSkillId[kind], skillId);
    InterlockedExchange(
        &g_recentMountedRuntimeSkillNativeReleaseTick[kind],
        static_cast<LONG>(GetTickCount()));
}

static void ObserveMountedDoubleJumpNativeRelease(int mountItemId, int skillId)
{
    ObserveMountedRuntimeSkillNativeRelease(
        MountedRuntimeSkillKind_DoubleJump,
        mountItemId,
        skillId);
}

static void ObserveMountedDemonJumpNativeRelease(int mountItemId, int skillId)
{
    ObserveMountedRuntimeSkillNativeRelease(
        MountedRuntimeSkillKind_DemonJump,
        mountItemId,
        skillId);
}

static bool HasRecentMountedRuntimeSkillIntent(
    MountedRuntimeSkillKind kind,
    int mountItemId,
    DWORD maxAgeMs)
{
    if (!kEnableMountedDoubleJumpRuntimeHooks)
    {
        return false;
    }

    if (HasRecentMountedRuntimeRouteArmForKind(kind, mountItemId, maxAgeMs))
    {
        return true;
    }

    const LONG recentMountItemId =
        InterlockedCompareExchange(&g_recentMountedRuntimeSkillIntentItemId[kind], 0, 0);
    if (recentMountItemId <= 0)
    {
        return false;
    }

    if (mountItemId > 0 && recentMountItemId != mountItemId)
    {
        return false;
    }

    const LONG recentTick =
        InterlockedCompareExchange(&g_recentMountedRuntimeSkillIntentTick[kind], 0, 0);
    if (recentTick <= 0)
    {
        return false;
    }

    const DWORD nowTick = GetTickCount();
    const DWORD ageMs = nowTick - static_cast<DWORD>(recentTick);
    return ageMs <= maxAgeMs;
}

static bool HasRecentMountedDoubleJumpIntent(int mountItemId, DWORD maxAgeMs)
{
    return HasRecentMountedRuntimeSkillIntent(
        MountedRuntimeSkillKind_DoubleJump,
        mountItemId,
        maxAgeMs);
}

static bool HasRecentMountedDemonJumpIntent(int mountItemId, DWORD maxAgeMs)
{
    return HasRecentMountedRuntimeSkillIntent(
        MountedRuntimeSkillKind_DemonJump,
        mountItemId,
        maxAgeMs);
}

static bool TryResolveRecentMountedRuntimeSkillNativeRelease(
    MountedRuntimeSkillKind kind,
    int expectedSkillId,
    int *mountItemIdOut,
    DWORD maxAgeMs = 450)
{
    if (!kEnableMountedDoubleJumpRuntimeHooks)
    {
        return false;
    }

    if (expectedSkillId <= 0)
    {
        return false;
    }

    const LONG recentSkillId =
        InterlockedCompareExchange(&g_recentMountedRuntimeSkillNativeReleaseSkillId[kind], 0, 0);
    if (recentSkillId <= 0 || recentSkillId != expectedSkillId)
    {
        return false;
    }

    const LONG recentMountItemId =
        InterlockedCompareExchange(&g_recentMountedRuntimeSkillNativeReleaseItemId[kind], 0, 0);
    if (recentMountItemId <= 0)
    {
        return false;
    }

    const LONG recentTick =
        InterlockedCompareExchange(&g_recentMountedRuntimeSkillNativeReleaseTick[kind], 0, 0);
    if (recentTick <= 0)
    {
        return false;
    }

    const DWORD nowTick = GetTickCount();
    const DWORD ageMs = nowTick - static_cast<DWORD>(recentTick);
    if (ageMs > maxAgeMs ||
        !HasRecentMountedRuntimeSkillIntent(
            kind,
            static_cast<int>(recentMountItemId),
            maxAgeMs))
    {
        return false;
    }

    if (mountItemIdOut)
    {
        *mountItemIdOut = static_cast<int>(recentMountItemId);
    }
    return true;
}

static bool TryResolveRecentMountedDemonJumpIntentRuntimeSkill(
    int runtimeSkillId,
    int *mountItemIdOut,
    int *configuredSkillIdOut,
    DWORD maxAgeMs = 1200)
{
    if (!kEnableMountedDoubleJumpRuntimeHooks || runtimeSkillId <= 0)
    {
        return false;
    }

    int mountItemId = 0;
    if (!TryGetRecentMountedDemonJumpIntentItemId(&mountItemId, maxAgeMs) ||
        mountItemId <= 0)
    {
        return false;
    }

    int currentMountItemId = 0;
    if (TryResolveCurrentUserMountItemIdWithFallback(&currentMountItemId, nullptr) &&
        currentMountItemId > 0 &&
        currentMountItemId != mountItemId)
    {
        return false;
    }

    const int configuredSkillId =
        ResolveMountedRuntimeSkillIdForKind(
            MountedRuntimeSkillKind_DemonJump,
            mountItemId);
    if (configuredSkillId <= 0 ||
        !CanUseMountedRuntimeSkillRuntimeForKind(
            MountedRuntimeSkillKind_DemonJump,
            mountItemId,
            runtimeSkillId))
    {
        return false;
    }

    if (mountItemIdOut)
    {
        *mountItemIdOut = mountItemId;
    }
    if (configuredSkillIdOut)
    {
        *configuredSkillIdOut = configuredSkillId;
    }
    return true;
}

static bool TryResolveRecentMountedDemonJumpIntent(
    int expectedSkillId,
    int *mountItemIdOut,
    DWORD maxAgeMs = 1200)
{
    int configuredSkillId = 0;
    return TryResolveRecentMountedDemonJumpIntentRuntimeSkill(
        expectedSkillId,
        mountItemIdOut,
        &configuredSkillId,
        maxAgeMs);
}

static bool TryResolveRecentMountedDoubleJumpNativeRelease(
    int expectedSkillId,
    int *mountItemIdOut,
    DWORD maxAgeMs = 450)
{
    return TryResolveRecentMountedRuntimeSkillNativeRelease(
        MountedRuntimeSkillKind_DoubleJump,
        expectedSkillId,
        mountItemIdOut,
        maxAgeMs);
}

static bool TryResolveRecentMountedDemonJumpNativeRelease(
    int expectedSkillId,
    int *mountItemIdOut,
    DWORD maxAgeMs = 450)
{
    if (TryResolveRecentMountedRuntimeSkillNativeRelease(
            MountedRuntimeSkillKind_DemonJump,
            expectedSkillId,
            mountItemIdOut,
            maxAgeMs))
    {
        return true;
    }

    return TryResolveRecentMountedDemonJumpIntent(
        expectedSkillId,
        mountItemIdOut,
        maxAgeMs);
}

static bool TryResolveRecentMountedRuntimeSkillNativeReleaseRuntimeSkill(
    MountedRuntimeSkillKind kind,
    int runtimeSkillId,
    int *mountItemIdOut,
    int *configuredSkillIdOut,
    DWORD maxAgeMs = 450)
{
    if (!kEnableMountedDoubleJumpRuntimeHooks)
    {
        return false;
    }

    if (runtimeSkillId <= 0)
    {
        return false;
    }

    const LONG recentSkillId =
        InterlockedCompareExchange(&g_recentMountedRuntimeSkillNativeReleaseSkillId[kind], 0, 0);
    if (recentSkillId <= 0)
    {
        return false;
    }

    const LONG recentMountItemId =
        InterlockedCompareExchange(&g_recentMountedRuntimeSkillNativeReleaseItemId[kind], 0, 0);
    if (recentMountItemId <= 0)
    {
        return false;
    }

    const LONG recentTick =
        InterlockedCompareExchange(&g_recentMountedRuntimeSkillNativeReleaseTick[kind], 0, 0);
    if (recentTick <= 0)
    {
        return false;
    }

    const DWORD nowTick = GetTickCount();
    const DWORD ageMs = nowTick - static_cast<DWORD>(recentTick);
    if (ageMs > maxAgeMs ||
        !HasRecentMountedRuntimeSkillIntent(
            kind,
            static_cast<int>(recentMountItemId),
            maxAgeMs))
    {
        return false;
    }

    if (!CanUseMountedRuntimeSkillRuntimeForKind(
            kind,
            static_cast<int>(recentMountItemId),
            runtimeSkillId))
    {
        return false;
    }

    if (mountItemIdOut)
    {
        *mountItemIdOut = static_cast<int>(recentMountItemId);
    }
    if (configuredSkillIdOut)
    {
        *configuredSkillIdOut = static_cast<int>(recentSkillId);
    }
    return true;
}

static bool TryResolveRecentMountedDoubleJumpNativeReleaseRuntimeSkill(
    int runtimeSkillId,
    int *mountItemIdOut,
    int *mountedDoubleJumpSkillIdOut,
    DWORD maxAgeMs = 450)
{
    return TryResolveRecentMountedRuntimeSkillNativeReleaseRuntimeSkill(
        MountedRuntimeSkillKind_DoubleJump,
        runtimeSkillId,
        mountItemIdOut,
        mountedDoubleJumpSkillIdOut,
        maxAgeMs);
}

static bool TryResolveRecentMountedDemonJumpNativeReleaseRuntimeSkill(
    int runtimeSkillId,
    int *mountItemIdOut,
    int *mountedDemonJumpSkillIdOut,
    DWORD maxAgeMs = 1200)
{
    if (TryResolveRecentMountedRuntimeSkillNativeReleaseRuntimeSkill(
            MountedRuntimeSkillKind_DemonJump,
            runtimeSkillId,
            mountItemIdOut,
            mountedDemonJumpSkillIdOut,
            maxAgeMs))
    {
        return true;
    }

    return TryResolveRecentMountedDemonJumpIntentRuntimeSkill(
        runtimeSkillId,
        mountItemIdOut,
        mountedDemonJumpSkillIdOut,
        maxAgeMs);
}

static bool TryGetRecentExtendedMountContext(int *mountItemIdOut)
{
    const LONG mountItemId = InterlockedCompareExchange(&g_recentExtendedMountContextItemId, 0, 0);
    if (mountItemId <= 0)
    {
        return false;
    }

    const LONG tick = InterlockedCompareExchange(&g_recentExtendedMountContextTick, 0, 0);
    if (tick <= 0)
    {
        return false;
    }

    const DWORD nowTick = GetTickCount();
    const DWORD elapsed = nowTick - static_cast<DWORD>(tick);
    if (elapsed > kMountedSoaringFallbackGraceMs)
    {
        ClearExtendedMountContext();
        return false;
    }

    if (mountItemIdOut)
    {
        *mountItemIdOut = static_cast<int>(mountItemId);
    }
    return true;
}

static bool TryGetRecentMountedRuntimeSkillIntentItemId(
    MountedRuntimeSkillKind kind,
    int *mountItemIdOut,
    DWORD maxAgeMs = 400)
{
    if (!kEnableMountedDoubleJumpRuntimeHooks || !mountItemIdOut)
    {
        return false;
    }

    if (TryGetRecentMountedRuntimeRouteArmMountItemIdForKind(
            kind,
            mountItemIdOut,
            maxAgeMs))
    {
        return true;
    }

    const LONG recentMountItemId =
        InterlockedCompareExchange(&g_recentMountedRuntimeSkillIntentItemId[kind], 0, 0);
    if (recentMountItemId <= 0)
    {
        return false;
    }

    const LONG recentTick =
        InterlockedCompareExchange(&g_recentMountedRuntimeSkillIntentTick[kind], 0, 0);
    if (recentTick <= 0)
    {
        return false;
    }

    const DWORD nowTick = GetTickCount();
    const DWORD allowedAgeMs = maxAgeMs > 0 ? maxAgeMs : 400;
    if (nowTick - static_cast<DWORD>(recentTick) > allowedAgeMs)
    {
        return false;
    }

    *mountItemIdOut = static_cast<int>(recentMountItemId);
    return true;
}

static bool TryGetRecentMountedDoubleJumpIntentItemId(int *mountItemIdOut, DWORD maxAgeMs = 400)
{
    return TryGetRecentMountedRuntimeSkillIntentItemId(
        MountedRuntimeSkillKind_DoubleJump,
        mountItemIdOut,
        maxAgeMs);
}

static bool TryGetRecentMountedDemonJumpIntentItemId(int *mountItemIdOut, DWORD maxAgeMs)
{
    return TryGetRecentMountedRuntimeSkillIntentItemId(
        MountedRuntimeSkillKind_DemonJump,
        mountItemIdOut,
        maxAgeMs);
}

static bool TryGetRecentMountedRuntimeSkillNativeReleaseItemId(
    MountedRuntimeSkillKind kind,
    int *mountItemIdOut,
    DWORD maxAgeMs = 450)
{
    if (!kEnableMountedDoubleJumpRuntimeHooks || !mountItemIdOut)
    {
        return false;
    }

    const LONG recentMountItemId =
        InterlockedCompareExchange(&g_recentMountedRuntimeSkillNativeReleaseItemId[kind], 0, 0);
    if (recentMountItemId <= 0)
    {
        return false;
    }

    const LONG recentTick =
        InterlockedCompareExchange(&g_recentMountedRuntimeSkillNativeReleaseTick[kind], 0, 0);
    if (recentTick <= 0)
    {
        return false;
    }

    const DWORD nowTick = GetTickCount();
    const DWORD allowedAgeMs = maxAgeMs > 0 ? maxAgeMs : 450;
    if (nowTick - static_cast<DWORD>(recentTick) > allowedAgeMs)
    {
        return false;
    }

    *mountItemIdOut = static_cast<int>(recentMountItemId);
    return true;
}

static bool TryGetRecentMountedDoubleJumpNativeReleaseItemId(
    int *mountItemIdOut,
    DWORD maxAgeMs = 450)
{
    return TryGetRecentMountedRuntimeSkillNativeReleaseItemId(
        MountedRuntimeSkillKind_DoubleJump,
        mountItemIdOut,
        maxAgeMs);
}

static bool TryGetRecentMountedDemonJumpNativeReleaseItemId(
    int *mountItemIdOut,
    DWORD maxAgeMs = 450)
{
    return TryGetRecentMountedRuntimeSkillNativeReleaseItemId(
        MountedRuntimeSkillKind_DemonJump,
        mountItemIdOut,
        maxAgeMs);
}

static bool TryReadCurrentUserLocalPtr(void **userLocalOut)
{
    if (userLocalOut)
    {
        *userLocalOut = nullptr;
    }

    if (!userLocalOut ||
        SafeIsBadReadPtr(reinterpret_cast<void *>(ADDR_UserLocal), sizeof(DWORD)))
    {
        return false;
    }

    DWORD userLocal = 0;
    __try
    {
        userLocal = *reinterpret_cast<DWORD *>(ADDR_UserLocal);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    if (!userLocal)
    {
        return false;
    }

    *userLocalOut = reinterpret_cast<void *>(static_cast<uintptr_t>(userLocal));
    return true;
}

static bool TryReadMountedDemonJumpContextState(
    int *rootSkillIdOut,
    int *currentSkillIdOut,
    DWORD *userLocalOut)
{
    if (rootSkillIdOut)
    {
        *rootSkillIdOut = 0;
    }
    if (currentSkillIdOut)
    {
        *currentSkillIdOut = 0;
    }
    if (userLocalOut)
    {
        *userLocalOut = 0;
    }

    void *userLocal = nullptr;
    if (!TryReadCurrentUserLocalPtr(&userLocal))
    {
        return false;
    }

    const uintptr_t userLocalAddr = reinterpret_cast<uintptr_t>(userLocal);
    const uintptr_t contextAddr = userLocalAddr + kMountedDemonJumpContextOffset;
    const uintptr_t rootSkillAddr = userLocalAddr + kMountedDemonJumpContextRootSkillOffset;
    if (SafeIsBadReadPtr(reinterpret_cast<void *>(contextAddr), sizeof(DWORD) * 2))
    {
        return false;
    }

    DWORD contextHead = 0;
    int rootSkillId = 0;
    __try
    {
        contextHead = *reinterpret_cast<DWORD *>(contextAddr);
        rootSkillId = *reinterpret_cast<int *>(rootSkillAddr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    if (!contextHead || rootSkillId <= 0)
    {
        return false;
    }

    int currentSkillId = 0;
    if (currentSkillIdOut)
    {
        tMountedDemonJumpContextCurrentSkillFn currentSkillFn =
            reinterpret_cast<tMountedDemonJumpContextCurrentSkillFn>(
                ADDR_MountedDemonJumpContextCurrentSkill4300A0);
        if (currentSkillFn)
        {
            __try
            {
                currentSkillId = currentSkillFn(
                    reinterpret_cast<void *>(contextAddr));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                currentSkillId = 0;
            }
        }
    }

    if (rootSkillIdOut)
    {
        *rootSkillIdOut = rootSkillId;
    }
    if (currentSkillIdOut)
    {
        *currentSkillIdOut = currentSkillId > 0 ? currentSkillId : rootSkillId;
    }
    if (userLocalOut)
    {
        *userLocalOut = static_cast<DWORD>(userLocalAddr);
    }
    return true;
}

static bool HasMountedDemonJumpContextPrimedForMount(
    int mountItemId,
    int *currentSkillIdOut,
    int *rootSkillIdOut)
{
    if (currentSkillIdOut)
    {
        *currentSkillIdOut = 0;
    }
    if (rootSkillIdOut)
    {
        *rootSkillIdOut = 0;
    }

    if (mountItemId <= 0)
    {
        return false;
    }

    int currentMountItemId = 0;
    if (!TryReadCurrentUserMountItemId(&currentMountItemId) ||
        currentMountItemId != mountItemId)
    {
        return false;
    }

    const int configuredSkillId = ResolveMountedRuntimeSkillIdForKind(
        MountedRuntimeSkillKind_DemonJump,
        mountItemId);
    if (configuredSkillId <= 0)
    {
        return false;
    }

    int rootSkillId = 0;
    int currentSkillId = 0;
    if (!TryReadMountedDemonJumpContextState(
            &rootSkillId,
            &currentSkillId,
            nullptr) ||
        rootSkillId != configuredSkillId)
    {
        return false;
    }

    if (currentSkillIdOut)
    {
        *currentSkillIdOut = currentSkillId > 0 ? currentSkillId : rootSkillId;
    }
    if (rootSkillIdOut)
    {
        *rootSkillIdOut = rootSkillId;
    }
    return true;
}

static bool ShouldSuppressMountedDemonJumpMountedContextClear(
    void *contextPtr,
    DWORD callerRet,
    int *mountItemIdOut,
    int *rootSkillIdOut,
    int *currentSkillIdOut)
{
    if (mountItemIdOut)
    {
        *mountItemIdOut = 0;
    }
    if (rootSkillIdOut)
    {
        *rootSkillIdOut = 0;
    }
    if (currentSkillIdOut)
    {
        *currentSkillIdOut = 0;
    }

    if (!kEnableMountedDoubleJumpRuntimeHooks ||
        !contextPtr ||
        (callerRet != ADDR_MountedDemonJumpContextMountedClearReturn433F48 &&
         callerRet != 0x00433FEC))
    {
        return false;
    }

    void *userLocal = nullptr;
    if (!TryReadCurrentUserLocalPtr(&userLocal) || !userLocal)
    {
        return false;
    }

    const uintptr_t expectedContext =
        reinterpret_cast<uintptr_t>(userLocal) + kMountedDemonJumpContextOffset;
    if (reinterpret_cast<uintptr_t>(contextPtr) != expectedContext)
    {
        return false;
    }

    int mountItemId = 0;
    if (!TryReadCurrentUserMountItemId(&mountItemId) || mountItemId <= 0)
    {
        return false;
    }

    const int configuredSkillId = ResolveMountedRuntimeSkillIdForKind(
        MountedRuntimeSkillKind_DemonJump,
        mountItemId);
    if (configuredSkillId != 30010110)
    {
        return false;
    }

    int rootSkillId = 0;
    int currentSkillId = 0;
    if (!HasMountedDemonJumpContextPrimedForMount(
            mountItemId,
            &currentSkillId,
            &rootSkillId) ||
        rootSkillId != configuredSkillId)
    {
        return false;
    }

    if (!HasRecentMountedDemonJumpIntent(mountItemId, 2500))
    {
        return false;
    }

    if (mountItemIdOut)
    {
        *mountItemIdOut = mountItemId;
    }
    if (rootSkillIdOut)
    {
        *rootSkillIdOut = rootSkillId;
    }
    if (currentSkillIdOut)
    {
        *currentSkillIdOut = currentSkillId;
    }
    return true;
}

static bool TryManualPrimeMountedDemonJumpContext(
    void *userLocal,
    int mountItemId,
    int *currentSkillIdOut,
    int *rootSkillIdOut)
{
    if (currentSkillIdOut)
    {
        *currentSkillIdOut = 0;
    }
    if (rootSkillIdOut)
    {
        *rootSkillIdOut = 0;
    }

    if (!userLocal || mountItemId <= 0)
    {
        return false;
    }

    const int configuredSkillId = ResolveMountedRuntimeSkillIdForKind(
        MountedRuntimeSkillKind_DemonJump,
        mountItemId);
    if (configuredSkillId != 30010110)
    {
        return false;
    }

    tMountedDemonJumpSkillEntryLookupFn skillEntryLookupFn =
        reinterpret_cast<tMountedDemonJumpSkillEntryLookupFn>(
            ADDR_MountedDemonJumpSkillEntryLookupAE0420);
    tMountedDemonJumpContextSeedFn seedFn =
        reinterpret_cast<tMountedDemonJumpContextSeedFn>(
            ADDR_MountedDemonJumpContextSeedAC6B00);
    if (!skillEntryLookupFn || !seedFn)
    {
        return false;
    }

    unsigned int *nativeSkillEntry = nullptr;
    signed int nativeLookupResult = 0;
    __try
    {
        nativeLookupResult = skillEntryLookupFn(configuredSkillId, &nativeSkillEntry);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        nativeLookupResult = 0;
        nativeSkillEntry = nullptr;
    }

    const uintptr_t bridgeEntryBase =
        SkillOverlayBridgeLookupSkillEntryPointer(configuredSkillId);
    const uintptr_t nativeEntryBase =
        reinterpret_cast<uintptr_t>(nativeSkillEntry);

    uintptr_t entryBase = 0;
    const char *entrySource = "none";
    if (bridgeEntryBase &&
        !SafeIsBadReadPtr(reinterpret_cast<void *>(bridgeEntryBase), 1736))
    {
        entryBase = bridgeEntryBase;
        entrySource = "bridge";
    }
    else if (nativeEntryBase &&
             !SafeIsBadReadPtr(reinterpret_cast<void *>(nativeEntryBase), 1736))
    {
        entryBase = nativeEntryBase;
        entrySource = "ae0420";
    }

    static LONG s_mountedDemonJumpSeedLookupLogBudget = 48;
    const LONG lookupBudgetAfterDecrement =
        InterlockedDecrement(&s_mountedDemonJumpSeedLookupLogBudget);
    if (lookupBudgetAfterDecrement >= 0)
    {
        WriteLogFmt(
            "[MountDemonJumpSeed] lookup mount=%d skill=%d nativeLevel=%d nativeEntry=0x%08X bridgeEntry=0x%08X source=%s",
            mountItemId,
            configuredSkillId,
            nativeLookupResult,
            static_cast<DWORD>(nativeEntryBase),
            static_cast<DWORD>(bridgeEntryBase),
            entrySource);
    }

    if (!entryBase)
    {
        return false;
    }

    DWORD hasSeedPayload = 0;
    int a4 = 0;
    int a8 = 0;
    int a10 = 0;
    int a11 = 0;
    int a12 = 0;
    BYTE a5 = 0;
    WORD a6 = 0;
    int a9 = 0;
    __try
    {
        hasSeedPayload = *reinterpret_cast<DWORD *>(entryBase + 1632);
        a4 = *reinterpret_cast<int *>(entryBase + 0);
        a5 = *reinterpret_cast<BYTE *>(entryBase + 1684);
        a6 = *reinterpret_cast<WORD *>(entryBase + 1688);
        a8 = *reinterpret_cast<int *>(entryBase + 1704);
        a9 = *reinterpret_cast<DWORD *>(entryBase + 1692) != 0 ? 1 : 0;
        a10 = *reinterpret_cast<int *>(entryBase + 1696);
        a11 = *reinterpret_cast<int *>(entryBase + 1700);
        a12 = *reinterpret_cast<int *>(entryBase + 1732);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    if (!hasSeedPayload)
    {
        static LONG s_mountedDemonJumpSeedPayloadLogBudget = 24;
        const LONG payloadBudgetAfterDecrement =
            InterlockedDecrement(&s_mountedDemonJumpSeedPayloadLogBudget);
        if (payloadBudgetAfterDecrement >= 0)
        {
            WriteLogFmt(
                "[MountDemonJumpSeed] missing payload mount=%d skill=%d source=%s entry=0x%08X",
                mountItemId,
                configuredSkillId,
                entrySource,
                static_cast<DWORD>(entryBase));
        }
        return false;
    }

    __try
    {
        seedFn(
            userLocal,
            static_cast<int>(entryBase + 1636),
            static_cast<int>(entryBase + 1660),
            a4,
            static_cast<int>(a5),
            static_cast<int>(a6),
            static_cast<int>(entryBase + 1708),
            a8,
            a9,
            a10,
            a11,
            a12);
        *reinterpret_cast<BYTE *>(
            reinterpret_cast<uintptr_t>(userLocal) + kMountedDemonJumpReadyFlagOffset) = 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    const bool primed = HasMountedDemonJumpContextPrimedForMount(
        mountItemId,
        currentSkillIdOut,
        rootSkillIdOut);

    static LONG s_mountedDemonJumpSeedResultLogBudget = 48;
    const LONG resultBudgetAfterDecrement =
        InterlockedDecrement(&s_mountedDemonJumpSeedResultLogBudget);
    if (resultBudgetAfterDecrement >= 0)
    {
        WriteLogFmt(
            "[MountDemonJumpSeed] result mount=%d skill=%d source=%s entry=0x%08X nativeLevel=%d payload=%u primed=%d root=%d current=%d",
            mountItemId,
            configuredSkillId,
            entrySource,
            static_cast<DWORD>(entryBase),
            nativeLookupResult,
            hasSeedPayload,
            primed ? 1 : 0,
            rootSkillIdOut ? *rootSkillIdOut : 0,
            currentSkillIdOut ? *currentSkillIdOut : 0);
    }

    return primed;
}

static bool PrimeMountedDemonJumpContextIfNeeded(
    int mountItemId,
    const char *reason,
    int *currentSkillIdOut)
{
    if (currentSkillIdOut)
    {
        *currentSkillIdOut = 0;
    }

    if (mountItemId <= 0)
    {
        return false;
    }

    int currentSkillId = 0;
    int rootSkillId = 0;
    if (HasMountedDemonJumpContextPrimedForMount(
            mountItemId,
            &currentSkillId,
            &rootSkillId))
    {
        if (currentSkillIdOut)
        {
            *currentSkillIdOut = currentSkillId;
        }
        return true;
    }

    int currentMountItemId = 0;
    if (!TryReadCurrentUserMountItemId(&currentMountItemId) ||
        currentMountItemId != mountItemId)
    {
        return false;
    }

    void *userLocal = nullptr;
    if (!TryReadCurrentUserLocalPtr(&userLocal))
    {
        return false;
    }

    bool nativePrimeAttempted = false;
    bool manualPrimeAttempted = false;
    tMountedDemonJumpContextPrimeFn primeFn =
        reinterpret_cast<tMountedDemonJumpContextPrimeFn>(
            ADDR_MountedDemonJumpContextPrimeB00AD0);
    if (primeFn)
    {
        nativePrimeAttempted = true;
        __try
        {
            primeFn(userLocal, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }
    bool primed = HasMountedDemonJumpContextPrimedForMount(
        mountItemId,
        &currentSkillId,
        &rootSkillId);
    if (!primed)
    {
        manualPrimeAttempted = true;
        primed = TryManualPrimeMountedDemonJumpContext(
            userLocal,
            mountItemId,
            &currentSkillId,
            &rootSkillId);
    }
    if (currentSkillIdOut)
    {
        *currentSkillIdOut = currentSkillId;
    }

    static LONG s_mountedDemonJumpPrimeLogBudget = 48;
    const LONG budgetAfterDecrement =
        InterlockedDecrement(&s_mountedDemonJumpPrimeLogBudget);
    if (budgetAfterDecrement >= 0)
    {
        WriteLogFmt(
            "[MountDemonJumpPrime] reason=%s mount=%d user=0x%08X root=%d current=%d primed=%d native=%d manual=%d",
            reason ? reason : "unknown",
            mountItemId,
            static_cast<DWORD>(reinterpret_cast<uintptr_t>(userLocal)),
            rootSkillId,
            currentSkillId,
            primed ? 1 : 0,
            nativePrimeAttempted ? 1 : 0,
            manualPrimeAttempted ? 1 : 0);
    }

    return primed;
}

static bool TryReadCurrentUserMountItemId(int *mountItemIdOut)
{
    if (!mountItemIdOut || SafeIsBadReadPtr(reinterpret_cast<void *>(ADDR_UserLocal), sizeof(DWORD)))
    {
        return false;
    }

    DWORD userLocal = 0;
    __try
    {
        userLocal = *reinterpret_cast<DWORD *>(ADDR_UserLocal);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    if (!userLocal)
    {
        return false;
    }

    const uintptr_t mountItemIdAddr = static_cast<uintptr_t>(userLocal) + 0x454;
    if (SafeIsBadReadPtr(reinterpret_cast<void *>(mountItemIdAddr), sizeof(DWORD)))
    {
        return false;
    }

    int mountItemId = 0;
    __try
    {
        mountItemId = *reinterpret_cast<int *>(mountItemIdAddr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    if (mountItemId <= 0)
    {
        return false;
    }
    *mountItemIdOut = mountItemId;
    return true;
}

static bool TryGetActiveMountedSoaringFlightItemId(int *mountItemIdOut, DWORD maxAgeMs = 0)
{
    if (!mountItemIdOut)
    {
        return false;
    }

    const LONG activeMountItemId =
        InterlockedCompareExchange(&g_activeMountedSoaringFlightItemId, 0, 0);
    if (activeMountItemId <= 0)
    {
        return false;
    }

    const LONG activeTick =
        InterlockedCompareExchange(&g_activeMountedSoaringFlightTick, 0, 0);
    if (activeTick <= 0)
    {
        return false;
    }

    const DWORD nowTick = GetTickCount();
    const DWORD allowedAgeMs =
        maxAgeMs > 0 ? maxAgeMs : 10000;
    if (nowTick - static_cast<DWORD>(activeTick) > allowedAgeMs)
    {
        return false;
    }

    *mountItemIdOut = static_cast<int>(activeMountItemId);
    return true;
}

static bool TryGetRecentMountedFlightScaleSampleMountItemId(
    int *mountItemIdOut,
    DWORD maxAgeMs = 10000)
{
    if (!mountItemIdOut)
    {
        return false;
    }

    const MountedFlightPhysicsScaleSample sample = g_MountedFlightPhysicsScaleSample;
    if (sample.mountItemId <= 0 || sample.tick == 0)
    {
        return false;
    }

    const DWORD nowTick = GetTickCount();
    const DWORD allowedAgeMs = maxAgeMs > 0 ? maxAgeMs : 10000;
    if (nowTick - sample.tick > allowedAgeMs)
    {
        return false;
    }

    *mountItemIdOut = sample.mountItemId;
    return true;
}

static void ClearMountedSoaringRuntimeFallbackState();

static bool TryResolveCurrentUserMountItemIdWithFallback(
    int *mountItemIdOut,
    const char **sourceOut)
{
    if (mountItemIdOut)
    {
        *mountItemIdOut = 0;
    }
    if (sourceOut)
    {
        *sourceOut = nullptr;
    }

    int mountItemId = 0;
    const char *source = nullptr;
    bool shouldObserveExtendedMountContext = false;
    if (TryReadCurrentUserMountItemId(&mountItemId))
    {
        source = "user";
        shouldObserveExtendedMountContext = true;
    }
    else if (TryGetRecentExtendedMountContext(&mountItemId) &&
             IsExtendedMountSoaringContextMount(mountItemId))
    {
        source = "recent-extended";
    }
    else if (TryGetActiveMountedSoaringFlightItemId(
                 &mountItemId,
                 kMountedSoaringFallbackGraceMs) &&
             IsExtendedMountSoaringContextMount(mountItemId))
    {
        source = "active-soaring";
    }
    else
    {
        // Do not let the cached scale baseline masquerade as a live mount
        // source. That cache is useful to preserve the next legitimate flight
        // boost sample, but using it as "current mount" resurrects stale mount
        // context after dismount.
        ClearMountedSoaringRuntimeFallbackState();
        return false;
    }

    if (shouldObserveExtendedMountContext &&
        IsExtendedMountSoaringContextMount(mountItemId))
    {
        ObserveExtendedMountContext(mountItemId);
    }

    if (mountItemIdOut)
    {
        *mountItemIdOut = mountItemId;
    }
    if (sourceOut)
    {
        *sourceOut = source;
    }
    return true;
}

static bool TryResolveMountedRuntimeSkillMountItemIdWithFallback(
    MountedRuntimeSkillKind kind,
    void *playerObj,
    int *mountItemIdOut,
    const char **sourceOut,
    DWORD maxAgeMs)
{
    if (mountItemIdOut)
    {
        *mountItemIdOut = 0;
    }
    if (sourceOut)
    {
        *sourceOut = nullptr;
    }

    int mountItemId = 0;
    const char *source = nullptr;
    if (playerObj && TryReadMountItemIdFromPlayerObject(playerObj, &mountItemId))
    {
        source = "player";
    }
    else if (TryReadCurrentUserMountItemId(&mountItemId))
    {
        source = "user";
    }
    else if (TryGetRecentMountedRuntimeRouteArmMountItemIdForKind(
                 kind,
                 &mountItemId,
                 maxAgeMs) &&
             ResolveMountedRuntimeSkillIdForKind(kind, mountItemId) > 0)
    {
        source = "route-arm";
    }
    else if (TryGetRecentMountedRuntimeSkillIntentItemId(kind, &mountItemId, maxAgeMs) &&
             ResolveMountedRuntimeSkillIdForKind(kind, mountItemId) > 0)
    {
        source = "intent";
    }
    else if (TryGetRecentMountedRuntimeSkillNativeReleaseItemId(kind, &mountItemId, maxAgeMs) &&
             ResolveMountedRuntimeSkillIdForKind(kind, mountItemId) > 0)
    {
        source = "native-release";
    }
    else if (TryResolveCurrentUserMountItemIdWithFallback(&mountItemId, &source) &&
             ResolveMountedRuntimeSkillIdForKind(kind, mountItemId) > 0)
    {
    }
    else
    {
        return false;
    }

    if (source &&
        (!strcmp(source, "player") || !strcmp(source, "user")) &&
        IsExtendedMountSoaringContextMount(mountItemId))
    {
        ObserveExtendedMountContext(mountItemId);
    }

    if (mountItemIdOut)
    {
        *mountItemIdOut = mountItemId;
    }
    if (sourceOut)
    {
        *sourceOut = source;
    }
    return true;
}

static bool TryResolveMountedDoubleJumpMountItemIdWithFallback(
    void *playerObj,
    int *mountItemIdOut,
    const char **sourceOut,
    DWORD maxAgeMs)
{
    return TryResolveMountedRuntimeSkillMountItemIdWithFallback(
        MountedRuntimeSkillKind_DoubleJump,
        playerObj,
        mountItemIdOut,
        sourceOut,
        maxAgeMs);
}

static bool TryResolveMountedDemonJumpMountItemIdWithFallback(
    void *playerObj,
    int *mountItemIdOut,
    const char **sourceOut,
    DWORD maxAgeMs)
{
    return TryResolveMountedRuntimeSkillMountItemIdWithFallback(
        MountedRuntimeSkillKind_DemonJump,
        playerObj,
        mountItemIdOut,
        sourceOut,
        maxAgeMs);
}

static bool TryResolveMountItemIdFromContextPointer(void *mountContext, int *mountItemIdOut)
{
    if (!mountContext || !mountItemIdOut)
    {
        return false;
    }

    if (SafeIsBadReadPtr(mountContext, sizeof(DWORD)))
    {
        return false;
    }

    tMountContextGetItemIdFn getItemIdFn =
        reinterpret_cast<tMountContextGetItemIdFn>(ADDR_7D4CA0);
    if (!getItemIdFn)
    {
        return false;
    }

    int mountItemId = 0;
    __try
    {
        mountItemId = getItemIdFn(mountContext);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    if (mountItemId <= 0)
    {
        return false;
    }

    *mountItemIdOut = mountItemId;
    return true;
}

static bool TryReadMountItemIdFromPlayerObject(void *playerObj, int *mountItemIdOut)
{
    int mountItemId = 0;
    if (!TryReadMountItemIdFromPlayerObjectRaw(playerObj, &mountItemId) ||
        mountItemId <= 0)
    {
        return false;
    }

    *mountItemIdOut = mountItemId;
    return true;
}

static bool TryReadMountItemIdFromPlayerObjectRaw(void *playerObj, int *mountItemIdOut)
{
    if (!playerObj || !mountItemIdOut)
    {
        return false;
    }

    const uintptr_t mountItemIdAddr = reinterpret_cast<uintptr_t>(playerObj) + 0x454;
    if (SafeIsBadReadPtr(reinterpret_cast<void *>(mountItemIdAddr), sizeof(DWORD)))
    {
        return false;
    }

    int mountItemId = 0;
    __try
    {
        mountItemId = *reinterpret_cast<int *>(mountItemIdAddr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    *mountItemIdOut = mountItemId;
    return true;
}

static bool TryResolveMountedMovementDataKeyFromMountItemId(int mountItemId, int *dataKeyOut)
{
    if (!dataKeyOut)
    {
        return false;
    }
    *dataKeyOut = 0;

    if (mountItemId <= 0)
    {
        return false;
    }

    tMountItemInfoLookupFn lookupFn =
        reinterpret_cast<tMountItemInfoLookupFn>(ADDR_6545A0);
    tMountItemInfoDataKeyFn dataKeyFn =
        reinterpret_cast<tMountItemInfoDataKeyFn>(ADDR_B22500);
    if (!lookupFn || !dataKeyFn ||
        SafeIsBadReadPtr(reinterpret_cast<void *>(static_cast<uintptr_t>(ADDR_F59D34)), sizeof(void *)))
    {
        return false;
    }

    void *mountInfoTable = nullptr;
    void *mountInfo = nullptr;
    int dataKey = 0;
    __try
    {
        mountInfoTable =
            *reinterpret_cast<void **>(static_cast<uintptr_t>(ADDR_F59D34));
        if (!mountInfoTable)
        {
            return false;
        }

        // Follow B92D10's native chain exactly:
        //   mountItemId -> sub_6545A0(dword_F59D34, itemId) -> sub_B22500(info)
        // This yields the same runtime data key later passed to 888B30, so we
        // can prime flight speed immediately instead of waiting for the first
        // natural mounted movement lookup to arrive a few seconds later.
        mountInfo = lookupFn(mountInfoTable, mountItemId);
        if (!mountInfo)
        {
            return false;
        }

        dataKey = dataKeyFn(mountInfo);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    if (dataKey <= 0)
    {
        return false;
    }

    *dataKeyOut = dataKey;

    static LONG s_mountDataKeyResolveLogBudget = 24;
    if (InterlockedDecrement(&s_mountDataKeyResolveLogBudget) >= 0)
    {
        WriteLogFmt(
            "[MountDataKeyResolve] mount=%d key=%d source=B92D10-native",
            mountItemId,
            dataKey);
    }
    return true;
}

static bool IsMountMovementObservedDataLookupCaller(DWORD callerRet)
{
    return callerRet == ADDR_B92F2C ||
           callerRet == ADDR_B930C6 ||
           callerRet == ADDR_B932B2;
}

static void RememberRecentMountedMovementRawSample(
    int mountItemId,
    int dataKey,
    int speed,
    int jump)
{
    if (mountItemId <= 0 || dataKey <= 0 || speed <= 0 || jump <= 0)
    {
        return;
    }

    const DWORD nowTick = GetTickCount();
    InterlockedExchange(&g_RecentMountedMovementRawMountItemId, mountItemId);
    InterlockedExchange(&g_RecentMountedMovementRawDataKey, dataKey);
    InterlockedExchange(&g_RecentMountedMovementRawSpeed, speed);
    InterlockedExchange(&g_RecentMountedMovementRawJump, jump);
    InterlockedExchange(&g_RecentMountedMovementRawTick, static_cast<LONG>(nowTick));
}

static bool TryGetRecentMountedMovementRawSample(
    int *mountItemIdOut,
    int *dataKeyOut,
    int *speedOut,
    int *jumpOut,
    DWORD maxAgeMs)
{
    if (mountItemIdOut)
    {
        *mountItemIdOut = 0;
    }
    if (dataKeyOut)
    {
        *dataKeyOut = 0;
    }
    if (speedOut)
    {
        *speedOut = 0;
    }
    if (jumpOut)
    {
        *jumpOut = 0;
    }

    const int mountItemId = InterlockedCompareExchange(
        &g_RecentMountedMovementRawMountItemId,
        0,
        0);
    const int dataKey = InterlockedCompareExchange(
        &g_RecentMountedMovementRawDataKey,
        0,
        0);
    const int speed = InterlockedCompareExchange(
        &g_RecentMountedMovementRawSpeed,
        0,
        0);
    const int jump = InterlockedCompareExchange(
        &g_RecentMountedMovementRawJump,
        0,
        0);
    const DWORD tick = static_cast<DWORD>(InterlockedCompareExchange(
        &g_RecentMountedMovementRawTick,
        0,
        0));
    if (mountItemId <= 0 || dataKey <= 0 || speed <= 0 || jump <= 0 || tick == 0)
    {
        return false;
    }

    const DWORD allowedAgeMs = maxAgeMs > 0 ? maxAgeMs : 1000;
    const DWORD nowTick = GetTickCount();
    if (nowTick - tick > allowedAgeMs)
    {
        return false;
    }

    int currentMountItemId = 0;
    if (!TryReadCurrentUserMountItemId(&currentMountItemId) ||
        currentMountItemId <= 0)
    {
        // Mounted raw samples should only affect the live mounted output path.
        // Once dismounted, keeping the last sample around leaks boosted speed
        // and jump for a short window before the next stat refresh catches up.
        ClearRecentMountedMovementRawSample();
        return false;
    }

    if (currentMountItemId != mountItemId)
    {
        return false;
    }

    if (mountItemIdOut)
    {
        *mountItemIdOut = mountItemId;
    }
    if (dataKeyOut)
    {
        *dataKeyOut = dataKey;
    }
    if (speedOut)
    {
        *speedOut = speed;
    }
    if (jumpOut)
    {
        *jumpOut = jump;
    }

    return true;
}

static void ClearRecentMountedMovementRawSample()
{
    InterlockedExchange(&g_RecentMountedMovementRawMountItemId, 0);
    InterlockedExchange(&g_RecentMountedMovementRawDataKey, 0);
    InterlockedExchange(&g_RecentMountedMovementRawSpeed, 0);
    InterlockedExchange(&g_RecentMountedMovementRawJump, 0);
    InterlockedExchange(&g_RecentMountedMovementRawTick, 0);
}

static bool ShouldAttemptMountedMovementNativeCacheRebuild(
    const MountedMovementOverride &mountedOverride,
    int mountItemId,
    int dataKey,
    int currentSpeed,
    int currentJump)
{
    if (!mountedOverride.matched ||
        !mountedOverride.useNativeMovement ||
        mountItemId <= 0 ||
        dataKey <= 0)
    {
        return false;
    }

    // Pure native mode should keep the client img/cache values. If a lookup for
    // that route still lands on the historical 190/123 edge, we are most likely
    // looking at a cache that was built before our cap-removal patches landed.
    if (currentSpeed != 190 && currentJump != 123)
    {
        return false;
    }

    const DWORD nowTick = GetTickCount();
    const LONG lastDataKey = InterlockedCompareExchange(
        &g_LastMountMovementNativeCacheRebuildDataKey,
        0,
        0);
    const DWORD lastTick = static_cast<DWORD>(InterlockedCompareExchange(
        &g_LastMountMovementNativeCacheRebuildTick,
        0,
        0));
    return lastDataKey != dataKey ||
           lastTick == 0 ||
           nowTick - lastTick > 2000;
}

static void RememberMountedMovementNativeCacheRebuildAttempt(int dataKey)
{
    InterlockedExchange(&g_LastMountMovementNativeCacheRebuildDataKey, dataKey);
    InterlockedExchange(
        &g_LastMountMovementNativeCacheRebuildTick,
        static_cast<LONG>(GetTickCount()));
}

static bool RebuildMountedMovementNativeCache()
{
    if (SafeIsBadReadPtr(reinterpret_cast<void *>(static_cast<uintptr_t>(ADDR_D68820)), 5) ||
        SafeIsBadReadPtr(reinterpret_cast<void *>(static_cast<uintptr_t>(ADDR_889410)), 5))
    {
        return false;
    }

    if (InterlockedCompareExchange(&g_MountMovementNativeCacheRebuildInProgress, 1, 0) != 0)
    {
        return false;
    }

    bool ok = false;
    DWORD exceptionCode = 0;
    __try
    {
        // D68820 is the table reset path for off_F56A40; 889410 is the
        // original client-side TamingMob cache population pass.
        reinterpret_cast<void (__cdecl *)()>(
            static_cast<uintptr_t>(ADDR_D68820))();
        __asm
        {
            xor ecx, ecx
            mov eax, ADDR_889410
            call eax
        }
        ClearMountedFlightPhysicsScaleSample();
        ClearRecentMountedMovementRawSample();
        ok = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        exceptionCode = static_cast<DWORD>(GetExceptionCode());
        ok = false;
    }

    InterlockedExchange(&g_MountMovementNativeCacheRebuildInProgress, 0);

    if (InterlockedDecrement(&g_MountMovementNativeCacheRebuildLogBudget) >= 0)
    {
        WriteLogFmt(
            "[MountMoveNativeCache] rebuild ok=%d exception=0x%08X",
            ok ? 1 : 0,
            exceptionCode);
    }
    return ok;
}

static bool TryApplyMountedMovementOverrideToCachedData(
    int mountItemId,
    int dataKey,
    uintptr_t dataPtr,
    MountedMovementOverride *appliedOverrideOut)
{
    if (appliedOverrideOut)
        *appliedOverrideOut = MountedMovementOverride();

    if (mountItemId <= 0 || dataKey <= 0 || dataPtr == 0)
        return false;

    MountedMovementOverride mountedOverride = {};
    if (!SkillOverlayBridgeResolveMountedMovementOverride(mountItemId, dataKey, mountedOverride) ||
        !mountedOverride.matched)
    {
        if (InterlockedDecrement(&g_MountMovementOverrideMissLogBudget) >= 0)
        {
            WriteLogFmt("[MountMoveMiss] stage=resolve mount=%d key=%d data=0x%08X",
                        mountItemId,
                        dataKey,
                        static_cast<DWORD>(dataPtr));
        }
        return false;
    }

    MountedMovementOverride requestedOverride = mountedOverride;
    bool normalizedFsForLargeNativeUnits = false;
    bool normalizedSwimForLargeNativeUnits = false;

    __try
    {
        const double nativeFs = *reinterpret_cast<double *>(dataPtr + 0x20);
        const DWORD nativeSwim = *reinterpret_cast<DWORD *>(dataPtr + 0x28);

        if (mountedOverride.hasFs &&
            mountedOverride.fs >= 100.0 &&
            nativeFs >= 10000.0 &&
            mountedOverride.fs < nativeFs)
        {
            double adjustedFs = mountedOverride.fs;
            for (int i = 0; i < 3 && adjustedFs < nativeFs; ++i)
            {
                adjustedFs *= 10.0;
            }
            if (adjustedFs > mountedOverride.fs)
            {
                mountedOverride.fs = adjustedFs;
                normalizedFsForLargeNativeUnits = true;
            }
        }

        if (mountedOverride.hasSwim &&
            mountedOverride.swim >= 100.0 &&
            nativeSwim >= 10000u &&
            mountedOverride.swim < static_cast<double>(nativeSwim))
        {
            double adjustedSwim = mountedOverride.swim;
            for (int i = 0; i < 3 && adjustedSwim < static_cast<double>(nativeSwim); ++i)
            {
                adjustedSwim *= 10.0;
            }
            if (adjustedSwim > mountedOverride.swim)
            {
                mountedOverride.swim = adjustedSwim;
                normalizedSwimForLargeNativeUnits = true;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    if (normalizedFsForLargeNativeUnits || normalizedSwimForLargeNativeUnits)
    {
        static LONG s_mountMoveUnitNormalizationLogBudget = 24;
        if (InterlockedDecrement(&s_mountMoveUnitNormalizationLogBudget) >= 0)
        {
            WriteLogFmt(
                "[MountMoveUnitFix] mount=%d key=%d requested=[fs=%.3f swim=%.3f] applied=[fs=%.3f swim=%.3f]",
                mountItemId,
                dataKey,
                requestedOverride.hasFs ? requestedOverride.fs : -1.0,
                requestedOverride.hasSwim ? requestedOverride.swim : -1.0,
                mountedOverride.hasFs ? mountedOverride.fs : -1.0,
                mountedOverride.hasSwim ? mountedOverride.swim : -1.0);
        }
    }

    bool wroteAny = false;

    __try
    {
        if (mountedOverride.hasSpeed)
        {
            *reinterpret_cast<int *>(dataPtr + 0x14) = mountedOverride.speed;
            wroteAny = true;
        }
        if (mountedOverride.hasJump)
        {
            *reinterpret_cast<int *>(dataPtr + 0x18) = mountedOverride.jump;
            wroteAny = true;
        }
        if (mountedOverride.hasFs)
        {
            *reinterpret_cast<double *>(dataPtr + 0x20) = mountedOverride.fs;
            wroteAny = true;
        }
        if (mountedOverride.hasSwim)
        {
            const double clampedSwim = mountedOverride.swim > 0.0 ? mountedOverride.swim : 0.0;
            *reinterpret_cast<DWORD *>(dataPtr + 0x28) =
                static_cast<DWORD>(clampedSwim + 0.5);
            wroteAny = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (InterlockedDecrement(&g_MountMovementOverrideMissLogBudget) >= 0)
        {
            WriteLogFmt("[MountMoveMiss] stage=write-exception mount=%d key=%d data=0x%08X code=0x%08X",
                        mountItemId,
                        dataKey,
                        static_cast<DWORD>(dataPtr),
                        static_cast<DWORD>(GetExceptionCode()));
        }
        wroteAny = false;
    }

    if (appliedOverrideOut && mountedOverride.matched)
        *appliedOverrideOut = mountedOverride;
    return wroteAny;
}

static const char *DescribeMountMovementObservedDataLookupCaller(DWORD callerRet)
{
    switch (callerRet)
    {
    case ADDR_B92F2C:
        return "normal";
    case ADDR_B930C6:
        return "special";
    case ADDR_B932B2:
        return "mechanic";
    default:
        return "unknown";
    }
}

static int __cdecl hkMountMovementDataLookup888B30(int dataKey)
{
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    int result = oMountMovementDataLookup888B30
                     ? oMountMovementDataLookup888B30(dataKey)
                     : 0;
    if (!IsMountMovementObservedDataLookupCaller(callerRet))
    {
        return result;
    }

    const bool shouldLog = InterlockedDecrement(&g_MountMovementObserveLogBudget) >= 0;

    int mountItemId = 0;
    if (!TryReadCurrentUserMountItemId(&mountItemId) || mountItemId <= 0)
    {
        // Ground movement lookup must not reuse soaring/extended fallback
        // context. After dismount those fallback windows can linger briefly and
        // keep refreshing the last mounted raw sample, which makes B93B80
        // continue raising speed/jump for a few seconds on foot.
        ClearRecentMountedMovementRawSample();
        return result;
    }

    MountedMovementOverride resolvedOverride = {};
    const bool hasResolvedOverride =
        mountItemId > 0 &&
        SkillOverlayBridgeResolveMountedMovementOverride(
            mountItemId,
            dataKey,
            resolvedOverride) &&
        resolvedOverride.matched;

    bool readable = false;
    int rawSpeed = 0;
    int rawJump = 0;
    double rawFs = 0.0;
    DWORD rawSwim = 0;
    DWORD rawSpeedOverrideFlag = 0;
    DWORD rawJumpOverrideFlag = 0;
    int beforePatchSpeed = 0;
    int beforePatchJump = 0;
    double beforePatchFs = 0.0;
    DWORD beforePatchSwim = 0;
    bool hadBeforePatchSnapshot = false;
    if (result > 0 &&
        !SafeIsBadReadPtr(reinterpret_cast<void *>(static_cast<uintptr_t>(result)), 0x38))
    {
        __try
        {
            const uintptr_t dataPtr = static_cast<uintptr_t>(result);
            rawSpeed = *reinterpret_cast<int *>(dataPtr + 0x14);
            rawJump = *reinterpret_cast<int *>(dataPtr + 0x18);
            rawFs = *reinterpret_cast<double *>(dataPtr + 0x20);
            rawSwim = *reinterpret_cast<DWORD *>(dataPtr + 0x28);
            rawSpeedOverrideFlag = *reinterpret_cast<DWORD *>(dataPtr + 0x30);
            rawJumpOverrideFlag = *reinterpret_cast<DWORD *>(dataPtr + 0x34);
            readable = true;
            beforePatchSpeed = rawSpeed;
            beforePatchJump = rawJump;
            beforePatchFs = rawFs;
            beforePatchSwim = rawSwim;
            hadBeforePatchSnapshot = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }
    }

    if (hadBeforePatchSnapshot &&
        hasResolvedOverride &&
        ShouldAttemptMountedMovementNativeCacheRebuild(
            resolvedOverride,
            mountItemId,
            dataKey,
            beforePatchSpeed,
            beforePatchJump))
    {
        RememberMountedMovementNativeCacheRebuildAttempt(dataKey);
        const bool rebuilt = RebuildMountedMovementNativeCache();
        const int rebuiltResult = rebuilt && oMountMovementDataLookup888B30
                                      ? oMountMovementDataLookup888B30(dataKey)
                                      : 0;
        if (rebuilt && rebuiltResult > 0)
        {
            result = rebuiltResult;
            readable = false;
            rawSpeed = 0;
            rawJump = 0;
            rawFs = 0.0;
            rawSwim = 0;
            rawSpeedOverrideFlag = 0;
            rawJumpOverrideFlag = 0;
            beforePatchSpeed = 0;
            beforePatchJump = 0;
            beforePatchFs = 0.0;
            beforePatchSwim = 0;
            hadBeforePatchSnapshot = false;

            if (!SafeIsBadReadPtr(reinterpret_cast<void *>(static_cast<uintptr_t>(result)), 0x38))
            {
                __try
                {
                    const uintptr_t dataPtr = static_cast<uintptr_t>(result);
                    rawSpeed = *reinterpret_cast<int *>(dataPtr + 0x14);
                    rawJump = *reinterpret_cast<int *>(dataPtr + 0x18);
                    rawFs = *reinterpret_cast<double *>(dataPtr + 0x20);
                    rawSwim = *reinterpret_cast<DWORD *>(dataPtr + 0x28);
                    rawSpeedOverrideFlag = *reinterpret_cast<DWORD *>(dataPtr + 0x30);
                    rawJumpOverrideFlag = *reinterpret_cast<DWORD *>(dataPtr + 0x34);
                    readable = true;
                    beforePatchSpeed = rawSpeed;
                    beforePatchJump = rawJump;
                    beforePatchFs = rawFs;
                    beforePatchSwim = rawSwim;
                    hadBeforePatchSnapshot = true;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    readable = false;
                }
            }
        }
    }

    MountedMovementOverride appliedOverride = {};
    const bool overrideApplied = TryApplyMountedMovementOverrideToCachedData(
        mountItemId,
        dataKey,
        static_cast<uintptr_t>(result),
        &appliedOverride);

    if (overrideApplied &&
        result > 0 &&
        !SafeIsBadReadPtr(reinterpret_cast<void *>(static_cast<uintptr_t>(result)), 0x38))
    {
        __try
        {
            const uintptr_t dataPtr = static_cast<uintptr_t>(result);
            rawSpeed = *reinterpret_cast<int *>(dataPtr + 0x14);
            rawJump = *reinterpret_cast<int *>(dataPtr + 0x18);
            rawFs = *reinterpret_cast<double *>(dataPtr + 0x20);
            rawSwim = *reinterpret_cast<DWORD *>(dataPtr + 0x28);
            rawSpeedOverrideFlag = *reinterpret_cast<DWORD *>(dataPtr + 0x30);
            rawJumpOverrideFlag = *reinterpret_cast<DWORD *>(dataPtr + 0x34);
            readable = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    if (overrideApplied && hadBeforePatchSnapshot)
    {
        RememberMountedFlightPhysicsScaleSample(
            mountItemId,
            dataKey,
            beforePatchFs,
            static_cast<double>(beforePatchSwim),
            appliedOverride);
    }
    if (hadBeforePatchSnapshot &&
        appliedOverride.matched &&
        appliedOverride.useNativeMovement)
    {
        MountedMovementOverride nativeScaleOverride = appliedOverride;
        nativeScaleOverride.hasFs = false;
        nativeScaleOverride.fs = 0.0;
        nativeScaleOverride.hasSwim = false;
        nativeScaleOverride.swim = 0.0;

        const double normalizedFs =
            NormalizeMountedFlightPhysicsHumanScalar(beforePatchFs);
        const double normalizedSwim =
            NormalizeMountedFlightPhysicsHumanScalar(static_cast<double>(beforePatchSwim));

        if (normalizedFs > 0.0)
        {
            nativeScaleOverride.hasFs = true;
            nativeScaleOverride.fs = normalizedFs;
        }
        if (normalizedSwim > 0.0)
        {
            nativeScaleOverride.hasSwim = true;
            nativeScaleOverride.swim = normalizedSwim;
        }

        if (nativeScaleOverride.hasFs || nativeScaleOverride.hasSwim)
        {
            RememberMountedFlightPhysicsScaleSample(
                mountItemId,
                dataKey,
                100.0,
                100.0,
                nativeScaleOverride);

            static LONG s_mountMoveNativeSampleLogBudget = 24;
            if (InterlockedDecrement(&s_mountMoveNativeSampleLogBudget) >= 0)
            {
                WriteLogFmt(
                    "[MountMoveNative] sourceSkill=%d mount=%d key=%d raw=[fs=%.3f swim=%u] normalized=[fs=%.3f swim=%.3f]",
                    nativeScaleOverride.sourceSkillId,
                    mountItemId,
                    dataKey,
                    beforePatchFs,
                    beforePatchSwim,
                    nativeScaleOverride.hasFs ? nativeScaleOverride.fs : -1.0,
                    nativeScaleOverride.hasSwim ? nativeScaleOverride.swim : -1.0);
            }
        }
    }

    if (readable)
    {
        RememberRecentMountedMovementRawSample(
            mountItemId,
            dataKey,
            rawSpeed,
            rawJump);
    }

    if (readable && shouldLog)
    {
        if (overrideApplied && hadBeforePatchSnapshot)
        {
            WriteLogFmt(
                "[MountMoveFix] sourceSkill=%d mount=%d key=%d applied=[speed=%d jump=%d fs=%.3f swim=%.3f] before=[speed=%d jump=%d fs=%.3f swim=%u] after=[speed=%d jump=%d fs=%.3f swim=%u]",
                appliedOverride.sourceSkillId,
                mountItemId,
                dataKey,
                appliedOverride.hasSpeed ? appliedOverride.speed : -1,
                appliedOverride.hasJump ? appliedOverride.jump : -1,
                appliedOverride.hasFs ? appliedOverride.fs : -1.0,
                appliedOverride.hasSwim ? appliedOverride.swim : -1.0,
                beforePatchSpeed,
                beforePatchJump,
                beforePatchFs,
                beforePatchSwim,
                rawSpeed,
                rawJump,
                rawFs,
                rawSwim);
        }
        WriteLogFmt(
            "[MountMoveObserve] path=%s caller=0x%08X mount=%d key=%d data=0x%08X raw=[speed=%d jump=%d fs=%.3f swim=%u speedOverride=%u jumpOverride=%u]",
            DescribeMountMovementObservedDataLookupCaller(callerRet),
            callerRet,
            mountItemId,
            dataKey,
            result,
            rawSpeed,
            rawJump,
            rawFs,
            rawSwim,
            rawSpeedOverrideFlag,
            rawJumpOverrideFlag);
    }
    else if (shouldLog)
    {
        WriteLogFmt(
            "[MountMoveObserve] path=%s caller=0x%08X mount=%d key=%d data=0x%08X raw=unreadable",
            DescribeMountMovementObservedDataLookupCaller(callerRet),
            callerRet,
            mountItemId,
            dataKey,
            result);
    }

    return result;
}

struct MountedFlightPhysicsSlotPatch
{
    uintptr_t slotAddr;
    double originalValue;
    double patchedValue;
    size_t relativeOffset;
    bool isStateSlot;
    bool active;
};

static void ClearMountedSoaringFlightActive()
{
    InterlockedExchange(&g_activeMountedSoaringFlightItemId, 0);
    InterlockedExchange(&g_activeMountedSoaringFlightTick, 0);
    InterlockedExchange(&g_activeMountedSoaringFlightStartTick, 0);
}

static void ClearMountedSoaringRuntimeFallbackState()
{
    ClearExtendedMountContext();
    ClearMountedSoaringFlightActive();
}

static void ObserveMountedSoaringFlightActive(int mountItemId)
{
    if (mountItemId <= 0)
    {
        return;
    }

    const DWORD nowTick = GetTickCount();
    const LONG previousMountItemId =
        InterlockedCompareExchange(&g_activeMountedSoaringFlightItemId, 0, 0);
    const LONG previousTick =
        InterlockedCompareExchange(&g_activeMountedSoaringFlightTick, 0, 0);
    const bool shouldResetStartTick =
        previousMountItemId != mountItemId ||
        previousTick <= 0 ||
        nowTick - static_cast<DWORD>(previousTick) > kMountedSoaringFlightActiveRefreshGapMs;

    InterlockedExchange(&g_activeMountedSoaringFlightItemId, mountItemId);
    InterlockedExchange(&g_activeMountedSoaringFlightTick, static_cast<LONG>(nowTick));
    if (shouldResetStartTick ||
        InterlockedCompareExchange(&g_activeMountedSoaringFlightStartTick, 0, 0) <= 0)
    {
        InterlockedExchange(&g_activeMountedSoaringFlightStartTick, static_cast<LONG>(nowTick));
    }
}

static bool TryResolveMountedFlightPhysicsScaleForAxis(
    bool preferFs,
    int *mountItemIdOut,
    int *dataKeyOut,
    double *scaleOut,
    const char **scaleSourceOut)
{
    if (mountItemIdOut)
        *mountItemIdOut = 0;
    if (dataKeyOut)
        *dataKeyOut = 0;
    if (scaleOut)
        *scaleOut = 0.0;
    if (scaleSourceOut)
        *scaleSourceOut = nullptr;

    int mountItemId = 0;
    if (!TryResolveCurrentUserMountItemIdWithFallback(&mountItemId, nullptr) ||
        !IsExtendedMountSoaringContextMount(mountItemId))
    {
        ClearMountedSoaringRuntimeFallbackState();
        return false;
    }

    MountedFlightPhysicsScaleSample sample = {};
    if (!TryPrimeMountedFlightPhysicsScaleSampleForMount(mountItemId, &sample) &&
        !(g_MountedFlightPhysicsScaleSample.mountItemId == mountItemId &&
          TryGetMountedFlightPhysicsScaleSampleForRoute(
              mountItemId,
              g_MountedFlightPhysicsScaleSample.dataKey,
              &sample)))
    {
        return false;
    }

    DWORD activeAgeMs = 0;
    DWORD activeDurationMs = 0;
    TryGetMountedSoaringFlightTiming(
        mountItemId,
        &activeAgeMs,
        &activeDurationMs);

    MountedMovementOverride soaringOverride = {};
    const bool hasSoaringOverride =
        SkillOverlayBridgeResolveMountedSoaringOverride(mountItemId, sample.dataKey, soaringOverride);
    const double resolvedOverrideFs =
        hasSoaringOverride && soaringOverride.hasFs && soaringOverride.fs > sample.overrideFs
            ? soaringOverride.fs
            : sample.overrideFs;
    const double resolvedOverrideSwim =
        hasSoaringOverride && soaringOverride.hasSwim && soaringOverride.swim > sample.overrideSwim
            ? soaringOverride.swim
            : sample.overrideSwim;

    const double swimScale =
        (resolvedOverrideSwim > sample.nativeSwim && sample.nativeSwim > 0.0)
            ? (resolvedOverrideSwim / sample.nativeSwim)
            : 0.0;
    const double fsScale =
        (resolvedOverrideFs > sample.nativeFs && sample.nativeFs > 0.0)
            ? (resolvedOverrideFs / sample.nativeFs)
            : 0.0;

    double scale = 0.0;
    const char *source = nullptr;
    if (preferFs)
    {
        if (fsScale > 1.001)
        {
            scale = fsScale;
            source = "fs";
        }
        else if (swimScale > 1.001)
        {
            scale = swimScale;
            source = "swim";
        }
    }
    else
    {
        if (swimScale > 1.001)
        {
            scale = swimScale;
            source = "swim";
        }
        else if (fsScale > 1.001)
        {
            scale = fsScale;
            source = "fs";
        }
    }

    if (!(scale > 1.001) || !(scale < 100000.0))
    {
        return false;
    }

    if (mountItemIdOut)
    {
        *mountItemIdOut = mountItemId;
    }
    if (dataKeyOut)
    {
        *dataKeyOut = sample.dataKey;
    }
    if (scaleOut)
    {
        *scaleOut = scale;
    }
    if (scaleSourceOut)
    {
        *scaleSourceOut = source;
    }
    return true;
}

static bool TryResolveMountedFlightPhysicsScale(
    int *mountItemIdOut,
    int *dataKeyOut,
    double *scaleOut,
    const char **scaleSourceOut)
{
    return TryResolveMountedFlightPhysicsScaleForAxis(
        false,
        mountItemIdOut,
        dataKeyOut,
        scaleOut,
        scaleSourceOut);
}

static bool TryReadEncodedDoubleSlot(uintptr_t slotAddr, double *outValue)
{
    if (!slotAddr || !outValue || SafeIsBadReadPtr(reinterpret_cast<void *>(slotAddr), 12))
    {
        return false;
    }

    tEncodedDoubleReadFn readFn =
        reinterpret_cast<tEncodedDoubleReadFn>(ADDR_445A20);
    if (!readFn)
    {
        return false;
    }

    __try
    {
        *outValue = readFn(reinterpret_cast<void *>(slotAddr));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool TryWriteEncodedDoubleSlot(uintptr_t slotAddr, double value)
{
    if (!slotAddr || SafeIsBadReadPtr(reinterpret_cast<void *>(slotAddr), 12))
    {
        return false;
    }

    tEncodedDoubleWriteFn writeFn =
        reinterpret_cast<tEncodedDoubleWriteFn>(ADDR_444CA0);
    if (!writeFn)
    {
        return false;
    }

    unsigned __int64 bits = 0;
    memcpy(&bits, &value, sizeof(bits));

    __try
    {
        writeFn(
            reinterpret_cast<void *>(slotAddr),
            static_cast<int>(bits & 0xFFFFFFFFu),
            static_cast<int>(bits >> 32));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool TryPatchMountedFlightPhysicsSlot(
    uintptr_t slotAddr,
    double scale,
    MountedFlightPhysicsSlotPatch *outPatch)
{
    if (outPatch)
    {
        outPatch->slotAddr = 0;
        outPatch->originalValue = 0.0;
        outPatch->patchedValue = 0.0;
        outPatch->relativeOffset = 0;
        outPatch->isStateSlot = false;
        outPatch->active = false;
    }

    if (!slotAddr || !(scale > 1.001))
    {
        return false;
    }

    double originalValue = 0.0;
    if (!TryReadEncodedDoubleSlot(slotAddr, &originalValue) ||
        !(originalValue > 0.0) ||
        !(originalValue < 1000000.0))
    {
        return false;
    }

    const double patchedValue = originalValue * scale;
    if (!(patchedValue > originalValue) || !(patchedValue < 1000000000.0))
    {
        return false;
    }

    if (!TryWriteEncodedDoubleSlot(slotAddr, patchedValue))
    {
        return false;
    }

    if (outPatch)
    {
        outPatch->slotAddr = slotAddr;
        outPatch->originalValue = originalValue;
        outPatch->patchedValue = patchedValue;
        outPatch->active = true;
    }
    return true;
}

static void RestoreMountedFlightPhysicsSlotPatches(
    MountedFlightPhysicsSlotPatch *patches,
    size_t patchCount)
{
    if (!patches || patchCount == 0)
    {
        return;
    }

    for (size_t i = 0; i < patchCount; ++i)
    {
        if (!patches[i].active)
        {
            continue;
        }

        TryWriteEncodedDoubleSlot(patches[i].slotAddr, patches[i].originalValue);
        patches[i].active = false;
    }
}

static size_t BeginMountedFlightPhysicsScalePatchForOffsetsWithScale(
    void *thisPtr,
    const char *label,
    double scale,
    int mountItemId,
    int dataKey,
    const char *scaleSource,
    const size_t *localSlotOffsets,
    size_t localSlotCount,
    const size_t *stateSlotOffsets,
    size_t stateSlotCount,
    MountedFlightPhysicsSlotPatch *patches,
    size_t patchCapacity)
{
    if (!thisPtr || !patches || patchCapacity == 0 || !(scale > 1.001))
    {
        return 0;
    }

    for (size_t i = 0; i < patchCapacity; ++i)
    {
        patches[i].slotAddr = 0;
        patches[i].originalValue = 0.0;
        patches[i].patchedValue = 0.0;
        patches[i].relativeOffset = 0;
        patches[i].isStateSlot = false;
        patches[i].active = false;
    }

    size_t patchCount = 0;
    const uintptr_t thisValue = reinterpret_cast<uintptr_t>(thisPtr);
    for (size_t offsetIndex = 0;
         localSlotOffsets &&
         offsetIndex < localSlotCount &&
         patchCount < patchCapacity;
         ++offsetIndex)
    {
        if (TryPatchMountedFlightPhysicsSlot(
                thisValue + localSlotOffsets[offsetIndex],
                scale,
                &patches[patchCount]))
        {
            patches[patchCount].relativeOffset = localSlotOffsets[offsetIndex];
            patches[patchCount].isStateSlot = false;
            ++patchCount;
        }
    }

    uintptr_t statePtr = 0;
    if (!SafeIsBadReadPtr(reinterpret_cast<void *>(thisValue + 332), sizeof(DWORD)))
    {
        __try
        {
            statePtr = *reinterpret_cast<uintptr_t *>(thisValue + 332);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            statePtr = 0;
        }
    }

    for (size_t offsetIndex = 0;
         statePtr &&
         stateSlotOffsets &&
         offsetIndex < stateSlotCount &&
         patchCount < patchCapacity;
         ++offsetIndex)
    {
        if (TryPatchMountedFlightPhysicsSlot(
                statePtr + stateSlotOffsets[offsetIndex],
                scale,
                &patches[patchCount]))
        {
            patches[patchCount].relativeOffset = stateSlotOffsets[offsetIndex];
            patches[patchCount].isStateSlot = true;
            ++patchCount;
        }
    }

    if (patchCount > 0 && InterlockedDecrement(&g_MountedFlightPhysicsScaleLogBudget) >= 0)
    {
        WriteLogFmt(
            "[MountFlightSpeed] %s mount=%d key=%d scale=%.3f source=%s count=%u s0=%c+%u %.6f->%.6f s1=%c+%u %.6f->%.6f s2=%c+%u %.6f->%.6f s3=%c+%u %.6f->%.6f",
            label ? label : "unknown",
            mountItemId,
            dataKey,
            scale,
            scaleSource ? scaleSource : "none",
            static_cast<unsigned int>(patchCount),
            patchCount >= 1 ? (patches[0].isStateSlot ? 'S' : 'L') : '-',
            patchCount >= 1 ? static_cast<unsigned int>(patches[0].relativeOffset) : 0,
            patchCount >= 1 ? patches[0].originalValue : 0.0,
            patchCount >= 1 ? patches[0].patchedValue : 0.0,
            patchCount >= 2 ? (patches[1].isStateSlot ? 'S' : 'L') : '-',
            patchCount >= 2 ? static_cast<unsigned int>(patches[1].relativeOffset) : 0,
            patchCount >= 2 ? patches[1].originalValue : 0.0,
            patchCount >= 2 ? patches[1].patchedValue : 0.0,
            patchCount >= 3 ? (patches[2].isStateSlot ? 'S' : 'L') : '-',
            patchCount >= 3 ? static_cast<unsigned int>(patches[2].relativeOffset) : 0,
            patchCount >= 3 ? patches[2].originalValue : 0.0,
            patchCount >= 3 ? patches[2].patchedValue : 0.0,
            patchCount >= 4 ? (patches[3].isStateSlot ? 'S' : 'L') : '-',
            patchCount >= 4 ? static_cast<unsigned int>(patches[3].relativeOffset) : 0,
            patchCount >= 4 ? patches[3].originalValue : 0.0,
            patchCount >= 4 ? patches[3].patchedValue : 0.0);
    }

    return patchCount;
}

static size_t BeginMountedFlightPhysicsScalePatchForOffsets(
    void *thisPtr,
    const char *label,
    const size_t *localSlotOffsets,
    size_t localSlotCount,
    const size_t *stateSlotOffsets,
    size_t stateSlotCount,
    MountedFlightPhysicsSlotPatch *patches,
    size_t patchCapacity)
{
    int mountItemId = 0;
    int dataKey = 0;
    double scale = 0.0;
    const char *scaleSource = nullptr;
    if (!TryResolveMountedFlightPhysicsScale(&mountItemId, &dataKey, &scale, &scaleSource))
    {
        return 0;
    }
    return BeginMountedFlightPhysicsScalePatchForOffsetsWithScale(
        thisPtr,
        label,
        scale,
        mountItemId,
        dataKey,
        scaleSource,
        localSlotOffsets,
        localSlotCount,
        stateSlotOffsets,
        stateSlotCount,
        patches,
        patchCapacity);
}

static size_t BeginMountedFlightPhysicsScalePatch(
    void *thisPtr,
    const char *label,
    MountedFlightPhysicsSlotPatch *patches,
    size_t patchCapacity)
{
    static const size_t kLocalSlotOffsets[] = {
        340, 352, 364, 376, 388, 400, 412, 424, 436, 448, 460
    };
    static const size_t kStateSlotOffsets[] = { 0, 12, 24, 36 };
    return BeginMountedFlightPhysicsScalePatchForOffsets(
        thisPtr,
        label,
        kLocalSlotOffsets,
        ARRAYSIZE(kLocalSlotOffsets),
        kStateSlotOffsets,
        ARRAYSIZE(kStateSlotOffsets),
        patches,
        patchCapacity);
}

static void ObserveMountedFlightPhysicsEntry(const char *label, void *thisPtr, size_t patchCount)
{
    int mountItemId = 0;
    const char *mountSource = nullptr;
    const bool hasMountContext =
        TryResolveCurrentUserMountItemIdWithFallback(&mountItemId, &mountSource);
    DWORD activeAgeMs = 0;
    DWORD activeDurationMs = 0;
    const bool hasActiveTiming =
        hasMountContext &&
        TryGetMountedSoaringFlightTiming(
            mountItemId,
            &activeAgeMs,
            &activeDurationMs);
    int scaleMountItemId = 0;
    int dataKey = 0;
    double scale = 0.0;
    const char *scaleSource = nullptr;
    const bool hasScale =
        TryResolveMountedFlightPhysicsScaleForAxis(
            true,
            &scaleMountItemId,
            &dataKey,
            &scale,
            &scaleSource);
    if (hasScale && scaleMountItemId > 0)
    {
        mountItemId = scaleMountItemId;
    }

    if (!hasScale && patchCount == 0 && !hasMountContext && !hasActiveTiming)
    {
        return;
    }

    if (InterlockedDecrement(&g_MountedFlightPhysicsEntryLogBudget) < 0)
    {
        return;
    }

    WriteLogFmt(
        "[MountFlightEntry] %s this=0x%08X mount=%d mountSource=%s hasActive=%d age=%u duration=%u key=%d hasScale=%d scale=%.3f source=%s patchCount=%u",
        label ? label : "unknown",
        (DWORD)(uintptr_t)thisPtr,
        mountItemId,
        mountSource ? mountSource : "none",
        hasActiveTiming ? 1 : 0,
        static_cast<unsigned int>(activeAgeMs),
        static_cast<unsigned int>(activeDurationMs),
        dataKey,
        hasScale ? 1 : 0,
        scale,
        scaleSource ? scaleSource : "none",
        static_cast<unsigned int>(patchCount));
}

struct MountedFlightPhysicsStateSnapshot
{
    double posX;
    double posY;
    double velX;
    double velY;
    bool valid;
};

static bool IsReasonableMountedFlightPhysicsValue(double value)
{
    return value == value &&
           value > -1000000000.0 &&
           value < 1000000000.0;
}

static bool TryReadMountedFlightPhysicsStateSnapshot(
    void *thisPtr,
    MountedFlightPhysicsStateSnapshot *outSnapshot)
{
    if (outSnapshot)
    {
        outSnapshot->posX = 0.0;
        outSnapshot->posY = 0.0;
        outSnapshot->velX = 0.0;
        outSnapshot->velY = 0.0;
        outSnapshot->valid = false;
    }

    if (!thisPtr || !outSnapshot)
    {
        return false;
    }

    const uintptr_t thisValue = reinterpret_cast<uintptr_t>(thisPtr);
    if (SafeIsBadReadPtr(reinterpret_cast<void *>(thisValue + 40), 32))
    {
        return false;
    }

    __try
    {
        outSnapshot->posX = *reinterpret_cast<double *>(thisValue + 40);
        outSnapshot->posY = *reinterpret_cast<double *>(thisValue + 48);
        outSnapshot->velX = *reinterpret_cast<double *>(thisValue + 56);
        outSnapshot->velY = *reinterpret_cast<double *>(thisValue + 64);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        outSnapshot->valid = false;
        return false;
    }

    outSnapshot->valid =
        IsReasonableMountedFlightPhysicsValue(outSnapshot->posX) &&
        IsReasonableMountedFlightPhysicsValue(outSnapshot->posY) &&
        IsReasonableMountedFlightPhysicsValue(outSnapshot->velX) &&
        IsReasonableMountedFlightPhysicsValue(outSnapshot->velY);
    return outSnapshot->valid;
}

struct MountedFlightPhysicsScalarSnapshot
{
    double pos;
    double vel;
    bool valid;
};

struct MountedFlightScalarBoostRecord
{
    uintptr_t thisPtr;
    int mountItemId;
    int dataKey;
    DWORD tick;
    MountedFlightPhysicsScalarSnapshot snapshot;
};

struct MountedFlightStateBoostRecord
{
    uintptr_t thisPtr;
    int mountItemId;
    int dataKey;
    DWORD tick;
    MountedFlightPhysicsStateSnapshot snapshot;
};

static const DWORD kMountedFlightBoostPreserveWindowMs = 120;
static volatile LONG g_MountedFlightBoostPreserveLogBudget = 32;
static MountedFlightScalarBoostRecord g_recentMountedFlightScalarBoost = {};
static MountedFlightStateBoostRecord g_recentMountedFlightStateBoost = {};

static bool IsMountedFlightVelocityDirectionCompatible(double currentValue, double boostedValue)
{
    if (fabs(currentValue) <= 0.000001 || fabs(boostedValue) <= 0.000001)
    {
        return true;
    }
    return (currentValue > 0.0 && boostedValue > 0.0) ||
           (currentValue < 0.0 && boostedValue < 0.0);
}

static void ObserveMountedFlightScalarBoost(
    void *thisPtr,
    int mountItemId,
    int dataKey,
    const MountedFlightPhysicsScalarSnapshot &snapshot)
{
    if (!thisPtr || mountItemId <= 0 || !snapshot.valid)
    {
        return;
    }

    g_recentMountedFlightScalarBoost.thisPtr = reinterpret_cast<uintptr_t>(thisPtr);
    g_recentMountedFlightScalarBoost.mountItemId = mountItemId;
    g_recentMountedFlightScalarBoost.dataKey = dataKey;
    g_recentMountedFlightScalarBoost.tick = GetTickCount();
    g_recentMountedFlightScalarBoost.snapshot = snapshot;
}

static bool TryRestoreRecentMountedFlightScalarBoost(
    void *thisPtr,
    int mountItemId,
    int dataKey,
    MountedFlightPhysicsScalarSnapshot *ioSnapshot)
{
    if (!thisPtr ||
        mountItemId <= 0 ||
        !ioSnapshot ||
        !ioSnapshot->valid)
    {
        return false;
    }

    const MountedFlightScalarBoostRecord recent = g_recentMountedFlightScalarBoost;
    if (!recent.snapshot.valid ||
        recent.thisPtr != reinterpret_cast<uintptr_t>(thisPtr) ||
        recent.mountItemId != mountItemId ||
        recent.dataKey != dataKey ||
        recent.tick == 0)
    {
        return false;
    }

    const DWORD nowTick = GetTickCount();
    if (nowTick - recent.tick > kMountedFlightBoostPreserveWindowMs)
    {
        return false;
    }

    const double currentAbsVel = fabs(ioSnapshot->vel);
    const double recentAbsVel = fabs(recent.snapshot.vel);
    if (!(recentAbsVel > 1.0) ||
        currentAbsVel >= recentAbsVel * 0.5 ||
        !IsMountedFlightVelocityDirectionCompatible(ioSnapshot->vel, recent.snapshot.vel))
    {
        return false;
    }

    const double oldVel = ioSnapshot->vel;
    ioSnapshot->vel = recent.snapshot.vel;
    ioSnapshot->valid = true;

    if (InterlockedDecrement(&g_MountedFlightBoostPreserveLogBudget) >= 0)
    {
        WriteLogFmt(
            "[MountFlightPreserve] scalar this=0x%08X mount=%d key=%d age=%u vel=%.6f->%.6f pos=%.6f",
            (DWORD)(uintptr_t)thisPtr,
            mountItemId,
            dataKey,
            nowTick - recent.tick,
            oldVel,
            ioSnapshot->vel,
            ioSnapshot->pos);
    }
    return true;
}

static void ObserveMountedFlightStateBoost(
    void *thisPtr,
    int mountItemId,
    int dataKey,
    const MountedFlightPhysicsStateSnapshot &snapshot)
{
    if (!thisPtr || mountItemId <= 0 || !snapshot.valid)
    {
        return;
    }

    g_recentMountedFlightStateBoost.thisPtr = reinterpret_cast<uintptr_t>(thisPtr);
    g_recentMountedFlightStateBoost.mountItemId = mountItemId;
    g_recentMountedFlightStateBoost.dataKey = dataKey;
    g_recentMountedFlightStateBoost.tick = GetTickCount();
    g_recentMountedFlightStateBoost.snapshot = snapshot;
}

static bool TryRestoreRecentMountedFlightStateBoost(
    void *thisPtr,
    int mountItemId,
    int dataKey,
    MountedFlightPhysicsStateSnapshot *ioSnapshot)
{
    if (!thisPtr ||
        mountItemId <= 0 ||
        !ioSnapshot ||
        !ioSnapshot->valid)
    {
        return false;
    }

    const MountedFlightStateBoostRecord recent = g_recentMountedFlightStateBoost;
    if (!recent.snapshot.valid ||
        recent.thisPtr != reinterpret_cast<uintptr_t>(thisPtr) ||
        recent.mountItemId != mountItemId ||
        recent.dataKey != dataKey ||
        recent.tick == 0)
    {
        return false;
    }

    const DWORD nowTick = GetTickCount();
    if (nowTick - recent.tick > kMountedFlightBoostPreserveWindowMs)
    {
        return false;
    }

    bool restoredAny = false;
    const double oldVelX = ioSnapshot->velX;
    const double oldVelY = ioSnapshot->velY;
    const double recentAbsVelX = fabs(recent.snapshot.velX);
    const double currentAbsVelX = fabs(ioSnapshot->velX);
    if (recentAbsVelX > 1.0 &&
        currentAbsVelX < recentAbsVelX * 0.5 &&
        IsMountedFlightVelocityDirectionCompatible(ioSnapshot->velX, recent.snapshot.velX))
    {
        ioSnapshot->velX = recent.snapshot.velX;
        restoredAny = true;
    }

    const double recentAbsVelY = fabs(recent.snapshot.velY);
    const double currentAbsVelY = fabs(ioSnapshot->velY);
    if (recentAbsVelY > 1.0 &&
        currentAbsVelY < recentAbsVelY * 0.5 &&
        IsMountedFlightVelocityDirectionCompatible(ioSnapshot->velY, recent.snapshot.velY))
    {
        ioSnapshot->velY = recent.snapshot.velY;
        restoredAny = true;
    }

    if (restoredAny && InterlockedDecrement(&g_MountedFlightBoostPreserveLogBudget) >= 0)
    {
        WriteLogFmt(
            "[MountFlightPreserve] state this=0x%08X mount=%d key=%d age=%u vel=(%.6f,%.6f)->(%.6f,%.6f) pos=(%.6f,%.6f)",
            (DWORD)(uintptr_t)thisPtr,
            mountItemId,
            dataKey,
            nowTick - recent.tick,
            oldVelX,
            oldVelY,
            ioSnapshot->velX,
            ioSnapshot->velY,
            ioSnapshot->posX,
            ioSnapshot->posY);
    }

    return restoredAny;
}

static bool TryReadMountedFlightPhysicsScalarSnapshot(
    void *thisPtr,
    size_t posOffset,
    size_t velOffset,
    MountedFlightPhysicsScalarSnapshot *outSnapshot)
{
    if (outSnapshot)
    {
        outSnapshot->pos = 0.0;
        outSnapshot->vel = 0.0;
        outSnapshot->valid = false;
    }

    if (!thisPtr || !outSnapshot)
    {
        return false;
    }

    const uintptr_t thisValue = reinterpret_cast<uintptr_t>(thisPtr);
    if (SafeIsBadReadPtr(reinterpret_cast<void *>(thisValue + posOffset), 16))
    {
        return false;
    }

    __try
    {
        outSnapshot->pos = *reinterpret_cast<double *>(thisValue + posOffset);
        outSnapshot->vel = *reinterpret_cast<double *>(thisValue + velOffset);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        outSnapshot->valid = false;
        return false;
    }

    outSnapshot->valid =
        IsReasonableMountedFlightPhysicsValue(outSnapshot->pos) &&
        IsReasonableMountedFlightPhysicsValue(outSnapshot->vel);
    return outSnapshot->valid;
}

static bool TryWriteMountedFlightPhysicsScalarSnapshot(
    void *thisPtr,
    size_t posOffset,
    size_t velOffset,
    const MountedFlightPhysicsScalarSnapshot &snapshot)
{
    if (!thisPtr ||
        !snapshot.valid ||
        !IsReasonableMountedFlightPhysicsValue(snapshot.pos) ||
        !IsReasonableMountedFlightPhysicsValue(snapshot.vel))
    {
        return false;
    }

    const uintptr_t thisValue = reinterpret_cast<uintptr_t>(thisPtr);
    if (SafeIsBadReadPtr(reinterpret_cast<void *>(thisValue + posOffset), 16))
    {
        return false;
    }

    __try
    {
        *reinterpret_cast<double *>(thisValue + posOffset) = snapshot.pos;
        *reinterpret_cast<double *>(thisValue + velOffset) = snapshot.vel;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool TryReadMountedFlightControlTargets(
    void *thisPtr,
    int *horizontalTargetOut,
    int *verticalTargetOut)
{
    if (horizontalTargetOut)
    {
        *horizontalTargetOut = 0;
    }
    if (verticalTargetOut)
    {
        *verticalTargetOut = 0;
    }

    if (!thisPtr)
    {
        return false;
    }

    const uintptr_t thisValue = reinterpret_cast<uintptr_t>(thisPtr);
    if (SafeIsBadReadPtr(reinterpret_cast<void *>(thisValue + 680), 8))
    {
        return false;
    }

    __try
    {
        if (horizontalTargetOut)
        {
            *horizontalTargetOut = *reinterpret_cast<int *>(thisValue + 680);
        }
        if (verticalTargetOut)
        {
            *verticalTargetOut = *reinterpret_cast<int *>(thisValue + 684);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (horizontalTargetOut)
        {
            *horizontalTargetOut = 0;
        }
        if (verticalTargetOut)
        {
            *verticalTargetOut = 0;
        }
        return false;
    }
}

enum MountedFlightNativeMotionBranch
{
    kMountedFlightNativeMotionBranchUnknown = 0,
    kMountedFlightNativeMotionBranchPrimary48 = 1,
    kMountedFlightNativeMotionBranchSecondary40 = 2,
    kMountedFlightNativeMotionBranchFallback = 3,
};

static bool TryReadMountedFlightBranchState(void *thisPtr, int *branchStateOut)
{
    if (branchStateOut)
    {
        *branchStateOut = 0;
    }

    if (!thisPtr)
    {
        return false;
    }

    const uintptr_t thisValue = reinterpret_cast<uintptr_t>(thisPtr);
    if (SafeIsBadReadPtr(reinterpret_cast<void *>(thisValue + 672), sizeof(DWORD)))
    {
        return false;
    }

    __try
    {
        if (branchStateOut)
        {
            *branchStateOut = *reinterpret_cast<int *>(thisValue + 672);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (branchStateOut)
        {
            *branchStateOut = 0;
        }
        return false;
    }
}

static bool TryResolveMountedFlightNativeMotionBranch(
    void *thisPtr,
    bool *mode48Out,
    bool *mode40Out,
    bool *mode44Out,
    int *branchOut)
{
    if (mode48Out)
    {
        *mode48Out = false;
    }
    if (mode40Out)
    {
        *mode40Out = false;
    }
    if (mode44Out)
    {
        *mode44Out = false;
    }
    if (branchOut)
    {
        *branchOut = kMountedFlightNativeMotionBranchUnknown;
    }

    if (!thisPtr)
    {
        return false;
    }

    const uintptr_t wrapperThis = reinterpret_cast<uintptr_t>(thisPtr) + 16;
    if (SafeIsBadReadPtr(reinterpret_cast<void *>(wrapperThis), sizeof(DWORD)))
    {
        return false;
    }

    __try
    {
        DWORD *vtbl = *reinterpret_cast<DWORD **>(wrapperThis);
        if (!vtbl)
        {
            return false;
        }

        typedef int(__thiscall *tVirtualIntNoArgFn)(void *thisPtr);
        tVirtualIntNoArgFn isPrimary48Fn =
            reinterpret_cast<tVirtualIntNoArgFn>(vtbl[48 / sizeof(DWORD)]);
        tVirtualIntNoArgFn isSecondary40Fn =
            reinterpret_cast<tVirtualIntNoArgFn>(vtbl[40 / sizeof(DWORD)]);
        tVirtualIntNoArgFn isFallback44Fn =
            reinterpret_cast<tVirtualIntNoArgFn>(vtbl[44 / sizeof(DWORD)]);

        const bool mode48 =
            isPrimary48Fn && isPrimary48Fn(reinterpret_cast<void *>(wrapperThis)) != 0;
        bool mode40 = false;
        bool mode44 = false;
        int branch = kMountedFlightNativeMotionBranchUnknown;
        if (mode48)
        {
            branch = kMountedFlightNativeMotionBranchPrimary48;
        }
        else
        {
            mode40 =
                isSecondary40Fn &&
                isSecondary40Fn(reinterpret_cast<void *>(wrapperThis)) != 0;
            if (mode40)
            {
                branch = kMountedFlightNativeMotionBranchSecondary40;
            }
            else
            {
                mode44 =
                    isFallback44Fn &&
                    isFallback44Fn(reinterpret_cast<void *>(wrapperThis)) != 0;
                branch = kMountedFlightNativeMotionBranchFallback;
            }
        }

        if (mode48Out)
        {
            *mode48Out = mode48;
        }
        if (mode40Out)
        {
            *mode40Out = mode40;
        }
        if (mode44Out)
        {
            *mode44Out = mode44;
        }
        if (branchOut)
        {
            *branchOut = branch;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (mode48Out)
        {
            *mode48Out = false;
        }
        if (mode40Out)
        {
            *mode40Out = false;
        }
        if (mode44Out)
        {
            *mode44Out = false;
        }
        if (branchOut)
        {
            *branchOut = kMountedFlightNativeMotionBranchUnknown;
        }
        return false;
    }
}

static void ObserveMountedFlightNativeMotionBranch(
    const char *label,
    void *thisPtr,
    int branchState,
    int horizontalTarget,
    int verticalTarget,
    bool hasMotionBranch,
    bool mode48,
    bool mode40,
    bool mode44,
    int motionBranch)
{
    static LONG s_mountedFlightNativeMotionBranchLogBudget = 64;
    if (InterlockedDecrement(&s_mountedFlightNativeMotionBranchLogBudget) < 0)
    {
        return;
    }

    WriteLogFmt(
        "[MountFlightBranch] %s this=0x%08X state=%d h=%d v=%d hasMotion=%d branch=%d f48=%d f40=%d f44=%d",
        label ? label : "unknown",
        (DWORD)(uintptr_t)thisPtr,
        branchState,
        horizontalTarget,
        verticalTarget,
        hasMotionBranch ? 1 : 0,
        motionBranch,
        mode48 ? 1 : 0,
        mode40 ? 1 : 0,
        mode44 ? 1 : 0);
}

static bool TryWriteMountedFlightPhysicsStateSnapshot(
    void *thisPtr,
    const MountedFlightPhysicsStateSnapshot &snapshot)
{
    if (!thisPtr ||
        !snapshot.valid ||
        !IsReasonableMountedFlightPhysicsValue(snapshot.posX) ||
        !IsReasonableMountedFlightPhysicsValue(snapshot.posY) ||
        !IsReasonableMountedFlightPhysicsValue(snapshot.velX) ||
        !IsReasonableMountedFlightPhysicsValue(snapshot.velY))
    {
        return false;
    }

    const uintptr_t thisValue = reinterpret_cast<uintptr_t>(thisPtr);
    if (SafeIsBadReadPtr(reinterpret_cast<void *>(thisValue + 40), 32))
    {
        return false;
    }

    __try
    {
        *reinterpret_cast<double *>(thisValue + 40) = snapshot.posX;
        *reinterpret_cast<double *>(thisValue + 48) = snapshot.posY;
        *reinterpret_cast<double *>(thisValue + 56) = snapshot.velX;
        *reinterpret_cast<double *>(thisValue + 64) = snapshot.velY;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static double ResolveMountedFlightPhysicsDeltaScaleFromBase(
    double scale,
    double extraScaleWeight,
    double maxDeltaScale)
{
    if (!(scale > 1.001))
    {
        return 1.0;
    }

    const double extraScale = scale > 1.0 ? (scale - 1.0) : 0.0;
    double deltaScale = 1.0 + extraScale * extraScaleWeight;
    if (maxDeltaScale > 1.001 && deltaScale > maxDeltaScale)
    {
        deltaScale = maxDeltaScale;
    }
    return deltaScale;
}

static double ResolveMountedFlightPhysicsEffectiveDeltaScale(double scale)
{
    if (!(scale > 1.001))
    {
        return 1.0;
    }

    const double kMaxEffectiveScale = 8.0;
    if (scale > kMaxEffectiveScale)
    {
        return kMaxEffectiveScale;
    }

    return scale;
}

static bool IsMountedFlightScaleSourceSwim(const char *scaleSource)
{
    return scaleSource && strcmp(scaleSource, "swim") == 0;
}

static double ResolveMountedFlightHorizontalDeltaExtraScaleWeight(
    int mountItemId,
    int dataKey,
    const char *scaleSource)
{
    // Native unlock-only mode stores a synthetic 100 -> N sample so mounts that
    // already use human-percent-like values (300 = 300%) can stay close to a
    // linear scale instead of reusing the old large-unit compensation weights.
    if (IsMountedFlightPhysicsHumanPercentRouteSample(mountItemId, dataKey))
    {
        return 1.0;
    }

    // Mounts whose sustained flight scale comes from `swim` start from a much
    // smaller native ratio (for example 300000/100000 = 3x on 1932063). The
    // old generic weight made those mounts feel almost unmodified.
    return IsMountedFlightScaleSourceSwim(scaleSource) ? 1.45 : 0.10;
}

static double ResolveMountedFlightHorizontalDeltaMaxScale(
    int mountItemId,
    int dataKey,
    const char *scaleSource)
{
    if (IsMountedFlightPhysicsHumanPercentRouteSample(mountItemId, dataKey))
    {
        return 8.0;
    }
    return IsMountedFlightScaleSourceSwim(scaleSource) ? 4.25 : 6.0;
}

static double ResolveMountedFlightHorizontalMinPosDelta(
    int mountItemId,
    int dataKey,
    const char *scaleSource)
{
    if (IsMountedFlightPhysicsHumanPercentRouteSample(mountItemId, dataKey))
    {
        return 0.10;
    }
    return IsMountedFlightScaleSourceSwim(scaleSource) ? 0.10 : 0.75;
}

static double ResolveMountedFlightHorizontalMinVelDelta(
    int mountItemId,
    int dataKey,
    const char *scaleSource)
{
    if (IsMountedFlightPhysicsHumanPercentRouteSample(mountItemId, dataKey))
    {
        return 1.0;
    }
    return IsMountedFlightScaleSourceSwim(scaleSource) ? 2.0 : 12.0;
}

static double ResolveMountedFlightVerticalDeltaExtraScaleWeight(
    int mountItemId,
    int dataKey,
    const char *scaleSource)
{
    if (IsMountedFlightPhysicsHumanPercentRouteSample(mountItemId, dataKey))
    {
        return 1.0;
    }
    return IsMountedFlightScaleSourceSwim(scaleSource) ? 1.35 : 0.08;
}

static double ResolveMountedFlightVerticalDeltaMaxScale(
    int mountItemId,
    int dataKey,
    const char *scaleSource)
{
    if (IsMountedFlightPhysicsHumanPercentRouteSample(mountItemId, dataKey))
    {
        return 4.5;
    }
    return IsMountedFlightScaleSourceSwim(scaleSource) ? 4.25 : 4.5;
}

static DWORD ResolveMountedFlightVerticalCruiseMinActiveMs(
    int mountItemId,
    int dataKey,
    const char *scaleSource)
{
    if (IsMountedFlightPhysicsHumanPercentRouteSample(mountItemId, dataKey))
    {
        return 120;
    }

    // Vertical cruise used to wait 2200ms to avoid stretching takeoff. That is
    // safe for high-ratio fs mounts, but swim-driven mounts never feel faster
    // with such a late gate. Start earlier once the flight is already in the
    // sustained cruise branch.
    return IsMountedFlightScaleSourceSwim(scaleSource) ? 500 : 2200;
}

static double ResolveMountedFlightVerticalMinPosDelta(
    int mountItemId,
    int dataKey,
    const char *scaleSource)
{
    if (IsMountedFlightPhysicsHumanPercentRouteSample(mountItemId, dataKey))
    {
        return 0.10;
    }
    return IsMountedFlightScaleSourceSwim(scaleSource) ? 0.18 : 1.5;
}

static double ResolveMountedFlightVerticalMinVelDelta(
    int mountItemId,
    int dataKey,
    const char *scaleSource)
{
    if (IsMountedFlightPhysicsHumanPercentRouteSample(mountItemId, dataKey))
    {
        return 1.0;
    }
    return IsMountedFlightScaleSourceSwim(scaleSource) ? 2.0 : 18.0;
}

static double ResolveMountedFlightVerticalMaxAbsRawVelDelta(
    int mountItemId,
    int dataKey,
    const char *scaleSource)
{
    if (IsMountedFlightPhysicsHumanPercentRouteSample(mountItemId, dataKey))
    {
        // Unlock-only / native-percent routes still hit a short vertical
        // impulse while entering cruise. Keeping the old 8.0 cap delays Y
        // scaling until roughly half a second into flight, which is why
        // "上下飞行起步" still feels sluggish even after key priming was
        // fixed. A slightly wider window keeps takeoff under control while
        // allowing vertical acceleration to begin earlier.
        return 14.0;
    }

    // The native "up+jump to enter flight" climb still flows through B844D0.
    // Those takeoff frames tend to carry a large velocity change spike, while
    // stable up/down cruise usually settles to near-constant velocity. Skip
    // scaling the impulse frames so mounted flight gets faster without turning
    // takeoff into a long upward launch.
    return IsMountedFlightScaleSourceSwim(scaleSource) ? 12.0 : 0.0;
}

static bool TryResolveMountedFlightScalarAdaptiveScale(
    const MountedFlightPhysicsScalarSnapshot &beforeSnapshot,
    const MountedFlightPhysicsScalarSnapshot &afterSnapshot,
    double baseScale,
    double minPosDelta,
    double minVelDelta,
    double *appliedScaleOut);

static bool TryGetMountedSoaringFlightTiming(
    int mountItemId,
    DWORD *ageMsOut,
    DWORD *activeDurationMsOut)
{
    if (ageMsOut)
    {
        *ageMsOut = 0;
    }
    if (activeDurationMsOut)
    {
        *activeDurationMsOut = 0;
    }
    if (mountItemId <= 0)
    {
        return false;
    }

    const LONG activeMountItemId =
        InterlockedCompareExchange(&g_activeMountedSoaringFlightItemId, 0, 0);
    if (activeMountItemId <= 0 || activeMountItemId != mountItemId)
    {
        return false;
    }

    const LONG activeTick =
        InterlockedCompareExchange(&g_activeMountedSoaringFlightTick, 0, 0);
    if (activeTick <= 0)
    {
        return false;
    }

    const LONG activeStartTick =
        InterlockedCompareExchange(&g_activeMountedSoaringFlightStartTick, 0, 0);
    if (activeStartTick <= 0)
    {
        return false;
    }

    const DWORD nowTick = GetTickCount();
    const DWORD ageMs = nowTick - static_cast<DWORD>(activeTick);
    if (ageMs > kMountedSoaringFlightActiveTimeoutMs)
    {
        ClearMountedSoaringFlightActive();
        return false;
    }

    const DWORD activeDurationMs = nowTick - static_cast<DWORD>(activeStartTick);
    if (ageMsOut)
    {
        *ageMsOut = ageMs;
    }
    if (activeDurationMsOut)
    {
        *activeDurationMsOut = activeDurationMs;
    }
    return true;
}

static bool HasFreshMountedSoaringFlightCruiseTiming(
    int mountItemId,
    DWORD *ageMsOut,
    DWORD *activeDurationMsOut)
{
    DWORD ageMs = 0;
    DWORD activeDurationMs = 0;
    if (!TryGetMountedSoaringFlightTiming(
            mountItemId,
            &ageMs,
            &activeDurationMs))
    {
        return false;
    }

    // Only boost sustained cruise frames. Skipping the first ~120ms avoids
    // stretching the takeoff / mounted jump arc. `ageMs` here is not a per-frame
    // flight heartbeat, so do not use it as a hard stop gate.
    if (activeDurationMs < kMountedFlightCruiseMinActiveMs)
    {
        return false;
    }

    if (ageMsOut)
    {
        *ageMsOut = ageMs;
    }
    if (activeDurationMsOut)
    {
        *activeDurationMsOut = activeDurationMs;
    }
    return true;
}

static void ApplyMountedFlightPhysicsDeltaScale(
    void *thisPtr,
    const char *label,
    const MountedFlightPhysicsStateSnapshot &beforeSnapshot,
    double scale,
    int mountItemId,
    int dataKey,
    const char *scaleSource,
    bool verticalOnly)
{
    if (!beforeSnapshot.valid || !(scale > 1.001))
    {
        return;
    }

    MountedFlightPhysicsStateSnapshot afterSnapshot = {};
    if (!TryReadMountedFlightPhysicsStateSnapshot(thisPtr, &afterSnapshot))
    {
        return;
    }

    const double effectiveScale = ResolveMountedFlightPhysicsEffectiveDeltaScale(scale);
    if (!(effectiveScale > 1.001))
    {
        return;
    }

    const double rawDeltaX = afterSnapshot.posX - beforeSnapshot.posX;
    const double rawDeltaY = afterSnapshot.posY - beforeSnapshot.posY;
    const double rawVelDeltaX = afterSnapshot.velX - beforeSnapshot.velX;
    const double rawVelDeltaY = afterSnapshot.velY - beforeSnapshot.velY;

    if (verticalOnly)
    {
        if (fabs(rawDeltaY) <= 0.000001 && fabs(rawVelDeltaY) <= 0.000001)
        {
            return;
        }
    }
    else if (fabs(rawDeltaX) <= 0.000001 &&
             fabs(rawDeltaY) <= 0.000001 &&
             fabs(rawVelDeltaX) <= 0.000001 &&
             fabs(rawVelDeltaY) <= 0.000001)
    {
        return;
    }

    MountedFlightPhysicsStateSnapshot boostedSnapshot = afterSnapshot;
    if (!verticalOnly)
    {
        boostedSnapshot.posX = beforeSnapshot.posX + rawDeltaX * effectiveScale;
        boostedSnapshot.velX = beforeSnapshot.velX + rawVelDeltaX * effectiveScale;
    }
    boostedSnapshot.posY = beforeSnapshot.posY + rawDeltaY * effectiveScale;
    boostedSnapshot.velY = beforeSnapshot.velY + rawVelDeltaY * effectiveScale;
    boostedSnapshot.valid = true;

    if (!TryWriteMountedFlightPhysicsStateSnapshot(thisPtr, boostedSnapshot))
    {
        return;
    }

    ObserveMountedFlightStateBoost(
        thisPtr,
        mountItemId,
        dataKey,
        boostedSnapshot);

    if (InterlockedDecrement(&g_MountedFlightPhysicsDeltaLogBudget) >= 0)
    {
        WriteLogFmt(
            "[MountFlightDelta] %s mount=%d key=%d scale=%.3f effective=%.3f source=%s rawDelta=(%.6f,%.6f) rawVelDelta=(%.6f,%.6f) boostedDelta=(%.6f,%.6f) boostedVelDelta=(%.6f,%.6f)",
            label ? label : "unknown",
            mountItemId,
            dataKey,
            scale,
            effectiveScale,
            scaleSource ? scaleSource : "none",
            rawDeltaX,
            rawDeltaY,
            rawVelDeltaX,
            rawVelDeltaY,
            boostedSnapshot.posX - beforeSnapshot.posX,
            boostedSnapshot.posY - beforeSnapshot.posY,
            boostedSnapshot.velX - beforeSnapshot.velX,
            boostedSnapshot.velY - beforeSnapshot.velY);
    }
}

static void ApplyMountedFlightPhysicsHorizontalDeltaScale(
    void *thisPtr,
    const char *label,
    const MountedFlightPhysicsStateSnapshot &beforeSnapshot,
    double scale,
    int mountItemId,
    int dataKey,
    const char *scaleSource,
    double minPosDelta,
    double minVelDelta)
{
    if (!beforeSnapshot.valid || !(scale > 1.001))
    {
        return;
    }

    MountedFlightPhysicsStateSnapshot afterSnapshot = {};
    if (!TryReadMountedFlightPhysicsStateSnapshot(thisPtr, &afterSnapshot))
    {
        return;
    }

    const MountedFlightPhysicsScalarSnapshot beforeHorizontal = {
        beforeSnapshot.posX,
        beforeSnapshot.velX,
        true,
    };
    const MountedFlightPhysicsScalarSnapshot afterHorizontal = {
        afterSnapshot.posX,
        afterSnapshot.velX,
        true,
    };

    const double effectiveScale = ResolveMountedFlightPhysicsEffectiveDeltaScale(scale);
    if (!(effectiveScale > 1.001))
    {
        return;
    }

    double adaptiveScale = 1.0;
    if (!TryResolveMountedFlightScalarAdaptiveScale(
            beforeHorizontal,
            afterHorizontal,
            effectiveScale,
            minPosDelta,
            minVelDelta,
            &adaptiveScale))
    {
        return;
    }

    const double rawDeltaX = afterSnapshot.posX - beforeSnapshot.posX;
    const double rawVelDeltaX = afterSnapshot.velX - beforeSnapshot.velX;
    if (fabs(rawDeltaX) <= 0.000001 && fabs(rawVelDeltaX) <= 0.000001)
    {
        return;
    }

    MountedFlightPhysicsStateSnapshot boostedSnapshot = afterSnapshot;
    boostedSnapshot.posX = beforeSnapshot.posX + rawDeltaX * adaptiveScale;
    boostedSnapshot.velX = beforeSnapshot.velX + rawVelDeltaX * adaptiveScale;
    boostedSnapshot.valid = true;

    if (!TryWriteMountedFlightPhysicsStateSnapshot(thisPtr, boostedSnapshot))
    {
        return;
    }

    ObserveMountedFlightStateBoost(
        thisPtr,
        mountItemId,
        dataKey,
        boostedSnapshot);

    if (InterlockedDecrement(&g_MountedFlightPhysicsDeltaLogBudget) >= 0)
    {
        WriteLogFmt(
            "[MountFlightDelta] %s mount=%d key=%d scale=%.3f effective=%.3f adaptive=%.3f source=%s rawDeltaX=%.6f rawVelDeltaX=%.6f boostedDeltaX=%.6f boostedVelDeltaX=%.6f",
            label ? label : "unknown",
            mountItemId,
            dataKey,
            scale,
            effectiveScale,
            adaptiveScale,
            scaleSource ? scaleSource : "none",
            rawDeltaX,
            rawVelDeltaX,
            boostedSnapshot.posX - beforeSnapshot.posX,
            boostedSnapshot.velX - beforeSnapshot.velX);
    }
}

static bool TryResolveMountedFlightScalarAdaptiveScale(
    const MountedFlightPhysicsScalarSnapshot &beforeSnapshot,
    const MountedFlightPhysicsScalarSnapshot &afterSnapshot,
    double baseScale,
    double minPosDelta,
    double minVelDelta,
    double *appliedScaleOut)
{
    if (appliedScaleOut)
    {
        *appliedScaleOut = 1.0;
    }
    if (!beforeSnapshot.valid || !afterSnapshot.valid)
    {
        return false;
    }

    if (!(baseScale > 1.001))
    {
        return false;
    }

    const double rawPosDelta = afterSnapshot.pos - beforeSnapshot.pos;
    const double rawVelDelta = afterSnapshot.vel - beforeSnapshot.vel;
    const double maxAbsVel =
        max(fabs(beforeSnapshot.vel), fabs(afterSnapshot.vel));
    if (fabs(rawPosDelta) <= 0.000001 && fabs(rawVelDelta) <= 0.000001)
    {
        return false;
    }

    double posStrength = 0.0;
    if (minPosDelta > 0.0)
    {
        posStrength = min(1.0, fabs(rawPosDelta) / minPosDelta);
    }
    else if (fabs(rawPosDelta) > 0.000001)
    {
        posStrength = 1.0;
    }

    double velStrength = 0.0;
    const double velMetric = max(fabs(rawVelDelta), maxAbsVel);
    if (minVelDelta > 0.0)
    {
        velStrength = min(1.0, velMetric / minVelDelta);
    }
    else if (velMetric > 0.000001)
    {
        velStrength = 1.0;
    }

    const double strength = max(posStrength, velStrength);
    if (!(strength > 0.01))
    {
        return false;
    }

    const double adaptiveScale = 1.0 + (baseScale - 1.0) * strength;
    if (!(adaptiveScale > 1.001))
    {
        return false;
    }

    if (appliedScaleOut)
    {
        *appliedScaleOut = adaptiveScale;
    }

    return true;
}

static void ApplyMountedFlightPhysicsScalarDeltaScale(
    void *thisPtr,
    const char *label,
    size_t posOffset,
    size_t velOffset,
    const MountedFlightPhysicsScalarSnapshot &beforeSnapshot,
    double scale,
    int mountItemId,
    int dataKey,
    const char *scaleSource,
    bool scalePosition,
    bool scaleVelocity,
    double minPosDelta,
    double minVelDelta,
    double maxAbsRawVelDelta)
{
    if (!beforeSnapshot.valid || !(scale > 1.001) || (!scalePosition && !scaleVelocity))
    {
        return;
    }

    MountedFlightPhysicsScalarSnapshot afterSnapshot = {};
    if (!TryReadMountedFlightPhysicsScalarSnapshot(
            thisPtr,
            posOffset,
            velOffset,
            &afterSnapshot))
    {
        return;
    }

    const double effectiveScale = ResolveMountedFlightPhysicsEffectiveDeltaScale(scale);
    if (!(effectiveScale > 1.001))
    {
        return;
    }

    const double rawPosDelta = afterSnapshot.pos - beforeSnapshot.pos;
    const double rawVelDelta = afterSnapshot.vel - beforeSnapshot.vel;
    if (maxAbsRawVelDelta > 0.0 && fabs(rawVelDelta) > maxAbsRawVelDelta)
    {
        return;
    }
    double adaptiveScale = 1.0;
    if (!TryResolveMountedFlightScalarAdaptiveScale(
            beforeSnapshot,
            afterSnapshot,
            effectiveScale,
            minPosDelta,
            minVelDelta,
            &adaptiveScale))
    {
        return;
    }

    MountedFlightPhysicsScalarSnapshot boostedSnapshot = afterSnapshot;
    if (scalePosition)
    {
        boostedSnapshot.pos = beforeSnapshot.pos + rawPosDelta * adaptiveScale;
    }
    if (scaleVelocity)
    {
        boostedSnapshot.vel = beforeSnapshot.vel + rawVelDelta * adaptiveScale;
    }
    boostedSnapshot.valid = true;
    if (!TryWriteMountedFlightPhysicsScalarSnapshot(
            thisPtr,
            posOffset,
            velOffset,
            boostedSnapshot))
    {
        return;
    }

    ObserveMountedFlightScalarBoost(
        thisPtr,
        mountItemId,
        dataKey,
        boostedSnapshot);

    if (InterlockedDecrement(&g_MountedFlightPhysicsDeltaLogBudget) >= 0)
    {
        WriteLogFmt(
            "[MountFlightScalarDelta] %s mount=%d key=%d scale=%.3f effective=%.3f adaptive=%.3f source=%s mode=%s rawPosDelta=%.6f rawVelDelta=%.6f boostedPosDelta=%.6f boostedVelDelta=%.6f",
            label ? label : "unknown",
            mountItemId,
            dataKey,
            scale,
            effectiveScale,
            adaptiveScale,
            scaleSource ? scaleSource : "none",
            scalePosition && scaleVelocity ? "pos+vel" : (scalePosition ? "pos" : "vel"),
            rawPosDelta,
            rawVelDelta,
            boostedSnapshot.pos - beforeSnapshot.pos,
            boostedSnapshot.vel - beforeSnapshot.vel);
    }
}

static bool TryResolveB8FE30ExternalFlightSlot(void *thisPtr, uintptr_t *slotAddrOut)
{
    if (slotAddrOut)
    {
        *slotAddrOut = 0;
    }
    if (!thisPtr || !slotAddrOut)
    {
        return false;
    }

    const uintptr_t thisValue = reinterpret_cast<uintptr_t>(thisPtr);
    const uintptr_t mountWrapper = thisValue + 16;
    if (SafeIsBadReadPtr(reinterpret_cast<void *>(mountWrapper), sizeof(DWORD)))
    {
        return false;
    }

    uintptr_t linkedStatePtr = 0;
    __try
    {
        DWORD *vtbl = *reinterpret_cast<DWORD **>(mountWrapper);
        if (!vtbl)
        {
            return false;
        }

        typedef int (__thiscall *tVirtualIntNoArgFn)(void *thisPtr);
        tVirtualIntNoArgFn getLinkedStateFn =
            reinterpret_cast<tVirtualIntNoArgFn>(vtbl[168 / sizeof(DWORD)]);
        if (!getLinkedStateFn)
        {
            return false;
        }

        linkedStatePtr = static_cast<uintptr_t>(getLinkedStateFn(reinterpret_cast<void *>(mountWrapper)));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        linkedStatePtr = 0;
    }

    if (!linkedStatePtr)
    {
        return false;
    }

    uintptr_t ownerField = 0;
    if (!SafeIsBadReadPtr(reinterpret_cast<void *>(thisValue + 24), sizeof(DWORD)))
    {
        __try
        {
            ownerField = *reinterpret_cast<DWORD *>(thisValue + 24);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ownerField = 0;
        }
    }
    if (!ownerField)
    {
        return false;
    }

    const uintptr_t ownerBase = ownerField - 4;
    if (SafeIsBadReadPtr(reinterpret_cast<void *>(ownerBase + 156), sizeof(DWORD)))
    {
        return false;
    }

    uintptr_t ownerObj = 0;
    uintptr_t flightObj = 0;
    __try
    {
        ownerObj = *reinterpret_cast<DWORD *>(ownerBase + 156);
        if (!ownerObj)
        {
            return false;
        }

        const uintptr_t ownerObjThis = ownerObj + 4;
        DWORD *vtbl = *reinterpret_cast<DWORD **>(ownerObjThis);
        if (!vtbl)
        {
            return false;
        }

        typedef int (__thiscall *tVirtualOwnerIntFn)(void *thisPtr);
        tVirtualOwnerIntFn getFlightObjFn =
            reinterpret_cast<tVirtualOwnerIntFn>(vtbl[32 / sizeof(DWORD)]);
        if (!getFlightObjFn)
        {
            return false;
        }

        flightObj = static_cast<uintptr_t>(getFlightObjFn(reinterpret_cast<void *>(ownerObjThis)));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        flightObj = 0;
    }

    if (!flightObj)
    {
        return false;
    }

    *slotAddrOut = flightObj - 16 + 364;
    return !SafeIsBadReadPtr(reinterpret_cast<void *>(*slotAddrOut), 12);
}

static void __fastcall hkMountedFlightPhysicsStepB83C90(
    void *thisPtr,
    void * /*edxUnused*/,
    int deltaMs)
{
    ObserveMountedFlightPhysicsEntry("B83C90", thisPtr, 0);

    if (oMountedFlightPhysicsStepB83C90)
    {
        oMountedFlightPhysicsStepB83C90(thisPtr, deltaMs);
    }
}

static int __fastcall hkMountedFlightPhysicsDispatchB87E60(
    void *thisPtr,
    void * /*edxUnused*/,
    int deltaMs)
{
    int branchState = 0;
    const bool hasBranchState =
        TryReadMountedFlightBranchState(thisPtr, &branchState);

    int horizontalTarget = 0;
    int verticalTarget = 0;
    if (hasBranchState && branchState != 0)
    {
        TryReadMountedFlightControlTargets(
            thisPtr,
            &horizontalTarget,
            &verticalTarget);
        ObserveMountedFlightPhysicsEntry("B87E60", thisPtr, 0);
    }

    int mountItemId = 0;
    int dataKey = 0;
    double baseScale = 0.0;
    const char *scaleSource = nullptr;
    DWORD activeAgeMs = 0;
    DWORD activeDurationMs = 0;
    const bool hasScale =
        hasBranchState &&
        branchState != 0 &&
        TryResolveMountedFlightPhysicsScaleForAxis(
            true,
            &mountItemId,
            &dataKey,
            &baseScale,
            &scaleSource);
    const bool hasFreshCruiseTiming =
        hasScale &&
        HasFreshMountedSoaringFlightCruiseTiming(
            mountItemId,
            &activeAgeMs,
            &activeDurationMs);

    const double cruiseScale =
        hasFreshCruiseTiming
            ? ResolveMountedFlightPhysicsDeltaScaleFromBase(
                  baseScale,
                  ResolveMountedFlightHorizontalDeltaExtraScaleWeight(
                      mountItemId,
                      dataKey,
                      scaleSource),
                  ResolveMountedFlightHorizontalDeltaMaxScale(
                      mountItemId,
                      dataKey,
                      scaleSource))
            : 1.0;

    MountedFlightPhysicsStateSnapshot beforeSnapshot = {};
    const bool hasBeforeSnapshot =
        hasFreshCruiseTiming &&
        cruiseScale > 1.001 &&
        horizontalTarget != 0 &&
        TryReadMountedFlightPhysicsStateSnapshot(thisPtr, &beforeSnapshot);

    const int result = oMountedFlightPhysicsDispatchB87E60
                           ? oMountedFlightPhysicsDispatchB87E60(thisPtr, deltaMs)
                           : 0;

    if (hasBeforeSnapshot)
    {
        // B87E60 is the sustained mounted flight dispatcher. Only boost the
        // horizontal pair after the vtbl+0x48 cruise branch has run.
        ApplyMountedFlightPhysicsHorizontalDeltaScale(
            thisPtr,
            "B87E60:X",
            beforeSnapshot,
            cruiseScale,
            mountItemId,
            dataKey,
            scaleSource,
            ResolveMountedFlightHorizontalMinPosDelta(
                mountItemId,
                dataKey,
                scaleSource),
            ResolveMountedFlightHorizontalMinVelDelta(
                mountItemId,
                dataKey,
                scaleSource));
    }

    return result;
}

static void __fastcall hkMountedFlightPhysicsStepB844D0(
    void *thisPtr,
    void * /*edxUnused*/,
    int deltaMs)
{
    int horizontalMountItemId = 0;
    int horizontalDataKey = 0;
    double horizontalScale = 0.0;
    const char *horizontalScaleSource = nullptr;
    const bool hasHorizontalScale =
        TryResolveMountedFlightPhysicsScaleForAxis(
            true,
            &horizontalMountItemId,
            &horizontalDataKey,
            &horizontalScale,
            &horizontalScaleSource);

    int verticalMountItemId = 0;
    int verticalDataKey = 0;
    double verticalScale = 0.0;
    const char *verticalScaleSource = nullptr;
    const bool hasVerticalScale =
        TryResolveMountedFlightPhysicsScaleForAxis(
            false,
            &verticalMountItemId,
            &verticalDataKey,
            &verticalScale,
            &verticalScaleSource);
    const bool hasAnyScale = hasHorizontalScale || hasVerticalScale;
    const int activeMountItemId =
        hasHorizontalScale ? horizontalMountItemId : verticalMountItemId;

    int branchState = 0;
    int horizontalTarget = 0;
    int verticalTarget = 0;
    const bool hasBranchState =
        TryReadMountedFlightBranchState(thisPtr, &branchState);
    const bool hasControlTargets =
        hasBranchState &&
        TryReadMountedFlightControlTargets(
            thisPtr,
            &horizontalTarget,
            &verticalTarget);
    bool motionMode48 = false;
    bool motionMode40 = false;
    bool motionMode44 = false;
    int motionBranch = kMountedFlightNativeMotionBranchUnknown;
    const bool hasMotionBranch =
        TryResolveMountedFlightNativeMotionBranch(
            thisPtr,
            &motionMode48,
            &motionMode40,
            &motionMode44,
            &motionBranch);
    const bool shouldRefreshActiveFlight =
        hasAnyScale &&
        activeMountItemId > 0 &&
        hasBranchState &&
        branchState != 0 &&
        hasMotionBranch &&
        motionBranch == kMountedFlightNativeMotionBranchSecondary40;
    if (shouldRefreshActiveFlight)
    {
        ObserveMountedSoaringFlightActive(activeMountItemId);
    }
    DWORD activeAgeMs = 0;
    DWORD activeDurationMs = 0;
    const bool hasFreshCruiseTiming =
        hasAnyScale &&
        HasFreshMountedSoaringFlightCruiseTiming(
            activeMountItemId,
            &activeAgeMs,
            &activeDurationMs);
    const bool shouldScaleHorizontal =
        hasHorizontalScale &&
        shouldRefreshActiveFlight &&
        hasFreshCruiseTiming;
    const bool shouldScaleVertical =
        hasVerticalScale &&
        shouldRefreshActiveFlight &&
        hasFreshCruiseTiming &&
        activeDurationMs >=
            ResolveMountedFlightVerticalCruiseMinActiveMs(
                verticalMountItemId,
                verticalDataKey,
                verticalScaleSource);
    const double horizontalDeltaScale =
        shouldScaleHorizontal
            ? ResolveMountedFlightPhysicsDeltaScaleFromBase(
                  horizontalScale,
                  ResolveMountedFlightHorizontalDeltaExtraScaleWeight(
                      horizontalMountItemId,
                      horizontalDataKey,
                      horizontalScaleSource),
                  ResolveMountedFlightHorizontalDeltaMaxScale(
                      horizontalMountItemId,
                      horizontalDataKey,
                      horizontalScaleSource))
            : 1.0;
    const double verticalDeltaScale =
        shouldScaleVertical
            ? ResolveMountedFlightPhysicsDeltaScaleFromBase(
                  verticalScale,
                  ResolveMountedFlightVerticalDeltaExtraScaleWeight(
                      verticalMountItemId,
                      verticalDataKey,
                      verticalScaleSource),
                  ResolveMountedFlightVerticalDeltaMaxScale(
                      verticalMountItemId,
                      verticalDataKey,
                      verticalScaleSource))
            : 1.0;

    MountedFlightPhysicsScalarSnapshot beforeHorizontalSnapshot = {};
    const bool hasBeforeHorizontalSnapshot =
        shouldScaleHorizontal &&
        horizontalDeltaScale > 1.001 &&
        TryReadMountedFlightPhysicsScalarSnapshot(
            thisPtr,
            40,
            56,
            &beforeHorizontalSnapshot);
    MountedFlightPhysicsScalarSnapshot beforeVerticalSnapshot = {};
    const bool hasBeforeVerticalSnapshot =
        shouldScaleVertical &&
        verticalDeltaScale > 1.001 &&
        TryReadMountedFlightPhysicsScalarSnapshot(
            thisPtr,
            48,
            64,
            &beforeVerticalSnapshot);

    ObserveMountedFlightPhysicsEntry("B844D0", thisPtr, 0);
    if (hasAnyScale || hasControlTargets || hasMotionBranch)
    {
        ObserveMountedFlightNativeMotionBranch(
            "B844D0",
            thisPtr,
            branchState,
            horizontalTarget,
            verticalTarget,
            hasMotionBranch,
            motionMode48,
            motionMode40,
            motionMode44,
            motionBranch);
    }

    if (oMountedFlightPhysicsStepB844D0)
    {
        oMountedFlightPhysicsStepB844D0(thisPtr, deltaMs);
    }

    if (hasBeforeHorizontalSnapshot)
    {
        // B844D0 also runs through mounted takeoff / jump states. The stable
        // sustained-flight path we observed lands on state!=0 + vtbl+0x28
        // (motion branch 2), so keep the boost there and leave the other
        // motion branches untouched to avoid stretching jump tails again.
        //
        // Only scale horizontal position here. Writing amplified velocity back
        // into the native state keeps cruise fast, but it also leaves residual
        // horizontal drift after the player releases flight input, which shows
        // up in game as the "停下来后抽搐慢慢挪动" tail. Per-frame position
        // amplification is enough to keep the mounted flight speed boost
        // visible while letting the native velocity settle cleanly on stop.
        ApplyMountedFlightPhysicsScalarDeltaScale(
            thisPtr,
            "B844D0:X",
            40,
            56,
            beforeHorizontalSnapshot,
            horizontalDeltaScale,
            horizontalMountItemId,
            horizontalDataKey,
            horizontalScaleSource,
            true,
            false,
            ResolveMountedFlightHorizontalMinPosDelta(
                horizontalMountItemId,
                horizontalDataKey,
                horizontalScaleSource),
            ResolveMountedFlightHorizontalMinVelDelta(
                horizontalMountItemId,
                horizontalDataKey,
                horizontalScaleSource),
            0.0);
    }
    if (hasBeforeVerticalSnapshot)
    {
        // Vertical flight needs a much later cruise gate than horizontal.
        // The "up+jump" takeoff climb keeps feeding into B844D0 well after the
        // horizontal cruise boost can safely start, so hold Y acceleration
        // until the flight has clearly settled. Keep it position-only;
        // amplifying vertical velocity here tends to leave the mount in a
        // sticky float state after releasing the fly key.
        ApplyMountedFlightPhysicsScalarDeltaScale(
            thisPtr,
            "B844D0:Y",
            48,
            64,
            beforeVerticalSnapshot,
            verticalDeltaScale,
            verticalMountItemId,
            verticalDataKey,
            verticalScaleSource,
            true,
            false,
            ResolveMountedFlightVerticalMinPosDelta(
                verticalMountItemId,
                verticalDataKey,
                verticalScaleSource),
            ResolveMountedFlightVerticalMinVelDelta(
                verticalMountItemId,
                verticalDataKey,
                verticalScaleSource),
            ResolveMountedFlightVerticalMaxAbsRawVelDelta(
                verticalMountItemId,
                verticalDataKey,
                verticalScaleSource));
    }
}

static void __fastcall hkMountedFlightPhysicsStepB88090(
    void *thisPtr,
    void * /*edxUnused*/,
    int deltaMs)
{
    ObserveMountedFlightPhysicsEntry("B88090", thisPtr, 0);

    if (oMountedFlightPhysicsStepB88090)
    {
        oMountedFlightPhysicsStepB88090(thisPtr, deltaMs);
    }
}

static int __fastcall hkMountedFlightPhysicsStateB84D70(
    void *thisPtr,
    void * /*edxUnused*/)
{
    // B84D70 mostly seeds takeoff / vertical velocity. Scaling it directly makes
    // mounted jump height explode long before horizontal flight feels faster, so
    // keep it observe-only and let B844D0 carry sustained flight acceleration.
    ObserveMountedFlightPhysicsEntry("B84D70", thisPtr, 0);

    const int result = oMountedFlightPhysicsStateB84D70
                           ? oMountedFlightPhysicsStateB84D70(thisPtr)
                           : 0;

    return result;
}

static int __fastcall hkMountedFlightPhysicsVerticalB8FE30(
    void *thisPtr,
    void * /*edxUnused*/)
{
    // B8FE30 is the vertical flight clamp / landing step. Touching it boosts
    // takeoff height and dive/fall response more than horizontal travel speed.
    ObserveMountedFlightPhysicsEntry("B8FE30", thisPtr, 0);
    const int result = oMountedFlightPhysicsVerticalB8FE30
                           ? oMountedFlightPhysicsVerticalB8FE30(thisPtr)
                           : 0;
    return result;
}

static int __fastcall hkMountedFlightPhysicsFinalizeB851F0(
    void *thisPtr,
    void * /*edxUnused*/,
    int deltaMs)
{
    const int result = oMountedFlightPhysicsFinalizeB851F0
                           ? oMountedFlightPhysicsFinalizeB851F0(thisPtr, deltaMs)
                           : 0;

    int mountItemId = 0;
    if (!TryReadCurrentUserMountItemId(&mountItemId) || mountItemId <= 0)
    {
        ClearMountedSoaringRuntimeFallbackState();
    }

    return result;
}

static bool ShouldSuppressMountedRuntimeSkillUseFailPrompt(
    MountedRuntimeSkillKind kind,
    void *thisPtr,
    int reason,
    DWORD callerRet,
    int *mountItemIdOut,
    int *configuredSkillIdOut)
{
    if (!kEnableMountedDoubleJumpRuntimeHooks || !thisPtr)
    {
        return false;
    }

    int mountItemId = 0;
    const bool resolvedMount = kind == MountedRuntimeSkillKind_DemonJump
                                   ? TryResolveMountedDemonJumpMountItemIdWithFallback(
                                         thisPtr,
                                         &mountItemId,
                                         nullptr,
                                         1200)
                                   : TryResolveMountedDoubleJumpMountItemIdWithFallback(
                                         thisPtr,
                                         &mountItemId,
                                         nullptr,
                                         1200);
    if (!resolvedMount)
    {
        return false;
    }

    const bool hasRecentIntent = kind == MountedRuntimeSkillKind_DemonJump
                                     ? HasRecentMountedDemonJumpIntent(mountItemId, 1200)
                                     : HasRecentMountedDoubleJumpIntent(mountItemId, 1200);
    const bool isMountedJumpProbeCaller =
        reason == 0 && callerRet == 0x00B30785;

    const int configuredSkillId = ResolveMountedRuntimeSkillIdForKind(kind, mountItemId);
    if (configuredSkillId <= 0)
    {
        if (!hasRecentIntent && !isMountedJumpProbeCaller)
        {
            return false;
        }

        // Custom mounts without mountedDoubleJumpEnabled should still suppress
        // the native "cannot use while mounted" spam when the recent input was
        // only probing a double-jump path on that mount. 80001095 currently
        // hits AE6260 from 0x00B30785 without always arming recent intent, so
        // keep a narrow caller-based escape hatch for that mounted jump probe.
        MountedMovementOverride mountedOverride = {};
        if (!SkillOverlayBridgeResolveMountedMovementOverride(
                mountItemId,
                0,
                mountedOverride) ||
            !mountedOverride.matched)
        {
            return false;
        }

        if (mountItemIdOut)
        {
            *mountItemIdOut = mountItemId;
        }
        if (configuredSkillIdOut)
        {
            *configuredSkillIdOut = 0;
        }
        return true;
    }

    if (!hasRecentIntent)
    {
        return false;
    }

    if (mountItemIdOut)
    {
        *mountItemIdOut = mountItemId;
    }
    if (configuredSkillIdOut)
    {
        *configuredSkillIdOut = configuredSkillId;
    }
    return true;
}

static bool ShouldSuppressMountedConfiguredUseFailPrompt(
    void *thisPtr,
    int reason,
    DWORD callerRet,
    MountedRuntimeSkillKind *kindOut,
    int *mountItemIdOut,
    int *configuredSkillIdOut)
{
    if (kindOut)
    {
        *kindOut = MountedRuntimeSkillKind_DoubleJump;
    }

    if (ShouldSuppressMountedRuntimeSkillUseFailPrompt(
            MountedRuntimeSkillKind_DoubleJump,
            thisPtr,
            reason,
            callerRet,
            mountItemIdOut,
            configuredSkillIdOut))
    {
        if (kindOut)
        {
            *kindOut = MountedRuntimeSkillKind_DoubleJump;
        }
        return true;
    }

    if (ShouldSuppressMountedRuntimeSkillUseFailPrompt(
            MountedRuntimeSkillKind_DemonJump,
            thisPtr,
            reason,
            callerRet,
            mountItemIdOut,
            configuredSkillIdOut))
    {
        if (kindOut)
        {
            *kindOut = MountedRuntimeSkillKind_DemonJump;
        }
        return true;
    }

    return false;
}

static bool ShouldMountedDoubleJumpBypassReturnZero(DWORD returnAddr)
{
    return returnAddr == 0x00B1BBAA ||
           returnAddr == 0x00B1C1C5;
}

static bool ShouldMountedDoubleJumpBypassReturnOne(DWORD returnAddr)
{
    return returnAddr == 0x00ADF02B ||
           returnAddr == 0x00B1BFA7 ||
           returnAddr == 0x00B1CCC7 ||
           returnAddr == 0x00B1D0B8;
}

static bool ShouldMountedDemonJumpForceReleaseFallthrough(DWORD returnAddr)
{
    // Mounted demon jump only survives the native late path when these
    // 42DE20 callers see zero and stay on their "continue" edge:
    //   00ADF02B / 00B1D0B8: test eax ; jne fail ; zero continues setup
    //   00B1BFA7 / 00B1CCC7: test eax ; jne skip-release ; zero enters
    //                        7DBC50/B2F370 and the mounted release chain
    // Reusing the mounted double-jump forceOne behavior here short-circuits
    // the demon child skill before it can reach the native packet/action path.
    return returnAddr == 0x00ADF02B ||
           returnAddr == 0x00B1BFA7 ||
           returnAddr == 0x00B1CCC7 ||
           returnAddr == 0x00B1D0B8;
}

static int __fastcall hkMountedStateGate42DE20(void *thisPtr, void * /*edxUnused*/)
{
    int result = oMountedStateGate42DE20
                     ? oMountedStateGate42DE20(thisPtr)
                     : 0;
    if (!kEnableMountedDoubleJumpRuntimeHooks)
    {
        return result;
    }
    if (result <= 0)
    {
        return result;
    }

    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();

    int mountItemId = 0;
    bool resolvedDoubleJumpMount = TryResolveMountedDoubleJumpMountItemIdWithFallback(
        thisPtr,
        &mountItemId,
        nullptr,
        1200);
    if (!resolvedDoubleJumpMount)
    {
        resolvedDoubleJumpMount = TryResolveMountedDemonJumpMountItemIdWithFallback(
            thisPtr,
            &mountItemId,
            nullptr,
            1200);
    }
    if (!resolvedDoubleJumpMount)
    {
        return result;
    }

    if (ShouldMountedDoubleJumpBypassReturnZero(callerRet) ||
        ShouldMountedDoubleJumpBypassReturnOne(callerRet))
    {
        // Observe intent before we know whether this mount really exposes a
        // mounted double-jump skill. That lets AE6260 distinguish "plain jump
        // on a custom mount" from the native fail-prompt spam path.
        ObserveMountedDoubleJumpIntent(mountItemId);
        ObserveMountedDemonJumpIntent(mountItemId);
    }

    const int mountedDoubleJumpSkillId =
        SkillOverlayBridgeResolveMountedDoubleJumpSkillId(mountItemId);
    const int mountedDemonJumpSkillId =
        SkillOverlayBridgeResolveMountedDemonJumpSkillId(mountItemId);
    if (mountedDoubleJumpSkillId <= 0 && mountedDemonJumpSkillId <= 0)
    {
        return result;
    }

    // 42DE20 is an early mounted-state gate. We still do not manufacture
    // intent here; we only consume the route intent that was armed earlier
    // by the real mounted double-jump path. In practice, different caller
    // sites can hit this same gate during the first mounted double-jump, so
    // a narrow caller whitelist still leaks one or more "搭乘中无法使用"
    // prompts. Keep the bypass scoped by fresh mounted double-jump intent
    // instead of by a fragile caller list.
    const bool hasRecentMountedDoubleJumpIntent =
        mountedDoubleJumpSkillId > 0 &&
        HasRecentMountedDoubleJumpIntent(mountItemId, 250);
    const bool hasRecentMountedDemonJumpIntent =
        mountedDemonJumpSkillId > 0 &&
        HasRecentMountedDemonJumpIntent(mountItemId, 250);
    int demonContextSkillId = 0;
    int demonContextRootSkillId = 0;
    bool hasMountedDemonContext = false;
    if (mountedDemonJumpSkillId > 0 && hasRecentMountedDemonJumpIntent)
    {
        PrimeMountedDemonJumpContextIfNeeded(
            mountItemId,
            "42DE20",
            &demonContextSkillId);
        hasMountedDemonContext = HasMountedDemonJumpContextPrimedForMount(
            mountItemId,
            &demonContextSkillId,
            &demonContextRootSkillId);
    }
    static DWORD s_lastMountedStateGateObserveLogTick = 0;
    const DWORD nowTick = GetTickCount();
    if ((mountedDoubleJumpSkillId > 0 || mountedDemonJumpSkillId > 0) &&
        nowTick - s_lastMountedStateGateObserveLogTick > 250)
    {
        s_lastMountedStateGateObserveLogTick = nowTick;
        WriteLogFmt(
            "[MountStateGate] 42DE20 caller=0x%08X mount=%d native=%d forceOne=%d doubleSkill=%d demonSkill=%d doubleIntent=%d demonIntent=%d demonRoot=%d demonCurrent=%d",
            callerRet,
            mountItemId,
            result,
            ShouldMountedDoubleJumpBypassReturnOne(callerRet) ? 1 : 0,
            mountedDoubleJumpSkillId,
            mountedDemonJumpSkillId,
            hasRecentMountedDoubleJumpIntent ? 1 : 0,
            hasRecentMountedDemonJumpIntent ? 1 : 0,
            hasMountedDemonContext ? demonContextRootSkillId : 0,
            hasMountedDemonContext ? demonContextSkillId : 0);
    }
    if (!hasRecentMountedDoubleJumpIntent && !hasRecentMountedDemonJumpIntent)
    {
        return result;
    }

    if (!hasRecentMountedDoubleJumpIntent &&
        hasRecentMountedDemonJumpIntent &&
        ShouldMountedDemonJumpForceReleaseFallthrough(callerRet))
    {
        static LONG s_mountedStateGateDemonFallthroughLogBudget = 24;
        const LONG budgetAfterDecrement =
            InterlockedDecrement(&s_mountedStateGateDemonFallthroughLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt(
                "[MountStateGate] 42DE20 demon release fallthrough caller=0x%08X mount=%d native=%d root=%d current=%d context=%d -> forceZero",
                callerRet,
                mountItemId,
                result,
                hasMountedDemonContext ? demonContextRootSkillId : 0,
                hasMountedDemonContext ? demonContextSkillId : 0,
                hasMountedDemonContext ? 1 : 0);
        }
        return 0;
    }

    const int forcedResult =
        ShouldMountedDoubleJumpBypassReturnOne(callerRet) ? 1 : 0;

    return forcedResult;
}

static int __fastcall hkMountedDemonJumpContextClear433380(
    void *contextPtr,
    void * /*edxUnused*/)
{
    int mountItemId = 0;
    int rootSkillId = 0;
    int currentSkillId = 0;
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    if (ShouldSuppressMountedDemonJumpMountedContextClear(
            contextPtr,
            callerRet,
            &mountItemId,
            &rootSkillId,
            &currentSkillId))
    {
        static LONG s_mountedDemonJumpMountedClearSuppressLogBudget = 64;
        const LONG budgetAfterDecrement =
            InterlockedDecrement(&s_mountedDemonJumpMountedClearSuppressLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt(
                "[MountDemonJumpContext] 433380 suppress mounted clear caller=0x%08X mount=%d root=%d current=%d context=0x%08X",
                callerRet,
                mountItemId,
                rootSkillId,
                currentSkillId,
                (DWORD)(uintptr_t)contextPtr);
        }
        return 0;
    }

    return oMountedDemonJumpContextClear433380
               ? oMountedDemonJumpContextClear433380(contextPtr)
               : 0;
}

static bool ShouldObserveMountedDemonJumpLatePath(
    int *mountItemIdOut,
    int *rootSkillIdOut,
    int *currentSkillIdOut,
    bool *recentIntentOut)
{
    if (mountItemIdOut)
    {
        *mountItemIdOut = 0;
    }
    if (rootSkillIdOut)
    {
        *rootSkillIdOut = 0;
    }
    if (currentSkillIdOut)
    {
        *currentSkillIdOut = 0;
    }
    if (recentIntentOut)
    {
        *recentIntentOut = false;
    }

    int mountItemId = 0;
    const bool hasMount =
        TryResolveCurrentUserMountItemIdWithFallback(&mountItemId, nullptr) &&
        mountItemId > 0;
    int rootSkillId = 0;
    int currentSkillId = 0;
    const bool hasContext = TryReadMountedDemonJumpContextState(
        &rootSkillId,
        &currentSkillId,
        nullptr);
    const bool hasRecentIntent =
        hasMount && HasRecentMountedDemonJumpIntent(mountItemId, 2500);
    const bool demonContextRelevant =
        hasContext &&
        (rootSkillId == 30010110 ||
         currentSkillId == 30010110 ||
         currentSkillId == 30010183 ||
         currentSkillId == 30010184 ||
         currentSkillId == 30010186);
    if (!hasRecentIntent && !demonContextRelevant)
    {
        return false;
    }

    if (mountItemIdOut)
    {
        *mountItemIdOut = mountItemId;
    }
    if (rootSkillIdOut)
    {
        *rootSkillIdOut = rootSkillId;
    }
    if (currentSkillIdOut)
    {
        *currentSkillIdOut = currentSkillId;
    }
    if (recentIntentOut)
    {
        *recentIntentOut = hasRecentIntent;
    }
    return true;
}

static int __fastcall hkMountedDemonJumpLateRoute575D60(
    void *thisPtr,
    void * /*edxUnused*/,
    int a2,
    int a3)
{
    int mountItemId = 0;
    int rootSkillId = 0;
    int currentSkillId = 0;
    bool hasRecentIntent = false;
    const bool shouldLog = ShouldObserveMountedDemonJumpLatePath(
        &mountItemId,
        &rootSkillId,
        &currentSkillId,
        &hasRecentIntent);
    if (shouldLog)
    {
        static LONG s_mountedDemonJumpLateRouteEnterLogBudget = 64;
        if (InterlockedDecrement(&s_mountedDemonJumpLateRouteEnterLogBudget) >= 0)
        {
            WriteLogFmt(
                "[MountDemonJumpLate] 575D60 enter this=0x%08X a2=%d a3=0x%08X mount=%d recent=%d root=%d current=%d",
                (DWORD)(uintptr_t)thisPtr,
                a2,
                a3,
                mountItemId,
                hasRecentIntent ? 1 : 0,
                rootSkillId,
                currentSkillId);
        }
    }

    const int result = oMountedDemonJumpLateRoute575D60
                           ? oMountedDemonJumpLateRoute575D60(thisPtr, a2, a3)
                           : 0;
    if (shouldLog)
    {
        static LONG s_mountedDemonJumpLateRouteLeaveLogBudget = 64;
        if (InterlockedDecrement(&s_mountedDemonJumpLateRouteLeaveLogBudget) >= 0)
        {
            int afterRootSkillId = 0;
            int afterCurrentSkillId = 0;
            TryReadMountedDemonJumpContextState(
                &afterRootSkillId,
                &afterCurrentSkillId,
                nullptr);
            WriteLogFmt(
                "[MountDemonJumpLate] 575D60 leave result=%d mount=%d root=%d current=%d",
                result,
                mountItemId,
                afterRootSkillId,
                afterCurrentSkillId);
        }
    }
    return result;
}

static void __stdcall hkMountedDemonJumpLateTick576020(
    unsigned int a1,
    int a2)
{
    int mountItemId = 0;
    int rootSkillId = 0;
    int currentSkillId = 0;
    bool hasRecentIntent = false;
    const bool shouldLog = ShouldObserveMountedDemonJumpLatePath(
        &mountItemId,
        &rootSkillId,
        &currentSkillId,
        &hasRecentIntent);
    if (shouldLog)
    {
        static LONG s_mountedDemonJumpLateTickEnterLogBudget = 64;
        if (InterlockedDecrement(&s_mountedDemonJumpLateTickEnterLogBudget) >= 0)
        {
            WriteLogFmt(
                "[MountDemonJumpLate] 576020 enter a1=%u a2=0x%08X mount=%d recent=%d root=%d current=%d",
                a1,
                a2,
                mountItemId,
                hasRecentIntent ? 1 : 0,
                rootSkillId,
                currentSkillId);
        }
    }

    if (oMountedDemonJumpLateTick576020)
    {
        oMountedDemonJumpLateTick576020(a1, a2);
    }

    if (shouldLog)
    {
        static LONG s_mountedDemonJumpLateTickLeaveLogBudget = 64;
        if (InterlockedDecrement(&s_mountedDemonJumpLateTickLeaveLogBudget) >= 0)
        {
            int afterRootSkillId = 0;
            int afterCurrentSkillId = 0;
            TryReadMountedDemonJumpContextState(
                &afterRootSkillId,
                &afterCurrentSkillId,
                nullptr);
            WriteLogFmt(
                "[MountDemonJumpLate] 576020 leave mount=%d root=%d current=%d",
                mountItemId,
                afterRootSkillId,
                afterCurrentSkillId);
        }
    }
}

static UINT __fastcall hkMountedDemonJumpContextInputB22630(
    void *thisPtr,
    void * /*edxUnused*/,
    UINT a2,
    int a3)
{
    int mountItemId = 0;
    int rootSkillId = 0;
    int currentSkillId = 0;
    bool hasRecentIntent = false;
    const bool shouldLog = ShouldObserveMountedDemonJumpLatePath(
        &mountItemId,
        &rootSkillId,
        &currentSkillId,
        &hasRecentIntent);
    if (shouldLog)
    {
        static LONG s_mountedDemonJumpContextInputEnterLogBudget = 96;
        if (InterlockedDecrement(&s_mountedDemonJumpContextInputEnterLogBudget) >= 0)
        {
            BYTE gateMode = 0;
            BYTE downLatch = 0;
            BYTE upLatch = 0;
            const uintptr_t userLocalAddr =
                reinterpret_cast<uintptr_t>(thisPtr);
            if (userLocalAddr)
            {
                __try
                {
                    gateMode =
                        *reinterpret_cast<BYTE *>(userLocalAddr + 24292);
                    downLatch =
                        *reinterpret_cast<BYTE *>(userLocalAddr + 24196);
                    upLatch =
                        *reinterpret_cast<BYTE *>(userLocalAddr + 24197);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    gateMode = 0;
                    downLatch = 0;
                    upLatch = 0;
                }
            }
            WriteLogFmt(
                "[MountDemonJumpLate] B22630 enter a2=%u a3=0x%08X mount=%d recent=%d root=%d current=%d mode=%u down=%u up=%u",
                a2,
                a3,
                mountItemId,
                hasRecentIntent ? 1 : 0,
                rootSkillId,
                currentSkillId,
                static_cast<unsigned int>(gateMode),
                static_cast<unsigned int>(downLatch),
                static_cast<unsigned int>(upLatch));
        }
    }

    const UINT result = oMountedDemonJumpContextInputB22630
                            ? oMountedDemonJumpContextInputB22630(thisPtr, a2, a3)
                            : 0;
    if (shouldLog)
    {
        static LONG s_mountedDemonJumpContextInputLeaveLogBudget = 96;
        if (InterlockedDecrement(&s_mountedDemonJumpContextInputLeaveLogBudget) >= 0)
        {
            BYTE gateMode = 0;
            BYTE downLatch = 0;
            BYTE upLatch = 0;
            const uintptr_t userLocalAddr =
                reinterpret_cast<uintptr_t>(thisPtr);
            if (userLocalAddr)
            {
                __try
                {
                    gateMode =
                        *reinterpret_cast<BYTE *>(userLocalAddr + 24292);
                    downLatch =
                        *reinterpret_cast<BYTE *>(userLocalAddr + 24196);
                    upLatch =
                        *reinterpret_cast<BYTE *>(userLocalAddr + 24197);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    gateMode = 0;
                    downLatch = 0;
                    upLatch = 0;
                }
            }
            int afterRootSkillId = 0;
            int afterCurrentSkillId = 0;
            TryReadMountedDemonJumpContextState(
                &afterRootSkillId,
                &afterCurrentSkillId,
                nullptr);
            if (mountItemId > 0 &&
                IsMountedDemonJumpRuntimeChildSkillId(afterCurrentSkillId))
            {
                RememberMountedDemonJumpNativeChildSkill(
                    mountItemId,
                    afterCurrentSkillId,
                    "B22630");
            }
            WriteLogFmt(
                "[MountDemonJumpLate] B22630 leave result=%u mount=%d root=%d current=%d mode=%u down=%u up=%u",
                result,
                mountItemId,
                afterRootSkillId,
                afterCurrentSkillId,
                static_cast<unsigned int>(gateMode),
                static_cast<unsigned int>(downLatch),
                static_cast<unsigned int>(upLatch));
        }
    }
    return result;
}

static BOOL __fastcall hkMountedDemonJumpFilterBDBFD0(
    void *thisPtr,
    void * /*edxUnused*/,
    int a2,
    int a3)
{
    const BOOL result = oMountedDemonJumpFilterBDBFD0
                            ? oMountedDemonJumpFilterBDBFD0(thisPtr, a2, a3)
                            : FALSE;
    int mountItemId = 0;
    int rootSkillId = 0;
    int currentSkillId = 0;
    bool hasRecentIntent = false;
    if (ShouldObserveMountedDemonJumpLatePath(
            &mountItemId,
            &rootSkillId,
            &currentSkillId,
            &hasRecentIntent))
    {
        static LONG s_mountedDemonJumpFilterLogBudget = 64;
        if (InterlockedDecrement(&s_mountedDemonJumpFilterLogBudget) >= 0)
        {
            WriteLogFmt(
                "[MountDemonJumpLate] BDBFD0 result=%d a2=%d a3=0x%08X mount=%d recent=%d root=%d current=%d",
                result ? 1 : 0,
                a2,
                a3,
                mountItemId,
                hasRecentIntent ? 1 : 0,
                rootSkillId,
                currentSkillId);
        }
    }
    return result;
}

static int __fastcall hkMountedDemonJumpBranchADEDA0(
    void *thisPtr,
    void * /*edxUnused*/,
    int a2)
{
    const int result = oMountedDemonJumpBranchADEDA0
                           ? oMountedDemonJumpBranchADEDA0(thisPtr, a2)
                           : 0;
    int mountItemId = 0;
    int rootSkillId = 0;
    int currentSkillId = 0;
    bool hasRecentIntent = false;
    if (ShouldObserveMountedDemonJumpLatePath(
            &mountItemId,
            &rootSkillId,
            &currentSkillId,
            &hasRecentIntent))
    {
        static LONG s_mountedDemonJumpBranchLogBudget = 64;
        if (InterlockedDecrement(&s_mountedDemonJumpBranchLogBudget) >= 0)
        {
            WriteLogFmt(
                "[MountDemonJumpLate] ADEDA0 result=%d a2=0x%08X mount=%d recent=%d root=%d current=%d",
                result,
                a2,
                mountItemId,
                hasRecentIntent ? 1 : 0,
                rootSkillId,
                currentSkillId);
        }
    }
    return result;
}

static void __fastcall hkMountedDemonJumpMoveB1DB10(
    void *thisPtr,
    void * /*edxUnused*/,
    int a2,
    int a3)
{
    int mountItemId = 0;
    int rootSkillId = 0;
    int currentSkillId = 0;
    bool hasRecentIntent = false;
    const bool shouldLog = ShouldObserveMountedDemonJumpLatePath(
        &mountItemId,
        &rootSkillId,
        &currentSkillId,
        &hasRecentIntent);
    if (shouldLog)
    {
        static LONG s_mountedDemonJumpMoveB1DB10LogBudget = 64;
        if (InterlockedDecrement(&s_mountedDemonJumpMoveB1DB10LogBudget) >= 0)
        {
            WriteLogFmt(
                "[MountDemonJumpLate] B1DB10 enter a2=%d a3=0x%08X mount=%d recent=%d root=%d current=%d",
                a2,
                a3,
                mountItemId,
                hasRecentIntent ? 1 : 0,
                rootSkillId,
                currentSkillId);
        }
    }

    if (oMountedDemonJumpMoveB1DB10)
    {
        oMountedDemonJumpMoveB1DB10(thisPtr, a2, a3);
    }
}

static void __fastcall hkMountedDemonJumpMoveB1C9E0(
    void *thisPtr,
    void * /*edxUnused*/,
    int a2,
    int a3)
{
    int mountItemId = 0;
    int rootSkillId = 0;
    int currentSkillId = 0;
    bool hasRecentIntent = false;
    const bool shouldLog = ShouldObserveMountedDemonJumpLatePath(
        &mountItemId,
        &rootSkillId,
        &currentSkillId,
        &hasRecentIntent);
    if (shouldLog)
    {
        static LONG s_mountedDemonJumpMoveB1C9E0LogBudget = 64;
        if (InterlockedDecrement(&s_mountedDemonJumpMoveB1C9E0LogBudget) >= 0)
        {
            WriteLogFmt(
                "[MountDemonJumpLate] B1C9E0 enter a2=%d a3=0x%08X mount=%d recent=%d root=%d current=%d",
                a2,
                a3,
                mountItemId,
                hasRecentIntent ? 1 : 0,
                rootSkillId,
                currentSkillId);
        }
    }

    if (oMountedDemonJumpMoveB1C9E0)
    {
        oMountedDemonJumpMoveB1C9E0(thisPtr, a2, a3);
    }
}

static void __fastcall hkMountedSkillPacketDispatchB26760(
    void *thisPtr,
    void * /*edxUnused*/,
    int skillId,
    int level)
{
    int mountItemId = 0;
    const char *mountSource = nullptr;
    const bool resolvedMount =
        TryResolveMountedDemonJumpMountItemIdWithFallback(
            thisPtr,
            &mountItemId,
            &mountSource,
            1200) ||
        TryReadMountItemIdFromPlayerObject(thisPtr, &mountItemId);
    const bool hasRecentIntent =
        resolvedMount && mountItemId > 0 &&
        HasRecentMountedDemonJumpIntent(mountItemId, 1200);
    int rewrittenSkillId = skillId;
    int rewrittenLevel = level;
    const char *rewriteSource = nullptr;
    int packet93SkillId = skillId;
    int packet93Level = level;
    bool packet93RewriteArmed = false;
    DWORD packet93RewriteTick = 0;
    if (resolvedMount &&
        mountItemId > 0 &&
        skillId == 30010110 &&
        hasRecentIntent)
    {
        packet93SkillId = 30010110;
        packet93Level = 1;
        int rootSkillId = 0;
        int currentSkillId = 0;
        if (TryReadMountedDemonJumpContextState(
                &rootSkillId,
                &currentSkillId,
                nullptr) &&
            rootSkillId == 30010110 &&
            IsMountedDemonJumpRuntimeChildSkillId(currentSkillId))
        {
            rewrittenSkillId = currentSkillId;
            rewrittenLevel = 1;
            rewriteSource = "native-context";
        }
        else
        {
            int recentChildSkillId = 0;
            const char *recentChildSource = nullptr;
            if (TryGetRecentMountedDemonJumpNativeChildSkill(
                    mountItemId,
                    &recentChildSkillId,
                    &recentChildSource,
                    1500) &&
                IsMountedDemonJumpRuntimeChildSkillId(recentChildSkillId))
            {
                rewrittenSkillId = recentChildSkillId;
                rewrittenLevel = 1;
                rewriteSource = recentChildSource;
            }
        }
        packet93RewriteArmed =
            ArmMountedDemonJumpPendingSpecialMoveRewrite(
                mountItemId,
                skillId,
                packet93SkillId,
                packet93Level,
                IsMountedDemonJumpRuntimeChildSkillId(rewrittenSkillId)
                    ? rewrittenSkillId
                    : 0,
                rewriteSource,
                &packet93RewriteTick);
    }
    const bool shouldLog =
        IsMountedDemonJumpRelatedSkillId(skillId) || hasRecentIntent;
    if (shouldLog)
    {
        int rootSkillId = 0;
        int currentSkillId = 0;
        const bool hasContext = TryReadMountedDemonJumpContextState(
            &rootSkillId,
            &currentSkillId,
            nullptr);
        static LONG s_mountedDemonJumpPacketDispatchLogBudget = 64;
        if (InterlockedDecrement(&s_mountedDemonJumpPacketDispatchLogBudget) >= 0)
        {
            WriteLogFmt(
                "[MountDemonJumpPacket] B26760 caller=0x%08X player=0x%08X skill=%d level=%d child=%d childLevel=%d packet93Skill=%d packet93Level=%d mount=%d source=%s recentIntent=%d root=%d current=%d context=%d rewrite=%s pending93=%d tick=%u",
                (DWORD)(uintptr_t)_ReturnAddress(),
                (DWORD)(uintptr_t)thisPtr,
                skillId,
                level & 0xFF,
                rewrittenSkillId,
                rewrittenLevel & 0xFF,
                packet93SkillId,
                packet93Level & 0xFF,
                mountItemId,
                mountSource ? mountSource : (resolvedMount ? "player" : "none"),
                hasRecentIntent ? 1 : 0,
                hasContext ? rootSkillId : 0,
                hasContext ? currentSkillId : 0,
                hasContext ? 1 : 0,
                rewriteSource ? rewriteSource : "none",
                packet93RewriteArmed ? 1 : 0,
                packet93RewriteTick);
        }
    }

    if (oMountedSkillPacketDispatchB26760)
    {
        oMountedSkillPacketDispatchB26760(
            thisPtr,
            skillId,
            static_cast<char>(level));
    }
}

static int __fastcall hkMountedSkillAttackPacketB28A00(
    void *thisPtr,
    void * /*edxUnused*/,
    int *skillIdPtr,
    int a3,
    int a4,
    int a5,
    int a6,
    unsigned int a7,
    int a8)
{
    const int skillId = skillIdPtr ? *skillIdPtr : 0;
    int mountItemId = 0;
    const char *mountSource = nullptr;
    const bool resolvedMount =
        TryResolveMountedDemonJumpMountItemIdWithFallback(
            thisPtr,
            &mountItemId,
            &mountSource,
            1200) ||
        TryReadMountItemIdFromPlayerObject(thisPtr, &mountItemId);
    const bool hasRecentIntent =
        resolvedMount && mountItemId > 0 &&
        HasRecentMountedDemonJumpIntent(mountItemId, 1200);
    const bool shouldLog =
        IsMountedDemonJumpRelatedSkillId(skillId) || hasRecentIntent;

    const int result = oMountedSkillAttackPacketB28A00
                           ? oMountedSkillAttackPacketB28A00(
                                 thisPtr,
                                 skillIdPtr,
                                 a3,
                                 a4,
                                 a5,
                                 a6,
                                 a7,
                                 a8)
                           : 0;

    if (shouldLog)
    {
        int rootSkillId = 0;
        int currentSkillId = 0;
        const bool hasContext = TryReadMountedDemonJumpContextState(
            &rootSkillId,
            &currentSkillId,
            nullptr);
        static LONG s_mountedDemonJumpAttackPacketLogBudget = 64;
        if (InterlockedDecrement(&s_mountedDemonJumpAttackPacketLogBudget) >= 0)
        {
            WriteLogFmt(
                "[MountDemonJumpPacket] B28A00 caller=0x%08X player=0x%08X skill=%d mount=%d source=%s recentIntent=%d root=%d current=%d context=%d result=%d count=%d arg4=%d opcode147Tail=%u",
                (DWORD)(uintptr_t)_ReturnAddress(),
                (DWORD)(uintptr_t)thisPtr,
                skillId,
                mountItemId,
                mountSource ? mountSource : (resolvedMount ? "player" : "none"),
                hasRecentIntent ? 1 : 0,
                hasContext ? rootSkillId : 0,
                hasContext ? currentSkillId : 0,
                hasContext ? 1 : 0,
                result,
                a5,
                a4,
                a7);
        }
    }

    return result;
}

static int __fastcall hkMountedUseFailPromptAE6260(
    void *thisPtr,
    void * /*edxUnused*/,
    int a2)
{
    int mountItemId = 0;
    int configuredSkillId = 0;
    MountedRuntimeSkillKind kind = MountedRuntimeSkillKind_DoubleJump;
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    if (ShouldSuppressMountedConfiguredUseFailPrompt(
            thisPtr,
            a2,
            callerRet,
            &kind,
            &mountItemId,
            &configuredSkillId))
    {
        static LONG s_mountUseFailPromptSuppressLogBudget = 24;
        if (InterlockedDecrement(&s_mountUseFailPromptSuppressLogBudget) >= 0)
        {
            WriteLogFmt(
                "[%s] AE6260 suppress prompt mount=%d skill=%d reason=%d caller=0x%08X",
                GetMountedRuntimeSkillLogTag(kind),
                mountItemId,
                configuredSkillId,
                a2,
                callerRet);
        }
        return 1;
    }

    int debugMountItemId = 0;
    if (TryResolveCurrentUserMountItemIdWithFallback(&debugMountItemId, nullptr))
    {
        MountedMovementOverride mountedOverride = {};
        if (debugMountItemId > 0 &&
            SkillOverlayBridgeResolveMountedMovementOverride(
                debugMountItemId,
                0,
                mountedOverride) &&
            mountedOverride.matched)
        {
            static LONG s_mountUseFailPromptObserveLogBudget = 24;
            if (InterlockedDecrement(&s_mountUseFailPromptObserveLogBudget) >= 0)
            {
                WriteLogFmt(
                    "[MountUseFail] AE6260 passthrough mount=%d reason=%d caller=0x%08X doubleIntent=%d doubleSkill=%d demonIntent=%d demonSkill=%d",
                    debugMountItemId,
                    a2,
                    callerRet,
                    HasRecentMountedDoubleJumpIntent(debugMountItemId, 1200) ? 1 : 0,
                    SkillOverlayBridgeResolveMountedDoubleJumpSkillId(debugMountItemId),
                    HasRecentMountedDemonJumpIntent(debugMountItemId, 1200) ? 1 : 0,
                    SkillOverlayBridgeResolveMountedDemonJumpSkillId(debugMountItemId));
            }
        }
    }

    return oMountedUseFailPromptAE6260
               ? oMountedUseFailPromptAE6260(thisPtr, a2)
               : 0;
}

static bool SetupMountedUseFailPromptSuppressHook()
{
    if (oMountedUseFailPromptAE6260)
    {
        return true;
    }

    oMountedUseFailPromptAE6260 = (tMountedUseFailPromptFn)InstallInlineHook(
        ADDR_AE6260, (void *)hkMountedUseFailPromptAE6260);
    if (!oMountedUseFailPromptAE6260)
    {
        WriteLog("[MountDoubleJump] hook failed: AE6260");
        return false;
    }

    WriteLogFmt("[MountDoubleJump] OK(AE6260): tramp=0x%08X",
                (DWORD)(uintptr_t)oMountedUseFailPromptAE6260);
    return true;
}

static bool TryResolveExtendedMountContextForSoaring(int *mountItemIdOut, bool *fromUserLocalOut)
{
    int mountItemId = 0;
    if (TryGetRecentExtendedMountContext(&mountItemId) &&
        IsExtendedMountSoaringContextMount(mountItemId))
    {
        if (mountItemIdOut)
        {
            *mountItemIdOut = mountItemId;
        }
        if (fromUserLocalOut)
        {
            *fromUserLocalOut = false;
        }
        return true;
    }

    const char *mountSource = nullptr;
    if (!TryResolveCurrentUserMountItemIdWithFallback(&mountItemId, &mountSource) ||
        !IsExtendedMountSoaringContextMount(mountItemId))
    {
        return false;
    }

    const bool fromUserLocal = mountSource && strcmp(mountSource, "user") == 0;
    if (fromUserLocal)
    {
        ObserveExtendedMountContext(mountItemId);
    }
    if (mountItemIdOut)
    {
        *mountItemIdOut = mountItemId;
    }
    if (fromUserLocalOut)
    {
        *fromUserLocalOut = fromUserLocal;
    }
    return true;
}

static int __cdecl hkMountActionGate4069E0(int mountItemId)
{
    if (IsExtendedMountActionGateMount(mountItemId))
    {
        ObserveExtendedMountContext(mountItemId);
        static LONG s_mountActionGateLogBudget = 8;
        const LONG budgetAfterDecrement = InterlockedDecrement(&s_mountActionGateLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[MountGate] 4069E0 extend mount=%d -> allow", mountItemId);
        }
        return 1;
    }

    return oMountActionGate4069E0
               ? oMountActionGate4069E0(mountItemId)
               : 0;
}

static int __cdecl hkMountActionGate406AB0(int mountItemId)
{
    const int result = oMountActionGate406AB0
                           ? oMountActionGate406AB0(mountItemId)
                           : 0;

    if (result <= 0 && IsExtendedMountActionGateMount(mountItemId))
    {
        ObserveExtendedMountContext(mountItemId);
        static LONG s_mountActionGate406AB0ForceAllowLogBudget = 24;
        const LONG budgetAfterDecrement = InterlockedDecrement(&s_mountActionGate406AB0ForceAllowLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[MountGate] 406AB0 extend mount=%d native=%d -> allow",
                        mountItemId,
                        result);
        }
        return 1;
    }

    if (IsExtendedMountServerValidatedSoaringMount(mountItemId))
    {
        static LONG s_mountActionGate406AB0LogBudget = 12;
        const LONG budgetAfterDecrement = InterlockedDecrement(&s_mountActionGate406AB0LogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[MountGate] 406AB0 mount=%d -> %d", mountItemId, result);
        }
    }

    return result;
}

static int __cdecl hkMountNativeFlightSkillMap7CF370(int mountItemId)
{
    const int extendedSkillId = ResolveExtendedMountNativeFlightSkillId(mountItemId);
    if (extendedSkillId > 0)
    {
        ObserveExtendedMountContext(mountItemId);
        if (extendedSkillId == 80001089)
        {
            SkillOverlayBridgeObserveExtendedMountSoaringIntent(mountItemId, extendedSkillId);
        }
        static LONG s_mountNativeFlightSkillLogBudget = 8;
        const LONG budgetAfterDecrement = InterlockedDecrement(&s_mountNativeFlightSkillLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[MountFlightMap] 7CF370 extend mount=%d -> skill=%d", mountItemId, extendedSkillId);
        }
        return extendedSkillId;
    }

    const int result = oMountNativeFlightSkillMap7CF370
                           ? oMountNativeFlightSkillMap7CF370(mountItemId)
                           : 0;

    if (IsExtendedMountServerValidatedSoaringMount(mountItemId))
    {
        static LONG s_mountNativeFlightObserveLogBudget = 12;
        const LONG budgetAfterDecrement = InterlockedDecrement(&s_mountNativeFlightObserveLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[MountFlightMap] 7CF370 native mount=%d -> skill=%d", mountItemId, result);
        }
    }

    return result;
}

static int ResolveExtendedMountNativeSoaringShadowMountItemId(int mountItemId)
{
    if (IsExtendedMountServerValidatedSoaringMount(mountItemId))
    {
        // 只用于 B26290 开头的 199xxxx 家族门槛；真实发包 skillId 仍保持 80001089。
        return 1992018;
    }
    return 0;
}

static int __fastcall hkMountNativeSoaringReleaseB26290(
    void *thisPtr,
    void * /*edxUnused*/,
    int skillId)
{
    int mountItemId = 0;
    bool hasMountItemId = TryReadMountItemIdFromPlayerObject(thisPtr, &mountItemId);
    if (!hasMountItemId &&
        TryResolveCurrentUserMountItemIdWithFallback(&mountItemId, nullptr))
    {
        hasMountItemId = mountItemId > 0;
    }
    if (skillId == 80001089 && hasMountItemId)
    {
        SkillOverlayBridgeObserveExtendedMountSoaringIntent(mountItemId, skillId);
    }
    const int shadowMountItemId =
        skillId == 80001089 && hasMountItemId &&
                !HasRecentMountedDoubleJumpIntent(mountItemId)
            ? ResolveExtendedMountNativeSoaringShadowMountItemId(mountItemId)
            : 0;
    const uintptr_t mountItemIdAddr = reinterpret_cast<uintptr_t>(thisPtr) + 0x454;
    bool patchedMountFamilyGate = false;

    if (shadowMountItemId > 0 &&
        mountItemId != shadowMountItemId &&
        !SafeIsBadReadPtr(reinterpret_cast<void *>(mountItemIdAddr), sizeof(DWORD)))
    {
        __try
        {
            *reinterpret_cast<int *>(mountItemIdAddr) = shadowMountItemId;
            patchedMountFamilyGate = true;
            static LONG s_mountNativeSoaringPatchLogBudget = 24;
            const LONG budgetAfterDecrement = InterlockedDecrement(&s_mountNativeSoaringPatchLogBudget);
            if (budgetAfterDecrement >= 0)
            {
                WriteLogFmt("[MountSoaringNative] B26290 shadow mount=%d -> %d skill=%d",
                            mountItemId,
                            shadowMountItemId,
                            skillId);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            patchedMountFamilyGate = false;
            static LONG s_mountNativeSoaringPatchExceptionLogBudget = 8;
            const LONG budgetAfterDecrement =
                InterlockedDecrement(&s_mountNativeSoaringPatchExceptionLogBudget);
            if (budgetAfterDecrement >= 0)
            {
                WriteLogFmt("[MountSoaringNative] B26290 shadow write exception code=0x%08X mount=%d shadow=%d skill=%d",
                            GetExceptionCode(),
                            mountItemId,
                            shadowMountItemId,
                            skillId);
            }
        }
    }

    int result = 0;
    DWORD callExceptionCode = 0;
    __try
    {
        result = oMountNativeSoaringReleaseB26290
                     ? oMountNativeSoaringReleaseB26290(thisPtr, skillId)
                     : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        callExceptionCode = GetExceptionCode();
        result = 0;
    }

    if (patchedMountFamilyGate &&
        !SafeIsBadReadPtr(reinterpret_cast<void *>(mountItemIdAddr), sizeof(DWORD)))
    {
        __try
        {
            *reinterpret_cast<int *>(mountItemIdAddr) = mountItemId;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    if (callExceptionCode != 0)
    {
        static LONG s_mountNativeSoaringCallExceptionLogBudget = 8;
        const LONG budgetAfterDecrement =
            InterlockedDecrement(&s_mountNativeSoaringCallExceptionLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[MountSoaringNative] B26290 call exception code=0x%08X mount=%d skill=%d shadow=%d",
                        callExceptionCode,
                        mountItemId,
                        skillId,
                        patchedMountFamilyGate ? shadowMountItemId : 0);
        }
    }

    if (skillId == 80001089 && hasMountItemId)
    {
        SkillOverlayBridgeObserveExtendedMountSoaringIntent(mountItemId, skillId);
        if (result > 0)
        {
            ObserveMountedSoaringFlightActive(mountItemId);
        }

        static LONG s_mountNativeSoaringResultLogBudget = 48;
        const LONG budgetAfterDecrement = InterlockedDecrement(&s_mountNativeSoaringResultLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[MountSoaringNative] B26290 final mount=%d skill=%d shadow=%d result=%d",
                        mountItemId,
                        skillId,
                        patchedMountFamilyGate ? shadowMountItemId : 0,
                        result);
        }
    }

    return result;
}

static BOOL CallNativeMountContextIsFlyingFamily7D4CD0(void *mountContext)
{
    typedef BOOL(__thiscall *tMountContextFamilyBaseGateFn)(void *mountContext);
    typedef void(__thiscall *tMountContextSharedRefFn)(void *sharedRefPtr);

    if (!mountContext || SafeIsBadReadPtr(mountContext, sizeof(DWORD)))
    {
        return FALSE;
    }

    tMountContextFamilyBaseGateFn baseGateFn =
        reinterpret_cast<tMountContextFamilyBaseGateFn>(ADDR_7D4C00);
    tMountContextSharedRefFn sharedRefFn =
        reinterpret_cast<tMountContextSharedRefFn>(ADDR_4010B0);
    if (!baseGateFn || !sharedRefFn)
    {
        return FALSE;
    }

    __try
    {
        if (!baseGateFn(mountContext))
        {
            return FALSE;
        }

        const uintptr_t mountInfoAddr =
            *reinterpret_cast<uintptr_t *>(reinterpret_cast<uintptr_t>(mountContext) + 0x1BF0);
        const uintptr_t kSentinelMountInfoAddr =
            static_cast<uintptr_t>(static_cast<intptr_t>(-24));
        if (!mountInfoAddr || mountInfoAddr == kSentinelMountInfoAddr)
        {
            return FALSE;
        }

        int *mountInfo = reinterpret_cast<int *>(mountInfoAddr);
        if (SafeIsBadReadPtr(mountInfo, 0x20))
        {
            return FALSE;
        }

        sharedRefFn(reinterpret_cast<void *>(mountInfoAddr + 0x18));
        if (mountInfo[7]-- == 1)
        {
            mountInfo[6] = 0;
        }

        return (mountInfo[3] / 10000) == 199 ? TRUE : FALSE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        static LONG s_mountFamilyNativeMirrorExceptionLogBudget = 8;
        const LONG budgetAfterDecrement =
            InterlockedDecrement(&s_mountFamilyNativeMirrorExceptionLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[MountFamily] 7D4CD0 mirror exception code=0x%08X",
                        GetExceptionCode());
        }
        return FALSE;
    }
}

static int __fastcall hkMountSoaringGate7DC1B0(
    void *thisPtr,
    void * /*edxUnused*/,
    int levelContext,
    void *mountContext,
    int skillId,
    unsigned int **skillEntryOut)
{
    int result = oMountSoaringGate7DC1B0
                     ? oMountSoaringGate7DC1B0(thisPtr, levelContext, mountContext, skillId, skillEntryOut)
                     : 0;

    if (result > 0 || skillId != 80001089 || !mountContext)
    {
        return result;
    }

    int mountItemId = 0;
    if (!TryResolveMountItemIdFromContextPointer(mountContext, &mountItemId))
    {
        if (!TryResolveExtendedMountContextForSoaring(&mountItemId, nullptr))
        {
            return result;
        }
    }

    ObserveExtendedMountContext(mountItemId);
    // Mounted double-jump must keep suppressing the soaring extension to avoid
    // being rerouted into the flight/glide branch. Mounted demon jump is the
    // opposite: it still needs the native soaring chain for takeoff/glide.
    if (HasRecentMountedDoubleJumpIntent(mountItemId))
    {
        return result;
    }
    const int mappedNativeFlightSkillId = ResolveExtendedMountNativeFlightSkillId(mountItemId);
    if (mappedNativeFlightSkillId <= 0)
    {
        return result;
    }

    if (IsExtendedMountFamilyGateMount(mountItemId))
    {
        if (skillEntryOut && !*skillEntryOut)
        {
            const uintptr_t soaringSkillEntry = SkillOverlayBridgeLookupSkillEntryPointer(80001089);
            if (soaringSkillEntry)
            {
                *skillEntryOut = reinterpret_cast<unsigned int *>(soaringSkillEntry);
            }
        }

        static LONG s_mountSoaringGatePostNativeForceLogBudget = 24;
        const LONG budgetAfterDecrement = InterlockedDecrement(&s_mountSoaringGatePostNativeForceLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[MountSoaringGate] 7DC1B0 post-native force mount=%d native=%d soaring=%d donor=%d -> allow",
                        mountItemId,
                        result,
                        skillId,
                        mappedNativeFlightSkillId);
        }

        SkillOverlayBridgeObserveExtendedMountSoaringIntent(mountItemId, skillId);
        return 1;
    }

    if (SafeIsBadReadPtr(mountContext, sizeof(DWORD)))
    {
        return result;
    }

    BOOL isFlyingFamily = FALSE;
    __try
    {
        isFlyingFamily = CallNativeMountContextIsFlyingFamily7D4CD0(mountContext);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        static LONG s_mountSoaringGateFamilyExceptionLogBudget = 8;
        const LONG budgetAfterDecrement = InterlockedDecrement(&s_mountSoaringGateFamilyExceptionLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[MountSoaringGate] 7DC1B0 family check exception code=0x%08X",
                        GetExceptionCode());
        }
        isFlyingFamily = FALSE;
    }
    if (!isFlyingFamily)
    {
        return result;
    }

    if (skillEntryOut && !*skillEntryOut)
    {
        const uintptr_t soaringSkillEntry = SkillOverlayBridgeLookupSkillEntryPointer(80001089);
        if (soaringSkillEntry)
        {
            *skillEntryOut = reinterpret_cast<unsigned int *>(soaringSkillEntry);
        }
    }

    static LONG s_mountSoaringGateLogBudget = 16;
    const LONG budgetAfterDecrement = InterlockedDecrement(&s_mountSoaringGateLogBudget);
    if (budgetAfterDecrement >= 0)
    {
        WriteLogFmt("[MountSoaringGate] 7DC1B0 extend mount=%d soaring=%d donor=%d -> allow",
                    mountItemId,
                    skillId,
                    mappedNativeFlightSkillId);
    }

    SkillOverlayBridgeObserveExtendedMountSoaringIntent(mountItemId, skillId);
    return 1;
}

static BOOL __fastcall hkMountContextIsFlyingFamily7D4CD0(void *thisPtr, void * /*edxUnused*/)
{
    int mountItemId = 0;
    bool fromFallbackContext = false;
    if (!TryResolveMountItemIdFromContextPointer(thisPtr, &mountItemId) &&
        TryResolveExtendedMountContextForSoaring(&mountItemId, nullptr))
    {
        fromFallbackContext = true;
    }

    if (mountItemId > 0)
    {
        ObserveExtendedMountContext(mountItemId);
    }

    const bool suppressExtendedSoaringForDoubleJump =
        HasRecentMountedDoubleJumpIntent(mountItemId);

    if (!suppressExtendedSoaringForDoubleJump &&
        IsExtendedMountFamilyGateMount(mountItemId))
    {
        ObserveMountedSoaringFlightActive(mountItemId);
        static LONG s_mountFamilyPreBypassLogBudget = 24;
        const LONG budgetAfterDecrement = InterlockedDecrement(&s_mountFamilyPreBypassLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[MountFamily] 7D4CD0 pre-bypass mount=%d source=%s -> allow",
                        mountItemId,
                        fromFallbackContext ? "fallback" : "context");
        }
        return TRUE;
    }

    BOOL result = FALSE;
    const bool contextReadable = thisPtr && !SafeIsBadReadPtr(thisPtr, sizeof(DWORD));
    if (contextReadable)
    {
        __try
        {
            result = CallNativeMountContextIsFlyingFamily7D4CD0(thisPtr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            static LONG s_mountFamilyExceptionLogBudget = 8;
            const LONG budgetAfterDecrement = InterlockedDecrement(&s_mountFamilyExceptionLogBudget);
            if (budgetAfterDecrement >= 0)
            {
                WriteLogFmt("[MountFamily] 7D4CD0 native exception code=0x%08X", GetExceptionCode());
            }
            result = FALSE;
        }
    }

    if (result)
    {
        ObserveMountedSoaringFlightActive(mountItemId);
        return TRUE;
    }

    if (suppressExtendedSoaringForDoubleJump)
    {
        return result;
    }

    if (!IsExtendedMountSoaringContextMount(mountItemId))
    {
        return result;
    }

    static LONG s_mountFamilyForceAllowLogBudget = 16;
    const LONG budgetAfterDecrement = InterlockedDecrement(&s_mountFamilyForceAllowLogBudget);
    if (budgetAfterDecrement >= 0)
    {
        WriteLogFmt("[MountFamily] 7D4CD0 extend mount=%d native=%d source=%s -> allow",
                    mountItemId,
                    result ? 1 : 0,
                    fromFallbackContext ? "fallback" : "context");
    }
    ObserveMountedSoaringFlightActive(mountItemId);
    return TRUE;
}

static int __fastcall hkMountFamilyGateA9AAA0(void *thisPtr, void * /*edxUnused*/)
{
    int result = 0;
    if (oMountFamilyGateA9AAA0)
    {
        __try
        {
            result = oMountFamilyGateA9AAA0(thisPtr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            static LONG s_mountFamilyGateExceptionLogBudget = 8;
            const LONG budgetAfterDecrement = InterlockedDecrement(&s_mountFamilyGateExceptionLogBudget);
            if (budgetAfterDecrement >= 0)
            {
                WriteLogFmt("[MountFamilyGate] A9AAA0 native exception code=0x%08X", GetExceptionCode());
            }
            result = 0;
        }
    }

    int mountItemId = 0;
    bool mountFromContext = false;
    bool mountFromFallback = false;

    if (thisPtr)
    {
        const uintptr_t mountItemIdAddr = reinterpret_cast<uintptr_t>(thisPtr) + 0x3C4;
        if (!SafeIsBadReadPtr(reinterpret_cast<void *>(mountItemIdAddr), sizeof(DWORD)))
        {
            __try
            {
                mountItemId = *reinterpret_cast<int *>(mountItemIdAddr);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                mountItemId = 0;
            }
            mountFromContext = mountItemId > 0;
        }
    }

    if (mountItemId <= 0)
    {
        if (TryResolveExtendedMountContextForSoaring(&mountItemId, nullptr))
        {
            mountFromFallback = mountItemId > 0;
        }
    }

    if (mountItemId > 0)
    {
        ObserveExtendedMountContext(mountItemId);
    }

    if (result > 0)
    {
        return result;
    }

    if (HasRecentMountedDoubleJumpIntent(mountItemId))
    {
        return result;
    }

    if (!IsExtendedMountFamilyGateMount(mountItemId))
    {
        return result;
    }

    static LONG s_mountFamilyGateLogBudget = 16;
    const LONG budgetAfterDecrement = InterlockedDecrement(&s_mountFamilyGateLogBudget);
    if (budgetAfterDecrement >= 0)
    {
        WriteLogFmt("[MountFamilyGate] A9AAA0 extend mount=%d native=%d source=%s -> allow",
                    mountItemId,
                    result,
                    mountFromContext ? "context" : (mountFromFallback ? "fallback" : "unknown"));
    }
    return 1;
}

static BOOL ResolveForcedNativeSkillGateAllow(int originalSkillId, int mappedSkillId)
{
    return SkillOverlayBridgeShouldForceNativeGateAllow(originalSkillId) ||
           (mappedSkillId != originalSkillId &&
            SkillOverlayBridgeShouldForceNativeGateAllow(mappedSkillId));
}

static BOOL ResolveRecentMountSoaringSkillGateAllow(
    int originalSkillId,
    int mappedSkillId,
    int *resolvedMountItemIdOut,
    bool *fromUserLocalOut)
{
    if (originalSkillId != 80001089 && mappedSkillId != 80001089)
    {
        return FALSE;
    }

    int mountItemId = 0;
    bool fromUserLocal = false;
    if (!TryResolveExtendedMountContextForSoaring(&mountItemId, &fromUserLocal))
    {
        return FALSE;
    }

    if (HasRecentMountedDoubleJumpIntent(mountItemId))
    {
        return FALSE;
    }

    if (resolvedMountItemIdOut)
    {
        *resolvedMountItemIdOut = mountItemId;
    }
    if (fromUserLocalOut)
    {
        *fromUserLocalOut = fromUserLocal;
    }
    return TRUE;
}

static BOOL ResolveMountedRuntimeSkillGateAllow(
    MountedRuntimeSkillKind kind,
    int originalSkillId,
    int mappedSkillId,
    int *resolvedMountItemIdOut)
{
    if (!kEnableMountedDoubleJumpRuntimeHooks)
    {
        return FALSE;
    }

    if (originalSkillId <= 0 && mappedSkillId <= 0)
    {
        return FALSE;
    }

    int mountItemId = 0;
    const bool resolvedMount = kind == MountedRuntimeSkillKind_DemonJump
                                   ? TryResolveMountedDemonJumpMountItemIdWithFallback(
                                         nullptr,
                                         &mountItemId,
                                         nullptr,
                                         1200)
                                   : TryResolveMountedDoubleJumpMountItemIdWithFallback(
                                         nullptr,
                                         &mountItemId,
                                         nullptr,
                                         1200);
    if (!resolvedMount)
    {
        return FALSE;
    }

    if (!CanUseMountedRuntimeSkillRuntimeForKind(kind, mountItemId, originalSkillId) &&
        !CanUseMountedRuntimeSkillRuntimeForKind(kind, mountItemId, mappedSkillId))
    {
        return FALSE;
    }

    if (resolvedMountItemIdOut)
    {
        *resolvedMountItemIdOut = mountItemId;
    }
    return TRUE;
}

static BOOL ResolveMountedConfiguredSkillGateAllow(
    int originalSkillId,
    int mappedSkillId,
    MountedRuntimeSkillKind *kindOut,
    int *resolvedMountItemIdOut)
{
    if (kindOut)
    {
        *kindOut = MountedRuntimeSkillKind_DoubleJump;
    }

    if (ResolveMountedRuntimeSkillGateAllow(
            MountedRuntimeSkillKind_DoubleJump,
            originalSkillId,
            mappedSkillId,
            resolvedMountItemIdOut))
    {
        if (kindOut)
        {
            *kindOut = MountedRuntimeSkillKind_DoubleJump;
        }
        return TRUE;
    }

    if (ResolveMountedRuntimeSkillGateAllow(
            MountedRuntimeSkillKind_DemonJump,
            originalSkillId,
            mappedSkillId,
            resolvedMountItemIdOut))
    {
        if (kindOut)
        {
            *kindOut = MountedRuntimeSkillKind_DemonJump;
        }
        return TRUE;
    }

    return FALSE;
}

static bool IsMountedDemonJumpRelatedSkillId(int skillId)
{
    return skillId == 30010110 ||
           skillId == 30010183 ||
           skillId == 30010184 ||
           skillId == 30010186 ||
           skillId == 20021181 ||
           skillId == 23001002 ||
           skillId == 33001002;
}

static bool IsMountedDemonJumpRuntimeChildSkillId(int skillId)
{
    return skillId == 30010183 ||
           skillId == 30010184 ||
           skillId == 30010186;
}

static bool IsMountedDemonJumpRuntimeProxySkillId(int skillId)
{
    return skillId == 20021181 ||
           skillId == 23001002 ||
           skillId == 33001002;
}

static bool IsMountedDemonJumpBridgeEntrySkillId(int skillId)
{
    return skillId == 30010110 ||
           IsMountedDemonJumpRuntimeProxySkillId(skillId);
}

static volatile LONG g_mountedDemonJumpCrashTraceRuntimeSkillId = 0;
static volatile LONG g_mountedDemonJumpCrashTraceMountItemId = 0;
static volatile LONG g_mountedDemonJumpCrashTraceTick = 0;
static volatile LONG g_recentMountedDemonJumpNativeChildSkillId = 0;
static volatile LONG g_recentMountedDemonJumpNativeChildMountItemId = 0;
static volatile LONG g_recentMountedDemonJumpNativeChildTick = 0;
static volatile LONG g_pendingMountedDemonJumpRewriteActive = 0;
static volatile LONG g_pendingMountedDemonJumpRewriteMountItemId = 0;
static volatile LONG g_pendingMountedDemonJumpRewriteExpectedSkillId = 0;
static volatile LONG g_pendingMountedDemonJumpRewritePacketSkillId = 0;
static volatile LONG g_pendingMountedDemonJumpRewritePacketLevel = 0;
static volatile LONG g_pendingMountedDemonJumpRewriteRuntimeChildSkillId = 0;
static volatile LONG g_pendingMountedDemonJumpRewriteArmTick = 0;
static BYTE g_pendingMountedDemonJumpRewritePacket[16] = {};

static void ArmMountedDemonJumpCrashTrace(int runtimeSkillId, int mountItemId)
{
    if (!IsMountedDemonJumpRelatedSkillId(runtimeSkillId) || mountItemId <= 0)
    {
        return;
    }

    InterlockedExchange(&g_mountedDemonJumpCrashTraceRuntimeSkillId, runtimeSkillId);
    InterlockedExchange(&g_mountedDemonJumpCrashTraceMountItemId, mountItemId);
    InterlockedExchange(&g_mountedDemonJumpCrashTraceTick, static_cast<LONG>(GetTickCount()));
}

static bool IsMountedDemonJumpCrashTraceFresh(
    int *runtimeSkillIdOut,
    int *mountItemIdOut,
    DWORD maxAgeMs)
{
    const LONG runtimeSkillId =
        InterlockedCompareExchange(&g_mountedDemonJumpCrashTraceRuntimeSkillId, 0, 0);
    const LONG mountItemId =
        InterlockedCompareExchange(&g_mountedDemonJumpCrashTraceMountItemId, 0, 0);
    const LONG tick =
        InterlockedCompareExchange(&g_mountedDemonJumpCrashTraceTick, 0, 0);
    if (!IsMountedDemonJumpRelatedSkillId(static_cast<int>(runtimeSkillId)) ||
        mountItemId <= 0 ||
        tick <= 0)
    {
        return false;
    }

    const DWORD nowTick = GetTickCount();
    if (nowTick - static_cast<DWORD>(tick) > maxAgeMs)
    {
        return false;
    }

    if (runtimeSkillIdOut)
    {
        *runtimeSkillIdOut = static_cast<int>(runtimeSkillId);
    }
    if (mountItemIdOut)
    {
        *mountItemIdOut = static_cast<int>(mountItemId);
    }
    return true;
}

static void RememberMountedDemonJumpNativeChildSkill(
    int mountItemId,
    int skillId,
    const char *source)
{
    if (mountItemId <= 0 || !IsMountedDemonJumpRuntimeChildSkillId(skillId))
    {
        return;
    }

    InterlockedExchange(&g_recentMountedDemonJumpNativeChildSkillId, skillId);
    InterlockedExchange(&g_recentMountedDemonJumpNativeChildMountItemId, mountItemId);
    InterlockedExchange(
        &g_recentMountedDemonJumpNativeChildTick,
        static_cast<LONG>(GetTickCount()));

    static LONG s_recentMountedDemonJumpNativeChildLogBudget = 48;
    if (InterlockedDecrement(&s_recentMountedDemonJumpNativeChildLogBudget) >= 0)
    {
        WriteLogFmt(
            "[MountDemonJumpChild] remember source=%s mount=%d child=%d",
            source ? source : "unknown",
            mountItemId,
            skillId);
    }
}

static bool TryGetRecentMountedDemonJumpNativeChildSkill(
    int mountItemId,
    int *skillIdOut,
    const char **sourceOut,
    DWORD maxAgeMs)
{
    if (skillIdOut)
    {
        *skillIdOut = 0;
    }
    if (sourceOut)
    {
        *sourceOut = nullptr;
    }

    const LONG recentSkillId =
        InterlockedCompareExchange(&g_recentMountedDemonJumpNativeChildSkillId, 0, 0);
    const LONG recentMountItemId =
        InterlockedCompareExchange(&g_recentMountedDemonJumpNativeChildMountItemId, 0, 0);
    const LONG recentTick =
        InterlockedCompareExchange(&g_recentMountedDemonJumpNativeChildTick, 0, 0);
    if (!IsMountedDemonJumpRuntimeChildSkillId(static_cast<int>(recentSkillId)) ||
        recentMountItemId <= 0 ||
        recentTick <= 0)
    {
        return false;
    }

    if (mountItemId > 0 && recentMountItemId != mountItemId)
    {
        return false;
    }

    const DWORD nowTick = GetTickCount();
    if (nowTick - static_cast<DWORD>(recentTick) > maxAgeMs)
    {
        return false;
    }

    if (skillIdOut)
    {
        *skillIdOut = static_cast<int>(recentSkillId);
    }
    if (sourceOut)
    {
        *sourceOut = "recent-child-cache";
    }
    return true;
}

static bool SendMountedDemonJumpSyntheticSpecialMovePacket(
    int skillId,
    int level,
    DWORD *tickOut)
{
    if (tickOut)
    {
        *tickOut = 0;
    }

    if (skillId <= 0)
    {
        return false;
    }

    typedef void(__thiscall *tOutPacketInitFn)(void *thisPtr, unsigned short opcode);
    typedef void(__thiscall *tOutPacketEncode4Fn)(void *thisPtr, int value);
    typedef void(__thiscall *tOutPacketEncode1Fn)(void *thisPtr, int value);
    typedef void(__thiscall *tNetSendPacketFn)(void *thisPtr, void *packet);
    typedef void(__thiscall *tGameFreeFn)(void *heapPtr, void *allocBase);
    typedef DWORD(__cdecl *tGameTickFn)();

    tOutPacketInitFn outPacketInitFn =
        reinterpret_cast<tOutPacketInitFn>(ADDR_750C20);
    tOutPacketEncode4Fn outPacketEncode4Fn =
        reinterpret_cast<tOutPacketEncode4Fn>(ADDR_417240);
    tOutPacketEncode1Fn outPacketEncode1Fn =
        reinterpret_cast<tOutPacketEncode1Fn>(0x004171F0);
    tNetSendPacketFn netSendPacketFn =
        reinterpret_cast<tNetSendPacketFn>(ADDR_4D63A0);
    tGameFreeFn gameFreeFn =
        reinterpret_cast<tGameFreeFn>(ADDR_4020B0);
    tGameTickFn gameTickFn =
        reinterpret_cast<tGameTickFn>(ADDR_B4C450);
    if (!outPacketInitFn ||
        !outPacketEncode4Fn ||
        !outPacketEncode1Fn ||
        !netSendPacketFn ||
        !gameFreeFn ||
        !gameTickFn)
    {
        return false;
    }

    DWORD netClient = 0;
    __try
    {
        netClient = *reinterpret_cast<DWORD *>(ADDR_NetClient);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        netClient = 0;
    }
    if (!netClient)
    {
        return false;
    }

    alignas(4) BYTE packetStorage[0x20] = {};
    DWORD tick = 0;
    __try
    {
        tick = gameTickFn();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        tick = GetTickCount();
    }

    bool sent = false;
    __try
    {
        outPacketInitFn(packetStorage, 0x93);
        outPacketEncode4Fn(packetStorage, static_cast<int>(tick));
        outPacketEncode4Fn(packetStorage, skillId);
        outPacketEncode1Fn(packetStorage, level);
        netSendPacketFn(reinterpret_cast<void *>(netClient), packetStorage);
        sent = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sent = false;
    }

    DWORD allocPtr = 0;
    __try
    {
        allocPtr = *reinterpret_cast<DWORD *>(packetStorage + 4);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        allocPtr = 0;
    }

    if (allocPtr)
    {
        __try
        {
            gameFreeFn(
                reinterpret_cast<void *>(ADDR_GameHeap),
                reinterpret_cast<void *>(allocPtr - 4));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    if (sent && tickOut)
    {
        *tickOut = tick;
    }
    return sent;
}

static bool ArmMountedDemonJumpPendingSpecialMoveRewrite(
    int mountItemId,
    int expectedSkillId,
    int packetSkillId,
    int packetLevel,
    int runtimeChildSkillId,
    const char *source,
    DWORD *tickOut)
{
    if (tickOut)
    {
        *tickOut = 0;
    }

    if (mountItemId <= 0 ||
        expectedSkillId <= 0 ||
        packetSkillId <= 0 ||
        packetLevel <= 0 ||
        packetLevel > 255)
    {
        return false;
    }

    const DWORD armTick = GetTickCount();
    InterlockedExchange(&g_pendingMountedDemonJumpRewriteMountItemId, mountItemId);
    InterlockedExchange(&g_pendingMountedDemonJumpRewriteExpectedSkillId, expectedSkillId);
    InterlockedExchange(&g_pendingMountedDemonJumpRewritePacketSkillId, packetSkillId);
    InterlockedExchange(&g_pendingMountedDemonJumpRewritePacketLevel, packetLevel);
    InterlockedExchange(
        &g_pendingMountedDemonJumpRewriteRuntimeChildSkillId,
        runtimeChildSkillId);
    InterlockedExchange(
        &g_pendingMountedDemonJumpRewriteArmTick,
        static_cast<LONG>(armTick));
    InterlockedExchange(&g_pendingMountedDemonJumpRewriteActive, 1);

    static LONG s_mountDemonJumpRewriteArmLogBudget = 64;
    if (InterlockedDecrement(&s_mountDemonJumpRewriteArmLogBudget) >= 0)
    {
        WriteLogFmt(
            "[MountDemonJump93Rewrite] arm mount=%d expectSkill=%d packetSkill=%d packetLevel=%d child=%d source=%s",
            mountItemId,
            expectedSkillId,
            packetSkillId,
            packetLevel,
            runtimeChildSkillId,
            source ? source : "none");
    }

    if (tickOut)
    {
        *tickOut = armTick;
    }
    return true;
}

static bool TryRewriteMountedDemonJumpOutgoingPacket(
    void **packetDataSlot,
    int *packetLenSlot,
    uintptr_t callerRetAddr)
{
    if (!packetDataSlot || !packetLenSlot || !*packetDataSlot || *packetLenSlot < 7)
    {
        return false;
    }

    if (InterlockedCompareExchange(&g_pendingMountedDemonJumpRewriteActive, 0, 0) == 0)
    {
        return false;
    }

    const LONG armTick =
        InterlockedCompareExchange(&g_pendingMountedDemonJumpRewriteArmTick, 0, 0);
    const DWORD nowTick = GetTickCount();
    if (armTick <= 0 || nowTick - static_cast<DWORD>(armTick) > 1500)
    {
        InterlockedExchange(&g_pendingMountedDemonJumpRewriteActive, 0);
        static LONG s_mountDemonJumpRewriteExpireLogBudget = 16;
        if (InterlockedDecrement(&s_mountDemonJumpRewriteExpireLogBudget) >= 0)
        {
            WriteLogFmt(
                "[MountDemonJump93Rewrite] expire age=%u caller=0x%08X",
                armTick > 0 ? (nowTick - static_cast<DWORD>(armTick)) : 0,
                (DWORD)callerRetAddr);
        }
        return false;
    }

    BYTE *packet = static_cast<BYTE *>(*packetDataSlot);
    unsigned short opcode = 0;
    int observedSkillId = 0;
    BYTE observedLevel = packet[6];
    memcpy(&opcode, packet, sizeof(opcode));
    memcpy(&observedSkillId, packet + 2, sizeof(observedSkillId));
    if (opcode != 0x0094)
    {
        return false;
    }

    const LONG expectedSkillId =
        InterlockedCompareExchange(&g_pendingMountedDemonJumpRewriteExpectedSkillId, 0, 0);
    if (expectedSkillId <= 0 || observedSkillId != expectedSkillId)
    {
        return false;
    }

    const LONG packetSkillId =
        InterlockedCompareExchange(&g_pendingMountedDemonJumpRewritePacketSkillId, 0, 0);
    LONG packetLevel =
        InterlockedCompareExchange(&g_pendingMountedDemonJumpRewritePacketLevel, 0, 0);
    const LONG mountItemId =
        InterlockedCompareExchange(&g_pendingMountedDemonJumpRewriteMountItemId, 0, 0);
    const LONG runtimeChildSkillId =
        InterlockedCompareExchange(&g_pendingMountedDemonJumpRewriteRuntimeChildSkillId, 0, 0);
    if (packetSkillId <= 0)
    {
        return false;
    }
    if (packetLevel <= 0)
    {
        packetLevel = 1;
    }
    if (packetLevel > 255)
    {
        packetLevel = 255;
    }

    DWORD packetTick = 0;
    __try
    {
        typedef DWORD(__cdecl *tGameTickFn)();
        tGameTickFn gameTickFn = reinterpret_cast<tGameTickFn>(ADDR_B4C450);
        packetTick = gameTickFn ? gameTickFn() : GetTickCount();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        packetTick = GetTickCount();
    }

    const unsigned short newOpcode = 0x0093;
    memcpy(g_pendingMountedDemonJumpRewritePacket, &newOpcode, sizeof(newOpcode));
    memcpy(g_pendingMountedDemonJumpRewritePacket + 2, &packetTick, sizeof(packetTick));
    memcpy(
        g_pendingMountedDemonJumpRewritePacket + 6,
        &packetSkillId,
        sizeof(packetSkillId));
    g_pendingMountedDemonJumpRewritePacket[10] =
        static_cast<BYTE>(packetLevel & 0xFF);

    *packetDataSlot = g_pendingMountedDemonJumpRewritePacket;
    *packetLenSlot = 11;
    InterlockedExchange(&g_pendingMountedDemonJumpRewriteActive, 0);

    static LONG s_mountDemonJumpRewriteApplyLogBudget = 64;
    if (InterlockedDecrement(&s_mountDemonJumpRewriteApplyLogBudget) >= 0)
    {
        WriteLogFmt(
            "[MountDemonJump93Rewrite] apply mount=%d oldSkill=%d oldLevel=%d newSkill=%d newLevel=%d child=%d tick=%u len=%d->%d caller=0x%08X",
            mountItemId,
            observedSkillId,
            observedLevel,
            packetSkillId,
            packetLevel,
            runtimeChildSkillId,
            packetTick,
            7,
            *packetLenSlot,
            (DWORD)callerRetAddr);
    }
    return true;
}

static int __fastcall hkMountedDemonJumpTrace8057F0(
    void *thisPtr,
    void * /*edxUnused*/,
    DWORD arg1,
    DWORD arg2)
{
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    int runtimeSkillId = 0;
    int mountItemId = 0;
    const bool trace =
        callerRet == 0x00B30CE7 &&
        IsMountedDemonJumpCrashTraceFresh(&runtimeSkillId, &mountItemId);
    if (trace)
    {
        WriteLogFmt("[MountDemonJumpTrace] 8057F0 enter caller=0x%08X skill=%d mount=%d this=0x%08X args=[0x%08X,0x%08X]",
                    callerRet,
                    runtimeSkillId,
                    mountItemId,
                    (DWORD)(uintptr_t)thisPtr,
                    arg1,
                    arg2);
    }

    const int result = oMountedDemonJumpTrace8057F0
                           ? oMountedDemonJumpTrace8057F0(thisPtr, arg1, arg2)
                           : 0;
    if (trace)
    {
        WriteLogFmt("[MountDemonJumpTrace] 8057F0 leave result=0x%08X", result);
    }
    return result;
}

static int __cdecl hkMountedDemonJumpTrace550FF0(int skillId)
{
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    int runtimeSkillId = 0;
    int mountItemId = 0;
    const bool trace =
        callerRet == 0x00B30CF1 &&
        IsMountedDemonJumpCrashTraceFresh(&runtimeSkillId, &mountItemId);
    if (trace)
    {
        WriteLogFmt("[MountDemonJumpTrace] 550FF0 enter caller=0x%08X skill=%d mount=%d argSkill=%d",
                    callerRet,
                    runtimeSkillId,
                    mountItemId,
                    skillId);
    }

    const int result = oMountedDemonJumpTrace550FF0
                           ? oMountedDemonJumpTrace550FF0(skillId)
                           : 0;
    if (trace &&
        result == 0 &&
        mountItemId > 0 &&
        IsMountedDemonJumpRuntimeChildSkillId(runtimeSkillId) &&
        HasRecentMountedDemonJumpIntent(mountItemId) &&
        SkillOverlayBridgeCanUseMountedDemonJumpRuntimeSkill(
            mountItemId,
            runtimeSkillId))
    {
        WriteLogFmt(
            "[MountDemonJumpTrace] 550FF0 force allow skill=%d mount=%d argSkill=%d original=%d -> 1",
            runtimeSkillId,
            mountItemId,
            skillId,
            result);
        return 1;
    }
    if (trace)
    {
        WriteLogFmt("[MountDemonJumpTrace] 550FF0 leave result=%d", result);
    }
    return result;
}

static int __fastcall hkMountedDemonJumpTrace829EC0(void *thisPtr, void * /*edxUnused*/)
{
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    int runtimeSkillId = 0;
    int mountItemId = 0;
    const bool trace =
        callerRet == 0x00B30D07 &&
        IsMountedDemonJumpCrashTraceFresh(&runtimeSkillId, &mountItemId);
    if (trace)
    {
        WriteLogFmt("[MountDemonJumpTrace] 829EC0 enter caller=0x%08X skill=%d mount=%d this=0x%08X",
                    callerRet,
                    runtimeSkillId,
                    mountItemId,
                    (DWORD)(uintptr_t)thisPtr);
    }

    const int result = oMountedDemonJumpTrace829EC0
                           ? oMountedDemonJumpTrace829EC0(thisPtr)
                           : 0;
    if (trace &&
        result == 0 &&
        mountItemId > 0 &&
        IsMountedDemonJumpRuntimeChildSkillId(runtimeSkillId) &&
        HasRecentMountedDemonJumpIntent(mountItemId) &&
        SkillOverlayBridgeCanUseMountedDemonJumpRuntimeSkill(
            mountItemId,
            runtimeSkillId))
    {
        WriteLogFmt(
            "[MountDemonJumpTrace] 829EC0 force allow skill=%d mount=%d this=0x%08X original=%d -> 1",
            runtimeSkillId,
            mountItemId,
            (DWORD)(uintptr_t)thisPtr,
            result);
        return 1;
    }
    if (trace)
    {
        WriteLogFmt("[MountDemonJumpTrace] 829EC0 leave result=%d", result);
    }
    return result;
}

static int __fastcall hkMountedDemonJumpTrace829F10(void *thisPtr, void * /*edxUnused*/)
{
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    int runtimeSkillId = 0;
    int mountItemId = 0;
    const bool trace =
        callerRet == 0x00B30D30 &&
        IsMountedDemonJumpCrashTraceFresh(&runtimeSkillId, &mountItemId);
    if (trace)
    {
        WriteLogFmt("[MountDemonJumpTrace] 829F10 enter caller=0x%08X skill=%d mount=%d this=0x%08X",
                    callerRet,
                    runtimeSkillId,
                    mountItemId,
                    (DWORD)(uintptr_t)thisPtr);
    }

    const int result = oMountedDemonJumpTrace829F10
                           ? oMountedDemonJumpTrace829F10(thisPtr)
                           : 0;
    const int expectedMatchValue =
        runtimeSkillId == 5311002 ? 5310008 : runtimeSkillId;
    if (trace &&
        result != expectedMatchValue &&
        mountItemId > 0 &&
        IsMountedDemonJumpRuntimeChildSkillId(runtimeSkillId) &&
        HasRecentMountedDemonJumpIntent(mountItemId) &&
        SkillOverlayBridgeCanUseMountedDemonJumpRuntimeSkill(
            mountItemId,
            runtimeSkillId))
    {
        WriteLogFmt(
            "[MountDemonJumpTrace] 829F10 force match skill=%d mount=%d this=0x%08X original=0x%08X -> 0x%08X",
            runtimeSkillId,
            mountItemId,
            (DWORD)(uintptr_t)thisPtr,
            result,
            expectedMatchValue);
        return expectedMatchValue;
    }
    if (trace)
    {
        WriteLogFmt("[MountDemonJumpTrace] 829F10 leave result=0x%08X", result);
    }
    return result;
}

static int __fastcall hkMountedDemonJumpTrace551170(void *thisPtr, void * /*edxUnused*/)
{
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    int runtimeSkillId = 0;
    int mountItemId = 0;
    const bool trace =
        callerRet == 0x00B30D41 &&
        IsMountedDemonJumpCrashTraceFresh(&runtimeSkillId, &mountItemId);
    if (trace)
    {
        WriteLogFmt("[MountDemonJumpTrace] 551170 enter caller=0x%08X skill=%d mount=%d this=0x%08X",
                    callerRet,
                    runtimeSkillId,
                    mountItemId,
                    (DWORD)(uintptr_t)thisPtr);
    }

    const int result = oMountedDemonJumpTrace551170
                           ? oMountedDemonJumpTrace551170(thisPtr)
                           : 0;
    if (trace &&
        result != 0 &&
        mountItemId > 0 &&
        IsMountedDemonJumpRuntimeChildSkillId(runtimeSkillId) &&
        HasRecentMountedDemonJumpIntent(mountItemId) &&
        SkillOverlayBridgeCanUseMountedDemonJumpRuntimeSkill(
            mountItemId,
            runtimeSkillId))
    {
        WriteLogFmt(
            "[MountDemonJumpTrace] 551170 force continue skill=%d mount=%d this=0x%08X original=%d -> 0",
            runtimeSkillId,
            mountItemId,
            (DWORD)(uintptr_t)thisPtr,
            result);
        return 0;
    }
    if (trace)
    {
        WriteLogFmt("[MountDemonJumpTrace] 551170 leave result=%d", result);
    }
    return result;
}

static int __fastcall hkMountedDemonJumpTraceA01BF0(
    void *thisPtr,
    void * /*edxUnused*/,
    int skillId)
{
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    int runtimeSkillId = 0;
    int mountItemId = 0;
    const bool trace =
        callerRet == 0x00B30D4F &&
        IsMountedDemonJumpCrashTraceFresh(&runtimeSkillId, &mountItemId);
    if (trace)
    {
        WriteLogFmt("[MountDemonJumpTrace] A01BF0 enter caller=0x%08X skill=%d mount=%d this=0x%08X argSkill=%d",
                    callerRet,
                    runtimeSkillId,
                    mountItemId,
                    (DWORD)(uintptr_t)thisPtr,
                    skillId);
    }

    const int result = oMountedDemonJumpTraceA01BF0
                           ? oMountedDemonJumpTraceA01BF0(thisPtr, skillId)
                           : 0;
    if (trace &&
        result != 0 &&
        mountItemId > 0 &&
        IsMountedDemonJumpRuntimeChildSkillId(runtimeSkillId) &&
        HasRecentMountedDemonJumpIntent(mountItemId) &&
        SkillOverlayBridgeCanUseMountedDemonJumpRuntimeSkill(
            mountItemId,
            runtimeSkillId))
    {
        WriteLogFmt(
            "[MountDemonJumpTrace] A01BF0 force continue skill=%d mount=%d this=0x%08X argSkill=%d original=%d -> 0",
            runtimeSkillId,
            mountItemId,
            (DWORD)(uintptr_t)thisPtr,
            skillId,
            result);
        return 0;
    }
    if (trace)
    {
        WriteLogFmt("[MountDemonJumpTrace] A01BF0 leave result=%d", result);
    }
    return result;
}

static int __fastcall hkMountedDemonJumpTrace4C1720(void *thisPtr, void * /*edxUnused*/)
{
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    int runtimeSkillId = 0;
    int mountItemId = 0;
    const bool trace =
        callerRet == 0x00B30D7F &&
        IsMountedDemonJumpCrashTraceFresh(&runtimeSkillId, &mountItemId);
    if (trace)
    {
        WriteLogFmt("[MountDemonJumpTrace] 4C1720 enter caller=0x%08X skill=%d mount=%d this=0x%08X",
                    callerRet,
                    runtimeSkillId,
                    mountItemId,
                    (DWORD)(uintptr_t)thisPtr);
    }

    const int result = oMountedDemonJumpTrace4C1720
                           ? oMountedDemonJumpTrace4C1720(thisPtr)
                           : 0;
    if (trace)
    {
        WriteLogFmt("[MountDemonJumpTrace] 4C1720 leave result=%d", result);
    }
    return result;
}

static int __fastcall hkMountedDemonJumpTrace52BCB0(void *thisPtr, void * /*edxUnused*/)
{
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    int runtimeSkillId = 0;
    int mountItemId = 0;
    const bool trace =
        callerRet == 0x00B30D8C &&
        IsMountedDemonJumpCrashTraceFresh(&runtimeSkillId, &mountItemId);
    if (trace)
    {
        WriteLogFmt("[MountDemonJumpTrace] 52BCB0 enter caller=0x%08X skill=%d mount=%d this=0x%08X",
                    callerRet,
                    runtimeSkillId,
                    mountItemId,
                    (DWORD)(uintptr_t)thisPtr);
    }

    const int result = oMountedDemonJumpTrace52BCB0
                           ? oMountedDemonJumpTrace52BCB0(thisPtr)
                           : 0;
    if (trace)
    {
        WriteLogFmt("[MountDemonJumpTrace] 52BCB0 leave result=%d", result);
    }
    return result;
}

static int __fastcall hkMountedDemonJumpTrace805850(
    void *thisPtr,
    void * /*edxUnused*/,
    DWORD arg1,
    DWORD arg2,
    DWORD arg3,
    DWORD arg4,
    DWORD arg5,
    DWORD arg6)
{
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
    int runtimeSkillId = 0;
    int mountItemId = 0;
    const bool trace =
        callerRet == 0x00B30DD8 &&
        IsMountedDemonJumpCrashTraceFresh(&runtimeSkillId, &mountItemId);
    if (trace)
    {
        WriteLogFmt("[MountDemonJumpTrace] 805850 enter caller=0x%08X skill=%d mount=%d this=0x%08X args=[0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X]",
                    callerRet,
                    runtimeSkillId,
                    mountItemId,
                    (DWORD)(uintptr_t)thisPtr,
                    arg1,
                    arg2,
                    arg3,
                    arg4,
                    arg5,
                    arg6);
    }

    const int result = oMountedDemonJumpTrace805850
                           ? oMountedDemonJumpTrace805850(
                                 thisPtr,
                                 arg1,
                                 arg2,
                                 arg3,
                                 arg4,
                                 arg5,
                                 arg6)
                           : 0;
    if (trace)
    {
        WriteLogFmt("[MountDemonJumpTrace] 805850 leave result=%d", result);
    }
    return result;
}

static bool IsAddressInCurrentModule(DWORD address)
{
    if (!address)
    {
        return false;
    }

    static HMODULE s_module = nullptr;
    static DWORD s_sizeOfImage = 0;
    if (!s_module)
    {
        s_module = GetModuleHandleW(L"SS.dll");
        if (s_module)
        {
            const uintptr_t base = reinterpret_cast<uintptr_t>(s_module);
            if (!SafeIsBadReadPtr(reinterpret_cast<void *>(base), sizeof(IMAGE_DOS_HEADER)))
            {
                const IMAGE_DOS_HEADER *dosHeader =
                    reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
                const uintptr_t ntHeaderAddr = base + static_cast<uintptr_t>(dosHeader->e_lfanew);
                if (!SafeIsBadReadPtr(reinterpret_cast<void *>(ntHeaderAddr), sizeof(IMAGE_NT_HEADERS32)))
                {
                    const IMAGE_NT_HEADERS32 *ntHeader =
                        reinterpret_cast<const IMAGE_NT_HEADERS32 *>(ntHeaderAddr);
                    s_sizeOfImage = ntHeader->OptionalHeader.SizeOfImage;
                }
            }
        }
    }

    if (!s_module || s_sizeOfImage == 0)
    {
        return false;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(s_module);
    const uintptr_t addr = static_cast<uintptr_t>(address);
    return addr >= base && addr < (base + static_cast<uintptr_t>(s_sizeOfImage));
}

static bool TryPreseedMountedDemonJumpLevelCache(
    const char *hookTag,
    int skillId,
    int lookupSkillId,
    void *cachePtr)
{
    if (!cachePtr ||
        SafeIsBadWritePtr(cachePtr, sizeof(uintptr_t)) ||
        (!IsMountedDemonJumpRelatedSkillId(skillId) &&
         !IsMountedDemonJumpRelatedSkillId(lookupSkillId)))
    {
        return false;
    }

    uintptr_t existingEntry = 0;
    __try
    {
        existingEntry = *reinterpret_cast<uintptr_t *>(cachePtr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        existingEntry = 0;
    }

    if (existingEntry)
    {
        return false;
    }

    int mountItemId = 0;
    if (!TryResolveMountedDemonJumpMountItemIdWithFallback(
            nullptr,
            &mountItemId,
            nullptr,
            1200) &&
        !TryGetRecentMountedDemonJumpIntentItemId(&mountItemId, 1200))
    {
        return false;
    }

    if (mountItemId <= 0 ||
        ResolveMountedRuntimeSkillIdForKind(
            MountedRuntimeSkillKind_DemonJump,
            mountItemId) != 30010110)
    {
        return false;
    }

    const uintptr_t bridgeEntry =
        SkillOverlayBridgeLookupSkillEntryPointer(30010110);
    if (!bridgeEntry ||
        SafeIsBadReadPtr(reinterpret_cast<void *>(bridgeEntry), 0x40))
    {
        return false;
    }

    __try
    {
        *reinterpret_cast<uintptr_t *>(cachePtr) = bridgeEntry;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    static LONG s_mountedDemonJumpCachePreseedLogBudget = 96;
    const LONG budgetAfterDecrement =
        InterlockedDecrement(&s_mountedDemonJumpCachePreseedLogBudget);
    if (budgetAfterDecrement >= 0)
    {
        WriteLogFmt(
            "[MountDemonJumpCache] %s stage=preseed query=%d lookup=%d mount=%d cache=0x%08X entry=0x%08X",
            hookTag ? hookTag : "skill-level",
            skillId,
            lookupSkillId,
            mountItemId,
            (DWORD)(uintptr_t)cachePtr,
            static_cast<DWORD>(bridgeEntry));
    }
    return true;
}

static bool TryBackfillMountedDemonJumpLevelCache(
    const char *hookTag,
    int skillId,
    int lookupSkillId,
    void *cachePtr,
    int mountItemId)
{
    if (!cachePtr ||
        mountItemId <= 0 ||
        SafeIsBadWritePtr(cachePtr, sizeof(uintptr_t)) ||
        (!IsMountedDemonJumpRelatedSkillId(skillId) &&
         !IsMountedDemonJumpRelatedSkillId(lookupSkillId)))
    {
        return false;
    }

    if (ResolveMountedRuntimeSkillIdForKind(
            MountedRuntimeSkillKind_DemonJump,
            mountItemId) != 30010110)
    {
        return false;
    }

    uintptr_t existingEntry = 0;
    __try
    {
        existingEntry = *reinterpret_cast<uintptr_t *>(cachePtr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        existingEntry = 0;
    }

    if (existingEntry)
    {
        return false;
    }

    const uintptr_t bridgeEntry =
        SkillOverlayBridgeLookupSkillEntryPointer(30010110);
    if (!bridgeEntry ||
        SafeIsBadReadPtr(reinterpret_cast<void *>(bridgeEntry), 0x40))
    {
        return false;
    }

    __try
    {
        *reinterpret_cast<uintptr_t *>(cachePtr) = bridgeEntry;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    static LONG s_mountedDemonJumpCachePreseedLogBudget = 96;
    const LONG budgetAfterDecrement =
        InterlockedDecrement(&s_mountedDemonJumpCachePreseedLogBudget);
    if (budgetAfterDecrement >= 0)
    {
        WriteLogFmt(
            "[MountDemonJumpCache] %s stage=backfill query=%d lookup=%d mount=%d cache=0x%08X entry=0x%08X",
            hookTag ? hookTag : "skill-level",
            skillId,
            lookupSkillId,
            mountItemId,
            (DWORD)(uintptr_t)cachePtr,
            static_cast<DWORD>(bridgeEntry));
    }
    return true;
}

static void ObserveMountedDemonJumpLevelQueryCaller(
    const char *hookTag,
    DWORD callerRet,
    int skillId,
    int lookupSkillId,
    int flags,
    int rawResult,
    int finalResult)
{
    if (!IsMountedDemonJumpRelatedSkillId(skillId) &&
        !IsMountedDemonJumpRelatedSkillId(lookupSkillId))
    {
        return;
    }

    if (IsAddressInCurrentModule(callerRet))
    {
        return;
    }

    static DWORD s_lastLogTick = 0;
    static DWORD s_lastCallerRet = 0;
    static int s_lastSkillId = 0;
    static int s_lastLookupSkillId = 0;
    static int s_lastFlags = 0;
    const DWORD nowTick = GetTickCount();
    if (callerRet == s_lastCallerRet &&
        skillId == s_lastSkillId &&
        lookupSkillId == s_lastLookupSkillId &&
        flags == s_lastFlags &&
        nowTick - s_lastLogTick < 1000)
    {
        return;
    }

    s_lastLogTick = nowTick;
    s_lastCallerRet = callerRet;
    s_lastSkillId = skillId;
    s_lastLookupSkillId = lookupSkillId;
    s_lastFlags = flags;
    WriteLogFmt("[MountDemonJump] %s caller=0x%08X query=%d lookup=%d flags=%d raw=%d final=%d",
                hookTag ? hookTag : "skill-level",
                callerRet,
                skillId,
                lookupSkillId,
                flags,
                rawResult,
                finalResult);
}

static BOOL ResolveMountedRuntimeSkillNativeReleaseAllowBySkill(
    MountedRuntimeSkillKind kind,
    int skillId,
    int *resolvedMountItemIdOut)
{
    if (!kEnableMountedDoubleJumpRuntimeHooks)
    {
        return FALSE;
    }

    int mountItemId = 0;
    const bool resolvedRecentRuntimeSkill =
        kind == MountedRuntimeSkillKind_DemonJump
            ? TryResolveRecentMountedDemonJumpNativeReleaseRuntimeSkill(
                  skillId,
                  &mountItemId,
                  nullptr)
            : TryResolveRecentMountedDoubleJumpNativeReleaseRuntimeSkill(
                  skillId,
                  &mountItemId,
                  nullptr);
    if (!resolvedRecentRuntimeSkill)
    {
        return FALSE;
    }

    int currentMountItemId = 0;
    if (TryResolveCurrentUserMountItemIdWithFallback(&currentMountItemId, nullptr) &&
        currentMountItemId > 0 &&
        currentMountItemId != mountItemId)
    {
        return FALSE;
    }

    if (!CanUseMountedRuntimeSkillRuntimeForKind(kind, mountItemId, skillId))
    {
        return FALSE;
    }

    if (resolvedMountItemIdOut)
    {
        *resolvedMountItemIdOut = mountItemId;
    }
    return TRUE;
}

static BOOL ResolveMountedRuntimeSkillNativeReleaseAllowByMountContext(
    MountedRuntimeSkillKind kind,
    void *mountContext,
    int *resolvedMountItemIdOut)
{
    if (!kEnableMountedDoubleJumpRuntimeHooks)
    {
        return FALSE;
    }

    const LONG recentSkillId =
        InterlockedCompareExchange(&g_recentMountedRuntimeSkillNativeReleaseSkillId[kind], 0, 0);
    if (recentSkillId <= 0)
    {
        if (kind != MountedRuntimeSkillKind_DemonJump)
        {
            return FALSE;
        }

        int contextMountItemId = 0;
        if (!mountContext ||
            !TryResolveMountItemIdFromContextPointer(mountContext, &contextMountItemId) ||
            contextMountItemId <= 0 ||
            !HasRecentMountedDemonJumpIntent(contextMountItemId, 1200))
        {
            return FALSE;
        }

        if (!CanUseMountedRuntimeSkillRuntimeForKind(
                kind,
                contextMountItemId,
                ResolveMountedRuntimeSkillIdForKind(kind, contextMountItemId)))
        {
            return FALSE;
        }

        if (resolvedMountItemIdOut)
        {
            *resolvedMountItemIdOut = contextMountItemId;
        }
        return TRUE;
    }

    int mountItemId = 0;
    const bool resolvedRecentMount =
        kind == MountedRuntimeSkillKind_DemonJump
            ? TryResolveRecentMountedDemonJumpNativeRelease(
                  static_cast<int>(recentSkillId),
                  &mountItemId)
            : TryResolveRecentMountedDoubleJumpNativeRelease(
                  static_cast<int>(recentSkillId),
                  &mountItemId);
    if (!resolvedRecentMount)
    {
        return FALSE;
    }

    int contextMountItemId = 0;
    if (!mountContext ||
        !TryResolveMountItemIdFromContextPointer(mountContext, &contextMountItemId) ||
        contextMountItemId <= 0 ||
        contextMountItemId != mountItemId)
    {
        return FALSE;
    }

    if (!CanUseMountedRuntimeSkillRuntimeForKind(
            kind,
            mountItemId,
            static_cast<int>(recentSkillId)))
    {
        return FALSE;
    }

    if (resolvedMountItemIdOut)
    {
        *resolvedMountItemIdOut = mountItemId;
    }
    return TRUE;
}

static BOOL ResolveMountedConfiguredNativeReleaseAllowBySkill(
    int skillId,
    MountedRuntimeSkillKind *kindOut,
    int *resolvedMountItemIdOut)
{
    if (kindOut)
    {
        *kindOut = MountedRuntimeSkillKind_DoubleJump;
    }

    if (ResolveMountedRuntimeSkillNativeReleaseAllowBySkill(
            MountedRuntimeSkillKind_DoubleJump,
            skillId,
            resolvedMountItemIdOut))
    {
        if (kindOut)
        {
            *kindOut = MountedRuntimeSkillKind_DoubleJump;
        }
        return TRUE;
    }

    if (ResolveMountedRuntimeSkillNativeReleaseAllowBySkill(
            MountedRuntimeSkillKind_DemonJump,
            skillId,
            resolvedMountItemIdOut))
    {
        if (kindOut)
        {
            *kindOut = MountedRuntimeSkillKind_DemonJump;
        }
        return TRUE;
    }

    return FALSE;
}

static BOOL ResolveMountedConfiguredNativeReleaseAllowByMountContext(
    void *mountContext,
    MountedRuntimeSkillKind *kindOut,
    int *resolvedMountItemIdOut)
{
    if (kindOut)
    {
        *kindOut = MountedRuntimeSkillKind_DoubleJump;
    }

    if (ResolveMountedRuntimeSkillNativeReleaseAllowByMountContext(
            MountedRuntimeSkillKind_DoubleJump,
            mountContext,
            resolvedMountItemIdOut))
    {
        if (kindOut)
        {
            *kindOut = MountedRuntimeSkillKind_DoubleJump;
        }
        return TRUE;
    }

    if (ResolveMountedRuntimeSkillNativeReleaseAllowByMountContext(
            MountedRuntimeSkillKind_DemonJump,
            mountContext,
            resolvedMountItemIdOut))
    {
        if (kindOut)
        {
            *kindOut = MountedRuntimeSkillKind_DemonJump;
        }
        return TRUE;
    }

    return FALSE;
}

static BOOL __cdecl hkMountedSkillWhitelist7CF270(int skillId)
{
    BOOL result = oMountedSkillWhitelist7CF270
                      ? oMountedSkillWhitelist7CF270(skillId)
                      : FALSE;
    const bool shouldObserve =
        IsMountedDemonJumpRelatedSkillId(skillId) ||
        skillId == 80001096;
    if (shouldObserve)
    {
        static LONG s_mountedSkillWhitelistObserveLogBudget = 96;
        const LONG budgetAfterDecrement =
            InterlockedDecrement(&s_mountedSkillWhitelistObserveLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            int currentMountItemId = 0;
            const bool hasCurrentMount =
                TryResolveCurrentUserMountItemIdWithFallback(
                    &currentMountItemId,
                    nullptr) &&
                currentMountItemId > 0;
            WriteLogFmt(
                "[MountWhitelist] 7CF270 observe skill=%d native=%d mount=%d hasDoubleIntent=%d hasDemonIntent=%d routeDouble=%d routeDemon=%d",
                skillId,
                result ? 1 : 0,
                hasCurrentMount ? currentMountItemId : 0,
                hasCurrentMount &&
                        HasRecentMountedDoubleJumpIntent(currentMountItemId, 1200)
                    ? 1
                    : 0,
                hasCurrentMount &&
                        HasRecentMountedDemonJumpIntent(currentMountItemId, 1200)
                    ? 1
                    : 0,
                hasCurrentMount &&
                        SkillOverlayBridgeHasRecentMountedDoubleJumpRouteArm(
                            currentMountItemId,
                            1200)
                    ? 1
                    : 0,
                hasCurrentMount &&
                        SkillOverlayBridgeHasRecentMountedDemonJumpRouteArm(
                            currentMountItemId,
                            1200)
                    ? 1
                    : 0);
        }
    }
    if (result)
    {
        return result;
    }

    int resolvedMountItemId = 0;
    MountedRuntimeSkillKind kind = MountedRuntimeSkillKind_DoubleJump;
    if (!ResolveMountedConfiguredNativeReleaseAllowBySkill(
            skillId,
            &kind,
            &resolvedMountItemId))
    {
        return result;
    }

    static LONG s_mountedSkillWhitelistForceAllowLogBudget = 24;
    const LONG budgetAfterDecrement =
        InterlockedDecrement(&s_mountedSkillWhitelistForceAllowLogBudget);
    if (budgetAfterDecrement >= 0)
    {
        WriteLogFmt("[%s] 7CF270 native release force allow skill=%d mount=%d",
                    GetMountedRuntimeSkillLogTag(kind),
                    skillId,
                    resolvedMountItemId);
    }
    return TRUE;
}

static int __fastcall hkMountedSkillContextGateA9BF40(
    void *thisPtr,
    void * /*edxUnused*/)
{
    int result = oMountedSkillContextGateA9BF40
                     ? oMountedSkillContextGateA9BF40(thisPtr)
                     : 0;
    {
        int debugMountItemId = 0;
        const bool shouldObserve =
            TryResolveMountItemIdFromContextPointer(thisPtr, &debugMountItemId) &&
            debugMountItemId > 0 &&
            (SkillOverlayBridgeResolveMountedDemonJumpSkillId(debugMountItemId) > 0 ||
             SkillOverlayBridgeResolveMountedDoubleJumpSkillId(debugMountItemId) > 0);
        if (shouldObserve)
        {
            static LONG s_mountedSkillContextGateObserveLogBudget = 96;
            const LONG budgetAfterDecrement =
                InterlockedDecrement(&s_mountedSkillContextGateObserveLogBudget);
            if (budgetAfterDecrement >= 0)
            {
                WriteLogFmt(
                    "[MountContextGate] A9BF40 observe native=%d mount=%d doubleIntent=%d demonIntent=%d doubleRecent=%d demonRecent=%d",
                    result,
                    debugMountItemId,
                    HasRecentMountedDoubleJumpIntent(debugMountItemId, 1200) ? 1 : 0,
                    HasRecentMountedDemonJumpIntent(debugMountItemId, 1200) ? 1 : 0,
                    SkillOverlayBridgeResolveMountedDoubleJumpSkillId(debugMountItemId),
                    SkillOverlayBridgeResolveMountedDemonJumpSkillId(debugMountItemId));
            }
        }
    }
    if (result > 0)
    {
        return result;
    }

    int resolvedMountItemId = 0;
    MountedRuntimeSkillKind kind = MountedRuntimeSkillKind_DoubleJump;
    if (!ResolveMountedConfiguredNativeReleaseAllowByMountContext(
            thisPtr,
            &kind,
            &resolvedMountItemId))
    {
        return result;
    }

    static LONG s_mountedSkillContextGateForceAllowLogBudget = 24;
    const LONG budgetAfterDecrement =
        InterlockedDecrement(&s_mountedSkillContextGateForceAllowLogBudget);
    if (budgetAfterDecrement >= 0)
    {
        WriteLogFmt("[%s] A9BF40 native release force allow mount=%d",
                    GetMountedRuntimeSkillLogTag(kind),
                    resolvedMountItemId);
    }
    return 1;
}

static int __fastcall hkMountedSkillContextGateCallsiteB3009F(
    void *mountContext,
    void * /*edxUnused*/)
{
    int result = 0;
    const DWORD originalTarget = g_MountedSkillContextGateCallsiteOriginalTarget
                                     ? g_MountedSkillContextGateCallsiteOriginalTarget
                                     : ADDR_A9BF40;
    if (originalTarget)
    {
        result =
            ((tMountedSkillContextGateFn)(uintptr_t)originalTarget)(mountContext);
    }
    int resolvedMountItemId = 0;
    MountedRuntimeSkillKind kind = MountedRuntimeSkillKind_DoubleJump;
    const bool hasMountedRuntimeSkillContext =
        ResolveMountedConfiguredNativeReleaseAllowByMountContext(
            mountContext,
            &kind,
            &resolvedMountItemId) != FALSE;
    if (result > 0)
    {
        static LONG s_mountedSkillContextGateCallsiteNativeAllowLogBudget = 24;
        const LONG budgetAfterDecrement =
            InterlockedDecrement(&s_mountedSkillContextGateCallsiteNativeAllowLogBudget);
        if (hasMountedRuntimeSkillContext && budgetAfterDecrement >= 0)
        {
            const LONG recentSkillId =
                InterlockedCompareExchange(&g_recentMountedRuntimeSkillNativeReleaseSkillId[kind], 0, 0);
            WriteLogFmt("[%s] B3009F callsite native allow mount=%d skill=%d result=%d",
                        GetMountedRuntimeSkillLogTag(kind),
                        resolvedMountItemId,
                        static_cast<int>(recentSkillId),
                        result);
        }
        return result;
    }

    if (!hasMountedRuntimeSkillContext)
    {
        return result;
    }

    static LONG s_mountedSkillContextGateCallsiteForceAllowLogBudget = 24;
    const LONG budgetAfterDecrement =
        InterlockedDecrement(&s_mountedSkillContextGateCallsiteForceAllowLogBudget);
    if (budgetAfterDecrement >= 0)
    {
        const LONG recentSkillId =
            InterlockedCompareExchange(&g_recentMountedRuntimeSkillNativeReleaseSkillId[kind], 0, 0);
        WriteLogFmt("[%s] B3009F callsite force allow mount=%d skill=%d",
                    GetMountedRuntimeSkillLogTag(kind),
                    resolvedMountItemId,
                    static_cast<int>(recentSkillId));
    }

    return 1;
}

static DWORD __cdecl hkMountedUnknownSkillReleaseBranchTargetB300AC(int skillId)
{
    int resolvedMountItemId = 0;
    int recentNativeSkillId = 0;
    MountedRuntimeSkillKind kind = MountedRuntimeSkillKind_DoubleJump;
    if ((TryResolveRecentMountedDoubleJumpNativeReleaseRuntimeSkill(
             skillId,
             &resolvedMountItemId,
             &recentNativeSkillId) &&
        (kind = MountedRuntimeSkillKind_DoubleJump, true)) ||
        (TryResolveRecentMountedDemonJumpNativeReleaseRuntimeSkill(
             skillId,
             &resolvedMountItemId,
             &recentNativeSkillId) &&
         (kind = MountedRuntimeSkillKind_DemonJump, true)))
    {
        const int configuredSkillId =
            resolvedMountItemId > 0
                ? ResolveMountedRuntimeSkillIdForKind(kind, resolvedMountItemId)
                : 0;
        if (kind == MountedRuntimeSkillKind_DemonJump)
        {
            int demonContextSkillId = 0;
            int demonContextRootSkillId = 0;
            PrimeMountedDemonJumpContextIfNeeded(
                resolvedMountItemId,
                "B300AC",
                &demonContextSkillId);
            const bool hasMountedDemonContext =
                HasMountedDemonJumpContextPrimedForMount(
                    resolvedMountItemId,
                    &demonContextSkillId,
                    &demonContextRootSkillId);
            const bool isDemonRootSkill =
                configuredSkillId > 0 && skillId == configuredSkillId;
            const bool isDemonProxySkill =
                IsMountedDemonJumpRuntimeProxySkillId(skillId);
            const bool isDemonChildRuntimeSkill =
                !isDemonRootSkill &&
                (skillId == 30010183 ||
                 skillId == 30010184 ||
                 skillId == 30010186);
            // 30010110 root cannot survive the full-release B300E3 path because
            // native 805850/B28A00 rejects it before the demon child skills fan
            // out. The real child skills 30010183/84/86 are different: they are
            // on B28A00's native whitelist, so they must go back through the
            // full-release branch instead of the generic B30240/B26760 packet.
            if (isDemonRootSkill && !isDemonProxySkill)
            {
                static LONG s_mountedUnknownSkillReleaseBranchKeepRootNativeLogBudget = 24;
                const LONG budgetAfterDecrement =
                    InterlockedDecrement(
                        &s_mountedUnknownSkillReleaseBranchKeepRootNativeLogBudget);
                if (budgetAfterDecrement >= 0)
                {
                    WriteLogFmt(
                        "[MountDemonJump] B300AC keep root native skill=%d recent=%d configured=%d mount=%d root=%d current=%d context=%d currentProxy=%d target=0x%08X",
                        skillId,
                        recentNativeSkillId,
                        configuredSkillId,
                        resolvedMountItemId,
                        demonContextRootSkillId,
                        demonContextSkillId,
                        hasMountedDemonContext ? 1 : 0,
                        isDemonProxySkill ? 1 : 0,
                        g_MountedUnknownSkillReleaseBranchOriginalTarget
                            ? g_MountedUnknownSkillReleaseBranchOriginalTarget
                            : ADDR_B30240);
                }
                return g_MountedUnknownSkillReleaseBranchOriginalTarget
                           ? g_MountedUnknownSkillReleaseBranchOriginalTarget
                           : ADDR_B30240;
            }

            if (isDemonChildRuntimeSkill)
            {
                RememberMountedDemonJumpNativeChildSkill(
                    resolvedMountItemId,
                    skillId,
                    "B300AC");
            }
            ArmMountedDemonJumpCrashTrace(skillId, resolvedMountItemId);
            static LONG s_mountedUnknownSkillReleaseBranchDemonRerouteLogBudget = 24;
            const LONG budgetAfterDecrement =
                InterlockedDecrement(
                    &s_mountedUnknownSkillReleaseBranchDemonRerouteLogBudget);
            if (budgetAfterDecrement >= 0)
            {
                WriteLogFmt(
                    "[MountDemonJump] B300AC reroute demon skill=%d recent=%d configured=%d mount=%d root=%d current=%d context=%d child=%d proxy=%d -> full-release(0x%08X)",
                    skillId,
                    recentNativeSkillId,
                    configuredSkillId,
                    resolvedMountItemId,
                    demonContextRootSkillId,
                    demonContextSkillId,
                    hasMountedDemonContext ? 1 : 0,
                    isDemonChildRuntimeSkill ? 1 : 0,
                    isDemonProxySkill ? 1 : 0,
                    ADDR_B300E3);
            }
            return ADDR_B300E3;
        }

        static LONG s_mountedUnknownSkillReleaseBranchRerouteLogBudget = 24;
        const LONG budgetAfterDecrement =
            InterlockedDecrement(&s_mountedUnknownSkillReleaseBranchRerouteLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[%s] B300AC reroute skill=%d recent=%d configured=%d mount=%d keep-runtime=%d -> full-release(0x%08X)",
                        GetMountedRuntimeSkillLogTag(kind),
                        skillId,
                        recentNativeSkillId,
                        configuredSkillId,
                        resolvedMountItemId,
                        skillId,
                        ADDR_B300E3);
        }
        return ADDR_B300E3;
    }

    return g_MountedUnknownSkillReleaseBranchOriginalTarget
               ? g_MountedUnknownSkillReleaseBranchOriginalTarget
               : ADDR_B30240;
}

__declspec(naked) static void hkMountedUnknownSkillReleaseBranchB300AC()
{
    __asm
    {
        push esi
        call hkMountedUnknownSkillReleaseBranchTargetB300AC
        add esp, 4
        jmp eax
    }
}

static BOOL __cdecl hkSkillNativeIdGate7CE790(int skillId)
{
    const int mappedSkillId = SkillOverlayBridgeResolveNativeGateSkillId(skillId);
    BOOL result = oSkillNativeIdGate7CE790
                      ? oSkillNativeIdGate7CE790(mappedSkillId)
                      : FALSE;
    if (!result && ResolveForcedNativeSkillGateAllow(skillId, mappedSkillId))
    {
        result = TRUE;
        static LONG s_skillGate7CE790ForceAllowLogBudget = 24;
        const LONG budgetAfterDecrement = InterlockedDecrement(&s_skillGate7CE790ForceAllowLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[SkillGate] 7CE790 force allow skill=%d mapped=%d",
                        skillId, mappedSkillId);
        }
    }
    if (!result)
    {
        int resolvedMountItemId = 0;
        MountedRuntimeSkillKind kind = MountedRuntimeSkillKind_DoubleJump;
        if (ResolveMountedConfiguredSkillGateAllow(
                skillId,
                mappedSkillId,
                &kind,
                &resolvedMountItemId))
        {
            result = TRUE;
            static LONG s_skillGate7CE790MountedDoubleJumpForceAllowLogBudget = 24;
            const LONG budgetAfterDecrement =
                InterlockedDecrement(&s_skillGate7CE790MountedDoubleJumpForceAllowLogBudget);
            if (budgetAfterDecrement >= 0)
            {
                WriteLogFmt("[%s] 7CE790 force allow skill=%d mapped=%d mount=%d",
                            GetMountedRuntimeSkillLogTag(kind),
                            skillId,
                            mappedSkillId,
                            resolvedMountItemId);
            }
        }
    }
    if (!result)
    {
        int resolvedMountItemId = 0;
        bool fromUserLocal = false;
        if (ResolveRecentMountSoaringSkillGateAllow(
                skillId,
                mappedSkillId,
                &resolvedMountItemId,
                &fromUserLocal))
        {
            result = TRUE;
            static LONG s_skillGate7CE790RecentMountForceAllowLogBudget = 24;
            const LONG budgetAfterDecrement =
                InterlockedDecrement(&s_skillGate7CE790RecentMountForceAllowLogBudget);
            if (budgetAfterDecrement >= 0)
            {
                WriteLogFmt("[SkillGate] 7CE790 recent mount force allow skill=%d mapped=%d mount=%d source=%s",
                            skillId,
                            mappedSkillId,
                            resolvedMountItemId,
                            fromUserLocal ? "userlocal" : "recent");
            }
        }
    }
    if (mappedSkillId != skillId)
    {
        WriteLogFmt("[SkillGate] 7CE790 map custom=%d donor=%d result=%d",
                    skillId, mappedSkillId, result ? 1 : 0);
    }
    if (skillId == 80001089 || mappedSkillId == 80001089)
    {
        static LONG s_skillGate7CE790FinalLogBudget = 48;
        const LONG budgetAfterDecrement = InterlockedDecrement(&s_skillGate7CE790FinalLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[SkillGate] 7CE790 final skill=%d mapped=%d result=%d",
                        skillId, mappedSkillId, result ? 1 : 0);
        }
    }
    return result;
}

static int __fastcall hkNativeGlyphLookup(void *thisPtr, void * /*edxUnused*/, unsigned int codepoint, RECT *outRectOrNull)
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
    BOOL result = oSkillNativeIdGate7D0000
                      ? oSkillNativeIdGate7D0000(mappedSkillId)
                      : FALSE;
    if (!result && ResolveForcedNativeSkillGateAllow(skillId, mappedSkillId))
    {
        result = TRUE;
        static LONG s_skillGate7D0000ForceAllowLogBudget = 24;
        const LONG budgetAfterDecrement = InterlockedDecrement(&s_skillGate7D0000ForceAllowLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[SkillGate] 7D0000 force allow skill=%d mapped=%d",
                        skillId, mappedSkillId);
        }
    }
    if (!result)
    {
        int resolvedMountItemId = 0;
        MountedRuntimeSkillKind kind = MountedRuntimeSkillKind_DoubleJump;
        if (ResolveMountedConfiguredSkillGateAllow(
                skillId,
                mappedSkillId,
                &kind,
                &resolvedMountItemId))
        {
            result = TRUE;
            static LONG s_skillGate7D0000MountedDoubleJumpForceAllowLogBudget = 24;
            const LONG budgetAfterDecrement =
                InterlockedDecrement(&s_skillGate7D0000MountedDoubleJumpForceAllowLogBudget);
            if (budgetAfterDecrement >= 0)
            {
                WriteLogFmt("[%s] 7D0000 force allow skill=%d mapped=%d mount=%d",
                            GetMountedRuntimeSkillLogTag(kind),
                            skillId,
                            mappedSkillId,
                            resolvedMountItemId);
            }
        }
    }
    if (!result)
    {
        int resolvedMountItemId = 0;
        bool fromUserLocal = false;
        if (ResolveRecentMountSoaringSkillGateAllow(
                skillId,
                mappedSkillId,
                &resolvedMountItemId,
                &fromUserLocal))
        {
            result = TRUE;
            static LONG s_skillGate7D0000RecentMountForceAllowLogBudget = 24;
            const LONG budgetAfterDecrement =
                InterlockedDecrement(&s_skillGate7D0000RecentMountForceAllowLogBudget);
            if (budgetAfterDecrement >= 0)
            {
                WriteLogFmt("[SkillGate] 7D0000 recent mount force allow skill=%d mapped=%d mount=%d source=%s",
                            skillId,
                            mappedSkillId,
                            resolvedMountItemId,
                            fromUserLocal ? "userlocal" : "recent");
            }
        }
    }
    if (mappedSkillId != skillId)
    {
        WriteLogFmt("[SkillGate] 7D0000 map custom=%d donor=%d result=%d",
                    skillId, mappedSkillId, result ? 1 : 0);
    }
    if (skillId == 80001089 || mappedSkillId == 80001089)
    {
        static LONG s_skillGate7D0000FinalLogBudget = 48;
        const LONG budgetAfterDecrement = InterlockedDecrement(&s_skillGate7D0000FinalLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[SkillGate] 7D0000 final skill=%d mapped=%d result=%d",
                        skillId, mappedSkillId, result ? 1 : 0);
        }
    }
    return result;
}

static int __fastcall hkSkillLevelBase(void *thisPtr, void * /*edxUnused*/, DWORD playerObj, int skillId, void *cachePtr)
{
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
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
    TryPreseedMountedDemonJumpLevelCache(
        "7DA7D0",
        skillId,
        lookupSkillId,
        cachePtr);

    int result = 0;
    int rawResult = 0;
    if (oSkillLevelBase)
    {
        __try
        {
            result = oSkillLevelBase(thisPtr, playerObj, lookupSkillId, cachePtr);
            rawResult = result;
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
    if (result <= 0)
    {
        int resolvedMountItemId = 0;
        MountedRuntimeSkillKind kind = MountedRuntimeSkillKind_DoubleJump;
        if (ResolveMountedConfiguredSkillGateAllow(
                skillId,
                lookupSkillId,
                &kind,
                &resolvedMountItemId))
        {
            result = 1;
            if (kind == MountedRuntimeSkillKind_DemonJump)
            {
                TryBackfillMountedDemonJumpLevelCache(
                    "7DA7D0",
                    skillId,
                    lookupSkillId,
                    cachePtr,
                    resolvedMountItemId);
            }
            static LONG s_skillLevelBaseMountedDoubleJumpFallbackLogBudget = 24;
            const LONG budgetAfterDecrement =
                InterlockedDecrement(&s_skillLevelBaseMountedDoubleJumpFallbackLogBudget);
            if (budgetAfterDecrement >= 0)
            {
                WriteLogFmt("[%s] 7DA7D0 fallback query=%d lookup=%d mount=%d -> result=1",
                            GetMountedRuntimeSkillLogTag(kind),
                            skillId,
                            lookupSkillId,
                            resolvedMountItemId);
            }
        }
    }
    if (result <= 0 && (skillId == 80001089 || lookupSkillId == 80001089))
    {
        int resolvedMountItemId = 0;
        bool fromUserLocal = false;
        if (TryResolveExtendedMountContextForSoaring(&resolvedMountItemId, &fromUserLocal))
        {
            result = 1;
            static LONG s_skillLevelBaseRecentMountFallbackLogBudget = 24;
            const LONG budgetAfterDecrement = InterlockedDecrement(&s_skillLevelBaseRecentMountFallbackLogBudget);
            if (budgetAfterDecrement >= 0)
            {
                WriteLogFmt("[SkillLevelHook] 7DA7D0 mount fallback query=%d lookup=%d mount=%d source=%s -> result=1",
                            skillId,
                            lookupSkillId,
                            resolvedMountItemId,
                            fromUserLocal ? "userlocal" : "recent");
            }
        }
    }
    if (skillId == 80001089 || lookupSkillId == 80001089)
    {
        static LONG s_skillLevelBaseFinalSoaringLogBudget = 48;
        const LONG budgetAfterDecrement = InterlockedDecrement(&s_skillLevelBaseFinalSoaringLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[SkillLevelHook] 7DA7D0 final query=%d lookup=%d result=%d",
                        skillId, lookupSkillId, result);
        }
    }
    ObserveMountedDemonJumpLevelQueryCaller(
        "7DA7D0",
        callerRet,
        skillId,
        lookupSkillId,
        -1,
        rawResult,
        result);
    SkillOverlayBridgeObserveLevelResult(lookupSkillId, result, true);
    return result;
}

static int __fastcall hkSkillLevelCurrent(void *thisPtr, void * /*edxUnused*/, DWORD playerObj, int skillId, void *cachePtr, int flags)
{
    const DWORD callerRet = (DWORD)(uintptr_t)_ReturnAddress();
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
    TryPreseedMountedDemonJumpLevelCache(
        "7DBC50",
        skillId,
        lookupSkillId,
        cachePtr);

    int result = 0;
    int rawResult = 0;
    if (oSkillLevelCurrent)
    {
        __try
        {
            result = oSkillLevelCurrent(thisPtr, playerObj, lookupSkillId, cachePtr, flags);
            rawResult = result;
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
        static DWORD s_lastCurrentRemapLogTick = 0;
        const DWORD nowTick = GetTickCount();
        if (ENABLE_HOTPATH_DIAGNOSTIC_LOGS && nowTick - s_lastCurrentRemapLogTick > 1000)
        {
            s_lastCurrentRemapLogTick = nowTick;
            WriteLogFmt("[SkillLevelHook] 7DBC50 query=%d -> %d flags=%d result=%d",
                        skillId, lookupSkillId, flags, result);
        }
    }
    if (result <= 0)
    {
        int resolvedMountItemId = 0;
        MountedRuntimeSkillKind kind = MountedRuntimeSkillKind_DoubleJump;
        if (ResolveMountedConfiguredSkillGateAllow(
                skillId,
                lookupSkillId,
                &kind,
                &resolvedMountItemId))
        {
            result = 1;
            if (kind == MountedRuntimeSkillKind_DemonJump)
            {
                TryBackfillMountedDemonJumpLevelCache(
                    "7DBC50",
                    skillId,
                    lookupSkillId,
                    cachePtr,
                    resolvedMountItemId);
            }
            static LONG s_skillLevelCurrentMountedDoubleJumpFallbackLogBudget = 24;
            const LONG budgetAfterDecrement =
                InterlockedDecrement(&s_skillLevelCurrentMountedDoubleJumpFallbackLogBudget);
            if (budgetAfterDecrement >= 0)
            {
                WriteLogFmt("[%s] 7DBC50 fallback query=%d lookup=%d flags=%d mount=%d -> result=1",
                            GetMountedRuntimeSkillLogTag(kind),
                            skillId,
                            lookupSkillId,
                            flags,
                            resolvedMountItemId);
            }
        }
    }
    if (result <= 0 && (skillId == 80001089 || lookupSkillId == 80001089))
    {
        int resolvedMountItemId = 0;
        bool fromUserLocal = false;
        if (TryResolveExtendedMountContextForSoaring(&resolvedMountItemId, &fromUserLocal))
        {
            result = 1;
            static LONG s_skillLevelCurrentRecentMountFallbackLogBudget = 24;
            const LONG budgetAfterDecrement = InterlockedDecrement(&s_skillLevelCurrentRecentMountFallbackLogBudget);
            if (budgetAfterDecrement >= 0)
            {
                WriteLogFmt("[SkillLevelHook] 7DBC50 mount fallback query=%d lookup=%d flags=%d mount=%d source=%s -> result=1",
                            skillId,
                            lookupSkillId,
                            flags,
                            resolvedMountItemId,
                            fromUserLocal ? "userlocal" : "recent");
            }
        }
    }
    if (skillId == 80001089 || lookupSkillId == 80001089)
    {
        static DWORD s_lastCurrentFinalSoaringLogTick = 0;
        const DWORD nowTick = GetTickCount();
        if (ENABLE_HOTPATH_DIAGNOSTIC_LOGS && nowTick - s_lastCurrentFinalSoaringLogTick > 1000)
        {
            s_lastCurrentFinalSoaringLogTick = nowTick;
            WriteLogFmt("[SkillLevelHook] 7DBC50 final query=%d lookup=%d flags=%d result=%d",
                        skillId, lookupSkillId, flags, result);
        }
    }
    ObserveMountedDemonJumpLevelQueryCaller(
        "7DBC50",
        callerRet,
        skillId,
        lookupSkillId,
        flags,
        rawResult,
        result);
    SkillOverlayBridgeObserveLevelResult(lookupSkillId, result, false);
    return result;
}

static bool SetupMountedSkillContextGateCallsiteHook()
{
    if (g_MountedSkillContextGateCallsiteOriginalTarget)
    {
        return true;
    }

    BYTE *pCallsite = (BYTE *)(uintptr_t)ADDR_B3009F;
    if (!pCallsite || SafeIsBadReadPtr(pCallsite, 5) || pCallsite[0] != 0xE8)
    {
        WriteLog("[MountDoubleJump] B3009F callsite missing/unexpected");
        return false;
    }

    g_MountedSkillContextGateCallsiteOriginalTarget =
        (DWORD)(uintptr_t)(pCallsite + 5 + *(int *)(pCallsite + 1));
    if (g_MountedSkillContextGateCallsiteOriginalTarget != ADDR_A9BF40)
    {
        WriteLogFmt("[MountDoubleJump] B3009F callsite target unexpected: 0x%08X",
                    g_MountedSkillContextGateCallsiteOriginalTarget);
        g_MountedSkillContextGateCallsiteOriginalTarget = 0;
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(pCallsite, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        WriteLog("[MountDoubleJump] B3009F callsite VirtualProtect failed");
        g_MountedSkillContextGateCallsiteOriginalTarget = 0;
        return false;
    }

    pCallsite[0] = 0xE8;
    *(int *)(pCallsite + 1) =
        (int)((uintptr_t)hkMountedSkillContextGateCallsiteB3009F - (uintptr_t)pCallsite - 5);

    VirtualProtect(pCallsite, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), pCallsite, 5);

    WriteLogFmt("[MountDoubleJump] OK(B3009F): original=0x%08X patchedCall=0x%08X",
                g_MountedSkillContextGateCallsiteOriginalTarget,
                (DWORD)(uintptr_t)hkMountedSkillContextGateCallsiteB3009F);
    return true;
}

static bool SetupMountedUnknownSkillReleaseBranchHook()
{
    if (g_MountedUnknownSkillReleaseBranchOriginalTarget)
    {
        return true;
    }

    BYTE *pBranch = (BYTE *)(uintptr_t)ADDR_B300AC;
    if (!pBranch || SafeIsBadReadPtr(pBranch, 5) || pBranch[0] != 0xE9)
    {
        WriteLog("[MountDoubleJump] B300AC branch missing/unexpected");
        return false;
    }

    g_MountedUnknownSkillReleaseBranchOriginalTarget =
        (DWORD)(uintptr_t)(pBranch + 5 + *(int *)(pBranch + 1));
    if (g_MountedUnknownSkillReleaseBranchOriginalTarget != ADDR_B30240)
    {
        WriteLogFmt("[MountDoubleJump] B300AC branch target unexpected: 0x%08X",
                    g_MountedUnknownSkillReleaseBranchOriginalTarget);
        g_MountedUnknownSkillReleaseBranchOriginalTarget = 0;
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(pBranch, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        WriteLog("[MountDoubleJump] B300AC branch VirtualProtect failed");
        g_MountedUnknownSkillReleaseBranchOriginalTarget = 0;
        return false;
    }

    pBranch[0] = 0xE9;
    *(int *)(pBranch + 1) =
        (int)((uintptr_t)hkMountedUnknownSkillReleaseBranchB300AC - (uintptr_t)pBranch - 5);

    VirtualProtect(pBranch, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), pBranch, 5);

    WriteLogFmt("[MountDoubleJump] OK(B300AC): original=0x%08X patchedJmp=0x%08X",
                g_MountedUnknownSkillReleaseBranchOriginalTarget,
                (DWORD)(uintptr_t)hkMountedUnknownSkillReleaseBranchB300AC);
    return true;
}

static bool InstallMountedDemonJumpCrashTraceHook(
    const char *tag,
    DWORD address,
    void *hook,
    void **originalOut)
{
    if (originalOut && *originalOut)
    {
        return true;
    }

    void *trampoline = InstallInlineHook(address, hook);
    if (!trampoline)
    {
        WriteLogFmt("[MountDemonJumpTrace] hook failed: %s (0x%08X)",
                    tag ? tag : "unknown",
                    address);
        return false;
    }

    if (originalOut)
    {
        *originalOut = trampoline;
    }

    WriteLogFmt("[MountDemonJumpTrace] OK(%s): addr=0x%08X tramp=0x%08X",
                tag ? tag : "unknown",
                address,
                (DWORD)(uintptr_t)trampoline);
    return true;
}

static bool SetupMountedDemonJumpCrashTraceHooks()
{
    bool ok = false;

    if (InstallMountedDemonJumpCrashTraceHook(
            "8057F0",
            0x008057F0,
            (void *)hkMountedDemonJumpTrace8057F0,
            reinterpret_cast<void **>(&oMountedDemonJumpTrace8057F0)))
    {
        ok = true;
    }
    if (InstallMountedDemonJumpCrashTraceHook(
            "550FF0",
            0x00550FF0,
            (void *)hkMountedDemonJumpTrace550FF0,
            reinterpret_cast<void **>(&oMountedDemonJumpTrace550FF0)))
    {
        ok = true;
    }
    if (InstallMountedDemonJumpCrashTraceHook(
            "829EC0",
            0x00829EC0,
            (void *)hkMountedDemonJumpTrace829EC0,
            reinterpret_cast<void **>(&oMountedDemonJumpTrace829EC0)))
    {
        ok = true;
    }
    if (InstallMountedDemonJumpCrashTraceHook(
            "829F10",
            0x00829F10,
            (void *)hkMountedDemonJumpTrace829F10,
            reinterpret_cast<void **>(&oMountedDemonJumpTrace829F10)))
    {
        ok = true;
    }
    if (InstallMountedDemonJumpCrashTraceHook(
            "551170",
            0x00551170,
            (void *)hkMountedDemonJumpTrace551170,
            reinterpret_cast<void **>(&oMountedDemonJumpTrace551170)))
    {
        ok = true;
    }
    if (InstallMountedDemonJumpCrashTraceHook(
            "A01BF0",
            0x00A01BF0,
            (void *)hkMountedDemonJumpTraceA01BF0,
            reinterpret_cast<void **>(&oMountedDemonJumpTraceA01BF0)))
    {
        ok = true;
    }
    if (InstallMountedDemonJumpCrashTraceHook(
            "4C1720",
            0x004C1720,
            (void *)hkMountedDemonJumpTrace4C1720,
            reinterpret_cast<void **>(&oMountedDemonJumpTrace4C1720)))
    {
        ok = true;
    }
    if (InstallMountedDemonJumpCrashTraceHook(
            "52BCB0",
            0x0052BCB0,
            (void *)hkMountedDemonJumpTrace52BCB0,
            reinterpret_cast<void **>(&oMountedDemonJumpTrace52BCB0)))
    {
        ok = true;
    }
    if (InstallMountedDemonJumpCrashTraceHook(
            "805850",
            0x00805850,
            (void *)hkMountedDemonJumpTrace805850,
            reinterpret_cast<void **>(&oMountedDemonJumpTrace805850)))
    {
        ok = true;
    }

    return ok;
}

static bool SetupMountedDemonJumpPacketObserveHooks()
{
    bool ok = false;

    if (!oMountedSkillPacketDispatchB26760)
    {
        oMountedSkillPacketDispatchB26760 =
            (tMountedSkillPacketDispatchFn)InstallInlineHook(
                ADDR_MountedSkillPacketDispatchB26760,
                (void *)hkMountedSkillPacketDispatchB26760);
        if (oMountedSkillPacketDispatchB26760)
        {
            ok = true;
            WriteLogFmt(
                "[MountDemonJumpPacket] OK(B26760): tramp=0x%08X",
                (DWORD)(uintptr_t)oMountedSkillPacketDispatchB26760);
        }
        else
        {
            WriteLog("[MountDemonJumpPacket] hook failed: B26760");
        }
    }
    else
    {
        ok = true;
    }

    if (!oMountedSkillAttackPacketB28A00)
    {
        oMountedSkillAttackPacketB28A00 =
            (tMountedSkillAttackPacketFn)InstallInlineHook(
                ADDR_MountedSkillAttackPacketB28A00,
                (void *)hkMountedSkillAttackPacketB28A00);
        if (oMountedSkillAttackPacketB28A00)
        {
            ok = true;
            WriteLogFmt(
                "[MountDemonJumpPacket] OK(B28A00): tramp=0x%08X",
                (DWORD)(uintptr_t)oMountedSkillAttackPacketB28A00);
        }
        else
        {
            WriteLog("[MountDemonJumpPacket] hook failed: B28A00");
        }
    }
    else
    {
        ok = true;
    }

    return ok;
}

static bool SetupMountedDemonJumpLatePathHooks()
{
    bool ok = false;

    if (!oMountedDemonJumpLateRoute575D60)
    {
        oMountedDemonJumpLateRoute575D60 =
            (tMountedDemonJumpLateRouteFn)InstallInlineHook(
                ADDR_MountedDemonJumpLateRoute575D60,
                (void *)hkMountedDemonJumpLateRoute575D60);
        if (oMountedDemonJumpLateRoute575D60)
        {
            ok = true;
            WriteLogFmt(
                "[MountDemonJumpLate] OK(575D60): tramp=0x%08X",
                (DWORD)(uintptr_t)oMountedDemonJumpLateRoute575D60);
        }
        else
        {
            WriteLog("[MountDemonJumpLate] hook failed: 575D60");
        }
    }
    else
    {
        ok = true;
    }

    if (!oMountedDemonJumpLateTick576020)
    {
        oMountedDemonJumpLateTick576020 =
            (tMountedDemonJumpLateTickFn)InstallInlineHook(
                ADDR_MountedDemonJumpLateTick576020,
                (void *)hkMountedDemonJumpLateTick576020);
        if (oMountedDemonJumpLateTick576020)
        {
            ok = true;
            WriteLogFmt(
                "[MountDemonJumpLate] OK(576020): tramp=0x%08X",
                (DWORD)(uintptr_t)oMountedDemonJumpLateTick576020);
        }
        else
        {
            WriteLog("[MountDemonJumpLate] hook failed: 576020");
        }
    }
    else
    {
        ok = true;
    }

    if (!oMountedDemonJumpContextInputB22630)
    {
        oMountedDemonJumpContextInputB22630 =
            (tMountedDemonJumpContextInputFn)InstallInlineHook(
                ADDR_MountedDemonJumpContextInputB22630,
                (void *)hkMountedDemonJumpContextInputB22630);
        if (oMountedDemonJumpContextInputB22630)
        {
            ok = true;
            WriteLogFmt(
                "[MountDemonJumpLate] OK(B22630): tramp=0x%08X",
                (DWORD)(uintptr_t)oMountedDemonJumpContextInputB22630);
        }
        else
        {
            WriteLog("[MountDemonJumpLate] hook failed: B22630");
        }
    }
    else
    {
        ok = true;
    }

    if (!oMountedDemonJumpMoveB1DB10)
    {
        oMountedDemonJumpMoveB1DB10 =
            (tMountedDemonJumpLateVoidRouteFn)InstallInlineHook(
                ADDR_MountedDemonJumpMoveB1DB10,
                (void *)hkMountedDemonJumpMoveB1DB10);
        if (oMountedDemonJumpMoveB1DB10)
        {
            ok = true;
            WriteLogFmt(
                "[MountDemonJumpLate] OK(B1DB10): tramp=0x%08X",
                (DWORD)(uintptr_t)oMountedDemonJumpMoveB1DB10);
        }
        else
        {
            WriteLog("[MountDemonJumpLate] hook failed: B1DB10");
        }
    }
    else
    {
        ok = true;
    }

    if (!oMountedDemonJumpMoveB1C9E0)
    {
        oMountedDemonJumpMoveB1C9E0 =
            (tMountedDemonJumpLateVoidRouteFn)InstallInlineHook(
                ADDR_MountedDemonJumpMoveB1C9E0,
                (void *)hkMountedDemonJumpMoveB1C9E0);
        if (oMountedDemonJumpMoveB1C9E0)
        {
            ok = true;
            WriteLogFmt(
                "[MountDemonJumpLate] OK(B1C9E0): tramp=0x%08X",
                (DWORD)(uintptr_t)oMountedDemonJumpMoveB1C9E0);
        }
        else
        {
            WriteLog("[MountDemonJumpLate] hook failed: B1C9E0");
        }
    }
    else
    {
        ok = true;
    }

    if (!oMountedDemonJumpBranchADEDA0)
    {
        oMountedDemonJumpBranchADEDA0 =
            (tMountedDemonJumpLateBranchFn)InstallInlineHook(
                ADDR_MountedDemonJumpBranchADEDA0,
                (void *)hkMountedDemonJumpBranchADEDA0);
        if (oMountedDemonJumpBranchADEDA0)
        {
            ok = true;
            WriteLogFmt(
                "[MountDemonJumpLate] OK(ADEDA0): tramp=0x%08X",
                (DWORD)(uintptr_t)oMountedDemonJumpBranchADEDA0);
        }
        else
        {
            WriteLog("[MountDemonJumpLate] hook failed: ADEDA0");
        }
    }
    else
    {
        ok = true;
    }

    if (!oMountedDemonJumpFilterBDBFD0)
    {
        oMountedDemonJumpFilterBDBFD0 =
            (tMountedDemonJumpLateFilterFn)InstallInlineHook(
                ADDR_MountedDemonJumpFilterBDBFD0,
                (void *)hkMountedDemonJumpFilterBDBFD0);
        if (oMountedDemonJumpFilterBDBFD0)
        {
            ok = true;
            WriteLogFmt(
                "[MountDemonJumpLate] OK(BDBFD0): tramp=0x%08X",
                (DWORD)(uintptr_t)oMountedDemonJumpFilterBDBFD0);
        }
        else
        {
            WriteLog("[MountDemonJumpLate] hook failed: BDBFD0");
        }
    }
    else
    {
        ok = true;
    }

    return ok;
}

static int __fastcall hkSkillEffect800260(void *thisPtr, void * /*edxUnused*/, int level)
{
    int result = 0;
    if (oSkillEffect800260)
    {
        __try
        {
            result = oSkillEffect800260(thisPtr, level);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            static DWORD s_lastSkillEffect800260ExceptionLogTick = 0;
            const DWORD nowTick = GetTickCount();
            if (nowTick - s_lastSkillEffect800260ExceptionLogTick > 1000)
            {
                s_lastSkillEffect800260ExceptionLogTick = nowTick;
                WriteLogFmt("[SuperPassiveEffectHook] 800260 EXCEPTION entry=0x%08X level=%d code=0x%08X",
                    (DWORD)(uintptr_t)thisPtr,
                    level,
                    GetExceptionCode());
            }
            result = 0;
        }
    }

    if (result > 0)
        SkillOverlayBridgeApplyConfiguredPassiveEffectBonuses((uintptr_t)thisPtr, level, (uintptr_t)result, "800260");
    return result;
}

static int __fastcall hkSkillEffect800580(void *thisPtr, void * /*edxUnused*/, int level)
{
    int result = 0;
    if (oSkillEffect800580)
    {
        __try
        {
            result = oSkillEffect800580(thisPtr, level);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            static DWORD s_lastSkillEffect800580ExceptionLogTick = 0;
            const DWORD nowTick = GetTickCount();
            if (nowTick - s_lastSkillEffect800580ExceptionLogTick > 1000)
            {
                s_lastSkillEffect800580ExceptionLogTick = nowTick;
                WriteLogFmt("[SuperPassiveEffectHook] 800580 EXCEPTION entry=0x%08X level=%d code=0x%08X",
                    (DWORD)(uintptr_t)thisPtr,
                    level,
                    GetExceptionCode());
            }
            result = 0;
        }
    }

    if (result > 0)
        SkillOverlayBridgeApplyConfiguredPassiveEffectBonuses((uintptr_t)thisPtr, level, (uintptr_t)result, "800580");
    return result;
}

static int __fastcall hkPassiveEffectDamage43DE00(void *thisPtr, void * /*edxUnused*/)
{
    int result = 0;
    if (oPassiveEffectDamage43DE00)
        result = oPassiveEffectDamage43DE00(thisPtr);
    return SkillOverlayBridgeOverridePassiveEffectGetterValue((uintptr_t)thisPtr, result, "damage", "43DE00");
}

static int __fastcall hkPassiveEffectDamage43DE50(void *thisPtr, void * /*edxUnused*/)
{
    int result = 0;
    if (oPassiveEffectDamage43DE50)
        result = oPassiveEffectDamage43DE50(thisPtr);
    return SkillOverlayBridgeOverridePassiveEffectGetterValue((uintptr_t)thisPtr, result, "damage", "43DE50");
}

static int __fastcall hkPassiveEffectAttackCount5E9EE0(void *thisPtr, void * /*edxUnused*/)
{
    int result = 0;
    if (oPassiveEffectAttackCount5E9EE0)
        result = oPassiveEffectAttackCount5E9EE0(thisPtr);
    return SkillOverlayBridgeOverridePassiveEffectGetterValue((uintptr_t)thisPtr, result, "attackCount", "5E9EE0");
}

static int __fastcall hkPassiveEffectMobCount7D1990(void *thisPtr, void * /*edxUnused*/)
{
    int result = 0;
    if (oPassiveEffectMobCount7D1990)
        result = oPassiveEffectMobCount7D1990(thisPtr);
    return SkillOverlayBridgeOverridePassiveEffectGetterValue((uintptr_t)thisPtr, result, "mobCount", "7D1990");
}

static int __fastcall hkPassiveEffectAttackCount7D19E0(void *thisPtr, void * /*edxUnused*/)
{
    int result = 0;
    if (oPassiveEffectAttackCount7D19E0)
        result = oPassiveEffectAttackCount7D19E0(thisPtr);
    return SkillOverlayBridgeOverridePassiveEffectGetterValue((uintptr_t)thisPtr, result, "attackCount", "7D19E0");
}

static int __fastcall hkPassiveEffectIgnore7D28E0(void *thisPtr, void * /*edxUnused*/)
{
    int result = 0;
    if (oPassiveEffectIgnore7D28E0)
        result = oPassiveEffectIgnore7D28E0(thisPtr);
    return SkillOverlayBridgeOverridePassiveEffectGetterValue((uintptr_t)thisPtr, result, "ignoreMobpdpR", "7D28E0");
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
        mov ecx, [esp + 12] // pushad 保存的原始 ESP
        mov ecx, [ecx + 4] // arg0 = skillId
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

static void __fastcall hkSkillPresentationDispatch(void *thisPtr, void * /*edxUnused*/, int *skillData, int a3, int a4, int a5, int a6, int a7)
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
    int *originalSkillDataPtr = skillData;

    if (skillData)
    {
        __try
        {
            originalSkillId = *skillData;
            if (IsMountedDemonJumpRelatedSkillId(originalSkillId))
            {
                static LONG s_mountedDemonJumpPresentationEnterLogBudget = 24;
                const LONG budgetAfterDecrement =
                    InterlockedDecrement(&s_mountedDemonJumpPresentationEnterLogBudget);
                if (budgetAfterDecrement >= 0)
                {
                    WriteLogFmt("[MountDemonJump] ABAF70 enter skill=%d ptr=0x%08X",
                                originalSkillId,
                                (DWORD)(uintptr_t)skillData);
                }
            }
            desiredSkillId = SkillOverlayBridgeResolveNativePresentationDesiredSkillId(originalSkillId);
            if (desiredSkillId > 0)
            {
                const uintptr_t desiredSkillDataPtr = SkillOverlayBridgeLookupSkillEntryPointer(desiredSkillId);
                if (desiredSkillDataPtr && desiredSkillDataPtr != (uintptr_t)skillData)
                {
                    skillData = (int *)desiredSkillDataPtr;
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
    if (!g_SkillWndThis || !g_SuperExpanded)
    {
        g_PanelDrawX = -9999;
        g_PanelDrawY = -9999;
#if defined(SSW_ENABLE_SECOND_CHILD_CARRIER_PROBE_RUNTIME)
        PollSecondChildCarrierProbeTick(0x0000E001, false);
#endif
        return;
    }

    static int s_lastPanelX = 0x7FFFFFFF;
    static int s_lastPanelY = 0x7FFFFFFF;
    static int s_missPosLogCount = 0;

    int panelX = -9999, panelY = -9999;
    const char *src = "none";
    if (!ComputeSuperPanelPos(&panelX, &panelY, &src))
    {
        if (s_missPosLogCount < 16)
        {
            WriteLog("[UpdatePos] FAIL: ComputeSuperPanelPos");
            s_missPosLogCount++;
        }
        return;
    }

    g_PanelDrawX = panelX;
    g_PanelDrawY = panelY;

    if (ENABLE_IMGUI_OVERLAY_PANEL)
    {
        if (g_IsD3D8Mode)
            SuperD3D8OverlaySetAnchor(g_PanelDrawX, g_PanelDrawY);
        else
            SuperImGuiOverlaySetAnchor(g_PanelDrawX, g_PanelDrawY);
        if ((g_PanelDrawX != s_lastPanelX || g_PanelDrawY != s_lastPanelY) && g_UpdatePosLogCount < 200)
        {
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

    if (g_NativeWndCreated && g_SuperCWnd)
    {
        int vtX = 0, vtY = 0;
        if (GetSkillWndAnchorPos(g_SkillWndThis, &vtX, &vtY))
        {
            g_PanelDrawX = vtX - PANEL_W + SUPER_CHILD_VT_DELTA_X;
            g_PanelDrawY = vtY + SUPER_CHILD_VT_DELTA_Y;
            src = "skill_vt_child";
        }
        else
        {
            g_PanelDrawX += SUPER_CHILD_OFFSET_X;
            g_PanelDrawY += SUPER_CHILD_OFFSET_Y;
        }
    }

    if (g_SuperCWnd && !SafeIsBadReadPtr((void *)g_SuperCWnd, 0x30))
    {
        if (g_SuperUsesSkillWndSecondSlot)
        {
            LogOfficialSecondChildState(g_SuperCWnd, "UpdatePos:BeforeMove");
        }
        SetSuperWndVisible(g_SuperCWnd, 1);
        MoveNativeChildWnd(g_SuperCWnd, g_PanelDrawX, g_PanelDrawY, "UpdatePosMove");
        MarkSuperWndDirty(g_SuperCWnd, "UpdatePosDirty");
        if (g_SuperUsesSkillWndSecondSlot)
        {
            LogOfficialSecondChildState(g_SuperCWnd, "UpdatePos:AfterMove");
        }
    }

    if ((g_PanelDrawX != s_lastPanelX || g_PanelDrawY != s_lastPanelY) && g_UpdatePosLogCount < 200)
    {
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

#if defined(SSW_ENABLE_SECOND_CHILD_CARRIER_PROBE_RUNTIME)
    PollSecondChildCarrierProbeTick(0x0000E002, false);
#endif
}

// ============================================================================
// WndProc Hook — F9切换 + 面板内点击（原生按钮不需要坐标检测了）
// ============================================================================
static LRESULT CALLBACK GameWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
#if defined(SSW_ENABLE_SECOND_CHILD_CARRIER_PROBE_RUNTIME)
    SSW_SecondChildCarrierProbe_ObserveWndProc(m, w, l);
#endif

    if (ENABLE_IMGUI_OVERLAY_PANEL)
    {
        switch (m)
        {
        case WM_CAPTURECHANGED:
        case WM_KILLFOCUS:
            if (g_IsD3D8Mode)
                SuperD3D8OverlayCancelMouseCapture();
            else
                SuperImGuiOverlayCancelMouseCapture();
            break;
        case WM_ACTIVATEAPP:
            if (!w)
            {
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
    if (m == WM_KEYDOWN && w == VK_F9)
    {
        if (g_SkillWndThis)
        {
            ToggleSuperWnd();
        }
        return 0;
    }

#if defined(SSW_ENABLE_SECOND_CHILD_CARRIER_PROBE_RUNTIME)
    if (m == WM_KEYDOWN && w == VK_F10)
    {
        RunSecondChildCarrierProbeHotkey();
        return 0;
    }

    if (m == WM_KEYDOWN && w == VK_F11)
    {
        PollSecondChildCarrierProbeTick(0x0000F011, true);
        return 0;
    }

    if (m == WM_KEYDOWN && w == VK_F12)
    {
        ReleaseSecondChildCarrierProbeHotkey();
        return 0;
    }
#endif

    if (m == WM_SETCURSOR && g_MouseSuppressFallbackActive)
    {
        SetCursor(nullptr);
        return TRUE;
    }

    if (!ENABLE_IMGUI_OVERLAY_PANEL && HandleSuperBtnD3DWndProc(h, m, w, l))
        return 0;

    // 兜底：即使原生消息没分发到sub_9ECFD0，也保证按钮可点
    // 注意：不能吞掉 WM_LBUTTONUP，否则原生按钮状态机会丢失 mouseUp，光标可能卡在 pressed
    if (g_Ready && g_NativeBtnCreated && !oSkillWndMsg)
    {
        if (m == WM_LBUTTONUP)
        {
            int mx = (short)LOWORD(l), my = (short)HIWORD(l);
            if (TryToggleByMousePoint(mx, my, "wndproc"))
            {
                static DWORD s_lastPassThroughLogTick = 0;
                DWORD now = GetTickCount();
                if (now - s_lastPassThroughLogTick > 200)
                {
                    s_lastPassThroughLogTick = now;
                    WriteLog("[WndProc] fallback toggle hit, pass WM_LBUTTONUP through");
                }
                // 不 return：继续传给原WndProc，让原生按钮完成 mouseUp 收尾
            }
        }
    }

    if (ENABLE_IMGUI_OVERLAY_PANEL)
    {
        if (m == WM_MOUSEACTIVATE && (g_IsD3D8Mode ? SuperD3D8OverlayShouldSuppressGameMouse() : SuperImGuiOverlayShouldSuppressGameMouse()))
        {
            // 点击非前台游戏里的 overlay 时，先激活游戏窗口，但吃掉这一下，避免穿透到原生窗口。
            return MA_ACTIVATEANDEAT;
        }

        bool overlayHandled = g_IsD3D8Mode ? SuperD3D8OverlayHandleWndProc(h, m, w, l) : SuperImGuiOverlayHandleWndProc(h, m, w, l);
        const bool overlayToggleRequested = g_IsD3D8Mode ? SuperD3D8OverlayConsumeToggleRequested() : SuperImGuiOverlayConsumeToggleRequested();
        if (overlayToggleRequested)
        {
            g_LastNativeMsgToggleTick = GetTickCount();
            ToggleSuperWnd("overlay_btn");
            overlayHandled = true;
        }
        bool suppressGameMouse = g_IsD3D8Mode ? SuperD3D8OverlayShouldSuppressGameMouse() : SuperImGuiOverlayShouldSuppressGameMouse();

        if (!overlayHandled)
        {
            switch (m)
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
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
                if (g_IsD3D8Mode)
                    SuperD3D8OverlayCancelMouseCapture();
                else
                    SuperImGuiOverlayCancelMouseCapture();
                if (Win32InputSpoofIsInstalled())
                {
                    Win32InputSpoofSetSuppressMouse(false);
                }
                break;
            default:
                break;
            }
        }

        if (overlayHandled && suppressGameMouse)
        {
            auto forwardOffscreenMouseToGame = [&]()
            {
                if (g_OriginalWndProc)
                {
                    CallWindowProc(g_OriginalWndProc, h, m, w, Win32InputSpoofMakeOffscreenMouseLParam());
                }
            };

            switch (m)
            {
            default:
                break;
            }

            if (m == WM_SETCURSOR)
            {
                SetCursor(nullptr);
                return TRUE;
            }

            if (m == WM_MOUSEMOVE)
            {
                forwardOffscreenMouseToGame();
                return 0;
            }

            switch (m)
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
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
                return 0;
            default:
                break;
            }
        }

        if (overlayHandled)
        {
            return 0;
        }
    }

    // 面板内点击（当前native child 仍未接上原生输入协议，先由WndProc兜底接管，避免点击穿透到底层窗口）
    if (!ENABLE_IMGUI_OVERLAY_PANEL && g_Ready && g_SkillWndThis && g_SuperExpanded)
    {
        if (m == WM_LBUTTONDOWN || m == WM_LBUTTONUP)
        {
            if (g_PanelDrawX <= -9000 || g_PanelDrawY <= -9000)
            {
                UpdateSuperCWnd();
            }
            int mx = (short)LOWORD(l), my = (short)HIWORD(l);
            int cx = g_PanelDrawX;
            int cy = g_PanelDrawY;
            if (cx > -9000 && cy > -9000)
            {
                int relX = mx - cx;
                int relY = my - cy;
                if (relX >= 0 && relX < PANEL_W && relY >= 0 && relY < PANEL_H)
                {
                    static int s_panelHitLogCount = 0;
                    if (s_panelHitLogCount < 40)
                    {
                        WriteLogFmt("[PanelHit:wndproc] msg=%u mx=%d my=%d rel=(%d,%d) panel=(%d,%d,%d,%d)",
                                    m, mx, my, relX, relY, cx, cy, PANEL_W, PANEL_H);
                        s_panelHitLogCount++;
                    }
                    if (m == WM_LBUTTONDOWN)
                    {
                        if (relY < 28)
                        {
                            static int s_panelTabLogCount = 0;
                            if (s_panelTabLogCount < 40)
                            {
                                WriteLogFmt("[PanelAction] tab=%d rel=(%d,%d)", relX / 56, relX, relY);
                                s_panelTabLogCount++;
                            }
                            g_SkillMgr.SetTab(relX / 56);
                        }
                        else
                        {
                            int rowIdx = (relY - 28) / 34;
                            SkillTab *tab = g_SkillMgr.GetCurrentTab();
                            if (tab && rowIdx >= 0 && rowIdx < tab->count)
                            {
                                static int s_panelSkillLogCount = 0;
                                if (s_panelSkillLogCount < 60)
                                {
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
typedef HRESULT(__stdcall *tPresent)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
typedef HRESULT(__stdcall *tReset)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
typedef HRESULT(__stdcall *tResetEx)(IDirect3DDevice9Ex *, D3DPRESENT_PARAMETERS *, D3DDISPLAYMODEEX *);
static HRESULT __stdcall hkReset(IDirect3DDevice9 *pDevice, D3DPRESENT_PARAMETERS *pPresentationParameters);
static HRESULT __stdcall hkResetEx(IDirect3DDevice9Ex *pDevice, D3DPRESENT_PARAMETERS *pPresentationParameters, D3DDISPLAYMODEEX *pFullscreenDisplayMode);
static HRESULT __stdcall hkPresent(IDirect3DDevice9 *pDevice, const RECT *pSourceRect, const RECT *pDestRect, HWND hDestWindowOverride, const RGNDATA *pDirtyRegion);
static tPresent oPresent = nullptr;
static tReset oReset = nullptr;
static tResetEx oResetEx = nullptr;
typedef HRESULT(__stdcall *tEndScene)(IDirect3DDevice9 *);
static tEndScene oEndScene = nullptr;
static tPresent g_LiveDevicePresent = nullptr;
static bool g_LivePresentVtableHooked = false;
static tReset g_LiveDeviceReset = nullptr;
static tResetEx g_LiveDeviceResetEx = nullptr;
static void **g_LiveDeviceVTable9 = nullptr;
static void **g_LiveDeviceVTable9Ex = nullptr;

// Forward declarations for CreateDevice interception strategy
static HRESULT __stdcall hkEndScene(IDirect3DDevice9 *pDevice);
static bool PatchLiveDeviceVTableEntry(void **vtable, int index, void *hookFunc, void **outOriginal);
static void EnsureLiveDeviceResetHooks(IDirect3DDevice9 *pDevice);

// --- CreateDevice interception strategy ---
// When inline hooks on Present/EndScene don't fire (e.g., D3D9on12, wrapper layers),
// we hook Direct3DCreate9 -> vtable-hook IDirect3D9::CreateDevice -> vtable-hook the real device's Present.
typedef IDirect3D9 *(WINAPI *tDirect3DCreate9Fn)(UINT SDKVersion);
typedef HRESULT(__stdcall *tCreateDevice)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
static tDirect3DCreate9Fn oDirectCreate9 = nullptr;
static tCreateDevice oCreateDevice = nullptr;
static bool g_DeviceCapturedViaCreateHook = false;

static HRESULT __stdcall hkCreateDevice(IDirect3D9 *pD3D, UINT Adapter, D3DDEVTYPE DeviceType,
                                        HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pPP, IDirect3DDevice9 **ppDevice)
{
    HRESULT hr = oCreateDevice(pD3D, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPP, ppDevice);
    if (SUCCEEDED(hr) && ppDevice && *ppDevice && !g_DeviceCapturedViaCreateHook)
    {
        IDirect3DDevice9 *pDevice = *ppDevice;
        g_pDevice = pDevice;
        g_DeviceCapturedViaCreateHook = true;
        WriteLogFmt("[D3D9-CreateHook] Captured real device=0x%08X type=%d", (DWORD)(uintptr_t)pDevice, (int)DeviceType);

        // Vtable-hook Present on the real device
        void **vtable = *(void ***)pDevice;
        if (vtable)
        {
            void *origPresent = nullptr;
            if (PatchLiveDeviceVTableEntry(vtable, 17, (void *)hkPresent, &origPresent))
            {
                if (origPresent && origPresent != (void *)hkPresent)
                    g_LiveDevicePresent = (tPresent)origPresent;
                g_LivePresentVtableHooked = true;
                WriteLogFmt("[D3D9-CreateHook] Present vtable hooked orig=0x%08X", (DWORD)(uintptr_t)origPresent);
            }

            void *origEndScene = nullptr;
            if (PatchLiveDeviceVTableEntry(vtable, 42, (void *)hkEndScene, &origEndScene))
            {
                if (origEndScene && origEndScene != (void *)hkEndScene && !oEndScene)
                    oEndScene = (tEndScene)origEndScene;
                WriteLogFmt("[D3D9-CreateHook] EndScene vtable hooked orig=0x%08X", (DWORD)(uintptr_t)origEndScene);
            }
        }
        EnsureLiveDeviceResetHooks(pDevice);
    }
    return hr;
}

static IDirect3D9 *WINAPI hkDirect3DCreate9(UINT SDKVersion)
{
    IDirect3D9 *pD3D = oDirectCreate9(SDKVersion);
    if (pD3D)
    {
        WriteLogFmt("[D3D9-CreateHook] Direct3DCreate9 called SDK=%d result=0x%08X", SDKVersion, (DWORD)(uintptr_t)pD3D);
        // Vtable-hook CreateDevice (index 16) on the real IDirect3D9
        void **vtable = *(void ***)pD3D;
        if (vtable)
        {
            void *origCreateDevice = nullptr;
            if (PatchLiveDeviceVTableEntry(vtable, 16, (void *)hkCreateDevice, &origCreateDevice))
            {
                if (origCreateDevice && origCreateDevice != (void *)hkCreateDevice)
                    oCreateDevice = (tCreateDevice)origCreateDevice;
                WriteLogFmt("[D3D9-CreateHook] CreateDevice vtable hooked orig=0x%08X", (DWORD)(uintptr_t)origCreateDevice);
            }
        }
    }
    return pD3D;
}

static bool PatchLiveDeviceVTableEntry(void **vtable, int index, void *hookFunc, void **outOriginal)
{
    if (!vtable || index < 0)
        return false;

    void **slot = &vtable[index];
    if (SafeIsBadReadPtr(slot, sizeof(void *)))
        return false;

    if (*slot == hookFunc)
        return true;

    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    if (outOriginal)
        *outOriginal = *slot;
    *slot = hookFunc;
    VirtualProtect(slot, sizeof(void *), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void *));
    return true;
}

static void EnsureLiveDeviceResetHooks(IDirect3DDevice9 *pDevice)
{
    if (!pDevice)
        return;

    void **vtable9 = *(void ***)pDevice;
    if (vtable9 && vtable9 != g_LiveDeviceVTable9)
    {
        void *originalReset = nullptr;
        if (PatchLiveDeviceVTableEntry(vtable9, 16, (void *)hkReset, &originalReset))
        {
            if (originalReset && originalReset != (void *)hkReset)
                g_LiveDeviceReset = (tReset)originalReset;
            g_LiveDeviceVTable9 = vtable9;
            WriteLogFmt("[D3D9] live vtbl Reset patched vtbl=0x%08X orig=0x%08X",
                        (DWORD)(uintptr_t)vtable9, (DWORD)(uintptr_t)g_LiveDeviceReset);
        }
    }

    IDirect3DDevice9Ex *pDeviceEx = nullptr;
    if (SUCCEEDED(pDevice->QueryInterface(__uuidof(IDirect3DDevice9Ex), (void **)&pDeviceEx)) && pDeviceEx)
    {
        void **vtableEx = *(void ***)pDeviceEx;
        if (vtableEx && vtableEx != g_LiveDeviceVTable9Ex)
        {
            void *originalResetEx = nullptr;
            if (PatchLiveDeviceVTableEntry(vtableEx, 132, (void *)hkResetEx, &originalResetEx))
            {
                if (originalResetEx && originalResetEx != (void *)hkResetEx)
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

static HRESULT __stdcall hkReset(IDirect3DDevice9 *pDevice, D3DPRESENT_PARAMETERS *pPresentationParameters)
{
    // Guard against infinite recursion: inline hook on function body + vtable hook
    // both redirect to hkReset. If re-entered, go straight to trampoline.
    if (g_InReset)
    {
        tReset resetFn = g_LiveDeviceReset ? g_LiveDeviceReset : oReset;
        return resetFn ? resetFn(pDevice, pPresentationParameters) : D3DERR_INVALIDCALL;
    }
    g_InReset = true;

    PrepareForD3DDeviceReset("reset");

    tReset resetFn = g_LiveDeviceReset ? g_LiveDeviceReset : oReset;
    HRESULT hr = resetFn ? resetFn(pDevice, pPresentationParameters) : D3DERR_INVALIDCALL;

    if (SUCCEEDED(hr))
    {
        g_pDevice = pDevice;
        if (ENABLE_IMGUI_OVERLAY_PANEL)
            SuperImGuiOverlayOnDeviceReset(pDevice);
        WriteLog("[D3D9] Reset OK: hard rebuild pending on next Present");
    }
    else
    {
        WriteLogFmt("[D3D9] Reset FAIL hr=0x%08X", (DWORD)hr);
    }

    g_InReset = false;
    return hr;
}

static volatile bool g_InResetEx = false;

static HRESULT __stdcall hkResetEx(IDirect3DDevice9Ex *pDevice, D3DPRESENT_PARAMETERS *pPresentationParameters, D3DDISPLAYMODEEX *pFullscreenDisplayMode)
{
    if (g_InResetEx)
    {
        tResetEx resetFn = g_LiveDeviceResetEx ? g_LiveDeviceResetEx : oResetEx;
        return resetFn ? resetFn(pDevice, pPresentationParameters, pFullscreenDisplayMode) : D3DERR_INVALIDCALL;
    }
    g_InResetEx = true;

    PrepareForD3DDeviceReset("reset_ex");

    tResetEx resetFn = g_LiveDeviceResetEx ? g_LiveDeviceResetEx : oResetEx;
    HRESULT hr = resetFn ? resetFn(pDevice, pPresentationParameters, pFullscreenDisplayMode) : D3DERR_INVALIDCALL;

    if (SUCCEEDED(hr))
    {
        g_pDevice = (IDirect3DDevice9 *)pDevice;
        if (ENABLE_IMGUI_OVERLAY_PANEL)
            SuperImGuiOverlayOnDeviceReset((IDirect3DDevice9 *)pDevice);
        WriteLog("[D3D9Ex] ResetEx OK: hard rebuild pending on next Present");
    }
    else
    {
        WriteLogFmt("[D3D9Ex] ResetEx FAIL hr=0x%08X", (DWORD)hr);
    }

    g_InResetEx = false;
    return hr;
}

// EndScene hook — fallback device capture when Present inline hook misses
// (e.g., game uses D3D9Ex or wrapper d3d9.dll with different Present address)
static HRESULT __stdcall hkEndScene(IDirect3DDevice9 *pDevice)
{
    if (pDevice && !g_pDevice)
    {
        g_pDevice = pDevice;
        WriteLogFmt("[D3D9] EndScene captured device=0x%08X", (DWORD)(uintptr_t)pDevice);

        // Vtable-hook Present on the live device so hkPresent fires from now on
        if (!g_LivePresentVtableHooked)
        {
            void **vtable = *(void ***)pDevice;
            if (vtable)
            {
                void *origPresent = nullptr;
                if (PatchLiveDeviceVTableEntry(vtable, 17, (void *)hkPresent, &origPresent))
                {
                    if (origPresent && origPresent != (void *)hkPresent)
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

static HRESULT __stdcall hkPresent(IDirect3DDevice9 *pDevice,
                                   const RECT *pSourceRect, const RECT *pDestRect,
                                   HWND hDestWindowOverride, const RGNDATA *pDirtyRegion)
{
    g_pDevice = pDevice;

    // Choose the correct original Present to call at the end:
    // - oPresent = inline hook trampoline (works when inline hook caught the right function)
    // - g_LiveDevicePresent = vtable original (works when EndScene fallback patched vtable)
    tPresent fnOrigPresent = oPresent ? oPresent : g_LiveDevicePresent;
    EnsureLiveDeviceResetHooks(pDevice);

    // 纹理加载（一次性，面板用）
    if (!g_TexturesLoaded)
    {
        LoadAllTextures(pDevice);
    }

    // 每帧同步SkillWnd全局指针，防止角色切换/重开窗口后悬空指针
    bool wasReady = g_Ready;
    uintptr_t swGlobal = 0;
    if (!SafeIsBadReadPtr((void *)ADDR_SkillWndEx, 4))
    {
        swGlobal = *(uintptr_t *)ADDR_SkillWndEx;
    }
    OnSkillWndPointerObserved(swGlobal, "present");

    if (!wasReady && g_Ready && g_SkillWndThis)
    {
        WriteLogFmt("[Present] SkillWnd: 0x%08X", (DWORD)g_SkillWndThis);
        WriteLog("[Present] Ready");
    }

    if (g_Ready && g_SkillWndThis && !g_NativeBtnCreated)
    {
        static DWORD s_lastPresentButtonRetryTick = 0;
        DWORD now = GetTickCount();
        if (now - s_lastPresentButtonRetryTick > 500)
        {
            s_lastPresentButtonRetryTick = now;
            WriteLogFmt("[Present] retry create native button hookInstalled=%d", oSkillWndInitChildren ? 1 : 0);
            if (CreateSuperButton(g_SkillWndThis))
                WriteLog("[Present] retry native button OK");
            else
                WriteLog("[Present] retry native button FAILED");
        }
    }

    if (ENABLE_IMGUI_OVERLAY_PANEL)
    {
        const bool hasIndependentBuffOverlay = SkillOverlayBridgeHasIndependentBuffOverlayEntries();
        const bool overlayRuntimeReady = (g_Ready || hasIndependentBuffOverlay) && g_GameHwnd && pDevice;
        if (overlayRuntimeReady && !SuperImGuiOverlayIsInitialized() && g_GameHwnd && pDevice)
        {
            if (!SuperImGuiOverlayEnsureInitialized(g_GameHwnd, pDevice, 1.0f, IMGUI_PANEL_ASSET_PATH))
            {
                static DWORD s_lastOverlayInitFailLogTick = 0;
                DWORD now = GetTickCount();
                if (now - s_lastOverlayInitFailLogTick > 1000)
                {
                    WriteLogFmt("[ImGuiOverlay] ensure init failed in Present device=0x%08X hwnd=0x%08X",
                                (DWORD)(uintptr_t)pDevice, (DWORD)(uintptr_t)g_GameHwnd);
                    s_lastOverlayInitFailLogTick = now;
                }
            }
        }

        if (SuperImGuiOverlayIsInitialized())
        {
            RECT superBtnRect = {};
            const bool hasSuperBtnRect = GetSuperButtonBaseRectForD3D(&superBtnRect);
            SuperImGuiOverlaySetPanelExpanded(g_SuperExpanded);
            SuperImGuiOverlaySetSuperButtonVisible(hasSuperBtnRect);
            SuperImGuiOverlaySetSuperButtonRect(hasSuperBtnRect ? &superBtnRect : nullptr);
        }

        if (overlayRuntimeReady && SuperImGuiOverlayIsInitialized())
        {
            if (g_SuperExpanded)
                UpdateSuperCWnd();
            SuperImGuiOverlaySetVisible(true);
            SuperImGuiOverlayRender(pDevice);
        }
        else if (SuperImGuiOverlayIsInitialized())
        {
            SuperImGuiOverlaySetVisible(false);
        }

        const bool suppressMouse = SuperImGuiOverlayShouldSuppressGameMouse();
        if (Win32InputSpoofIsInstalled())
        {
            Win32InputSpoofSetSuppressMouse(suppressMouse);
        }
        UpdateGameMouseSuppressionFallback(suppressMouse);
        if (g_LastOverlaySuppressMouse && !suppressMouse)
        {
            RefreshGameCursorImmediately();
        }
        g_LastOverlaySuppressMouse = suppressMouse;
        HRESULT presentHr = fnOrigPresent ? fnOrigPresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion) : D3D_OK;
        SkillOverlayBridgeBeginFrameObservation();
        return presentHr;
    }

    // 每帧刷新扩展层锚点（真正绘制在 sub_9DEE30 里做）
    // v10.4+: native child 已有 move/refresh/toggle 三条同步链，这里先停掉 Present 中的每帧搬运，
    // 避免与原生移动链互相打架，导致拖动抽搐和视口轻微漂移。
    if (g_SkillWndThis && g_SuperExpanded)
    {
        if (!g_NativeWndCreated || !g_SuperCWnd || ENABLE_PRESENT_NATIVE_CHILD_UPDATE)
        {
            UpdateSuperCWnd();
        }
    }

    // v7.6: 拖动过程中原生dirty链不会稳定重画，直接在Present里按最新锚点绘制面板纹理。
    if (ENABLE_PRESENT_PANEL_DRAW && g_SuperExpanded && g_texPanelBg)
    {
        int drawX = g_PanelDrawX;
        int drawY = g_PanelDrawY;
        if ((drawX <= -9000 || drawY <= -9000) && g_SuperCWnd)
        {
            drawX = CWnd_GetRenderX(g_SuperCWnd);
            drawY = CWnd_GetRenderY(g_SuperCWnd);
        }
        if (drawX > -9000 && drawY > -9000)
        {
            static int s_presentDrawLogCount = 0;
            if (s_presentDrawLogCount < 20)
            {
                WriteLogFmt("[PresentDraw] panel=(%d,%d)", drawX, drawY);
                s_presentDrawLogCount++;
            }
            DrawTexturedQuad(pDevice, g_texPanelBg,
                             (float)drawX, (float)drawY, (float)PANEL_W, (float)PANEL_H);
        }
    }

    DrawSuperButtonTextureInPresent(pDevice);

    // v7.2: 默认关闭Present点击轮询，避免与WndProc路径双触发
    if (ENABLE_PRESENT_CLICK_POLL && g_Ready && g_NativeBtnCreated)
    {
        static bool s_prevLBtnDown = false;
        bool isDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (s_prevLBtnDown && !isDown)
        {
            POINT pt = {};
            if (GetCursorPos(&pt))
            {
                HWND hwndForClient = g_GameHwnd ? g_GameHwnd : hDestWindowOverride;
                if (hwndForClient)
                {
                    ScreenToClient(hwndForClient, &pt);
                }
                TryToggleByMousePoint(pt.x, pt.y, "present");
            }
        }
        s_prevLBtnDown = isDown;
    }

    HRESULT presentHr = fnOrigPresent ? fnOrigPresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion) : D3D_OK;
    SkillOverlayBridgeBeginFrameObservation();
    return presentHr;
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
    typedef IDirect3D9 *(WINAPI * tDirect3DCreate9)(UINT);
    tDirect3DCreate9 pfnCreate9 = nullptr;
    HMODULE hD3D9 = ::GetModuleHandleA("d3d9.dll");
    if (hD3D9)
        pfnCreate9 = (tDirect3DCreate9)::GetProcAddress(hD3D9, "Direct3DCreate9");
    if (!pfnCreate9)
        pfnCreate9 = Direct3DCreate9; // fallback to linked import

    IDirect3D9 *pD3D = pfnCreate9(D3D_SDK_VERSION);
    if (!pD3D)
    {
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

    IDirect3DDevice9 *pDev = nullptr;
    // Try HAL first (shares vtable with real game device), fall back to NULLREF
    HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                    hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDev);
    if (FAILED(hr) || !pDev)
    {
        WriteLogFmt("[D3D9] HAL device failed (hr=0x%08X), trying NULLREF", (DWORD)hr);
        hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF,
                                hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDev);
    }

    if (FAILED(hr) || !pDev)
    {
        pD3D->Release();
        DestroyWindow(hWnd);
        UnregisterClassA("SSWDummy", wc.hInstance);
        return false;
    }

    DWORD *vtable = *(DWORD **)pDev;
    DWORD presentAddr = vtable[17];
    DWORD endSceneAddr = vtable[42];
    BYTE *pPresent = FollowJmpChain((void *)presentAddr);
    BYTE *pEndScene = FollowJmpChain((void *)endSceneAddr);

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
        if (hModPresent)
            GetModuleFileNameA(hModPresent, modNameP, sizeof(modNameP));
        if (hModEndScene)
            GetModuleFileNameA(hModEndScene, modNameE, sizeof(modNameE));
        WriteLogFmt("[D3D9] Present in: %s", modNameP[0] ? modNameP : "(unknown)");
        WriteLogFmt("[D3D9] EndScene in: %s", modNameE[0] ? modNameE : "(unknown)");
    }

    int copyLen = CalcMinCopyLen(pPresent);
    if (copyLen < 5)
        copyLen = 5;
    oReset = nullptr;
    oResetEx = nullptr;
    g_LiveDeviceReset = nullptr;
    g_LiveDeviceResetEx = nullptr;

    oPresent = (tPresent)GenericInlineHook5(pPresent, (void *)hkPresent, copyLen);
    if (!oPresent)
    {
        WriteLog("[D3D9] Present inline hook failed (will rely on EndScene fallback)");
    }

    // EndScene fallback: hook vtable[42] to capture device even if Present inline hook
    // doesn't match the game's actual Present function (e.g., D3D9Ex / wrapper d3d9.dll)
    int esLen = CalcMinCopyLen(pEndScene);
    if (esLen < 5)
        esLen = 5;
    oEndScene = (tEndScene)GenericInlineHook5(pEndScene, (void *)hkEndScene, esLen);
    if (!oEndScene)
    {
        WriteLog("[D3D9] EndScene inline hook failed");
    }

    if (!oPresent && !oEndScene)
    {
        WriteLog("[D3D9] FAIL: both Present and EndScene hooks failed");
        pDev->Release();
        pD3D->Release();
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
            BYTE *pCreate9 = (BYTE *)::GetProcAddress(hD3D9Live, "Direct3DCreate9");
            if (pCreate9)
            {
                pCreate9 = FollowJmpChain(pCreate9);
                int createLen = CalcMinCopyLen(pCreate9);
                if (createLen < 5)
                    createLen = 5;
                oDirectCreate9 = (tDirect3DCreate9Fn)GenericInlineHook5(pCreate9, (void *)hkDirect3DCreate9, createLen);
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
        void **dummyVtable = *(void ***)pDev;
        if (dummyVtable)
        {
            void *origPresentVtbl = nullptr;
            void *origEndSceneVtbl = nullptr;
            if (PatchLiveDeviceVTableEntry(dummyVtable, 17, (void *)hkPresent, &origPresentVtbl))
            {
                // If inline hook already installed, the original function entry is patched with jmp->hkPresent
                // so calling it would recurse. Only use vtable original when we DON'T have an inline trampoline.
                if (!oPresent && !g_LiveDevicePresent && origPresentVtbl && origPresentVtbl != (void *)hkPresent)
                    g_LiveDevicePresent = (tPresent)origPresentVtbl;
                WriteLogFmt("[D3D9] dummy vtable Present patched orig=0x%08X (using=%s)",
                            (DWORD)(uintptr_t)origPresentVtbl,
                            oPresent ? "inline_tramp" : (g_LiveDevicePresent ? "vtbl_orig" : "none"));
            }
            if (PatchLiveDeviceVTableEntry(dummyVtable, 42, (void *)hkEndScene, &origEndSceneVtbl))
            {
                if (!oEndScene && origEndSceneVtbl && origEndSceneVtbl != (void *)hkEndScene)
                    oEndScene = (tEndScene)origEndSceneVtbl;
                WriteLogFmt("[D3D9] dummy vtable EndScene patched orig=0x%08X", (DWORD)(uintptr_t)origEndSceneVtbl);
            }

            // Also patch Reset (index 16) on the dummy vtable
            void *origResetVtbl = nullptr;
            if (PatchLiveDeviceVTableEntry(dummyVtable, 16, (void *)hkReset, &origResetVtbl))
            {
                if (!oReset && origResetVtbl && origResetVtbl != (void *)hkReset)
                    oReset = (tReset)origResetVtbl;
                WriteLogFmt("[D3D9] dummy vtable Reset patched orig=0x%08X", (DWORD)(uintptr_t)origResetVtbl);
            }
        }
    }

    pDev->Release();
    pD3D->Release();
    DestroyWindow(hWnd);
    UnregisterClassA("SSWDummy", wc.hInstance);
    return true;
}

// ============================================================================
// D3D8 兼容层 — 运行时检测 + D3D8 Present hook + shared ImGui panel
// ============================================================================

// D3D8 function pointer types (用 void* 代替 IDirect3DDevice8* 因为不包含 d3d8.h)
typedef HRESULT(__stdcall *tD3D8Present)(void *pDevice8, const RECT *, const RECT *, HWND, const RGNDATA *);
typedef HRESULT(__stdcall *tD3D8Reset)(void *pDevice8, void *pPresentationParameters);
static tD3D8Present oD3D8Present = nullptr;
static tD3D8Reset oD3D8Reset = nullptr;

static bool TryInstallD3D8Rel32ThunkHook(BYTE *entry, void *hookFunc, void **outOriginal, const char *tag)
{
    if (!entry || !hookFunc || !outOriginal)
        return false;
    if (SafeIsBadReadPtr(entry, 5))
        return false;
    if (entry[0] != 0xE9)
        return false;

    BYTE *resolved = FollowJmpChain(entry);
    if (!resolved || resolved == entry)
        return false;

    DWORD oldProtect = 0;
    if (!VirtualProtect(entry, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    *outOriginal = (void *)resolved;
    entry[0] = 0xE9;
    *(DWORD *)(entry + 1) = (DWORD)(uintptr_t)hookFunc - (DWORD)(uintptr_t)entry - 5;
    VirtualProtect(entry, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), entry, 5);

    WriteLogFmt("[D3D8] %s rel32 thunk patched entry=0x%08X resolved=0x%08X",
                tag ? tag : "hook",
                (DWORD)(uintptr_t)entry,
                (DWORD)(uintptr_t)resolved);
    return true;
}

// D3D8 Present hook — 直接在游戏 D3D8 设备上渲染 overlay
static HRESULT __stdcall hkD3D8Present(void *pDevice8,
                                       const RECT *pSourceRect, const RECT *pDestRect,
                                       HWND hDestWindowOverride, const RGNDATA *pDirtyRegion)
{
    const char *d3d8Stage = "begin";
    __try
    {
        d3d8Stage = "resolve_hwnd";
        if (!g_D3D8GameHwnd)
        {
            HWND hwnd = hDestWindowOverride;
            if (!hwnd)
                hwnd = g_GameHwnd;
            if (!hwnd)
            {
                DWORD pid = GetCurrentProcessId();
                struct FindCtx
                {
                    DWORD pid;
                    HWND result;
                };
                FindCtx ctx = {pid, nullptr};
                EnumWindows([](HWND h, LPARAM lp) -> BOOL
                            {
                    FindCtx* c = (FindCtx*)lp;
                    DWORD wp;
                    GetWindowThreadProcessId(h, &wp);
                    if (wp == c->pid && IsWindowVisible(h)) {
                        c->result = h;
                        return FALSE;
                    }
                    return TRUE; }, (LPARAM)&ctx);
                hwnd = ctx.result;
            }
            if (hwnd)
            {
                g_D3D8GameHwnd = hwnd;
                if (!g_GameHwnd)
                    g_GameHwnd = hwnd;
                WriteLogFmt("[D3D8] first Present: hwnd=0x%08X", (DWORD)(uintptr_t)hwnd);
            }
        }

        d3d8Stage = "observe_skillwnd";
        bool wasReady = g_Ready;
        uintptr_t swGlobal = 0;
        if (!SafeIsBadReadPtr((void *)ADDR_SkillWndEx, 4))
        {
            swGlobal = *(uintptr_t *)ADDR_SkillWndEx;
        }
        OnSkillWndPointerObserved(swGlobal, "d3d8_present");

        if (!wasReady && g_Ready && g_SkillWndThis)
        {
            WriteLogFmt("[D3D8-Present] SkillWnd: 0x%08X", (DWORD)g_SkillWndThis);
            WriteLog("[D3D8-Present] Ready");
        }

        d3d8Stage = "retry_native_button";
        if (g_Ready && g_SkillWndThis && !g_NativeBtnCreated)
        {
            static DWORD s_lastD3D8PresentButtonRetryTick = 0;
            DWORD now = GetTickCount();
            if (now - s_lastD3D8PresentButtonRetryTick > 500)
            {
                s_lastD3D8PresentButtonRetryTick = now;
                WriteLogFmt("[D3D8-Present] retry create native button hookInstalled=%d", oSkillWndInitChildren ? 1 : 0);
                if (CreateSuperButton(g_SkillWndThis))
                    WriteLog("[D3D8-Present] retry native button OK");
                else
                    WriteLog("[D3D8-Present] retry native button FAILED");
            }
        }

        d3d8Stage = "overlay_present";
        if (ENABLE_IMGUI_OVERLAY_PANEL)
        {
            const bool hasIndependentBuffOverlay = SkillOverlayBridgeHasIndependentBuffOverlayEntries();
            const bool overlayActivationReady =
                (g_Ready || hasIndependentBuffOverlay) &&
                g_D3D8GameHwnd &&
                pDevice8;

            d3d8Stage = "ensure_d3d8_textures";
            if (g_Ready && g_NativeBtnCreated)
                EnsureD3D8SuperTexturesLoaded(pDevice8);

            d3d8Stage = "overlay_init";
            if (overlayActivationReady && !SuperD3D8OverlayIsInitialized() && g_D3D8GameHwnd && pDevice8)
            {
                if (!SuperD3D8OverlayEnsureInitialized(g_D3D8GameHwnd, pDevice8, 1.0f, IMGUI_PANEL_ASSET_PATH))
                {
                    static DWORD s_lastD3D8OverlayInitFailLogTick = 0;
                    DWORD now = GetTickCount();
                    if (now - s_lastD3D8OverlayInitFailLogTick > 1000)
                    {
                        WriteLogFmt("[D3D8ImGuiOverlay] ensure init failed in Present device=0x%08X hwnd=0x%08X",
                                    (DWORD)(uintptr_t)pDevice8, (DWORD)(uintptr_t)g_D3D8GameHwnd);
                        s_lastD3D8OverlayInitFailLogTick = now;
                    }
                }
            }

            d3d8Stage = "overlay_render";
            if (SuperD3D8OverlayIsInitialized())
            {
                RECT superBtnRect = {};
                const bool hasSuperBtnRect = GetSuperButtonBaseRectForD3D(&superBtnRect);
                SuperD3D8OverlaySetPanelExpanded(g_SuperExpanded);
                SuperD3D8OverlaySetSuperButtonVisible(hasSuperBtnRect);
                SuperD3D8OverlaySetSuperButtonRect(hasSuperBtnRect ? &superBtnRect : nullptr);
            }
            if (overlayActivationReady && SuperD3D8OverlayIsInitialized())
            {
                if (g_SuperExpanded)
                    UpdateSuperCWnd();
                SuperD3D8OverlaySetVisible(true);
                SuperD3D8OverlayRender(pDevice8);
            }
            else if (SuperD3D8OverlayIsInitialized())
            {
                SuperD3D8OverlaySetVisible(false);
            }

            d3d8Stage = "mouse_suppress";
            const bool suppressMouse = SuperD3D8OverlayShouldSuppressGameMouse();
            if (Win32InputSpoofIsInstalled())
            {
                Win32InputSpoofSetSuppressMouse(suppressMouse);
            }
            UpdateGameMouseSuppressionFallback(suppressMouse);
            if (g_LastOverlaySuppressMouse && !suppressMouse)
            {
                RefreshGameCursorImmediately();
            }
            g_LastOverlaySuppressMouse = suppressMouse;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        WriteLogFmt("[D3D8] EXCEPTION in Present stage=%s code=0x%08X",
                    d3d8Stage ? d3d8Stage : "unknown",
                    GetExceptionCode());
        ResetSuperBtnD3DInteractionState();
        g_LastOverlaySuppressMouse = false;
        if (ENABLE_IMGUI_OVERLAY_PANEL && SuperD3D8OverlayIsInitialized())
        {
            SuperD3D8OverlaySetVisible(false);
        }
    }

    HRESULT hr = D3D_OK;
    __try
    {
        d3d8Stage = "call_orig_present";
        hr = oD3D8Present ? oD3D8Present(pDevice8, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion) : D3D_OK;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        WriteLogFmt("[D3D8] EXCEPTION in Present stage=%s code=0x%08X",
                    d3d8Stage ? d3d8Stage : "call_orig_present",
                    GetExceptionCode());
        hr = D3D_OK;
    }
    SkillOverlayBridgeBeginFrameObservation();
    return hr;
}

// D3D8 Reset hook — 释放 D3D8 纹理
static HRESULT __stdcall hkD3D8Reset(void *pDevice8, void *pPresentationParameters)
{
    WriteLog("[D3D8] Reset called, releasing D3D8 textures");
    if (ENABLE_IMGUI_OVERLAY_PANEL)
    {
        SuperD3D8OverlayOnDeviceLost();
    }

    HRESULT hr = oD3D8Reset ? oD3D8Reset(pDevice8, pPresentationParameters) : D3DERR_INVALIDCALL;

    if (SUCCEEDED(hr))
    {
        if (ENABLE_IMGUI_OVERLAY_PANEL)
        {
            SuperD3D8OverlayOnDeviceReset(pDevice8);
        }
        WriteLog("[D3D8] Reset OK, overlay device objects refreshed");
    }
    else
    {
        WriteLogFmt("[D3D8] Reset FAIL hr=0x%08X", (DWORD)hr);
    }
    return hr;
}

// 安装 D3D8 hooks
static bool SetupD3D8Hook()
{
    WriteLog("[D3D8] Setup...");

    HMODULE hD3D8 = ::GetModuleHandleA("d3d8.dll");
    if (!hD3D8)
    {
        WriteLog("[D3D8] FAIL: d3d8.dll not loaded");
        return false;
    }

    typedef void *(__stdcall * tDirect3DCreate8)(UINT);
    tDirect3DCreate8 pfnCreate8 = (tDirect3DCreate8)::GetProcAddress(hD3D8, "Direct3DCreate8");
    if (!pfnCreate8)
    {
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
    void *pD3D8 = pfnCreate8(220);
    if (!pD3D8)
    {
        WriteLog("[D3D8] FAIL: Direct3DCreate8 returned null");
        DestroyWindow(hWnd);
        UnregisterClassA("SSWDummyD3D8", wc.hInstance);
        return false;
    }

    // IDirect3D8::CreateDevice = vtable[15]
    // IDirect3D8 vtable: 0=QI, 1=AddRef, 2=Release, ..., 15=CreateDevice
    DWORD *vtableD3D8 = *(DWORD **)pD3D8;

    // 查询当前显示模式以获取有效的 BackBufferFormat
    // IDirect3D8::GetAdapterDisplayMode = vtable[8]
    // HRESULT GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE* pMode)
    // D3DDISPLAYMODE: { Width(4), Height(4), RefreshRate(4), Format(4) } = 16 bytes
    typedef HRESULT(__stdcall * tGetAdapterDisplayMode8)(void *, UINT, void *);
    tGetAdapterDisplayMode8 pfnGetDisplayMode = (tGetAdapterDisplayMode8)vtableD3D8[8];
    BYTE displayMode[16] = {};
    DWORD backBufferFormat = 22; // D3DFMT_X8R8G8B8 fallback
    if (pfnGetDisplayMode)
    {
        HRESULT hrDM = pfnGetDisplayMode(pD3D8, 0, displayMode);
        if (SUCCEEDED(hrDM))
        {
            DWORD dmFormat = *(DWORD *)(displayMode + 12); // Format at offset 12
            if (dmFormat != 0)
            {
                backBufferFormat = dmFormat;
                WriteLogFmt("[D3D8] display mode: %dx%d fmt=%d",
                            *(DWORD *)(displayMode + 0), *(DWORD *)(displayMode + 4), dmFormat);
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
    *(DWORD *)(d3dpp8 + 0x08) = backBufferFormat; // BackBufferFormat — 必须是有效格式
    *(DWORD *)(d3dpp8 + 0x14) = 1;                // SwapEffect = D3DSWAPEFFECT_DISCARD
    *(HWND *)(d3dpp8 + 0x18) = hWnd;              // hDeviceWindow
    *(DWORD *)(d3dpp8 + 0x1C) = 1;                // Windowed = TRUE

    // 调用 IDirect3D8::CreateDevice (vtable[15])
    // HRESULT CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
    //                      DWORD BehaviorFlags, D3DPRESENT_PARAMETERS8* pPP, IDirect3DDevice8** ppDevice)
    typedef HRESULT(__stdcall * tD3D8CreateDevice)(void *pD3D8, UINT, DWORD, HWND, DWORD, void *, void **);
    tD3D8CreateDevice pfnCreateDevice8 = (tD3D8CreateDevice)vtableD3D8[15];

    void *pDummyDevice8 = nullptr;
    HRESULT hr = pfnCreateDevice8(pD3D8, 0, 1 /*D3DDEVTYPE_HAL*/, hWnd,
                                  0x20 /*D3DCREATE_SOFTWARE_VERTEXPROCESSING*/, d3dpp8, &pDummyDevice8);

    if (FAILED(hr) || !pDummyDevice8)
    {
        WriteLogFmt("[D3D8] dummy device creation failed hr=0x%08X, trying NULLREF", (DWORD)hr);
        // D3D8 没有 D3DDEVTYPE_NULLREF，尝试 REF (2)
        hr = pfnCreateDevice8(pD3D8, 0, 2 /*D3DDEVTYPE_REF*/, hWnd,
                              0x20, d3dpp8, &pDummyDevice8);
    }

    if (FAILED(hr) || !pDummyDevice8)
    {
        WriteLogFmt("[D3D8] FAIL: all device creation attempts failed hr=0x%08X", (DWORD)hr);
        // 释放 IDirect3D8 (vtable[2] = Release)
        typedef ULONG(__stdcall * tRelease)(void *);
        ((tRelease)vtableD3D8[2])(pD3D8);
        DestroyWindow(hWnd);
        UnregisterClassA("SSWDummyD3D8", wc.hInstance);
        return false;
    }

    // 从 dummy device 获取 vtable
    DWORD *vtableDevice8 = *(DWORD **)pDummyDevice8;
    // IDirect3DDevice8 vtable indices:
    //   14 = Reset
    //   15 = Present
    DWORD presentAddr = vtableDevice8[15];
    DWORD resetAddr = vtableDevice8[14];

    WriteLogFmt("[D3D8] device vtable=0x%08X Present=[%d]=0x%08X Reset=[%d]=0x%08X",
                (DWORD)(uintptr_t)vtableDevice8, 15, presentAddr, 14, resetAddr);

    // Inline hook D3D8 Present
    BYTE *pPresentEntry8 = (BYTE *)(uintptr_t)presentAddr;
    BYTE *pPresent8 = FollowJmpChain((void *)presentAddr);
    if (TryInstallD3D8Rel32ThunkHook(pPresentEntry8, (void *)hkD3D8Present, (void **)&oD3D8Present, "Present"))
    {
        WriteLogFmt("[D3D8] Present thunk-entry hooked entry=0x%08X orig=0x%08X",
                    (DWORD)(uintptr_t)pPresentEntry8,
                    (DWORD)(uintptr_t)oD3D8Present);
    }
    else
    {
        int copyLenPresent = CalcMinCopyLen(pPresent8);
        if (copyLenPresent < 5)
            copyLenPresent = 5;
        oD3D8Present = (tD3D8Present)GenericInlineHook5(pPresent8, (void *)hkD3D8Present, copyLenPresent);
        if (!oD3D8Present)
        {
            WriteLog("[D3D8] Present inline hook failed, trying vtable patch");
            // Fallback: vtable patch
            DWORD oldProt;
            VirtualProtect(&vtableDevice8[15], sizeof(void *), PAGE_READWRITE, &oldProt);
            oD3D8Present = (tD3D8Present)vtableDevice8[15];
            vtableDevice8[15] = (DWORD)(uintptr_t)hkD3D8Present;
            VirtualProtect(&vtableDevice8[15], sizeof(void *), oldProt, &oldProt);
            WriteLogFmt("[D3D8] Present vtable patched, orig=0x%08X", (DWORD)(uintptr_t)oD3D8Present);
        }
        else
        {
            WriteLogFmt("[D3D8] Present inline hooked at 0x%08X tramp=0x%08X",
                        (DWORD)(uintptr_t)pPresent8, (DWORD)(uintptr_t)oD3D8Present);
        }
    }

    // Inline hook D3D8 Reset
    BYTE *pResetEntry8 = (BYTE *)(uintptr_t)resetAddr;
    BYTE *pReset8 = FollowJmpChain((void *)resetAddr);
    if (TryInstallD3D8Rel32ThunkHook(pResetEntry8, (void *)hkD3D8Reset, (void **)&oD3D8Reset, "Reset"))
    {
        WriteLogFmt("[D3D8] Reset thunk-entry hooked entry=0x%08X orig=0x%08X",
                    (DWORD)(uintptr_t)pResetEntry8,
                    (DWORD)(uintptr_t)oD3D8Reset);
    }
    else
    {
        int copyLenReset = CalcMinCopyLen(pReset8);
        if (copyLenReset < 5)
            copyLenReset = 5;
        oD3D8Reset = (tD3D8Reset)GenericInlineHook5(pReset8, (void *)hkD3D8Reset, copyLenReset);
        if (!oD3D8Reset)
        {
            WriteLog("[D3D8] Reset inline hook failed, trying vtable patch");
            DWORD oldProt;
            VirtualProtect(&vtableDevice8[14], sizeof(void *), PAGE_READWRITE, &oldProt);
            oD3D8Reset = (tD3D8Reset)vtableDevice8[14];
            vtableDevice8[14] = (DWORD)(uintptr_t)hkD3D8Reset;
            VirtualProtect(&vtableDevice8[14], sizeof(void *), oldProt, &oldProt);
            WriteLogFmt("[D3D8] Reset vtable patched, orig=0x%08X", (DWORD)(uintptr_t)oD3D8Reset);
        }
        else
        {
            WriteLogFmt("[D3D8] Reset inline hooked at 0x%08X tramp=0x%08X",
                        (DWORD)(uintptr_t)pReset8, (DWORD)(uintptr_t)oD3D8Reset);
        }
    }

    // 释放 dummy 设备和 IDirect3D8
    typedef ULONG(__stdcall * tRelease)(void *);
    ((tRelease)(*(DWORD **)pDummyDevice8)[2])(pDummyDevice8);
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
        ADDR_9E17D0, (void *)hkSkillWndInitChildren);
    if (!oSkillWndInitChildren)
    {
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
        ADDR_9DDB30, (void *)hkSkillWndMsgNaked);
    if (!oSkillWndMsg)
    {
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
    bool sendHookOk = false;
    bool recvHookOk = false;
    SkillOverlayBridgeSetResetPreviewReceiveHookReady(false);

    BYTE *pTarget = FollowJmpChain((void *)ADDR_43D94D);
    if (!pTarget)
    {
        WriteLog("[PacketHook] target missing");
    }
    else if (pTarget[0] != 0xE8)
    {
        WriteLogFmt("[PacketHook] unexpected prologue at 43D94D: opcode=0x%02X", pTarget[0]);
    }
    else
    {
        g_SendPacketOriginalCallTarget = (DWORD)(uintptr_t)(pTarget + 5 + *(int *)(pTarget + 1));
        oSendPacket = pTarget + 5;

        DWORD oldProtect = 0;
        if (!VirtualProtect(pTarget, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            WriteLog("[PacketHook] VirtualProtect failed");
            g_SendPacketOriginalCallTarget = 0;
            oSendPacket = nullptr;
        }
        else
        {
            pTarget[0] = 0xE9;
            *(int *)(pTarget + 1) = (int)((uintptr_t)hkSendPacketNaked - (uintptr_t)pTarget - 5);
            VirtualProtect(pTarget, 5, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), pTarget, 5);

            sendHookOk = true;
            WriteLogFmt("[PacketHook] OK(43D94D): originalCall=0x%08X continue=0x%08X",
                        g_SendPacketOriginalCallTarget,
                        (DWORD)(uintptr_t)oSendPacket);
        }
    }

    BYTE *pRecvEntry = (BYTE *)ADDR_4D6A13;
    BYTE *pRecvTarget = FollowJmpChain((void *)ADDR_4D6A13);
    bool recvUsesExternalStubEntry = false;
    if (pRecvTarget == pRecvEntry)
    {
        BYTE *pRecvStubTarget = TryFollowAbsoluteRegisterJumpStub(pRecvEntry);
        if (pRecvStubTarget)
        {
            pRecvTarget = pRecvStubTarget;
            recvUsesExternalStubEntry = true;
            WriteLogFmt("[PacketHook] recv external stub detected entry=0x%08X -> target=0x%08X",
                        (DWORD)(uintptr_t)pRecvEntry,
                        (DWORD)(uintptr_t)pRecvTarget);
            const uintptr_t potentialIncreaseAddress = TryExtractPotentialIncreaseAddressFromRecvStub(pRecvStubTarget);
            if (potentialIncreaseAddress)
            {
                g_ExternalPotentialIncreaseAddressRuntime = potentialIncreaseAddress;
                SkillOverlayBridgeSetPotentialIncreaseAddress(potentialIncreaseAddress);
                WriteLogFmt("[PacketHook] recv external stub potentialIncrease=0x%08X",
                            (DWORD)potentialIncreaseAddress);
                PatchExternalPotentialIncreaseStub(pRecvStubTarget);
            }
        }
    }

    if (!pRecvTarget)
    {
        WriteLog("[PacketHook] recv target missing");
    }
    else if (!MatchesRecvPacketDirectPrologue(pRecvTarget))
    {
        BYTE *pRecvFallbackTarget = recvUsesExternalStubEntry ? pRecvEntry : pRecvTarget;
        if (recvUsesExternalStubEntry)
        {
            WriteLogFmt("[PacketHook] recv stub target keeps custom logic: %02X %02X %02X, fallback hook on entry=0x%08X",
                        pRecvTarget[0],
                        pRecvTarget[1],
                        pRecvTarget[2],
                        (DWORD)(uintptr_t)pRecvFallbackTarget);
        }
        else
        {
            WriteLogFmt("[PacketHook] unexpected recv prologue at 4D6A13: %02X %02X %02X",
                        pRecvTarget[0],
                        pRecvTarget[1],
                        pRecvTarget[2]);
        }
        const size_t recvCopyLen = CalculateRelocatedByteCount(pRecvFallbackTarget, 5);
        if (recvCopyLen == 0)
        {
            WriteLog("[PacketHook] recv fallback copy length resolve failed");
        }
        else
        {
            oRecvPacket = GenericInlineHook5(pRecvFallbackTarget, (void *)hkRecvPacketNakedFallback, (int)recvCopyLen);
            if (!oRecvPacket)
            {
                WriteLog("[PacketHook] recv fallback hook failed");
            }
            else
            {
                recvHookOk = true;
                WriteLogFmt("[PacketHook] fallback recv hook OK target=0x%08X tramp=0x%08X copyLen=%u",
                            (DWORD)(uintptr_t)pRecvFallbackTarget,
                            (DWORD)(uintptr_t)oRecvPacket,
                            (unsigned int)recvCopyLen);
            }
        }
    }
    else
    {
        oRecvPacket = pRecvTarget + 9;

        DWORD oldProtect = 0;
        if (!VirtualProtect(pRecvTarget, 9, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            WriteLog("[PacketHook] recv VirtualProtect failed");
            oRecvPacket = nullptr;
        }
        else
        {
            pRecvTarget[0] = 0xE9;
            *(int *)(pRecvTarget + 1) = (int)((uintptr_t)hkRecvPacketNaked - (uintptr_t)pRecvTarget - 5);
            for (int i = 5; i < 9; ++i)
                pRecvTarget[i] = 0x90;
            VirtualProtect(pRecvTarget, 9, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), pRecvTarget, 9);

            recvHookOk = true;
            WriteLogFmt("[PacketHook] OK(4D6A13 recv): continue=0x%08X",
                        (DWORD)(uintptr_t)oRecvPacket);
        }
    }

    bool localHookAnyOk = false;
    if (SetupLocalIndependentPotentialPrimaryFlatStatHook())
        localHookAnyOk = true;
    if (SetupLocalIndependentPotentialPrimaryPercentStatHook())
        localHookAnyOk = true;
    if (SetupLocalIndependentPotentialFlatStatHook())
        localHookAnyOk = true;
    if (SetupLocalIndependentPotentialDisplayFunctionHooks())
        localHookAnyOk = true;
    if (SetupAbilityRedHashContainerHooks())
        localHookAnyOk = true;
    if (SetupAbilityRedExtendedAggregateHook())
        localHookAnyOk = true;
    if (kEnableMountMovementAbilityRedHooks)
    {
        if (SetupAbilityRedMasterAggregateHook())
            localHookAnyOk = true;
    }
    else
    {
        WriteLog("[AbilityRedMaster] 856C60 hook disabled for mount movement rollback");
    }
    if (kEnableGlobalMovementSetterProtectionHooks)
    {
        if (SetupAbilityRedMovementSetterHooks())
            localHookAnyOk = true;
    }
    else
    {
        WriteLog("[MoveSetter] movement setter protection disabled");
    }
    if (kEnableGlobalMovementOutputClampHook)
    {
        if (SetupMovementOutputClampHook())
            localHookAnyOk = true;
    }
    else
    {
        WriteLog("[MoveClamp] movement output clamp hook disabled");
    }
    if (SetupAbilityRedSiblingCalcHooks())
        localHookAnyOk = true;
    if (SetupAbilityRedDiff84C470PreSubHook())
        localHookAnyOk = true;
    if (SetupAbilityRedAdditionalDiffHooks())
        localHookAnyOk = true;
    if (SetupAbilityRedPositiveStyleHooks())
        localHookAnyOk = true;
    if (SetupAbilityRedBakeWriteHooks())
        localHookAnyOk = true;
    if (SetupAbilityRedBake198Hooks())
        localHookAnyOk = true;
    if (SetupAbilityRedFinalValueHooks())
        localHookAnyOk = true;
    if (SetupAbilityRedDisplayCandidateHook())
        localHookAnyOk = true;
    if (SetupAbilityRedDisplayCallsiteHook())
        localHookAnyOk = true;
    if (SetupAbilityRedLevelReadHook())
        localHookAnyOk = true;
    if (SetupAbilityRedSkillWriteHooks())
        localHookAnyOk = true;

    WriteLogFmt("[IndependentBuffLocal] local read-point hooks active=%d mode=recv_plus_readpoint",
        localHookAnyOk ? 1 : 0);
    if (!SetupPotentialTextDisplayHook())
        WriteLog("[PotentialTextHook] display text hook install failed");
    if (!SetupSurfaceDrawImageObservationHook())
        WriteLog("[ObservedSceneFade] surface draw observation hook install failed");
    if (!SetupNativeCursorStateHook())
        WriteLog("[ObservedCursorState] 5F3EC0 hook install failed");

    SkillOverlayBridgeSetResetPreviewReceiveHookReady(sendHookOk && recvHookOk);
    if (!SetupStatusBarBuffSlotHooks())
        WriteLog("[StatusBarBuffSlot] slot refresh hook install failed");
    return sendHookOk && recvHookOk;
}

static bool SetupStatusBarBuffSlotHooks()
{
    if (g_StatusBarBuffSlotHooksInstalled)
        return true;

    bool ok = false;

    auto calcSafeCopyLen = [](BYTE* target) -> int
    {
        if (!target)
            return 0;
        int copyLen = CalcMinCopyLen(target);
        if (copyLen < 5)
            copyLen = 5;
        return copyLen;
    };

    BYTE* pPrimary = FollowJmpChain((void*)ADDR_StatusBarRefreshSlotsPrimary);
    if (pPrimary)
    {
        const int copyLen = calcSafeCopyLen(pPrimary);
        oStatusBarRefreshSlotsPrimary = (tStatusBarInternalRefreshFn)GenericInlineHook5(
            pPrimary,
            (void*)hkStatusBarRefreshSlotsPrimaryNaked,
            copyLen);
        if (oStatusBarRefreshSlotsPrimary)
        {
            WriteLogFmt("[StatusBarBuffSlot] primary hook OK entry=0x%08X tramp=0x%08X copyLen=%d",
                (DWORD)(uintptr_t)pPrimary,
                (DWORD)(uintptr_t)oStatusBarRefreshSlotsPrimary,
                copyLen);
            ok = true;
        }
        else
        {
            WriteLog("[StatusBarBuffSlot] primary hook failed");
        }
    }

    BYTE* pSecondary = FollowJmpChain((void*)ADDR_StatusBarRefreshSlotsSecondary);
    if (pSecondary)
    {
        const int copyLen = calcSafeCopyLen(pSecondary);
        oStatusBarRefreshSlotsSecondary = (tStatusBarInternalRefreshFn)GenericInlineHook5(
            pSecondary,
            (void*)hkStatusBarRefreshSlotsSecondaryNaked,
            copyLen);
        if (oStatusBarRefreshSlotsSecondary)
        {
            WriteLogFmt("[StatusBarBuffSlot] secondary hook OK entry=0x%08X tramp=0x%08X copyLen=%d",
                (DWORD)(uintptr_t)pSecondary,
                (DWORD)(uintptr_t)oStatusBarRefreshSlotsSecondary,
                copyLen);
            ok = true;
        }
        else
        {
            WriteLog("[StatusBarBuffSlot] secondary hook failed");
        }
    }

    // 9F5FE0 begins with a relative CALL within the first copied bytes:
    //   push esi / mov esi, ecx / call 9F4F00
    // Current GenericInlineHook5 trampolines are raw memcpy and do not relocate
    // rel32 call targets, so hooking 9F5FE0 directly corrupts the trampoline.
    // We already observe the real fixed-slot refresh through 9F4F00/9F4C30, so
    // disable this risky aggregate-entry hook instead of crashing on repeated use.
    WriteLog("[StatusBarBuffSlot] internal hook disabled: 9F5FE0 rel32 call trampoline unsafe");

    BYTE* pCleanup = FollowJmpChain((void*)ADDR_StatusBarCleanupTransient);
    if (pCleanup)
    {
        const int copyLen = calcSafeCopyLen(pCleanup);
        oStatusBarCleanupTransient = (tStatusBarInternalRefreshFn)GenericInlineHook5(
            pCleanup,
            (void*)hkStatusBarCleanupTransientNaked,
            copyLen);
        if (oStatusBarCleanupTransient)
        {
            WriteLogFmt("[StatusBarBuffSlot] cleanup hook OK entry=0x%08X tramp=0x%08X copyLen=%d",
                (DWORD)(uintptr_t)pCleanup,
                (DWORD)(uintptr_t)oStatusBarCleanupTransient,
                copyLen);
            ok = true;
        }
        else
        {
            WriteLog("[StatusBarBuffSlot] cleanup hook failed");
        }
    }

    BYTE* pTransientRefresh = FollowJmpChain((void*)0x009FC110);
    if (pTransientRefresh)
    {
        const int copyLen = calcSafeCopyLen(pTransientRefresh);
        oStatusBarTransientRefresh = (tStatusBarTransientRefreshFn)GenericInlineHook5(
            pTransientRefresh,
            (void*)hkStatusBarTransientRefreshNaked,
            copyLen);
        if (oStatusBarTransientRefresh)
        {
            WriteLogFmt("[StatusBarBuffSlot] transient refresh hook OK entry=0x%08X tramp=0x%08X copyLen=%d",
                (DWORD)(uintptr_t)pTransientRefresh,
                (DWORD)(uintptr_t)oStatusBarTransientRefresh,
                copyLen);
            ok = true;
        }
        else
        {
            WriteLog("[StatusBarBuffSlot] transient refresh hook failed");
        }
    }

    BYTE* pTransientDispatch = FollowJmpChain((void*)0x009FCC10);
    if (pTransientDispatch)
    {
        const int copyLen = calcSafeCopyLen(pTransientDispatch);
        oStatusBarTransientDispatch = (tStatusBarTransientDispatchFn)GenericInlineHook5(
            pTransientDispatch,
            (void*)hkStatusBarTransientDispatchNaked,
            copyLen);
        if (oStatusBarTransientDispatch)
        {
            WriteLogFmt("[StatusBarBuffSlot] transient dispatch hook OK entry=0x%08X tramp=0x%08X copyLen=%d",
                (DWORD)(uintptr_t)pTransientDispatch,
                (DWORD)(uintptr_t)oStatusBarTransientDispatch,
                copyLen);
            ok = true;
        }
        else
        {
            WriteLog("[StatusBarBuffSlot] transient dispatch hook failed");
        }
    }

    BYTE* pTransientToggle = FollowJmpChain((void*)0x009FCBD0);
    if (pTransientToggle)
    {
        const int copyLen = calcSafeCopyLen(pTransientToggle);
        oStatusBarTransientToggle = (tStatusBarTransientToggleFn)GenericInlineHook5(
            pTransientToggle,
            (void*)hkStatusBarTransientToggleNaked,
            copyLen);
        if (oStatusBarTransientToggle)
        {
            WriteLogFmt("[StatusBarBuffSlot] transient toggle hook OK entry=0x%08X tramp=0x%08X copyLen=%d",
                (DWORD)(uintptr_t)pTransientToggle,
                (DWORD)(uintptr_t)oStatusBarTransientToggle,
                copyLen);
            ok = true;
        }
        else
        {
            WriteLog("[StatusBarBuffSlot] transient toggle hook failed");
        }
    }

    g_StatusBarBuffSlotHooksInstalled = ok;
    return ok;
}

static bool SetupSurfaceDrawImageObservationHook()
{
    if (oSurfaceDrawImageFn)
        return true;

    BYTE *pTarget = FollowJmpChain((void *)ADDR_401C90);
    if (!pTarget)
    {
        WriteLog("[ObservedSceneFade] 401C90 target missing");
        return false;
    }

    int copyLen = CalcMinCopyLen(pTarget);
    if (copyLen < 5)
        copyLen = 5;

    oSurfaceDrawImageFn = (tSurfaceDrawImageFn)GenericInlineHook5(
        pTarget,
        (void *)hkSurfaceDrawImage,
        copyLen);
    if (!oSurfaceDrawImageFn)
    {
        WriteLog("[ObservedSceneFade] 401C90 hook failed");
        return false;
    }

    WriteLogFmt("[ObservedSceneFade] 401C90 hook OK entry=0x%08X tramp=0x%08X copyLen=%d",
        (DWORD)(uintptr_t)pTarget,
        (DWORD)(uintptr_t)oSurfaceDrawImageFn,
        copyLen);
    return true;
}

static bool SetupNativeCursorStateHook()
{
    if (oNativeCursorStateSetFn)
        return true;

    BYTE* pTarget = FollowJmpChain((void*)ADDR_5F3EC0);
    if (!pTarget)
    {
        WriteLog("[ObservedCursorState] 5F3EC0 target missing");
        return false;
    }

    int copyLen = CalcMinCopyLen(pTarget);
    if (copyLen < 5)
        copyLen = 5;

    oNativeCursorStateSetFn = (tNativeCursorStateSetFn)GenericInlineHook5(
        pTarget,
        (void*)hkNativeCursorStateSetNaked,
        copyLen);
    if (!oNativeCursorStateSetFn)
    {
        WriteLog("[ObservedCursorState] 5F3EC0 hook failed");
        return false;
    }

    WriteLogFmt("[ObservedCursorState] 5F3EC0 hook OK entry=0x%08X tramp=0x%08X copyLen=%d",
        (DWORD)(uintptr_t)pTarget,
        (DWORD)(uintptr_t)oNativeCursorStateSetFn,
        copyLen);
    return true;
}

static bool SetupLocalIndependentPotentialPrimaryFlatStatHook()
{
    BYTE* pEntry = (BYTE*)ADDR_853B49;
    BYTE* pTarget = FollowJmpChain((void*)ADDR_853B49);
    if (!pTarget && pEntry)
        pTarget = TryFollowAbsoluteRegisterJumpStub(pEntry);
    if (!pTarget)
    {
        WriteLog("[IndependentBuffLocal] target missing primary");
        return false;
    }

    static const BYTE kExpected[] = {
        0x85, 0xC0,
        0x0F, 0x84, 0x9C, 0x02, 0x00, 0x00,
        0x8B, 0x7C, 0x24, 0x2C,
        0x8B, 0x56, 0x28,
        0x69, 0xFF, 0xF0, 0x00, 0x00, 0x00,
        0x03, 0x78, 0x18
    };

    if (memcmp(pTarget, kExpected, sizeof(kExpected)) != 0)
    {
        char hexDump[256] = {};
        size_t cursor = 0;
        for (size_t dumpIndex = 0; dumpIndex < 16; ++dumpIndex)
        {
            cursor += sprintf_s(hexDump + cursor, sizeof(hexDump) - cursor, "%02X%s",
                (unsigned int)pTarget[dumpIndex],
                dumpIndex + 1 < 16 ? " " : "");
            if (cursor + 4 >= sizeof(hexDump))
                break;
        }
        WriteLog("[IndependentBuffLocal] skip 853B49 unexpected prologue");
        WriteLogFmt("[IndependentBuffLocal] 853B49 bytes=%s", hexDump);
        return false;
    }

    const DWORD continueNonZero = (DWORD)(uintptr_t)(pTarget + sizeof(kExpected));
    const DWORD continueZero = *(DWORD*)(pTarget + 4) + (DWORD)(uintptr_t)(pTarget + 8);

    DWORD oldProtect = 0;
    if (!VirtualProtect(pTarget, sizeof(kExpected), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        WriteLog("[IndependentBuffLocal] VirtualProtect failed primary");
        return false;
    }

    pTarget[0] = 0xE9;
    *(int*)(pTarget + 1) = (int)((uintptr_t)hkLocalIndependentPotentialPrimaryFlatStatsNaked - (uintptr_t)pTarget - 5);
    for (size_t i = 5; i < sizeof(kExpected); ++i)
        pTarget[i] = 0x90;

    VirtualProtect(pTarget, sizeof(kExpected), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), pTarget, sizeof(kExpected));
    oLocalIndependentPotentialPrimaryFlatStats = pTarget + sizeof(kExpected);
    g_LocalIndependentPotentialPrimaryContinueNonZero = continueNonZero;
    g_LocalIndependentPotentialPrimaryContinueZero = continueZero;
    WriteLogFmt("[IndependentBuffLocal] OK(853B49): continueNonZero=0x%08X continueZero=0x%08X",
        g_LocalIndependentPotentialPrimaryContinueNonZero,
        g_LocalIndependentPotentialPrimaryContinueZero);
    return true;
}

static bool SetupLocalIndependentPotentialPrimaryPercentStatHook()
{
    BYTE* pEntry = (BYTE*)ADDR_853E5A;
    BYTE* pTarget = FollowJmpChain((void*)ADDR_853E5A);
    if (!pTarget && pEntry)
        pTarget = TryFollowAbsoluteRegisterJumpStub(pEntry);
    if (!pTarget)
    {
        WriteLog("[IndependentBuffLocal] target missing primary percent");
        return false;
    }

    static const BYTE kExpected[] = {
        0x85, 0xED,
        0x0F, 0x84, 0xB5, 0x00, 0x00, 0x00,
        0x8B, 0x74, 0x24, 0x30,
        0x8B, 0x4C, 0x24, 0x34,
        0x69, 0xF6, 0xF0, 0x00, 0x00, 0x00,
        0x03, 0x75, 0x18
    };

    if (memcmp(pTarget, kExpected, sizeof(kExpected)) != 0)
    {
        char hexDump[256] = {};
        size_t cursor = 0;
        for (size_t dumpIndex = 0; dumpIndex < 16; ++dumpIndex)
        {
            cursor += sprintf_s(hexDump + cursor, sizeof(hexDump) - cursor, "%02X%s",
                (unsigned int)pTarget[dumpIndex],
                dumpIndex + 1 < 16 ? " " : "");
            if (cursor + 4 >= sizeof(hexDump))
                break;
        }
        WriteLog("[IndependentBuffLocal] skip 853E5A unexpected prologue");
        WriteLogFmt("[IndependentBuffLocal] 853E5A bytes=%s", hexDump);
        return false;
    }

    const DWORD continueNonZero = (DWORD)(uintptr_t)(pTarget + sizeof(kExpected));
    const DWORD continueZero = *(DWORD*)(pTarget + 4) + (DWORD)(uintptr_t)(pTarget + 8);

    DWORD oldProtect = 0;
    if (!VirtualProtect(pTarget, sizeof(kExpected), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        WriteLog("[IndependentBuffLocal] VirtualProtect failed primary percent");
        return false;
    }

    pTarget[0] = 0xE9;
    *(int*)(pTarget + 1) = (int)((uintptr_t)hkLocalIndependentPotentialPrimaryPercentStatsNaked - (uintptr_t)pTarget - 5);
    for (size_t i = 5; i < sizeof(kExpected); ++i)
        pTarget[i] = 0x90;

    VirtualProtect(pTarget, sizeof(kExpected), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), pTarget, sizeof(kExpected));
    oLocalIndependentPotentialPrimaryPercentStats = pTarget + sizeof(kExpected);
    g_LocalIndependentPotentialPrimaryPercentContinueNonZero = continueNonZero;
    g_LocalIndependentPotentialPrimaryPercentContinueZero = continueZero;
    WriteLogFmt("[IndependentBuffLocal] OK(853E5A): continueNonZero=0x%08X continueZero=0x%08X",
        g_LocalIndependentPotentialPrimaryPercentContinueNonZero,
        g_LocalIndependentPotentialPrimaryPercentContinueZero);
    return true;
}

static bool SetupLocalIndependentPotentialFlatStatHook()
{
    BYTE* pEntry = (BYTE*)ADDR_856879;
    BYTE* pTarget = FollowJmpChain((void*)ADDR_856879);
    if (!pTarget && pEntry)
        pTarget = TryFollowAbsoluteRegisterJumpStub(pEntry);
    if (!pTarget)
    {
        WriteLog("[IndependentBuffLocal] target missing");
        return false;
    }

    if (pTarget[0] >= 0xB8 && pTarget[0] <= 0xBF)
    {
        BYTE* pResolvedStub = TryFollowAbsoluteRegisterJumpStub(pTarget);
        if (pResolvedStub)
        {
            WriteLogFmt("[IndependentBuffLocal] resolved absolute jump stub 0x%08X -> 0x%08X",
                (DWORD)(uintptr_t)pTarget,
                (DWORD)(uintptr_t)pResolvedStub);
            pTarget = pResolvedStub;
        }
    }

    static const BYTE kExpectedLong[] = {
        0x85, 0xC0,
        0x0F, 0x84, 0xFB, 0x02, 0x00, 0x00,
        0x8B, 0x7C, 0x24, 0x2C,
        0x69, 0xFF, 0xF0, 0x00, 0x00, 0x00,
        0x03, 0x78, 0x18
    };
    static const BYTE kExpectedShort[] = {
        0x85, 0xC0,
        0x74, 0x0D,
        0x8B, 0x7C, 0x24, 0x2C,
        0x69, 0xFF, 0xF0, 0x00, 0x00, 0x00,
        0x03, 0x78, 0x18
    };

    const BYTE* expected = nullptr;
    size_t expectedLen = 0;
    DWORD continueNonZero = 0;
    DWORD continueZero = 0;

    if (memcmp(pTarget, kExpectedLong, sizeof(kExpectedLong)) == 0)
    {
        expected = kExpectedLong;
        expectedLen = sizeof(kExpectedLong);
        continueNonZero = (DWORD)(uintptr_t)(pTarget + expectedLen);
        continueZero = *(DWORD*)(pTarget + 4) + (DWORD)(uintptr_t)(pTarget + 8);
    }
    else if (memcmp(pTarget, kExpectedShort, sizeof(kExpectedShort)) == 0)
    {
        expected = kExpectedShort;
        expectedLen = sizeof(kExpectedShort);
        continueNonZero = (DWORD)(uintptr_t)(pTarget + expectedLen);
        continueZero = (DWORD)(uintptr_t)(pTarget + expectedLen);
        WriteLogFmt("[IndependentBuffLocal] using short-branch variant target=0x%08X continue=0x%08X",
            (DWORD)(uintptr_t)pTarget,
            continueNonZero);
    }
    else
    {
        char hexDump[256] = {};
        size_t cursor = 0;
        for (size_t dumpIndex = 0; dumpIndex < 16; ++dumpIndex)
        {
            cursor += sprintf_s(hexDump + cursor, sizeof(hexDump) - cursor, "%02X%s",
                (unsigned int)pTarget[dumpIndex],
                dumpIndex + 1 < 16 ? " " : "");
            if (cursor + 4 >= sizeof(hexDump))
                break;
        }
        WriteLogFmt("[IndependentBuffLocal] skip 856879 unexpected prologue");
        WriteLogFmt("[IndependentBuffLocal] 856879 bytes=%s", hexDump);
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(pTarget, expectedLen, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        WriteLog("[IndependentBuffLocal] VirtualProtect failed");
        return false;
    }

    pTarget[0] = 0xE9;
    *(int*)(pTarget + 1) = (int)((uintptr_t)hkLocalIndependentPotentialFlatStatsNaked - (uintptr_t)pTarget - 5);
    for (size_t i = 5; i < expectedLen; ++i)
        pTarget[i] = 0x90;

    VirtualProtect(pTarget, expectedLen, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), pTarget, expectedLen);
    oLocalIndependentPotentialFlatStats = pTarget + expectedLen;
    g_LocalIndependentPotentialContinueNonZero = continueNonZero;
    g_LocalIndependentPotentialContinueZero = continueZero;
    WriteLogFmt("[IndependentBuffLocal] OK(856879): continueNonZero=0x%08X continueZero=0x%08X",
        g_LocalIndependentPotentialContinueNonZero,
        g_LocalIndependentPotentialContinueZero);
    return true;
}

static bool SetupLocalIndependentPotentialDisplayFunctionHooks()
{
    bool anyOk = false;

    if (!oLocalIndependentPotentialSkillLevelDisplayFn)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_AE0A70);
        if (pTarget)
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;
            oLocalIndependentPotentialSkillLevelDisplayFn =
                (tLocalIndependentPotentialSkillLevelDisplayFn)GenericInlineHook5(
                    pTarget,
                    (void *)hkLocalIndependentPotentialSkillLevelDisplayFunction,
                    copyLen);
            if (oLocalIndependentPotentialSkillLevelDisplayFn)
            {
                anyOk = true;
                WriteLogFmt("[IndependentBuffLocalDisplay] OK(AE0A70): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oLocalIndependentPotentialSkillLevelDisplayFn,
                    copyLen);
            }
            else
            {
                WriteLog("[IndependentBuffLocalDisplay] AE0A70 hook failed");
            }
        }
        else
        {
            WriteLog("[IndependentBuffLocalDisplay] AE0A70 target missing");
        }
    }
    else
    {
        anyOk = true;
    }

    if (!oLocalIndependentPotentialPercentQuadDisplayFn)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_8538C0);
        if (pTarget)
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;
            oLocalIndependentPotentialPercentQuadDisplayFn =
                (tLocalIndependentPotentialPercentQuadDisplayFn)GenericInlineHook5(
                    pTarget,
                    (void *)hkLocalIndependentPotentialPercentQuadDisplayFunction,
                    copyLen);
            if (oLocalIndependentPotentialPercentQuadDisplayFn)
            {
                anyOk = true;
                WriteLogFmt("[IndependentBuffLocalDisplay] OK(8538C0): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oLocalIndependentPotentialPercentQuadDisplayFn,
                    copyLen);
            }
            else
            {
                WriteLog("[IndependentBuffLocalDisplay] 8538C0 hook failed");
            }
        }
        else
        {
            WriteLog("[IndependentBuffLocalDisplay] 8538C0 target missing");
        }
    }
    else
    {
        anyOk = true;
    }

    if (!oLocalIndependentPotentialPercentFullDisplayFn)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_853E10);
        if (pTarget)
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;
            oLocalIndependentPotentialPercentFullDisplayFn =
                (tLocalIndependentPotentialPercentFullDisplayFn)GenericInlineHook5(
                    pTarget,
                    (void *)hkLocalIndependentPotentialPercentFullDisplayFunction,
                    copyLen);
            if (oLocalIndependentPotentialPercentFullDisplayFn)
            {
                anyOk = true;
                WriteLogFmt("[IndependentBuffLocalDisplay] OK(853E10): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oLocalIndependentPotentialPercentFullDisplayFn,
                    copyLen);
            }
            else
            {
                WriteLog("[IndependentBuffLocalDisplay] 853E10 hook failed");
            }
        }
        else
        {
            WriteLog("[IndependentBuffLocalDisplay] 853E10 target missing");
        }
    }
    else
    {
        anyOk = true;
    }

    if (!oLocalIndependentPotentialFlatBasicDisplayFn)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_853B00);
        if (pTarget)
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;
            oLocalIndependentPotentialFlatBasicDisplayFn =
                (tLocalIndependentPotentialFlatBasicDisplayFn)GenericInlineHook5(
                    pTarget,
                    (void *)hkLocalIndependentPotentialFlatBasicDisplayFunction,
                    copyLen);
            if (oLocalIndependentPotentialFlatBasicDisplayFn)
            {
                anyOk = true;
                WriteLogFmt("[IndependentBuffLocalDisplay] OK(853B00): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oLocalIndependentPotentialFlatBasicDisplayFn,
                    copyLen);
            }
            else
            {
                WriteLog("[IndependentBuffLocalDisplay] 853B00 hook failed");
            }
        }
        else
        {
            WriteLog("[IndependentBuffLocalDisplay] 853B00 target missing");
        }
    }
    else
    {
        anyOk = true;
    }

    if (!oLocalIndependentPotentialFlatExtendedDisplayFn)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_856830);
        if (pTarget)
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;
            oLocalIndependentPotentialFlatExtendedDisplayFn =
                (tLocalIndependentPotentialFlatExtendedDisplayFn)GenericInlineHook5(
                    pTarget,
                    (void *)hkLocalIndependentPotentialFlatExtendedDisplayFunction,
                    copyLen);
            if (oLocalIndependentPotentialFlatExtendedDisplayFn)
            {
                anyOk = true;
                WriteLogFmt("[IndependentBuffLocalDisplay] OK(856830): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oLocalIndependentPotentialFlatExtendedDisplayFn,
                    copyLen);
            }
            else
            {
                WriteLog("[IndependentBuffLocalDisplay] 856830 hook failed");
            }
        }
        else
        {
            WriteLog("[IndependentBuffLocalDisplay] 856830 target missing");
        }
    }
    else
    {
        anyOk = true;
    }

    return anyOk;
}

static bool SetupAbilityRedDisplayCallsiteHook()
{
    if (g_AbilityRedDisplayCallsiteOriginalTarget)
        return true;

    BYTE *pCallsite = (BYTE *)(uintptr_t)ADDR_AE6C21;
    if (!pCallsite || SafeIsBadReadPtr(pCallsite, 5) || pCallsite[0] != 0xE8)
    {
        WriteLog("[AbilityRedDisplay] AE6C21 callsite missing/unexpected");
        return false;
    }

    g_AbilityRedDisplayCallsiteOriginalTarget =
        (DWORD)(uintptr_t)(pCallsite + 5 + *(int *)(pCallsite + 1));

    DWORD oldProtect = 0;
    if (!VirtualProtect(pCallsite, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        WriteLog("[AbilityRedDisplay] AE6C21 VirtualProtect failed");
        g_AbilityRedDisplayCallsiteOriginalTarget = 0;
        return false;
    }

    pCallsite[0] = 0xE8;
    *(int *)(pCallsite + 1) = (int)((uintptr_t)hkAbilityRedDisplayCallsiteNaked - (uintptr_t)pCallsite - 5);

    VirtualProtect(pCallsite, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), pCallsite, 5);

    WriteLogFmt("[AbilityRedDisplay] OK(AE6C21): original=0x%08X patchedCall=0x%08X",
        g_AbilityRedDisplayCallsiteOriginalTarget,
        (DWORD)(uintptr_t)hkAbilityRedDisplayCallsiteNaked);
    return true;
}

static bool SetupAbilityRedLevelReadHook()
{
    if (oAbilityRedLevelReadHook)
        return true;

    BYTE *pTarget = FollowJmpChain((void *)ADDR_AE43D5);
    if (!pTarget)
    {
        WriteLog("[AbilityRedLevelRead] AE43D5 target missing");
        return false;
    }

    int copyLen = CalcMinCopyLen(pTarget);
    if (copyLen < 5)
        copyLen = 5;

    oAbilityRedLevelReadHook = GenericInlineHook5(
        pTarget,
        (void *)hkAbilityRedLevelReadNaked,
        copyLen);
    if (!oAbilityRedLevelReadHook)
    {
        WriteLog("[AbilityRedLevelRead] AE43D5 hook failed");
        return false;
    }

    WriteLogFmt("[AbilityRedLevelRead] OK(AE43D5): entry=0x%08X tramp=0x%08X copyLen=%d",
        (DWORD)(uintptr_t)pTarget,
        (DWORD)(uintptr_t)oAbilityRedLevelReadHook,
        copyLen);
    return true;
}

static bool SetupAbilityRedSkillWriteHooks()
{
    bool anyOk = false;

    if (!oAbilityRedSkillWrite52FE14Hook)
    {
        BYTE *pTarget = (BYTE *)(uintptr_t)ADDR_52FE14;
        oAbilityRedSkillWrite52FE14Hook = GenericInlineHook5(
            pTarget,
            (void *)hkAbilityRedSkillWrite52FE14Naked,
            5);
        if (oAbilityRedSkillWrite52FE14Hook)
        {
            anyOk = true;
            WriteLogFmt("[AbilityRedSkillWrite] OK(52FE14): tramp=0x%08X",
                (DWORD)(uintptr_t)oAbilityRedSkillWrite52FE14Hook);
        }
        else
        {
            WriteLog("[AbilityRedSkillWrite] 52FE14 hook failed");
        }
    }
    else
    {
        anyOk = true;
    }

    if (!oAbilityRedSkillWrite6226CEHook)
    {
        BYTE *pTarget = (BYTE *)(uintptr_t)ADDR_6226CE;
        oAbilityRedSkillWrite6226CEHook = GenericInlineHook5(
            pTarget,
            (void *)hkAbilityRedSkillWrite6226CENaked,
            5);
        if (oAbilityRedSkillWrite6226CEHook)
        {
            anyOk = true;
            WriteLogFmt("[AbilityRedSkillWrite] OK(6226CE): tramp=0x%08X",
                (DWORD)(uintptr_t)oAbilityRedSkillWrite6226CEHook);
        }
        else
        {
            WriteLog("[AbilityRedSkillWrite] 6226CE hook failed");
        }
    }
    else
    {
        anyOk = true;
    }

    if (!oAbilityRedSkillWrite49CA01Hook)
    {
        BYTE *pTarget = (BYTE *)(uintptr_t)ADDR_49CA01;
        oAbilityRedSkillWrite49CA01Hook = GenericInlineHook5(
            pTarget,
            (void *)hkAbilityRedSkillWrite49CA01Naked,
            5);
        if (oAbilityRedSkillWrite49CA01Hook)
        {
            anyOk = true;
            WriteLogFmt("[AbilityRedSkillWrite] OK(49CA01): tramp=0x%08X",
                (DWORD)(uintptr_t)oAbilityRedSkillWrite49CA01Hook);
        }
        else
        {
            WriteLog("[AbilityRedSkillWrite] 49CA01 hook failed");
        }
    }
    else
    {
        anyOk = true;
    }

    return anyOk;
}

static bool SetupAbilityRedHashContainerHooks()
{
    bool anyOk = false;

    if (!oAbilityRedHashLookupHook)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_49C9C0);
        if (pTarget)
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;

            oAbilityRedHashLookupHook = GenericInlineHook5(
                pTarget,
                (void *)hkAbilityRedHashLookupNaked,
                copyLen);
            if (oAbilityRedHashLookupHook)
            {
                anyOk = true;
                WriteLogFmt("[AbilityRedHashLookup] OK(49C9C0): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oAbilityRedHashLookupHook,
                    copyLen);
            }
            else
            {
                WriteLog("[AbilityRedHashLookup] 49C9C0 hook failed");
            }
        }
        else
        {
            WriteLog("[AbilityRedHashLookup] 49C9C0 target missing");
        }
    }
    else
    {
        anyOk = true;
    }

    if (!oAbilityRedHashInsertHook)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_52FD80);
        if (pTarget)
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;

            oAbilityRedHashInsertHook = GenericInlineHook5(
                pTarget,
                (void *)hkAbilityRedHashInsertNaked,
                copyLen);
            if (oAbilityRedHashInsertHook)
            {
                anyOk = true;
                WriteLogFmt("[AbilityRedHashInsert] OK(52FD80): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oAbilityRedHashInsertHook,
                    copyLen);
            }
            else
            {
                WriteLog("[AbilityRedHashInsert] 52FD80 hook failed");
            }
        }
        else
        {
            WriteLog("[AbilityRedHashInsert] 52FD80 target missing");
        }
    }
    else
    {
        anyOk = true;
    }

    return anyOk;
}

static bool SetupAbilityRedExtendedAggregateHook()
{
    if (oAbilityRedExtendedAggregateFn)
        return true;

    BYTE *pTarget = FollowJmpChain((void *)ADDR_856BA0);
    if (!pTarget)
    {
        WriteLog("[AbilityRedAggregate] 856BA0 target missing");
        return false;
    }

    int copyLen = CalcMinCopyLen(pTarget);
    if (copyLen < 5)
        copyLen = 5;

    oAbilityRedExtendedAggregateFn =
        (tAbilityRedExtendedAggregateFn)GenericInlineHook5(
            pTarget,
            (void *)hkAbilityRedExtendedAggregateFunction,
            copyLen);
    if (!oAbilityRedExtendedAggregateFn)
    {
        WriteLog("[AbilityRedAggregate] 856BA0 hook failed");
        return false;
    }

    WriteLogFmt("[AbilityRedAggregate] OK(856BA0): entry=0x%08X tramp=0x%08X copyLen=%d",
        (DWORD)(uintptr_t)pTarget,
        (DWORD)(uintptr_t)oAbilityRedExtendedAggregateFn,
        copyLen);
    return true;
}

static bool SetupAbilityRedMasterAggregateHook()
{
    if (oAbilityRedMasterAggregateFn)
        return true;

    BYTE *pTarget = FollowJmpChain((void *)ADDR_856C60);
    if (!pTarget)
    {
        WriteLog("[AbilityRedMaster] 856C60 target missing");
        return false;
    }

    int copyLen = CalcMinCopyLen(pTarget);
    if (copyLen < 5)
        copyLen = 5;

    oAbilityRedMasterAggregateFn =
        (tAbilityRedMasterAggregateFn)GenericInlineHook5(
            pTarget,
            (void *)hkAbilityRedMasterAggregateFunction,
            copyLen);
    if (!oAbilityRedMasterAggregateFn)
    {
        WriteLog("[AbilityRedMaster] 856C60 hook failed");
        return false;
    }

    WriteLogFmt("[AbilityRedMaster] OK(856C60): entry=0x%08X tramp=0x%08X copyLen=%d",
        (DWORD)(uintptr_t)pTarget,
        (DWORD)(uintptr_t)oAbilityRedMasterAggregateFn,
        copyLen);
    return true;
}

static bool SetupAbilityRedMovementSetterHooks()
{
    bool anyOk = false;

    if (!oAbilityRedMovementSpeedSetter831F00Fn)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_831F00);
        if (!pTarget)
        {
            WriteLog("[MoveSetter] 831F00 target missing");
        }
        else
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;

            oAbilityRedMovementSpeedSetter831F00Fn =
                (tAbilityRedMovementSetterFn)GenericInlineHook5(
                    pTarget,
                    (void *)hkAbilityRedMovementSpeedSetter831F00,
                    copyLen);
            if (!oAbilityRedMovementSpeedSetter831F00Fn)
            {
                WriteLog("[MoveSetter] 831F00 hook failed");
            }
            else
            {
                anyOk = true;
                WriteLogFmt("[MoveSetter] OK(831F00): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oAbilityRedMovementSpeedSetter831F00Fn,
                    copyLen);
            }
        }
    }
    else
    {
        anyOk = true;
    }

    if (!oAbilityRedMovementJumpSetter832000Fn)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_832000);
        if (!pTarget)
        {
            WriteLog("[MoveSetter] 832000 target missing");
        }
        else
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;

            oAbilityRedMovementJumpSetter832000Fn =
                (tAbilityRedMovementSetterFn)GenericInlineHook5(
                    pTarget,
                    (void *)hkAbilityRedMovementJumpSetter832000,
                    copyLen);
            if (!oAbilityRedMovementJumpSetter832000Fn)
            {
                WriteLog("[MoveSetter] 832000 hook failed");
            }
            else
            {
                anyOk = true;
                WriteLogFmt("[MoveSetter] OK(832000): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oAbilityRedMovementJumpSetter832000Fn,
                    copyLen);
            }
        }
    }
    else
    {
        anyOk = true;
    }

    return anyOk;
}

static bool SetupMovementOutputClampHook()
{
    if (oMovementOutputClampComputeB93B80Fn)
        return true;

    BYTE *pTarget = FollowJmpChain((void *)ADDR_B93B80);
    if (!pTarget)
    {
        WriteLog("[MoveClamp] B93B80 target missing");
        return false;
    }

    int copyLen = CalcMinCopyLen(pTarget);
    if (copyLen < 5)
        copyLen = 5;

    oMovementOutputClampComputeB93B80Fn =
        (tMovementOutputClampComputeFn)GenericInlineHook5(
            pTarget,
            (void *)hkMovementOutputClampComputeB93B80,
            copyLen);
    if (!oMovementOutputClampComputeB93B80Fn)
    {
        WriteLog("[MoveClamp] B93B80 hook failed");
        return false;
    }

    WriteLogFmt("[MoveClamp] OK(B93B80): entry=0x%08X tramp=0x%08X copyLen=%d",
        (DWORD)(uintptr_t)pTarget,
        (DWORD)(uintptr_t)oMovementOutputClampComputeB93B80Fn,
        copyLen);
    return true;
}

static bool SetupAbilityRedSiblingCalcHooks()
{
    bool anyOk = false;

    if (!oAbilityRedSiblingCalc82F780Fn)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_82F780);
        if (!pTarget)
        {
            WriteLog("[AbilityRedSibling] 82F780 target missing");
        }
        else
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;

            oAbilityRedSiblingCalc82F780Fn =
                (tAbilityRedSiblingCalcFn)GenericInlineHook5(
                    pTarget,
                    (void *)hkAbilityRedSiblingCalc82F780,
                    copyLen);
            if (!oAbilityRedSiblingCalc82F780Fn)
            {
                WriteLog("[AbilityRedSibling] 82F780 hook failed");
            }
            else
            {
                WriteLogFmt("[AbilityRedSibling] OK(82F780): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oAbilityRedSiblingCalc82F780Fn,
                    copyLen);
            }
        }
    }
    if (oAbilityRedSiblingCalc82F780Fn)
        anyOk = true;

    if (!oAbilityRedSiblingCalc82F870Fn)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_82F870);
        if (!pTarget)
        {
            WriteLog("[AbilityRedSibling] 82F870 target missing");
        }
        else
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;

            oAbilityRedSiblingCalc82F870Fn =
                (tAbilityRedSiblingCalcFn)GenericInlineHook5(
                    pTarget,
                    (void *)hkAbilityRedSiblingCalc82F870,
                    copyLen);
            if (!oAbilityRedSiblingCalc82F870Fn)
            {
                WriteLog("[AbilityRedSibling] 82F870 hook failed");
            }
            else
            {
                WriteLogFmt("[AbilityRedSibling] OK(82F870): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oAbilityRedSiblingCalc82F870Fn,
                    copyLen);
            }
        }
    }
    if (oAbilityRedSiblingCalc82F870Fn)
        anyOk = true;

    if (!oAbilityRedSiblingCalc82F960Fn)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_82F960);
        if (!pTarget)
        {
            WriteLog("[AbilityRedSibling] 82F960 target missing");
        }
        else
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;

            oAbilityRedSiblingCalc82F960Fn =
                (tAbilityRedSiblingCalcFn)GenericInlineHook5(
                    pTarget,
                    (void *)hkAbilityRedSiblingCalc82F960,
                    copyLen);
            if (!oAbilityRedSiblingCalc82F960Fn)
            {
                WriteLog("[AbilityRedSibling] 82F960 hook failed");
            }
            else
            {
                WriteLogFmt("[AbilityRedSibling] OK(82F960): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oAbilityRedSiblingCalc82F960Fn,
                    copyLen);
            }
        }
    }
    if (oAbilityRedSiblingCalc82F960Fn)
        anyOk = true;

    if (!oAbilityRedSiblingCalc82FA50Fn)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_82FA50);
        if (!pTarget)
        {
            WriteLog("[AbilityRedSibling] 82FA50 target missing");
        }
        else
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;

            oAbilityRedSiblingCalc82FA50Fn =
                (tAbilityRedSiblingCalcFn)GenericInlineHook5(
                    pTarget,
                    (void *)hkAbilityRedSiblingCalc82FA50,
                    copyLen);
            if (!oAbilityRedSiblingCalc82FA50Fn)
            {
                WriteLog("[AbilityRedSibling] 82FA50 hook failed");
            }
            else
            {
                WriteLogFmt("[AbilityRedSibling] OK(82FA50): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oAbilityRedSiblingCalc82FA50Fn,
                    copyLen);
            }
        }
    }
    if (oAbilityRedSiblingCalc82FA50Fn)
        anyOk = true;

    return anyOk;
}

static bool SetupAbilityRedDiff84C470PreSubHook()
{
    if (g_AbilityRedDiff84C470PreSubContinue)
        return true;

    BYTE *pSite = (BYTE *)(uintptr_t)ADDR_9F7546;
    if (!pSite ||
        SafeIsBadReadPtr(pSite, 5) ||
        pSite[0] != 0x03 ||
        pSite[1] != 0xF3 ||
        pSite[2] != 0x03 ||
        pSite[3] != 0x75 ||
        pSite[4] != 0x48)
    {
        WriteLog("[AbilityRedDiff] 9F7546 site missing/unexpected");
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(pSite, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        WriteLog("[AbilityRedDiff] 9F7546 VirtualProtect failed");
        return false;
    }

    g_AbilityRedDiff84C470PreSubContinue = ADDR_9F754B;
    pSite[0] = 0xE9;
    *(int *)(pSite + 1) = (int)((uintptr_t)hkAbilityRedDiff84C470PreSubNaked - (uintptr_t)pSite - 5);

    VirtualProtect(pSite, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), pSite, 5);

    WriteLogFmt("[AbilityRedDiff] OK(9F7546): continue=0x%08X hook=0x%08X",
        g_AbilityRedDiff84C470PreSubContinue,
        (DWORD)(uintptr_t)hkAbilityRedDiff84C470PreSubNaked);
    return true;
}

static bool InstallAbilityRedMidHook(
    DWORD siteAddress,
    const BYTE* expectedBytes,
    size_t expectedLength,
    void* hookProc,
    DWORD continueAddress,
    DWORD* continueSlot,
    const char* tag)
{
    if (continueSlot && *continueSlot)
        return true;
    if (!expectedBytes || expectedLength < 5 || !hookProc || !continueSlot)
        return false;

    BYTE* pSite = reinterpret_cast<BYTE*>(static_cast<uintptr_t>(siteAddress));
    if (!pSite || SafeIsBadReadPtr(pSite, expectedLength) || memcmp(pSite, expectedBytes, expectedLength) != 0)
    {
        WriteLogFmt("[AbilityRed] %s site missing/unexpected", tag ? tag : "midhook");
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(pSite, expectedLength, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        WriteLogFmt("[AbilityRed] %s VirtualProtect failed", tag ? tag : "midhook");
        return false;
    }

    *continueSlot = continueAddress;
    pSite[0] = 0xE9;
    *reinterpret_cast<int*>(pSite + 1) =
        static_cast<int>(reinterpret_cast<uintptr_t>(hookProc) - reinterpret_cast<uintptr_t>(pSite) - 5);
    for (size_t i = 5; i < expectedLength; ++i)
        pSite[i] = 0x90;

    VirtualProtect(pSite, expectedLength, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), pSite, expectedLength);

    WriteLogFmt(
        "[AbilityRed] OK(%s): site=0x%08X continue=0x%08X hook=0x%08X",
        tag ? tag : "midhook",
        siteAddress,
        continueAddress,
        static_cast<DWORD>(reinterpret_cast<uintptr_t>(hookProc)));
    return true;
}

static bool SetupAbilityRedAdditionalDiffHooks()
{
    bool anyOk = false;

    static const BYTE kWdefPreSub[] = { 0x03, 0xF3, 0x03, 0x75, 0x48 };
    if (InstallAbilityRedMidHook(
            ADDR_9F7241,
            kWdefPreSub,
            sizeof(kWdefPreSub),
            reinterpret_cast<void*>(hkAbilityRedDiff84BE40PreSubNaked),
            ADDR_9F7246,
            &g_AbilityRedDiff84BE40PreSubContinue,
            "9F7241 wdef diff"))
    {
        anyOk = true;
    }

    static const BYTE kAccPreSub[] = { 0x03, 0xF7, 0x03, 0x75, 0x48 };
    if (InstallAbilityRedMidHook(
            ADDR_9F7893,
            kAccPreSub,
            sizeof(kAccPreSub),
            reinterpret_cast<void*>(hkAbilityRedDiff84CA90AccPreSubNaked),
            ADDR_9F7898,
            &g_AbilityRedDiff84CA90AccPreSubContinue,
            "9F7893 acc diff"))
    {
        anyOk = true;
    }

    static const BYTE kMagicAccPreSub[] = { 0x03, 0xF3, 0x03, 0x75, 0x48 };
    if (InstallAbilityRedMidHook(
            ADDR_9F7C7F,
            kMagicAccPreSub,
            sizeof(kMagicAccPreSub),
            reinterpret_cast<void*>(hkAbilityRedDiff84CA90MagicAccPreSubNaked),
            ADDR_9F7C84,
            &g_AbilityRedDiff84CA90MagicAccPreSubContinue,
            "9F7C7F magic-acc diff"))
    {
        anyOk = true;
    }

    static const BYTE kAvoidPreSub[] = { 0x03, 0xF3, 0x03, 0x75, 0x48 };
    if (InstallAbilityRedMidHook(
            ADDR_9F8048,
            kAvoidPreSub,
            sizeof(kAvoidPreSub),
            reinterpret_cast<void*>(hkAbilityRedDiff84CBD0AvoidPreSubNaked),
            ADDR_9F804D,
            &g_AbilityRedDiff84CBD0AvoidPreSubContinue,
            "9F8048 avoid diff"))
    {
        anyOk = true;
    }

    static const BYTE kMagicAvoidPreSub[] = { 0x03, 0x45, 0x48, 0x03, 0xF0 };
    if (InstallAbilityRedMidHook(
            ADDR_9F82A8,
            kMagicAvoidPreSub,
            sizeof(kMagicAvoidPreSub),
            reinterpret_cast<void*>(hkAbilityRedDiff84CBD0MagicAvoidPreSubNaked),
            ADDR_9F82AD,
            &g_AbilityRedDiff84CBD0MagicAvoidPreSubContinue,
            "9F82A8 magic-avoid diff"))
    {
        anyOk = true;
    }

    return anyOk;
}

static bool SetupAbilityRedPositiveStyleHooks()
{
    static const BYTE kAttackStyle[] = { 0x8B, 0x45, 0x68, 0x8D, 0x4D, 0x34, 0x51 };
    return InstallAbilityRedMidHook(
            ADDR_9F6E6F,
            kAttackStyle,
            sizeof(kAttackStyle),
            reinterpret_cast<void*>(hkAbilityRedAttackRangeStyleNaked),
            ADDR_9F6E76,
            &g_AbilityRedAttackRangeStyleContinue,
            "9F6E6F attack style");
}

static bool SetupAbilityRedBakeWriteHooks()
{
    bool anyOk = false;

    if (!oAbilityRedBake857BB6Hook)
    {
        BYTE *pTarget = (BYTE *)(uintptr_t)ADDR_857BB6;
        oAbilityRedBake857BB6Hook = GenericInlineHook5(
            pTarget,
            (void *)hkAbilityRedBake857BB6Naked,
            6);
        if (oAbilityRedBake857BB6Hook)
        {
            anyOk = true;
            WriteLogFmt("[AbilityRedBake] OK(857BB6): tramp=0x%08X",
                (DWORD)(uintptr_t)oAbilityRedBake857BB6Hook);
        }
        else
        {
            WriteLog("[AbilityRedBake] 857BB6 hook failed");
        }
    }
    else
    {
        anyOk = true;
    }

    if (!oAbilityRedBake857C29Hook)
    {
        BYTE *pTarget = (BYTE *)(uintptr_t)ADDR_857C29;
        oAbilityRedBake857C29Hook = GenericInlineHook5(
            pTarget,
            (void *)hkAbilityRedBake857C29Naked,
            6);
        if (oAbilityRedBake857C29Hook)
        {
            anyOk = true;
            WriteLogFmt("[AbilityRedBake] OK(857C29): tramp=0x%08X",
                (DWORD)(uintptr_t)oAbilityRedBake857C29Hook);
        }
        else
        {
            WriteLog("[AbilityRedBake] 857C29 hook failed");
        }
    }
    else
    {
        anyOk = true;
    }

    if (!oAbilityRedBake857C9CHook)
    {
        BYTE *pTarget = (BYTE *)(uintptr_t)ADDR_857C9C;
        oAbilityRedBake857C9CHook = GenericInlineHook5(
            pTarget,
            (void *)hkAbilityRedBake857C9CNaked,
            6);
        if (oAbilityRedBake857C9CHook)
        {
            anyOk = true;
            WriteLogFmt("[AbilityRedBake] OK(857C9C): tramp=0x%08X",
                (DWORD)(uintptr_t)oAbilityRedBake857C9CHook);
        }
        else
        {
            WriteLog("[AbilityRedBake] 857C9C hook failed");
        }
    }
    else
    {
        anyOk = true;
    }

    if (!oAbilityRedBake857D0FHook)
    {
        BYTE *pTarget = (BYTE *)(uintptr_t)ADDR_857D0F;
        oAbilityRedBake857D0FHook = GenericInlineHook5(
            pTarget,
            (void *)hkAbilityRedBake857D0FNaked,
            6);
        if (oAbilityRedBake857D0FHook)
        {
            anyOk = true;
            WriteLogFmt("[AbilityRedBake] OK(857D0F): tramp=0x%08X",
                (DWORD)(uintptr_t)oAbilityRedBake857D0FHook);
        }
        else
        {
            WriteLog("[AbilityRedBake] 857D0F hook failed");
        }
    }
    else
    {
        anyOk = true;
    }

    return anyOk;
}

static bool SetupAbilityRedBake198Hooks()
{
    bool anyOk = false;

    if (!oAbilityRedBake1988569C3Hook)
    {
        BYTE *pTarget = (BYTE *)(uintptr_t)ADDR_8569C3;
        oAbilityRedBake1988569C3Hook = GenericInlineHook5(
            pTarget,
            (void *)hkAbilityRedBake1988569C3Naked,
            6);
        if (oAbilityRedBake1988569C3Hook)
        {
            anyOk = true;
            WriteLogFmt("[AbilityRedBake198] OK(8569C3): tramp=0x%08X",
                (DWORD)(uintptr_t)oAbilityRedBake1988569C3Hook);
        }
        else
        {
            WriteLog("[AbilityRedBake198] 8569C3 hook failed");
        }
    }
    else
    {
        anyOk = true;
    }

    if (!oAbilityRedBake198856D57Hook)
    {
        BYTE *pTarget = (BYTE *)(uintptr_t)ADDR_856D57;
        oAbilityRedBake198856D57Hook = GenericInlineHook5(
            pTarget,
            (void *)hkAbilityRedBake198856D57Naked,
            6);
        if (oAbilityRedBake198856D57Hook)
        {
            anyOk = true;
            WriteLogFmt("[AbilityRedBake198] OK(856D57): tramp=0x%08X",
                (DWORD)(uintptr_t)oAbilityRedBake198856D57Hook);
        }
        else
        {
            WriteLog("[AbilityRedBake198] 856D57 hook failed");
        }
    }
    else
    {
        anyOk = true;
    }

    if (!oAbilityRedBake19885725FHook)
    {
        BYTE *pTarget = (BYTE *)(uintptr_t)ADDR_85725F;
        oAbilityRedBake19885725FHook = GenericInlineHook5(
            pTarget,
            (void *)hkAbilityRedBake19885725FNaked,
            6);
        if (oAbilityRedBake19885725FHook)
        {
            anyOk = true;
            WriteLogFmt("[AbilityRedBake198] OK(85725F): tramp=0x%08X",
                (DWORD)(uintptr_t)oAbilityRedBake19885725FHook);
        }
        else
        {
            WriteLog("[AbilityRedBake198] 85725F hook failed");
        }
    }
    else
    {
        anyOk = true;
    }

    if (!oAbilityRedBake198857C3BHook)
    {
        BYTE *pTarget = (BYTE *)(uintptr_t)ADDR_857C3B;
        oAbilityRedBake198857C3BHook = GenericInlineHook5(
            pTarget,
            (void *)hkAbilityRedBake198857C3BNaked,
            6);
        if (oAbilityRedBake198857C3BHook)
        {
            anyOk = true;
            WriteLogFmt("[AbilityRedBake198] OK(857C3B): tramp=0x%08X",
                (DWORD)(uintptr_t)oAbilityRedBake198857C3BHook);
        }
        else
        {
            WriteLog("[AbilityRedBake198] 857C3B hook failed");
        }
    }
    else
    {
        anyOk = true;
    }

    if (!oAbilityRedBake198858AEDHook)
    {
        BYTE *pTarget = (BYTE *)(uintptr_t)ADDR_858AED;
        oAbilityRedBake198858AEDHook = GenericInlineHook5(
            pTarget,
            (void *)hkAbilityRedBake198858AEDNaked,
            6);
        if (oAbilityRedBake198858AEDHook)
        {
            anyOk = true;
            WriteLogFmt("[AbilityRedBake198] OK(858AED): tramp=0x%08X",
                (DWORD)(uintptr_t)oAbilityRedBake198858AEDHook);
        }
        else
        {
            WriteLog("[AbilityRedBake198] 858AED hook failed");
        }
    }
    else
    {
        anyOk = true;
    }

    if (!oAbilityRedBake198831A50Hook)
    {
        BYTE *pTarget = (BYTE *)(uintptr_t)ADDR_831A50;
        oAbilityRedBake198831A50Hook = GenericInlineHook5(
            pTarget,
            (void *)hkAbilityRedBake198831A50Naked,
            6);
        if (oAbilityRedBake198831A50Hook)
        {
            anyOk = true;
            WriteLogFmt("[AbilityRedBake198] OK(831A50): tramp=0x%08X",
                (DWORD)(uintptr_t)oAbilityRedBake198831A50Hook);
        }
        else
        {
            WriteLog("[AbilityRedBake198] 831A50 hook failed");
        }
    }
    else
    {
        anyOk = true;
    }

    if (!oAbilityRedBake19883AF02Hook)
    {
        BYTE *pTarget = (BYTE *)(uintptr_t)ADDR_83AF02;
        oAbilityRedBake19883AF02Hook = GenericInlineHook5(
            pTarget,
            (void *)hkAbilityRedBake19883AF02Naked,
            6);
        if (oAbilityRedBake19883AF02Hook)
        {
            anyOk = true;
            WriteLogFmt("[AbilityRedBake198] OK(83AF02): tramp=0x%08X",
                (DWORD)(uintptr_t)oAbilityRedBake19883AF02Hook);
        }
        else
        {
            WriteLog("[AbilityRedBake198] 83AF02 hook failed");
        }
    }
    else
    {
        anyOk = true;
    }

    return anyOk;
}

static bool SetupAbilityRedFinalValueHooks()
{
    bool anyOk = false;

    if (!oAbilityRedFinalCalc84C470Fn)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_84C470);
        if (!pTarget)
        {
            WriteLog("[AbilityRedFinal] 84C470 target missing");
        }
        else
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;

            oAbilityRedFinalCalc84C470Fn =
                (tAbilityRedFinalCalc6Fn)GenericInlineHook5(
                    pTarget,
                    (void *)hkAbilityRedFinalCalc84C470,
                    copyLen);
            if (!oAbilityRedFinalCalc84C470Fn)
            {
                WriteLog("[AbilityRedFinal] 84C470 hook failed");
            }
            else
            {
                WriteLogFmt("[AbilityRedFinal] OK(84C470): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oAbilityRedFinalCalc84C470Fn,
                    copyLen);
            }
        }
    }
    if (oAbilityRedFinalCalc84C470Fn)
        anyOk = true;

    if (!oAbilityRedFinalCalc84CA90Fn)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_84CA90);
        if (!pTarget)
        {
            WriteLog("[AbilityRedFinal] 84CA90 target missing");
        }
        else
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;

            oAbilityRedFinalCalc84CA90Fn =
                (tAbilityRedFinalCalc5Fn)GenericInlineHook5(
                    pTarget,
                    (void *)hkAbilityRedFinalCalc84CA90,
                    copyLen);
            if (!oAbilityRedFinalCalc84CA90Fn)
            {
                WriteLog("[AbilityRedFinal] 84CA90 hook failed");
            }
            else
            {
                WriteLogFmt("[AbilityRedFinal] OK(84CA90): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oAbilityRedFinalCalc84CA90Fn,
                    copyLen);
            }
        }
    }
    if (oAbilityRedFinalCalc84CA90Fn)
        anyOk = true;

    if (!oAbilityRedFinalCalc84CBD0Fn)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_84CBD0);
        if (!pTarget)
        {
            WriteLog("[AbilityRedFinal] 84CBD0 target missing");
        }
        else
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;

            oAbilityRedFinalCalc84CBD0Fn =
                (tAbilityRedFinalCalc5Fn)GenericInlineHook5(
                    pTarget,
                    (void *)hkAbilityRedFinalCalc84CBD0,
                    copyLen);
            if (!oAbilityRedFinalCalc84CBD0Fn)
            {
                WriteLog("[AbilityRedFinal] 84CBD0 hook failed");
            }
            else
            {
                WriteLogFmt("[AbilityRedFinal] OK(84CBD0): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oAbilityRedFinalCalc84CBD0Fn,
                    copyLen);
            }
        }
    }
    if (oAbilityRedFinalCalc84CBD0Fn)
        anyOk = true;

    return anyOk;
}

static bool SetupAbilityRedDisplayCandidateHook()
{
    if (oAbilityRedDisplayCandidateFn)
        return true;

    BYTE *pTarget = FollowJmpChain((void *)ADDR_AE0E60);
    if (!pTarget)
    {
        WriteLog("[AbilityRedDisplay] AE0E60 target missing");
        return false;
    }

    int copyLen = CalcMinCopyLen(pTarget);
    if (copyLen < 5)
        copyLen = 5;

    oAbilityRedDisplayCandidateFn =
        (tAbilityRedDisplayCandidateFn)GenericInlineHook5(
            pTarget,
            (void *)hkAbilityRedDisplayCandidateFunction,
            copyLen);
    if (!oAbilityRedDisplayCandidateFn)
    {
        WriteLog("[AbilityRedDisplay] AE0E60 hook failed");
        return false;
    }

    WriteLogFmt("[AbilityRedDisplay] OK(AE0E60): entry=0x%08X tramp=0x%08X copyLen=%d",
        (DWORD)(uintptr_t)pTarget,
        (DWORD)(uintptr_t)oAbilityRedDisplayCandidateFn,
        copyLen);
    return true;
}

static bool SetupPotentialTextDisplayHook()
{
    if (oPotentialTextFormat)
        return true;

    BYTE* pTarget = FollowJmpChain((void*)ADDR_A4CA60);
    if (!pTarget)
    {
        WriteLog("[PotentialTextHook] A4CA60 target missing");
        return false;
    }

    const int copyLen = CalcMinCopyLen(pTarget);
    if (copyLen < 5)
    {
        WriteLogFmt("[PotentialTextHook] A4CA60 invalid copyLen=%d", copyLen);
        return false;
    }

    oPotentialTextFormat = (tPotentialTextFormatFn)GenericInlineHook5(
        pTarget,
        (void*)hkPotentialTextFormat,
        copyLen);
    if (!oPotentialTextFormat)
    {
        WriteLog("[PotentialTextHook] A4CA60 hook failed");
        return false;
    }

    WriteLogFmt("[PotentialTextHook] OK(A4CA60): entry=0x%08X tramp=0x%08X copyLen=%d",
        (DWORD)(uintptr_t)pTarget,
        (DWORD)(uintptr_t)oPotentialTextFormat,
        copyLen);
    return true;
}

static bool SetupLocalIndependentPotentialSkillLevelDisplayHook()
{
    if (oLocalIndependentPotentialSkillLevelDisplay)
        return true;

    BYTE* pTarget = FollowJmpChain((void*)ADDR_AE0B23);
    if (!pTarget)
        pTarget = TryFollowAbsoluteRegisterJumpStub((BYTE*)ADDR_AE0B23);
    if (pTarget && pTarget[0] >= 0xB8 && pTarget[0] <= 0xBF)
    {
        BYTE* pResolvedStub = TryFollowAbsoluteRegisterJumpStub(pTarget);
        if (pResolvedStub)
            pTarget = pResolvedStub;
    }
    if (!pTarget)
    {
        WriteLog("[IndependentBuffLocalDisplay] AE0B23 target missing");
        return false;
    }

    const int copyLen = CalcMinCopyLen(pTarget);
    if (copyLen < 5)
    {
        WriteLogFmt("[IndependentBuffLocalDisplay] AE0B23 invalid copyLen=%d", copyLen);
        return false;
    }

    oLocalIndependentPotentialSkillLevelDisplay = GenericInlineHook5(
        pTarget,
        (void*)hkLocalIndependentPotentialSkillLevelDisplayNaked,
        copyLen);
    if (!oLocalIndependentPotentialSkillLevelDisplay)
    {
        WriteLog("[IndependentBuffLocalDisplay] AE0B23 hook failed");
        return false;
    }

    WriteLogFmt("[IndependentBuffLocalDisplay] OK(AE0B23): entry=0x%08X tramp=0x%08X copyLen=%d",
        (DWORD)(uintptr_t)pTarget,
        (DWORD)(uintptr_t)oLocalIndependentPotentialSkillLevelDisplay,
        copyLen);
    return true;
}

static bool SetupLocalIndependentPotentialDamageDisplayHook()
{
    if (oLocalIndependentPotentialDamageDisplay)
        return true;

    BYTE* pTarget = FollowJmpChain((void*)ADDR_AE0FDC);
    if (!pTarget)
        pTarget = TryFollowAbsoluteRegisterJumpStub((BYTE*)ADDR_AE0FDC);
    if (pTarget && pTarget[0] >= 0xB8 && pTarget[0] <= 0xBF)
    {
        BYTE* pResolvedStub = TryFollowAbsoluteRegisterJumpStub(pTarget);
        if (pResolvedStub)
            pTarget = pResolvedStub;
    }
    if (!pTarget)
    {
        WriteLog("[IndependentBuffLocalDisplay] AE0FDC target missing");
        return false;
    }

    const int copyLen = CalcMinCopyLen(pTarget);
    if (copyLen < 5)
    {
        WriteLogFmt("[IndependentBuffLocalDisplay] AE0FDC invalid copyLen=%d", copyLen);
        return false;
    }

    oLocalIndependentPotentialDamageDisplay = GenericInlineHook5(
        pTarget,
        (void*)hkLocalIndependentPotentialDamageDisplayNaked,
        copyLen);
    if (!oLocalIndependentPotentialDamageDisplay)
    {
        WriteLog("[IndependentBuffLocalDisplay] AE0FDC hook failed");
        return false;
    }

    WriteLogFmt("[IndependentBuffLocalDisplay] OK(AE0FDC): entry=0x%08X tramp=0x%08X copyLen=%d",
        (DWORD)(uintptr_t)pTarget,
        (DWORD)(uintptr_t)oLocalIndependentPotentialDamageDisplay,
        copyLen);
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
    BYTE *pRoot = FollowJmpChain((void *)ADDR_B31349);
    if (pRoot)
    {
        oSkillReleaseClassifierRoot = GenericInlineHook5(pRoot, (void *)hkSkillReleaseClassifierRootNaked, 10);
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
    BYTE *pTarget = FollowJmpChain((void *)ADDR_B3144D);
    if (!pTarget)
    {
        WriteLog("[SkillReleaseHook] classifier branch target missing");
    }
    else
    {
        oSkillReleaseClassifier = GenericInlineHook5(pTarget, (void *)hkSkillReleaseClassifierNaked, 6);
        if (!oSkillReleaseClassifier)
        {
            WriteLog("[SkillReleaseHook] classifier branch hook failed");
        }
        else
        {
            branchOk = true;
            WriteLogFmt("[SkillReleaseHook] OK(B3144D): tramp=0x%08X", (DWORD)(uintptr_t)oSkillReleaseClassifier);
        }
    }

    bool b2f370Ok = false;
    BYTE *pB2F370 = FollowJmpChain((void *)ADDR_B2F370);
    if (!pB2F370)
    {
        WriteLog("[SkillReleaseHook] B2F370 target missing");
    }
    else
    {
        // 00B2F370 前三条指令长度 = 2 + 5 + 6 = 13
        oSkillReleaseClassifierB2F370 = GenericInlineHook5(
            pB2F370,
            (void *)hkSkillReleaseClassifierB2F370Naked,
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

    if (!rootOk && !branchOk && !b2f370Ok)
    {
        return false;
    }
    return true;
}

static bool SetupSkillPresentationHook()
{
    oSkillPresentationDispatch = (tSkillPresentationDispatch)InstallInlineHook(
        ADDR_ABAF70, (void *)hkSkillPresentationDispatch);
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
        ADDR_402F60, (void *)hkMakeGameWString);
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
        ADDR_506EE0, (void *)hkButtonResolveCurrentDrawObj);
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
        ADDR_507020, (void *)hkButtonDrawCurrentState);
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
        ADDR_507DF0, (void *)hkButtonMetric507DF0);
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
        ADDR_507ED0, (void *)hkButtonMetric507ED0);
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
        ADDR_5095A0, (void *)hkButtonRefreshState5095A0);
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
        BYTE *pPatch = (BYTE *)ADDR_52972E;
        DWORD oldProt = 0;
        if (VirtualProtect(pPatch, 8, PAGE_EXECUTE_READWRITE, &oldProt))
        {
            // 5 字节 jmp rel32
            pPatch[0] = 0xE9;
            *(DWORD *)(pPatch + 1) = (DWORD)((uintptr_t)hkRectWrite52972E - (uintptr_t)pPatch - 5);
            // 3 字节 nop
            pPatch[5] = 0x90;
            pPatch[6] = 0x90;
            pPatch[7] = 0x90;
            VirtualProtect(pPatch, 8, oldProt, &oldProt);
            FlushInstructionCache(GetCurrentProcess(), pPatch, 8);
            WriteLog("[RectWriteHook] OK: 0x52972E patched");
        }
        else
        {
            WriteLog("[RectWriteHook] FAIL: VirtualProtect failed");
        }
    }

    return ok;
}

static bool SetupMountMovementObservationHooks()
{
    if (oMountMovementDataLookup888B30)
    {
        return true;
    }

    oMountMovementDataLookup888B30 = (tMountMovementDataLookupFn)InstallInlineHook(
        ADDR_888B30, (void *)hkMountMovementDataLookup888B30);
    if (!oMountMovementDataLookup888B30)
    {
        WriteLog("[MountMoveObserve] hook failed: 888B30");
        return false;
    }

    WriteLogFmt("[MountMoveObserve] OK(888B30): tramp=0x%08X",
                (DWORD)(uintptr_t)oMountMovementDataLookup888B30);
    return true;
}

static bool SetupMountedFlightPhysicsSpeedHooks()
{
    bool ok = false;

    if (!oMountedFlightPhysicsDispatchB87E60)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_B87E60);
        if (!pTarget)
        {
            WriteLog("[MountFlightSpeed] B87E60 target missing");
        }
        else
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;

            oMountedFlightPhysicsDispatchB87E60 =
                (tMountedFlightPhysicsDispatchFn)GenericInlineHook5(
                    pTarget,
                    (void *)hkMountedFlightPhysicsDispatchB87E60,
                    copyLen);
            if (oMountedFlightPhysicsDispatchB87E60)
            {
                ok = true;
                WriteLogFmt("[MountFlightSpeed] OK(B87E60): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oMountedFlightPhysicsDispatchB87E60,
                    copyLen);
            }
            else
            {
                WriteLog("[MountFlightSpeed] B87E60 hook failed");
            }
        }
    }
    else
    {
        ok = true;
    }

    if (!oMountedFlightPhysicsStepB83C90)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_B83C90);
        if (!pTarget)
        {
            WriteLog("[MountFlightSpeed] B83C90 target missing");
        }
        else
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;

            oMountedFlightPhysicsStepB83C90 =
                (tMountedFlightPhysicsStepFn)GenericInlineHook5(
                    pTarget,
                    (void *)hkMountedFlightPhysicsStepB83C90,
                    copyLen);
            if (oMountedFlightPhysicsStepB83C90)
            {
                ok = true;
                WriteLogFmt("[MountFlightSpeed] OK(B83C90): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oMountedFlightPhysicsStepB83C90,
                    copyLen);
            }
            else
            {
                WriteLog("[MountFlightSpeed] B83C90 hook failed");
            }
        }
    }
    else
    {
        ok = true;
    }

    if (!oMountedFlightPhysicsStepB844D0)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_B844D0);
        if (!pTarget)
        {
            WriteLog("[MountFlightSpeed] B844D0 target missing");
        }
        else
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;

            oMountedFlightPhysicsStepB844D0 =
                (tMountedFlightPhysicsStepFn)GenericInlineHook5(
                    pTarget,
                    (void *)hkMountedFlightPhysicsStepB844D0,
                    copyLen);
            if (oMountedFlightPhysicsStepB844D0)
            {
                ok = true;
                WriteLogFmt("[MountFlightSpeed] OK(B844D0): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oMountedFlightPhysicsStepB844D0,
                    copyLen);
            }
            else
            {
                WriteLog("[MountFlightSpeed] B844D0 hook failed");
            }
        }
    }
    else
    {
        ok = true;
    }

    if (!oMountedFlightPhysicsStateB84D70)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_B84D70);
        if (!pTarget)
        {
            WriteLog("[MountFlightSpeed] B84D70 target missing");
        }
        else
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;

            oMountedFlightPhysicsStateB84D70 =
                (tMountedFlightPhysicsStateFn)GenericInlineHook5(
                    pTarget,
                    (void *)hkMountedFlightPhysicsStateB84D70,
                    copyLen);
            if (oMountedFlightPhysicsStateB84D70)
            {
                ok = true;
                WriteLogFmt("[MountFlightSpeed] OK(B84D70): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oMountedFlightPhysicsStateB84D70,
                    copyLen);
            }
            else
            {
                WriteLog("[MountFlightSpeed] B84D70 hook failed");
            }
        }
    }
    else
    {
        ok = true;
    }

    if (!oMountedFlightPhysicsFinalizeB851F0)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_B851F0);
        if (!pTarget)
        {
            WriteLog("[MountFlightSpeed] B851F0 target missing");
        }
        else
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;

            oMountedFlightPhysicsFinalizeB851F0 =
                (tMountedFlightPhysicsFinalizeFn)GenericInlineHook5(
                    pTarget,
                    (void *)hkMountedFlightPhysicsFinalizeB851F0,
                    copyLen);
            if (oMountedFlightPhysicsFinalizeB851F0)
            {
                ok = true;
                WriteLogFmt("[MountFlightSpeed] OK(B851F0): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oMountedFlightPhysicsFinalizeB851F0,
                    copyLen);
            }
            else
            {
                WriteLog("[MountFlightSpeed] B851F0 hook failed");
            }
        }
    }
    else
    {
        ok = true;
    }

    if (!oMountedFlightPhysicsStepB88090)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_B88090);
        if (!pTarget)
        {
            WriteLog("[MountFlightSpeed] B88090 target missing");
        }
        else
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;

            oMountedFlightPhysicsStepB88090 =
                (tMountedFlightPhysicsStepFn)GenericInlineHook5(
                    pTarget,
                    (void *)hkMountedFlightPhysicsStepB88090,
                    copyLen);
            if (oMountedFlightPhysicsStepB88090)
            {
                ok = true;
                WriteLogFmt("[MountFlightSpeed] OK(B88090): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oMountedFlightPhysicsStepB88090,
                    copyLen);
            }
            else
            {
                WriteLog("[MountFlightSpeed] B88090 hook failed");
            }
        }
    }
    else
    {
        ok = true;
    }

    if (!oMountedFlightPhysicsVerticalB8FE30)
    {
        BYTE *pTarget = FollowJmpChain((void *)ADDR_B8FE30);
        if (!pTarget)
        {
            WriteLog("[MountFlightSpeed] B8FE30 target missing");
        }
        else
        {
            int copyLen = CalcMinCopyLen(pTarget);
            if (copyLen < 5)
                copyLen = 5;

            oMountedFlightPhysicsVerticalB8FE30 =
                (tMountedFlightPhysicsVerticalFn)GenericInlineHook5(
                    pTarget,
                    (void *)hkMountedFlightPhysicsVerticalB8FE30,
                    copyLen);
            if (oMountedFlightPhysicsVerticalB8FE30)
            {
                ok = true;
                WriteLogFmt("[MountFlightSpeed] OK(B8FE30): entry=0x%08X tramp=0x%08X copyLen=%d",
                    (DWORD)(uintptr_t)pTarget,
                    (DWORD)(uintptr_t)oMountedFlightPhysicsVerticalB8FE30,
                    copyLen);
            }
            else
            {
                WriteLog("[MountFlightSpeed] B8FE30 hook failed");
            }
        }
    }
    else
    {
        ok = true;
    }

    return ok;
}

static bool SetupSkillNativeIdGateHooks()
{
    bool ok = false;

    BYTE *pGate7CE790 = FollowJmpChain((void *)ADDR_7CE790);
    if (pGate7CE790)
    {
        oSkillNativeIdGate7CE790 = (tSkillNativeIdGateFn)GenericInlineHook5(
            pGate7CE790, (void *)hkSkillNativeIdGate7CE790, 9);
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

    BYTE *pGate7D0000 = FollowJmpChain((void *)ADDR_7D0000);
    if (pGate7D0000)
    {
        oSkillNativeIdGate7D0000 = (tSkillNativeIdGateFn)GenericInlineHook5(
            pGate7D0000, (void *)hkSkillNativeIdGate7D0000, 9);
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

    if (kEnableMountedDoubleJumpRuntimeHooks)
    {
        oMountedSkillWhitelist7CF270 = (tSkillNativeIdGateFn)InstallInlineHook(
            ADDR_7CF270, (void *)hkMountedSkillWhitelist7CF270);
        if (oMountedSkillWhitelist7CF270)
        {
            ok = true;
            WriteLogFmt("[MountDoubleJump] OK(7CF270): tramp=0x%08X",
                        (DWORD)(uintptr_t)oMountedSkillWhitelist7CF270);
        }
        else
        {
            WriteLog("[MountDoubleJump] hook failed: 7CF270");
        }

        // A9BF40 的入口前 8 字节包含 rel32 call(7D4C00)。
        // 现有 InstallInlineHook trampoline 只做“原字节搬运”，不会重定位这类相对调用，
        // 一旦真正走原函数 trampoline，会在骑宠召唤/动作链里直接跑飞崩溃。
        // 这里继续禁用入口 hook，改为只 patch B2F370 内 00B3009F 那一个 callsite。
        oMountedSkillContextGateA9BF40 = nullptr;
        WriteLog("[MountDoubleJump] hook disabled: A9BF40 rel32-call trampoline unsafe");
        if (SetupMountedSkillContextGateCallsiteHook())
        {
            ok = true;
        }
        if (SetupMountedUnknownSkillReleaseBranchHook())
        {
            ok = true;
        }
        if (SetupMountedDemonJumpCrashTraceHooks())
        {
            ok = true;
        }
        if (SetupMountedDemonJumpPacketObserveHooks())
        {
            ok = true;
        }
        if (SetupMountedDemonJumpLatePathHooks())
        {
            ok = true;
        }
        if (SetupMountedUseFailPromptSuppressHook())
        {
            ok = true;
        }

        oMountedStateGate42DE20 = (tMountedStateGateFn)InstallInlineHook(
            ADDR_42DE20, (void *)hkMountedStateGate42DE20);
        if (oMountedStateGate42DE20)
        {
            ok = true;
            WriteLogFmt("[MountDoubleJump] OK(42DE20): tramp=0x%08X",
                        (DWORD)(uintptr_t)oMountedStateGate42DE20);
        }
        else
        {
            WriteLog("[MountDoubleJump] hook failed: 42DE20");
        }

        oMountedDemonJumpContextClear433380 =
            (tMountedDemonJumpContextClearFn)InstallInlineHook(
                ADDR_MountedDemonJumpContextClear433380,
                (void *)hkMountedDemonJumpContextClear433380);
        if (oMountedDemonJumpContextClear433380)
        {
            ok = true;
            WriteLogFmt(
                "[MountDemonJumpContext] OK(433380): tramp=0x%08X",
                (DWORD)(uintptr_t)oMountedDemonJumpContextClear433380);
        }
        else
        {
            WriteLog("[MountDemonJumpContext] hook failed: 433380");
        }

    }
    else
    {
        oMountedSkillWhitelist7CF270 = nullptr;
        oMountedSkillContextGateA9BF40 = nullptr;
        oMountedStateGate42DE20 = nullptr;
        oMountedUseFailPromptAE6260 = nullptr;
        oMountedDemonJumpContextClear433380 = nullptr;
        oMountedSkillPacketDispatchB26760 = nullptr;
        oMountedSkillAttackPacketB28A00 = nullptr;
        oMountedDemonJumpLateRoute575D60 = nullptr;
        oMountedDemonJumpLateTick576020 = nullptr;
        oMountedDemonJumpContextInputB22630 = nullptr;
        oMountedDemonJumpMoveB1DB10 = nullptr;
        oMountedDemonJumpMoveB1C9E0 = nullptr;
        oMountedDemonJumpBranchADEDA0 = nullptr;
        oMountedDemonJumpFilterBDBFD0 = nullptr;
        g_MountedSkillContextGateCallsiteOriginalTarget = 0;
        g_MountedUnknownSkillReleaseBranchOriginalTarget = 0;
        WriteLog("[MountDoubleJump] runtime hooks disabled");
    }

    // 坐骑攀爬/绳索动作在 0042C300 case 51/52 前会先过 4069E0 白名单。
    // 这里把扩展骑宠补进去，避免只改服务端后出现“能飞但不能爬”的半支持状态。
    oMountActionGate4069E0 = (tMountActionGateFn)InstallInlineHook(
        ADDR_4069E0, (void *)hkMountActionGate4069E0);
    if (oMountActionGate4069E0)
    {
        ok = true;
        WriteLogFmt("[MountGate] OK(4069E0): tramp=0x%08X", (DWORD)(uintptr_t)oMountActionGate4069E0);
    }
    else
    {
        WriteLog("[MountGate] hook failed: 4069E0");
    }

    oMountActionGate406AB0 = (tMountActionGateFn)InstallInlineHook(
        ADDR_406AB0, (void *)hkMountActionGate406AB0);
    if (oMountActionGate406AB0)
    {
        WriteLogFmt("[MountGate] OK(406AB0): tramp=0x%08X",
                    (DWORD)(uintptr_t)oMountActionGate406AB0);
    }
    else
    {
        WriteLog("[MountGate] hook failed: 406AB0");
    }

    oMountNativeFlightSkillMap7CF370 = (tMountNativeFlightSkillMapFn)InstallInlineHook(
        ADDR_7CF370, (void *)hkMountNativeFlightSkillMap7CF370);
    if (oMountNativeFlightSkillMap7CF370)
    {
        ok = true;
        WriteLogFmt("[MountFlightMap] OK(7CF370): tramp=0x%08X", (DWORD)(uintptr_t)oMountNativeFlightSkillMap7CF370);
    }
    else
    {
        WriteLog("[MountFlightMap] hook failed: 7CF370");
    }

    oMountContextIsFlyingFamily7D4CD0 = (tMountContextIsFlyingFamilyFn)InstallInlineHook(
        ADDR_7D4CD0, (void *)hkMountContextIsFlyingFamily7D4CD0);
    if (oMountContextIsFlyingFamily7D4CD0)
    {
        ok = true;
        WriteLogFmt("[MountFamily] OK(7D4CD0): tramp=0x%08X",
                    (DWORD)(uintptr_t)oMountContextIsFlyingFamily7D4CD0);
    }
    else
    {
        WriteLog("[MountFamily] hook failed: 7D4CD0");
    }

    oMountSoaringGate7DC1B0 = (tMountSoaringGateFn)InstallInlineHook(
        ADDR_7DC1B0, (void *)hkMountSoaringGate7DC1B0);
    if (oMountSoaringGate7DC1B0)
    {
        ok = true;
        WriteLogFmt("[MountSoaringGate] OK(7DC1B0): tramp=0x%08X",
                    (DWORD)(uintptr_t)oMountSoaringGate7DC1B0);
    }
    else
    {
        WriteLog("[MountSoaringGate] hook failed: 7DC1B0");
    }

    BYTE *pMountNativeSoaringRelease = FollowJmpChain((void *)ADDR_B26290);
    if (pMountNativeSoaringRelease)
    {
        // B26290 前 4 条指令长度 = 2 + 5 + 6 = 13；无 rel32 调用，适合直接复制 trampoline。
        oMountNativeSoaringReleaseB26290 = (tMountNativeSoaringReleaseFn)GenericInlineHook5(
            pMountNativeSoaringRelease,
            (void *)hkMountNativeSoaringReleaseB26290,
            13);
    }
    else
    {
        oMountNativeSoaringReleaseB26290 = nullptr;
    }
    if (oMountNativeSoaringReleaseB26290)
    {
        ok = true;
        WriteLogFmt("[MountSoaringNative] OK(B26290): tramp=0x%08X",
                    (DWORD)(uintptr_t)oMountNativeSoaringReleaseB26290);
    }
    else
    {
        WriteLog("[MountSoaringNative] hook failed: B26290");
    }

    oMountFamilyGateA9AAA0 = (tMountFamilyGateFn)InstallInlineHook(
        ADDR_A9AAA0, (void *)hkMountFamilyGateA9AAA0);
    if (oMountFamilyGateA9AAA0)
    {
        ok = true;
        WriteLogFmt("[MountFamilyGate] OK(A9AAA0): tramp=0x%08X",
                    (DWORD)(uintptr_t)oMountFamilyGateA9AAA0);
    }
    else
    {
        WriteLog("[MountFamilyGate] hook failed: A9AAA0");
    }

    if (kEnableMountMovementObservationHooks)
    {
        if (SetupMountMovementObservationHooks())
            ok = true;
    }
    else
    {
        WriteLog("[MountMoveObserve] observation hook disabled");
    }

    if (kEnableMountedFlightPhysicsSpeedHooks)
    {
        if (SetupMountedFlightPhysicsSpeedHooks())
            ok = true;
    }
    else
    {
        WriteLog("[MountFlightSpeed] physics speed hooks disabled");
    }

    if (kEnableMountMovementCapPatches)
    {
        if (ApplyMountMovementCapPatches())
            ok = true;
    }
    else
    {
        WriteLog("[RuntimePatch] mount movement cap patches disabled for rollback");
    }

    return ok;
}

static bool SetupSkillLevelLookupHooks()
{
    bool ok = false;

    oSkillLevelBase = (tSkillLevelBaseFn)InstallInlineHook(
        ADDR_7DA7D0, (void *)hkSkillLevelBase);
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
        ADDR_7DBC50, (void *)hkSkillLevelCurrent);
    if (oSkillLevelCurrent)
    {
        ok = true;
        WriteLogFmt("[SkillLevelHook] OK(7DBC50): tramp=0x%08X", (DWORD)(uintptr_t)oSkillLevelCurrent);
    }
    else
    {
        WriteLog("[SkillLevelHook] hook failed: 7DBC50");
    }

    if (SetupSkillEffectPassiveBonusHooks())
        ok = true;
    else
        WriteLog("[SuperPassiveEffectHook] effect hooks failed (non-fatal)");

    return ok;
}

static bool SetupSkillEffectPassiveBonusHooks()
{
    bool ok = false;

    if (!oSkillEffect800260)
    {
        oSkillEffect800260 = (tSkillEffectFn)InstallInlineHook(
            ADDR_800260, (void *)hkSkillEffect800260);
        if (oSkillEffect800260)
        {
            ok = true;
            WriteLogFmt("[SuperPassiveEffectHook] OK(800260): tramp=0x%08X", (DWORD)(uintptr_t)oSkillEffect800260);
        }
        else
        {
            WriteLog("[SuperPassiveEffectHook] hook failed: 800260");
        }
    }
    else
    {
        ok = true;
    }

    if (!oSkillEffect800580)
    {
        oSkillEffect800580 = (tSkillEffectFn)InstallInlineHook(
            ADDR_800580, (void *)hkSkillEffect800580);
        if (oSkillEffect800580)
        {
            ok = true;
            WriteLogFmt("[SuperPassiveEffectHook] OK(800580): tramp=0x%08X", (DWORD)(uintptr_t)oSkillEffect800580);
        }
        else
        {
            WriteLog("[SuperPassiveEffectHook] hook failed: 800580");
        }
    }
    else
    {
        ok = true;
    }

    if (!oPassiveEffectDamage43DE00)
    {
        oPassiveEffectDamage43DE00 = (tPassiveEffectGetterFn)InstallInlineHook(
            ADDR_43DE00, (void *)hkPassiveEffectDamage43DE00);
        if (oPassiveEffectDamage43DE00)
        {
            ok = true;
            WriteLogFmt("[SuperPassiveGetterHook] OK(43DE00 damage): tramp=0x%08X", (DWORD)(uintptr_t)oPassiveEffectDamage43DE00);
        }
        else
        {
            WriteLog("[SuperPassiveGetterHook] hook failed: 43DE00 damage");
        }
    }
    else
    {
        ok = true;
    }

    if (!oPassiveEffectDamage43DE50)
    {
        oPassiveEffectDamage43DE50 = (tPassiveEffectGetterFn)InstallInlineHook(
            ADDR_43DE50, (void *)hkPassiveEffectDamage43DE50);
        if (oPassiveEffectDamage43DE50)
        {
            ok = true;
            WriteLogFmt("[SuperPassiveGetterHook] OK(43DE50 damageAlt): tramp=0x%08X", (DWORD)(uintptr_t)oPassiveEffectDamage43DE50);
        }
        else
        {
            WriteLog("[SuperPassiveGetterHook] hook failed: 43DE50 damageAlt");
        }
    }
    else
    {
        ok = true;
    }

    if (!oPassiveEffectAttackCount5E9EE0)
    {
        oPassiveEffectAttackCount5E9EE0 = (tPassiveEffectGetterFn)InstallInlineHook(
            ADDR_5E9EE0, (void *)hkPassiveEffectAttackCount5E9EE0);
        if (oPassiveEffectAttackCount5E9EE0)
        {
            ok = true;
            WriteLogFmt("[SuperPassiveGetterHook] OK(5E9EE0 attackCount): tramp=0x%08X", (DWORD)(uintptr_t)oPassiveEffectAttackCount5E9EE0);
        }
        else
        {
            WriteLog("[SuperPassiveGetterHook] hook failed: 5E9EE0 attackCount");
        }
    }
    else
    {
        ok = true;
    }

    if (!oPassiveEffectMobCount7D1990)
    {
        oPassiveEffectMobCount7D1990 = (tPassiveEffectGetterFn)InstallInlineHook(
            ADDR_7D1990, (void *)hkPassiveEffectMobCount7D1990);
        if (oPassiveEffectMobCount7D1990)
        {
            ok = true;
            WriteLogFmt("[SuperPassiveGetterHook] OK(7D1990 mobCount): tramp=0x%08X", (DWORD)(uintptr_t)oPassiveEffectMobCount7D1990);
        }
        else
        {
            WriteLog("[SuperPassiveGetterHook] hook failed: 7D1990 mobCount");
        }
    }
    else
    {
        ok = true;
    }

    if (!oPassiveEffectAttackCount7D19E0)
    {
        oPassiveEffectAttackCount7D19E0 = (tPassiveEffectGetterFn)InstallInlineHook(
            ADDR_7D19E0, (void *)hkPassiveEffectAttackCount7D19E0);
        if (oPassiveEffectAttackCount7D19E0)
        {
            ok = true;
            WriteLogFmt("[SuperPassiveGetterHook] OK(7D19E0 attackCount): tramp=0x%08X", (DWORD)(uintptr_t)oPassiveEffectAttackCount7D19E0);
        }
        else
        {
            WriteLog("[SuperPassiveGetterHook] hook failed: 7D19E0 attackCount");
        }
    }
    else
    {
        ok = true;
    }

    if (!oPassiveEffectIgnore7D28E0)
    {
        oPassiveEffectIgnore7D28E0 = (tPassiveEffectGetterFn)InstallInlineHook(
            ADDR_7D28E0, (void *)hkPassiveEffectIgnore7D28E0);
        if (oPassiveEffectIgnore7D28E0)
        {
            ok = true;
            WriteLogFmt("[SuperPassiveGetterHook] OK(7D28E0 ignoreMobpdpR): tramp=0x%08X", (DWORD)(uintptr_t)oPassiveEffectIgnore7D28E0);
        }
        else
        {
            WriteLog("[SuperPassiveGetterHook] hook failed: 7D28E0 ignoreMobpdpR");
        }
    }
    else
    {
        ok = true;
    }

    return ok;
}

static bool SetupNativeTextGlyphHook()
{
    if (SafeIsBadReadPtr((void *)ADDR_5000E520, 8))
    {
        WriteLogFmt("[NativeText] glyph target unreadable: 0x%08X", ADDR_5000E520);
        return false;
    }

    oNativeGlyphLookup = (tNativeGlyphLookupFn)InstallInlineHook(
        ADDR_5000E520, (void *)hkNativeGlyphLookup);
    if (!oNativeGlyphLookup)
    {
        WriteLog("[NativeText] glyph hook failed: 5000E520");
        return false;
    }

    RetroSkillDWriteRegisterNativeGlyphLookup((void *)oNativeGlyphLookup);
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
        ADDR_9D95A0, (void *)hkSkillWndMoveNaked);
    if (!oSkillWndMove)
    {
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
        ADDR_9E1770, (void *)hkSkillWndRefreshNaked);
    if (!oSkillWndRefresh)
    {
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
        ADDR_9DEE30, (void *)hkSkillWndDrawNaked);
    if (!oSkillWndDraw)
    {
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
        (BYTE *)ADDR_BBC965,
        (void *)hkPostB9F6E0DrawNaked,
        9);
    if (!oPostB9F6E0DrawContinue)
    {
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
        ADDR_9E14D0, (void *)hkSkillWndDtorNaked);
    if (!oSkillWndDtor)
    {
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
    if (g_OriginalWndProc && g_GameHwnd)
        return true;

    g_GameHwnd = GetRealGameWindow();
    if (!g_GameHwnd)
    {
        WriteLog("[WndProc] Game window not found");
        return false;
    }
    g_OriginalWndProc = (WNDPROC)SetWindowLongPtrA(g_GameHwnd, GWLP_WNDPROC, (LONG_PTR)GameWndProc);
    WriteLogFmt("[WndProc] Hooked: 0x%08X", (DWORD)g_OriginalWndProc);
    return g_OriginalWndProc != nullptr;
}

static void EnsureDeferredInteractionHooks(const char *reason)
{
    static DWORD s_lastWndProcRetryTick = 0;
    static bool s_inputSpoofAttempted = false;

    const DWORD now = GetTickCount();

    if (!g_OriginalWndProc && (now - s_lastWndProcRetryTick >= 1000))
    {
        s_lastWndProcRetryTick = now;
        if (SetupWndProcHook())
        {
            WriteLogFmt("[WndProc] deferred install OK reason=%s", reason ? reason : "unknown");
        }
        else
        {
            WriteLogFmt("[WndProc] deferred install pending reason=%s", reason ? reason : "unknown");
        }
    }

    if (!s_inputSpoofAttempted)
    {
        s_inputSpoofAttempted = true;
        if (!Win32InputSpoofInstall())
            WriteLogFmt("[InputSpoof] deferred install failed reason=%s", reason ? reason : "unknown");
        else
            WriteLogFmt("[InputSpoof] deferred install OK reason=%s", reason ? reason : "unknown");
    }
}

// ============================================================================
