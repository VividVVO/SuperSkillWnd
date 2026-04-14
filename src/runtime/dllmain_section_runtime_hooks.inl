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
        if (count <= 20 || (count % 500 == 0))
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
static void *oSkillReleaseClassifierRoot = nullptr;
static void *oSkillReleaseClassifier = nullptr;
static void *oSkillReleaseClassifierB2F370 = nullptr;
typedef BOOL(__cdecl *tSkillNativeIdGateFn)(int skillId);
static tSkillNativeIdGateFn oSkillNativeIdGate7CE790 = nullptr;
static tSkillNativeIdGateFn oSkillNativeIdGate7D0000 = nullptr;
typedef int(__cdecl *tMountActionGateFn)(int mountItemId);
static tMountActionGateFn oMountActionGate4069E0 = nullptr;
static tMountActionGateFn oMountActionGate406AB0 = nullptr;
typedef int(__cdecl *tMountNativeFlightSkillMapFn)(int mountItemId);
static tMountNativeFlightSkillMapFn oMountNativeFlightSkillMap7CF370 = nullptr;
typedef int(__thiscall *tMountSoaringGateFn)(void *thisPtr, int levelContext, void *mountContext, int skillId, unsigned int **skillEntryOut);
static tMountSoaringGateFn oMountSoaringGate7DC1B0 = nullptr;
typedef int(__thiscall *tMountContextGetItemIdFn)(void *mountContext);
typedef BOOL(__thiscall *tMountContextIsFlyingFamilyFn)(void *mountContext);
typedef int(__thiscall *tNativeGlyphLookupFn)(void *fontCache, unsigned int codepoint, RECT *outRectOrNull);
static tNativeGlyphLookupFn oNativeGlyphLookup = nullptr;
typedef int(__thiscall *tSkillLevelBaseFn)(void *thisPtr, DWORD playerObj, int skillId, void *cachePtr);
typedef int(__thiscall *tSkillLevelCurrentFn)(void *thisPtr, DWORD playerObj, int skillId, void *cachePtr, int flags);
static tSkillLevelBaseFn oSkillLevelBase = nullptr;
static tSkillLevelCurrentFn oSkillLevelCurrent = nullptr;
typedef void(__thiscall *tSkillPresentationDispatch)(void *thisPtr, int *skillData, int a3, int a4, int a5, int a6, int a7);
static tSkillPresentationDispatch oSkillPresentationDispatch = nullptr;
static volatile DWORD g_ClassifierOverrideSkillId = 0;
static volatile DWORD g_ForcedNativeReleaseJump = 0;

static void __cdecl hkSendPacketInspect(void *packetData, int packetLen, uintptr_t callerRetAddr)
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

static bool IsExtendedMountActionGateMount(int mountItemId)
{
    // return false;
    return true;
   // return false;
    // 客户端 sub_4069E0 仍只硬编码放行到 1992015，导致 1999xxx 自定义坐骑
    // 即使 WZ 带 ladder/rope 资源、服务端也认可攀爬，case 51/52 仍会直接回退。
    if (mountItemId >= 1932016 && mountItemId <= 1999999)
    {
        return true;
    }
    return false;
}

static int ResolveExtendedMountNativeFlightSkillId(int mountItemId)
{
    // return 0;
    return 80001077;
    //return 0;
    // 客户端原生只为 1992000..1992015 建了飞行技能映射。
    // 对 1999xxx 自定义坐骑统一复用一条稳定 donor 飞行链，避免“服务端允许飞行，
    // 但本地 jump+up / 二次起飞 / 80001089 链仍查不到 skillId”。
    if (mountItemId >= 1932016 && mountItemId <= 1999999)
    {
        return 80001077;
    }
    return 0;
}

static int __cdecl hkMountActionGate4069E0(int mountItemId)
{
    if (IsExtendedMountActionGateMount(mountItemId))
    {
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

    if (mountItemId == 1932031 || mountItemId == 1932163)
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

    if (mountItemId == 1932031 || mountItemId == 1932163)
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

    tMountContextIsFlyingFamilyFn isFlyingFamilyFn =
        reinterpret_cast<tMountContextIsFlyingFamilyFn>(ADDR_7D4CD0);
    tMountContextGetItemIdFn getItemIdFn =
        reinterpret_cast<tMountContextGetItemIdFn>(ADDR_7D4CA0);
    if (!isFlyingFamilyFn || !getItemIdFn || !isFlyingFamilyFn(mountContext))
    {
        return result;
    }

    const int mountItemId = getItemIdFn(mountContext);
    const int mappedNativeFlightSkillId = ResolveExtendedMountNativeFlightSkillId(mountItemId);
    if (mappedNativeFlightSkillId <= 0)
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

    return 1;
}

static BOOL ResolveForcedNativeSkillGateAllow(int originalSkillId, int mappedSkillId)
{
    return SkillOverlayBridgeShouldForceNativeGateAllow(originalSkillId) ||
           (mappedSkillId != originalSkillId &&
            SkillOverlayBridgeShouldForceNativeGateAllow(mappedSkillId));
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
    if (mappedSkillId != skillId)
    {
        WriteLogFmt("[SkillGate] 7CE790 map custom=%d donor=%d result=%d",
                    skillId, mappedSkillId, result ? 1 : 0);
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
    if (mappedSkillId != skillId)
    {
        WriteLogFmt("[SkillGate] 7D0000 map custom=%d donor=%d result=%d",
                    skillId, mappedSkillId, result ? 1 : 0);
    }
    return result;
}

static int __fastcall hkSkillLevelBase(void *thisPtr, void * /*edxUnused*/, DWORD playerObj, int skillId, void *cachePtr)
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

static int __fastcall hkSkillLevelCurrent(void *thisPtr, void * /*edxUnused*/, DWORD playerObj, int skillId, void *cachePtr, int flags)
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
        const bool overlayUiReady = g_Ready && g_SkillWndThis && g_NativeBtnCreated;
        if (overlayUiReady && !SuperImGuiOverlayIsInitialized() && g_GameHwnd && pDevice)
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

        if (overlayUiReady && SuperImGuiOverlayIsInitialized())
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

        return fnOrigPresent ? fnOrigPresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion) : D3D_OK;
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
            const bool overlayActivationReady = g_Ready && g_SkillWndThis && g_NativeBtnCreated;

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

    SkillOverlayBridgeSetResetPreviewReceiveHookReady(sendHookOk && recvHookOk);
    return sendHookOk && recvHookOk;
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
