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

    if (IsOfficialSecondChildObject(wndObj, true, true)) {
        // official second-child 的 COM/render 偶尔会被旧路径留下脏值；move 成功后强行对齐一次，避免拖动期位置漂移
        CWnd_SetRenderPos(wndObj, x, y);
        CWnd_SetComPos(wndObj, x, y);
        LogOfficialSecondChildState(wndObj, logTag ? logTag : "MoveChild");
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

typedef int (__stdcall *tSecondChildMsgFn)(DWORD msg, DWORD a2, DWORD a3, DWORD a4);

static void LogOfficialSecondChildState(uintptr_t wndObj, const char* tag)
{
    if (InterlockedDecrement(&g_SecondChildStateLogBudget) < 0)
        return;

    uintptr_t slot = g_SkillWndThis ? GetSkillWndSecondChildPtr(g_SkillWndThis) : 0;
    if (!wndObj) {
        WriteLogFmt("[%s] child=null slot=0x%08X panel=(%d,%d)",
            tag ? tag : "SecondChildState",
            (DWORD)slot,
            g_PanelDrawX,
            g_PanelDrawY);
        return;
    }

    if (SafeIsBadReadPtr((void*)wndObj, 0x84)) {
        WriteLogFmt("[%s] child=0x%08X unreadable slot=0x%08X panel=(%d,%d)",
            tag ? tag : "SecondChildState",
            (DWORD)wndObj,
            (DWORD)slot,
            g_PanelDrawX,
            g_PanelDrawY);
        return;
    }

    DWORD vt1 = *(DWORD*)(wndObj + 0x00);
    DWORD vt2 = *(DWORD*)(wndObj + 0x04);
    DWORD vt3 = *(DWORD*)(wndObj + 0x08);
    int refCount = *(int*)(wndObj + CWND_OFF_REFCNT * 4);
    int width = *(int*)(wndObj + CWND_OFF_W * 4);
    int height = *(int*)(wndObj + CWND_OFF_H * 4);
    int z = *(int*)(wndObj + CWND_OFF_ZORDER * 4);
    int renderX = CWnd_GetRenderX(wndObj);
    int renderY = CWnd_GetRenderY(wndObj);
    int comX = CWnd_GetX(wndObj);
    int comY = CWnd_GetY(wndObj);
    uintptr_t surface = *(uintptr_t*)(wndObj + CWND_OFF_COM * 4);

    WriteLogFmt("[%s] child=0x%08X slot=0x%08X vt=[%08X,%08X,%08X] ref=%d wh=(%d,%d) z=%d render=(%d,%d) com=(%d,%d) panel=(%d,%d) surface=0x%08X",
        tag ? tag : "SecondChildState",
        (DWORD)wndObj,
        (DWORD)slot,
        vt1, vt2, vt3,
        refCount,
        width, height,
        z,
        renderX, renderY,
        comX, comY,
        g_PanelDrawX, g_PanelDrawY,
        (DWORD)surface);
}

static bool IsOfficialSecondChildObject(uintptr_t wndObj, bool allowCustomVT1, bool allowCustomVT2)
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
    bool vt2Ok = (vt2 == ADDR_VT_SkillWndSecondChild2);
    if (!vt2Ok && allowCustomVT2 && g_CustomVTable2) {
        vt2Ok = (vt2 == (DWORD)g_CustomVTable2);
    }

    if (!vt1Ok || !vt2Ok || vt3 != ADDR_VT_SkillWndSecondChild3) {
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

    if (!IsOfficialSecondChildObject(wndObj, false, false)) {
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

static int __stdcall SuperSecondChildMsgHook(DWORD msg, DWORD a2, DWORD a3, DWORD a4)
{
    uintptr_t slot = g_SkillWndThis ? GetSkillWndSecondChildPtr(g_SkillWndThis) : 0;
    bool ours =
        g_SuperExpanded &&
        g_SuperUsesSkillWndSecondSlot &&
        g_SuperCWnd &&
        slot == g_SuperCWnd &&
        slot == g_CustomVTable2OwnerChild;
    bool mouseUpCloseMsg = (msg == WM_LBUTTONUP || msg == WM_RBUTTONUP);

    if (mouseUpCloseMsg && ours) {
        if (InterlockedDecrement(&g_SecondChildMouseSwallowBudget) >= 0) {
            WriteLogFmt("[NativeWnd:VT2] swallow msg=0x%04X child=0x%08X slot=0x%08X a=[%08X,%08X,%08X]",
                msg, (DWORD)g_SuperCWnd, (DWORD)slot, a2, a3, a4);
            LogOfficialSecondChildState(slot, "NativeWnd:VT2:SwallowState");
        }
        return 0;
    }

    if ((msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) &&
        InterlockedDecrement(&g_SecondChildMousePassBudget) >= 0) {
        WriteLogFmt("[NativeWnd:VT2] pass msg=0x%04X child=0x%08X slot=0x%08X ours=%d a=[%08X,%08X,%08X]",
            msg, (DWORD)g_SuperCWnd, (DWORD)slot, ours ? 1 : 0, a2, a3, a4);
    }

    tSecondChildMsgFn orig = (tSecondChildMsgFn)g_OriginalSecondChildMsgFn;
    return orig ? orig(msg, a2, a3, a4) : 0;
}

static bool ApplySuperChildCustomMouseGuardVTable(uintptr_t wndObj)
{
    if (!IsOfficialSecondChildObject(wndObj, true, false)) {
        WriteLogFmt("[NativeWnd] FAIL: custom VT2 target is not official second-child child=0x%08X", (DWORD)wndObj);
        return false;
    }

    DWORD origVT2 = *(DWORD*)(wndObj + 0x04);
    if (origVT2 == (DWORD)g_CustomVTable2 && g_CustomVTable2OwnerChild == wndObj) {
        WriteLogFmt("[NativeWnd] Custom VT2 already installed: child=0x%08X vt2=0x%08X", (DWORD)wndObj, origVT2);
        return true;
    }
    if (origVT2 != ADDR_VT_SkillWndSecondChild2) {
        WriteLogFmt("[NativeWnd] FAIL: unexpected VT2 target child=0x%08X vt2=0x%08X", (DWORD)wndObj, origVT2);
        return false;
    }

    const int kVT2Entries = 64;
    if (SafeIsBadReadPtr((void*)origVT2, kVT2Entries * sizeof(DWORD))) {
        WriteLogFmt("[NativeWnd] FAIL: original VT2 invalid 0x%08X", origVT2);
        return false;
    }

    if (!g_CustomVTable2) {
        g_CustomVTable2 = (DWORD*)VirtualAlloc(nullptr, kVT2Entries * sizeof(DWORD),
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!g_CustomVTable2) {
            WriteLog("[NativeWnd] FAIL: VirtualAlloc custom VT2");
            return false;
        }
        memcpy(g_CustomVTable2, (void*)origVT2, kVT2Entries * sizeof(DWORD));
    }

    DWORD origMsg = g_CustomVTable2[2];
    if (origMsg && origMsg != (DWORD)&SuperSecondChildMsgHook)
        g_OriginalSecondChildMsgFn = origMsg;
    g_CustomVTable2[2] = (DWORD)&SuperSecondChildMsgHook;
    *(DWORD*)(wndObj + 0x04) = (DWORD)g_CustomVTable2;
    g_CustomVTable2OwnerChild = wndObj;

    WriteLogFmt("[NativeWnd] Custom VT2 installed: child=0x%08X orig=0x%08X new=0x%08X msgOrig=0x%08X hook=0x%08X",
        (DWORD)wndObj, origVT2, (DWORD)g_CustomVTable2, g_OriginalSecondChildMsgFn, (DWORD)&SuperSecondChildMsgHook);
    return true;
}

static void RestoreSuperChildCustomMouseGuardVTable(uintptr_t wndObj, const char* reason)
{
    if (!wndObj || SafeIsBadReadPtr((void*)(wndObj + 0x04), sizeof(DWORD)))
        return;

    DWORD vt2 = *(DWORD*)(wndObj + 0x04);
    if (g_CustomVTable2 && vt2 == (DWORD)g_CustomVTable2) {
        *(DWORD*)(wndObj + 0x04) = ADDR_VT_SkillWndSecondChild2;
        WriteLogFmt("[NativeWnd] Custom VT2 restored: child=0x%08X reason=%s",
            (DWORD)wndObj, reason ? reason : "unknown");
    }

    if (g_CustomVTable2OwnerChild == wndObj)
        g_CustomVTable2OwnerChild = 0;
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
