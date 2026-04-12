// Isolated one-shot probe for making the official SkillWnd 9DC220 second-child
// the SuperSkillWnd primary carrier.
//
// This file is intentionally not listed in build.bat. It is a drop-in test
// module: add it to the build only when you want one DLL run to collect all
// carrier migration data. Nothing in the current project calls these exports.
//
// Suggested one-shot flags:
//   0x0000007F = create + allow replace + patch VT1 + patch VT2 + native smoke
//              + image candidates + layer dump.
//   0x0000017F = same, then release carrier before returning.
//
// Log prefix:
//   [CarrierProbe:*] goes to C:\SuperSkillWnd.log through Common.h.

#include "core/Common.h"
#include "core/GameAddresses.h"
#include "skill/skill_overlay_bridge.h"
#include "ui/retro_skill_app.h"
#include "ui/retro_skill_state.h"

#include <oleauto.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
static const int kProbePanelW = 174;
static const int kProbePanelH = 299;
static const int kProbePanelLeftGap = 0;
static const int kProbeSecondChildMode = 1;
static const int kProbeSecondChildVtDeltaX = 0;
static const int kProbeSecondChildVtDeltaY = 2;

enum ProbeFlags : DWORD
{
    ProbeFlag_CreateCarrier       = 0x00000001,
    ProbeFlag_AllowReplaceSlot    = 0x00000002,
    ProbeFlag_PatchVT1Draw        = 0x00000004,
    ProbeFlag_PatchVT2MouseGuard  = 0x00000008,
    ProbeFlag_NativeSmokeRender   = 0x00000010,
    ProbeFlag_ResolveCandidates   = 0x00000020,
    ProbeFlag_DumpLayerState      = 0x00000040,
    ProbeFlag_AutoReleaseCarrier  = 0x00000100,
};

struct ProbeCarrierState
{
    uintptr_t skillWnd = 0;
    uintptr_t child = 0;
    uintptr_t lastSlot = 0;
    DWORD createdTick = 0;
    DWORD lastSeenTick = 0;
    DWORD lastMouseMsg = 0;
    DWORD lastGuardTick = 0;
    int generation = 0;
    int panelX = -9999;
    int panelY = -9999;
    bool vt1Patched = false;
    bool vt2Patched = false;
    bool alive = false;
    bool lostByMouseUp = false;
};

enum NativePanelHitKind
{
    NativePanelHit_None = 0,
    NativePanelHit_PassiveTab,
    NativePanelHit_ActiveTab,
    NativePanelHit_SkillIcon,
    NativePanelHit_SkillRow,
    NativePanelHit_PlusButton,
    NativePanelHit_ScrollThumb,
    NativePanelHit_InitButton,
};

struct NativePanelHit
{
    NativePanelHitKind kind = NativePanelHit_None;
    int tab = -1;
    int skillIndex = -1;
    int skillId = 0;
    int relX = 0;
    int relY = 0;
    int rowLocalY = 0;
};

struct NativePanelInput
{
    int mouseX = -9999;
    int mouseY = -9999;
    int wheel = 0;
    bool lDown = false;
    bool lUp = false;
    bool rUp = false;
    bool lHeld = false;
};

struct NativePanelRenderContext
{
    DWORD* surface = nullptr;
    int panelX = 0;
    int panelY = 0;
    int width = kProbePanelW;
    int height = kProbePanelH;
    float scale = 1.0f;
};

struct NativeImageCandidate
{
    const wchar_t* key;
    const wchar_t* paths[4];
    int drawX;
    int drawY;
};

typedef int (__cdecl *tCloneVariant)(VARIANTARG* pvarg, VARIANTARG* pvargSrc);
typedef DWORD* (__thiscall *tGetSurface)(uintptr_t thisPtr, DWORD** outSurface);
typedef int (__thiscall *tSurfaceDrawImage)(DWORD* surface, int x, int y, int imageObj, DWORD* alphaVar);
typedef LONG (__thiscall *tMoveNativeWnd)(uintptr_t thisPtr, int x, int y);
typedef int (__stdcall *tSecondChildMsgFn)(DWORD msg, DWORD a2, DWORD a3, DWORD a4);

static ProbeCarrierState g_probeCarrier = {};
static DWORD* g_probeCustomVT1 = nullptr;
static DWORD* g_probeCustomVT2 = nullptr;
static DWORD g_probeOriginalSecondChildMsgFn = ADDR_9D98F0;
static uintptr_t g_probeCustomVT2OwnerChild = 0;
static volatile LONG g_probeDrawLogBudget = 80;
static volatile LONG g_probeMsgLogBudget = 96;
static volatile LONG g_probeMsgSwallowBudget = 96;
static RetroSkillRuntimeState g_probePanelState;
static RetroSkillBehaviorHooks g_probePanelHooks = {};
static bool g_probePanelHooksReady = false;
static NativePanelInput g_probePanelInput = {};

static const NativeImageCandidate kProbeImageCandidates[] = {
    { L"Passive.0",        { L"UI/UIWindow2.img/SkillEx/Passive/0", L"UI/UIWindow2.img/SkillEx/main/Passive/0", nullptr, nullptr }, 10, 27 },
    { L"Passive.1",        { L"UI/UIWindow2.img/SkillEx/Passive/1", L"UI/UIWindow2.img/SkillEx/main/Passive/1", nullptr, nullptr }, 10, 27 },
    { L"ActivePassive.0",  { L"UI/UIWindow2.img/SkillEx/ActivePassive/0", L"UI/UIWindow2.img/SkillEx/main/ActivePassive/0", nullptr, nullptr }, 89, 27 },
    { L"ActivePassive.1",  { L"UI/UIWindow2.img/SkillEx/ActivePassive/1", L"UI/UIWindow2.img/SkillEx/main/ActivePassive/1", nullptr, nullptr }, 89, 27 },
    { L"skill0",           { L"UI/UIWindow2.img/Skill/main/skill0", L"UI/UIWindow2.img/SkillEx/main/skill0", nullptr, nullptr }, 9, 93 },
    { L"skill1",           { L"UI/UIWindow2.img/Skill/main/skill1", L"UI/UIWindow2.img/SkillEx/main/skill1", nullptr, nullptr }, 9, 133 },
    { L"BtSpUp.normal",    { L"UI/UIWindow2.img/SkillEx/main/BtSpUp/normal/0", L"UI/UIWindow2.img/Skill/main/BtSpUp/normal/0", nullptr, nullptr }, 134, 114 },
    { L"BtSpUp.disabled",  { L"UI/UIWindow2.img/SkillEx/main/BtSpUp/disabled/0", L"UI/UIWindow2.img/Skill/main/BtSpUp/disabled/0", nullptr, nullptr }, 134, 154 },
    { L"BtSpUp.pressed",   { L"UI/UIWindow2.img/SkillEx/main/BtSpUp/pressed/0", L"UI/UIWindow2.img/Skill/main/BtSpUp/pressed/0", nullptr, nullptr }, 134, 194 },
    { L"BtSpUp.mouseOver", { L"UI/UIWindow2.img/SkillEx/main/BtSpUp/mouseOver/0", L"UI/UIWindow2.img/Skill/main/BtSpUp/mouseOver/0", nullptr, nullptr }, 134, 234 },
};

static bool ProbeReadDword(uintptr_t address, DWORD* outValue)
{
    if (outValue) *outValue = 0;
    if (!address || !outValue || SafeIsBadReadPtr(reinterpret_cast<void*>(address), sizeof(DWORD)))
        return false;
    *outValue = *reinterpret_cast<DWORD*>(address);
    return true;
}

static DWORD ProbeReadDwordOrZero(uintptr_t address)
{
    DWORD value = 0;
    ProbeReadDword(address, &value);
    return value;
}

static uintptr_t ProbeReadGlobalSkillWnd()
{
    if (SafeIsBadReadPtr(reinterpret_cast<void*>(ADDR_SkillWndEx), sizeof(DWORD)))
        return 0;
    return *reinterpret_cast<DWORD*>(ADDR_SkillWndEx);
}

static uintptr_t ProbeGetSecondChildPtr(uintptr_t skillWndThis)
{
    if (!skillWndThis || SafeIsBadReadPtr(reinterpret_cast<void*>(skillWndThis + 3048), sizeof(DWORD)))
        return 0;
    return *reinterpret_cast<DWORD*>(skillWndThis + 3048);
}

static bool ProbeGetSkillWndAnchorPos(uintptr_t skillWndThis, int* outX, int* outY)
{
    if (!skillWndThis || !outX || !outY)
        return false;
    uintptr_t thisForVT = skillWndThis + 4;
    if (SafeIsBadReadPtr(reinterpret_cast<void*>(thisForVT), sizeof(DWORD)))
        return false;
    DWORD vt = *reinterpret_cast<DWORD*>(thisForVT);
    if (!vt || SafeIsBadReadPtr(reinterpret_cast<void*>(vt + 0x30), sizeof(DWORD) * 2))
        return false;
    DWORD fnGetX = *reinterpret_cast<DWORD*>(vt + 0x30);
    DWORD fnGetY = *reinterpret_cast<DWORD*>(vt + 0x34);
    if (!fnGetX || !fnGetY)
        return false;

    int x = 0;
    int y = 0;
    DWORD ecxVal = static_cast<DWORD>(thisForVT);
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
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (x < -10000 || x > 10000 || y < -10000 || y > 10000)
        return false;
    *outX = x;
    *outY = y;
    return true;
}

static bool ProbeGetSkillWndComPos(uintptr_t skillWndThis, int* outX, int* outY)
{
    if (!skillWndThis || !outX || !outY)
        return false;
    int x = CWnd_GetX(skillWndThis);
    int y = CWnd_GetY(skillWndThis);
    if (x < -10000 || x > 10000 || y < -10000 || y > 10000)
        return false;
    *outX = x;
    *outY = y;
    return true;
}

static bool ProbeComputePanelPos(uintptr_t skillWndThis, int explicitPanelX, int explicitPanelY, int* outX, int* outY, const char** outSrc)
{
    if (!outX || !outY)
        return false;
    if (outSrc) *outSrc = "none";
    if (explicitPanelX > -9000 && explicitPanelY > -9000) {
        *outX = explicitPanelX;
        *outY = explicitPanelY;
        if (outSrc) *outSrc = "explicit";
        return true;
    }
    int vtX = 0;
    int vtY = 0;
    if (ProbeGetSkillWndAnchorPos(skillWndThis, &vtX, &vtY)) {
        *outX = vtX - kProbePanelW + kProbeSecondChildVtDeltaX;
        *outY = vtY + kProbeSecondChildVtDeltaY;
        if (outSrc) *outSrc = "skill_vt_second_child";
        return true;
    }
    int comX = 0;
    int comY = 0;
    if (ProbeGetSkillWndComPos(skillWndThis, &comX, &comY)) {
        *outX = comX - kProbePanelW - kProbePanelLeftGap;
        *outY = comY;
        if (outSrc) *outSrc = "skill_com_fallback";
        return true;
    }
    return false;
}

static void ProbeDumpAddresses()
{
    WriteLogFmt("[CarrierProbe:Addr] SkillWndEx=0x%08X 9DC220=0x%08X 9D98F0=0x%08X 9D93A0=0x%08X B9E880=0x%08X",
        ADDR_SkillWndEx, ADDR_9DC220, ADDR_9D98F0, ADDR_9D93A0, ADDR_B9E880);
    WriteLogFmt("[CarrierProbe:Addr] VT1=0x%08X VT2=0x%08X VT3=0x%08X B9AB50=0x%08X 56D630=0x%08X B9A5D0=0x%08X",
        ADDR_VT_SkillWndSecondChild1, ADDR_VT_SkillWndSecondChild2, ADDR_VT_SkillWndSecondChild3,
        ADDR_B9AB50, ADDR_56D630, ADDR_B9A5D0);
    WriteLogFmt("[CarrierProbe:Addr] Surface=0x%08X DrawImage=0x%08X ResRootPtr=0x%08X pvargSrc=0x%08X",
        ADDR_435A50, ADDR_401C90, ADDR_F6A84C, ADDR_pvargSrc);
}

static void ProbeDumpSlot(uintptr_t skillWndThis, const char* tag)
{
    if (!skillWndThis) {
        WriteLogFmt("[CarrierProbe:%s] skillWnd=null", tag ? tag : "Slot");
        return;
    }
    const uintptr_t wrap = skillWndThis + 3044;
    DWORD wrap0 = ProbeReadDwordOrZero(wrap + 0);
    DWORD slot = ProbeReadDwordOrZero(wrap + 4);
    DWORD wrap8 = ProbeReadDwordOrZero(wrap + 8);
    DWORD wrap12 = ProbeReadDwordOrZero(wrap + 12);
    WriteLogFmt("[CarrierProbe:%s] skillWnd=0x%08X wrap=0x%08X wrap[0,4,8,C]=[%08X,%08X,%08X,%08X]",
        tag ? tag : "Slot",
        static_cast<DWORD>(skillWndThis),
        static_cast<DWORD>(wrap),
        wrap0,
        slot,
        wrap8,
        wrap12);
}

static void ProbeDumpCWndCore(uintptr_t wndObj, const char* tag)
{
    if (!wndObj) {
        WriteLogFmt("[CarrierProbe:%s] wnd=null", tag ? tag : "CWnd");
        return;
    }
    if (SafeIsBadReadPtr(reinterpret_cast<void*>(wndObj), 0x84)) {
        WriteLogFmt("[CarrierProbe:%s] wnd=0x%08X unreadable size=0x84",
            tag ? tag : "CWnd",
            static_cast<DWORD>(wndObj));
        return;
    }

    DWORD vt1 = *reinterpret_cast<DWORD*>(wndObj + 0x00);
    DWORD vt2 = *reinterpret_cast<DWORD*>(wndObj + 0x04);
    DWORD vt3 = *reinterpret_cast<DWORD*>(wndObj + 0x08);
    int refCount = *reinterpret_cast<int*>(wndObj + CWND_OFF_REFCNT * 4);
    int width = *reinterpret_cast<int*>(wndObj + CWND_OFF_W * 4);
    int height = *reinterpret_cast<int*>(wndObj + CWND_OFF_H * 4);
    int zOrder = *reinterpret_cast<int*>(wndObj + CWND_OFF_ZORDER * 4);
    DWORD surface = *reinterpret_cast<DWORD*>(wndObj + CWND_OFF_COM * 4);
    int renderX = CWnd_GetRenderX(wndObj);
    int renderY = CWnd_GetRenderY(wndObj);
    int comX = CWnd_GetX(wndObj);
    int comY = CWnd_GetY(wndObj);

    WriteLogFmt("[CarrierProbe:%s] wnd=0x%08X vt=[%08X,%08X,%08X] ref=%d wh=(%d,%d) z=%d surface=0x%08X render=(%d,%d) com=(%d,%d)",
        tag ? tag : "CWnd",
        static_cast<DWORD>(wndObj),
        vt1, vt2, vt3,
        refCount, width, height, zOrder, surface,
        renderX, renderY, comX, comY);
    WriteLogFmt("[CarrierProbe:%s] dwords +30..+4C=[%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X]",
        tag ? tag : "CWnd",
        ProbeReadDwordOrZero(wndObj + 0x30),
        ProbeReadDwordOrZero(wndObj + 0x34),
        ProbeReadDwordOrZero(wndObj + 0x38),
        ProbeReadDwordOrZero(wndObj + 0x3C),
        ProbeReadDwordOrZero(wndObj + 0x40),
        ProbeReadDwordOrZero(wndObj + 0x44),
        ProbeReadDwordOrZero(wndObj + 0x48),
        ProbeReadDwordOrZero(wndObj + 0x4C));

    if (surface && !SafeIsBadReadPtr(reinterpret_cast<void*>(surface + 0x78), sizeof(DWORD))) {
        WriteLogFmt("[CarrierProbe:%s:Surface] surface=0x%08X canvas=(%d,%d) logical=(%d,%d) pos=(%d,%d)",
            tag ? tag : "CWnd",
            surface,
            ProbeReadDwordOrZero(surface + 0x5C),
            ProbeReadDwordOrZero(surface + 0x60),
            ProbeReadDwordOrZero(surface + 0x6C),
            ProbeReadDwordOrZero(surface + 0x70),
            ProbeReadDwordOrZero(surface + COM_OFF_X),
            ProbeReadDwordOrZero(surface + COM_OFF_Y));
    }
}

static bool ProbeIsOfficialSecondChild(uintptr_t wndObj, bool allowCustomVT1, bool allowCustomVT2)
{
    if (!wndObj || SafeIsBadReadPtr(reinterpret_cast<void*>(wndObj), 0x84))
        return false;
    DWORD vt1 = *reinterpret_cast<DWORD*>(wndObj + 0x00);
    DWORD vt2 = *reinterpret_cast<DWORD*>(wndObj + 0x04);
    DWORD vt3 = *reinterpret_cast<DWORD*>(wndObj + 0x08);
    bool vt1Ok = (vt1 == ADDR_VT_SkillWndSecondChild1);
    if (!vt1Ok && allowCustomVT1 && g_probeCustomVT1)
        vt1Ok = (vt1 == reinterpret_cast<DWORD>(g_probeCustomVT1));
    bool vt2Ok = (vt2 == ADDR_VT_SkillWndSecondChild2);
    if (!vt2Ok && allowCustomVT2 && g_probeCustomVT2)
        vt2Ok = (vt2 == reinterpret_cast<DWORD>(g_probeCustomVT2));
    if (!vt1Ok || !vt2Ok || vt3 != ADDR_VT_SkillWndSecondChild3)
        return false;
    int refCount = *reinterpret_cast<int*>(wndObj + CWND_OFF_REFCNT * 4);
    int width = *reinterpret_cast<int*>(wndObj + CWND_OFF_W * 4);
    int height = *reinterpret_cast<int*>(wndObj + CWND_OFF_H * 4);
    return refCount > 0 && refCount < 100 && width > 0 && height > 0 && width <= 4096 && height <= 4096;
}

static void ProbeReleaseUiObj(void* obj)
{
    if (!obj)
        return;
    __try {
        DWORD vt = *reinterpret_cast<DWORD*>(obj);
        if (vt && !SafeIsBadReadPtr(reinterpret_cast<void*>(vt + 8), sizeof(DWORD)))
            (*(void (__stdcall **)(void*))(vt + 8))(obj);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void ProbeClearGameVariant(VARIANTARG* pv)
{
    if (!pv)
        return;
    __try {
        if (pv->vt == VT_BSTR) {
            LONG lVal = pv->lVal;
            pv->vt = VT_EMPTY;
            if (lVal && !SafeIsBadReadPtr(reinterpret_cast<void*>(ADDR_F671CC_PTR), sizeof(DWORD))) {
                DWORD fnPtr = *reinterpret_cast<DWORD*>(ADDR_F671CC_PTR);
                if (fnPtr) {
                    typedef void (__stdcall *tReleaseGameString)(LONG);
                    reinterpret_cast<tReleaseGameString>(fnPtr)(lVal - 4);
                }
            }
            return;
        }
        VariantClear(pv);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static bool ProbeResolveNativeImage(const unsigned short* pathWide, void** outImage, const char* logTag)
{
    if (outImage) *outImage = nullptr;
    if (!pathWide || !outImage)
        return false;
    if (SafeIsBadReadPtr(reinterpret_cast<void*>(ADDR_F6A84C), sizeof(DWORD))) {
        WriteLogFmt("[CarrierProbe:%s] FAIL resRootPtr unreadable", logTag ? logTag : "Resolve");
        return false;
    }
    uintptr_t resRoot = *reinterpret_cast<DWORD*>(ADDR_F6A84C);
    if (!resRoot) {
        WriteLogFmt("[CarrierProbe:%s] FAIL resRoot=null path=%S", logTag ? logTag : "Resolve", pathWide);
        return false;
    }
    VARIANTARG* pvargSrc = reinterpret_cast<VARIANTARG*>(ADDR_pvargSrc);
    if (SafeIsBadReadPtr(pvargSrc, sizeof(VARIANTARG))) {
        WriteLogFmt("[CarrierProbe:%s] FAIL pvargSrc unreadable path=%S", logTag ? logTag : "Resolve", pathWide);
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
        reinterpret_cast<tCloneVariant>(ADDR_4016D0)(&vA, pvargSrc);
        stage = "cloneB";
        reinterpret_cast<tCloneVariant>(ADDR_4016D0)(&vB, pvargSrc);
        stage = "resolveChain";
        DWORD resRoot32 = static_cast<DWORD>(resRoot);
        DWORD pathWide32 = reinterpret_cast<DWORD>(pathWide);
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
            WriteLogFmt("[CarrierProbe:%s] FAIL rawObj=null path=%S", logTag ? logTag : "Resolve", pathWide);
            __leave;
        }
        if (hr < 0 || !drawObj) {
            WriteLogFmt("[CarrierProbe:%s] FAIL drawIface hr=0x%08X raw=0x%08X path=%S",
                logTag ? logTag : "Resolve", static_cast<DWORD>(hr), rawObj, pathWide);
            __leave;
        }
        *outImage = reinterpret_cast<void*>(drawObj);
        ok = true;
        WriteLogFmt("[CarrierProbe:%s] OK raw=0x%08X draw=0x%08X path=%S",
            logTag ? logTag : "Resolve", rawObj, drawObj, pathWide);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLogFmt("[CarrierProbe:%s] EXCEPTION stage=%s code=0x%08X path=%S",
            logTag ? logTag : "Resolve", stage, GetExceptionCode(), pathWide);
    }
    ProbeClearGameVariant(&vB);
    ProbeClearGameVariant(&vA);
    ProbeClearGameVariant(&resVar);
    return ok;
}

static bool ProbeNativeDrawImage(DWORD* surface, int x, int y, const unsigned short* path, const char* tag)
{
    if (!surface || !path)
        return false;
    VARIANTARG alphaVar = {};
    VariantInit(&alphaVar);
    alphaVar.vt = VT_I4;
    alphaVar.lVal = 255;
    void* imageObj = nullptr;
    bool ok = false;
    if (ProbeResolveNativeImage(path, &imageObj, tag) && imageObj) {
        __try {
            reinterpret_cast<tSurfaceDrawImage>(ADDR_401C90)(
                surface, x, y, reinterpret_cast<int>(imageObj), reinterpret_cast<DWORD*>(&alphaVar));
            ok = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            WriteLogFmt("[CarrierProbe:%s] EXCEPTION draw code=0x%08X path=%S",
                tag ? tag : "DrawImage", GetExceptionCode(), path);
        }
    }
    WriteLogFmt("[CarrierProbe:%s] draw=%s xy=(%d,%d) surface=0x%08X image=0x%08X path=%S",
        tag ? tag : "DrawImage",
        ok ? "OK" : "FAIL",
        x,
        y,
        reinterpret_cast<DWORD>(surface),
        reinterpret_cast<DWORD>(imageObj),
        path);
    ProbeReleaseUiObj(imageObj);
    ProbeClearGameVariant(&alphaVar);
    return ok;
}

static bool ProbeSurfaceClearRect(DWORD* surface, int x, int y, int w, int h, unsigned char value, const char* tag)
{
    if (!surface || w <= 0 || h <= 0)
        return false;
    bool ok = false;
    __try {
        DWORD vt = *reinterpret_cast<DWORD*>(surface);
        if (vt && !SafeIsBadReadPtr(reinterpret_cast<void*>(vt + 140), sizeof(DWORD))) {
            typedef int (__stdcall *tSurfaceClearRect)(DWORD*, int, int, int, int, char);
            tSurfaceClearRect fnClear = *reinterpret_cast<tSurfaceClearRect*>(vt + 140);
            if (fnClear) {
                fnClear(surface, x, y, w, h, static_cast<char>(value));
                ok = true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLogFmt("[CarrierProbe:%s] EXCEPTION clear rect code=0x%08X surface=0x%08X rect=(%d,%d,%d,%d)",
            tag ? tag : "ClearRect", GetExceptionCode(), reinterpret_cast<DWORD>(surface), x, y, w, h);
    }
    return ok;
}

static void ProbeEnsureNativePanelRuntime()
{
    if (g_probePanelHooksReady)
        return;
    ResetRetroSkillData(g_probePanelState);
    ConfigureRetroSkillDefaultBehaviorHooks(g_probePanelHooks, g_probePanelState);
    SkillOverlayBridgeConfigureHooks(g_probePanelHooks);
    g_probePanelHooksReady = true;
    WriteLogFmt("[CarrierProbe:Step5:Runtime] nativePanel hooks=1 source=%s",
        SkillOverlayBridgeGetActiveSourceName() ? SkillOverlayBridgeGetActiveSourceName() : "(null)");
}

static RECT ProbeRect(int x, int y, int w, int h)
{
    RECT rc = { x, y, x + w, y + h };
    return rc;
}

static bool ProbePtInRect(int x, int y, const RECT& rc)
{
    return x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
}

static NativePanelHit ProbeHitTestNativePanel(RetroSkillRuntimeState& state, int relX, int relY)
{
    NativePanelHit hit = {};
    hit.relX = relX;
    hit.relY = relY;
    if (ProbePtInRect(relX, relY, ProbeRect(10, 27, 76, 20))) {
        hit.kind = NativePanelHit_PassiveTab;
        hit.tab = 0;
        return hit;
    }
    if (ProbePtInRect(relX, relY, ProbeRect(89, 27, 76, 20))) {
        hit.kind = NativePanelHit_ActiveTab;
        hit.tab = 1;
        return hit;
    }
    const int listX = 9;
    const int listY = 93;
    const int listW = 140;
    const int listH = 248 - 93;
    if (ProbePtInRect(relX, relY, ProbeRect(listX, listY, listW, listH))) {
        std::vector<SkillEntry>& skills = (state.activeTab == 0) ? state.passiveSkills : state.activeSkills;
        const int rowStep = 40;
        const int rowH = 35;
        const int scrollPx = state.usesGameSkillData ? state.gameVisibleStartIndex * rowStep : static_cast<int>(state.scrollOffset);
        const int listLocalY = relY - listY + scrollPx;
        const int row = listLocalY / rowStep;
        const int rowLocalY = listLocalY % rowStep;
        if (row >= 0 && row < static_cast<int>(skills.size()) && rowLocalY >= 0 && rowLocalY < rowH) {
            SkillEntry& skill = skills[static_cast<size_t>(row)];
            hit.skillIndex = row;
            hit.skillId = skill.skillId;
            hit.rowLocalY = rowLocalY;
            const int rowLocalX = relX - listX;
            if (rowLocalX >= 127 && rowLocalX < 139 && rowLocalY >= 22 && rowLocalY < 32) {
                hit.kind = NativePanelHit_PlusButton;
                return hit;
            }
            if (rowLocalX >= 3 && rowLocalX < 35 && rowLocalY >= 2 && rowLocalY < 34) {
                hit.kind = NativePanelHit_SkillIcon;
                return hit;
            }
            hit.kind = NativePanelHit_SkillRow;
            return hit;
        }
    }
    if (ProbePtInRect(relX, relY, ProbeRect(kProbePanelW - 60 + 4, kProbePanelH - 26 + 2, 42, 12))) {
        hit.kind = NativePanelHit_InitButton;
        return hit;
    }
    return hit;
}

static const char* ProbeHitKindName(NativePanelHitKind kind)
{
    switch (kind) {
    case NativePanelHit_PassiveTab: return "PassiveTab";
    case NativePanelHit_ActiveTab: return "ActiveTab";
    case NativePanelHit_SkillIcon: return "SkillIcon";
    case NativePanelHit_SkillRow: return "SkillRow";
    case NativePanelHit_PlusButton: return "PlusButton";
    case NativePanelHit_ScrollThumb: return "ScrollThumb";
    case NativePanelHit_InitButton: return "InitButton";
    default: return "None";
    }
}

static bool ProbeApplyNativePanelHit(const NativePanelHit& hit, bool lUp, bool rUp)
{
    if (hit.kind == NativePanelHit_None)
        return false;
    WriteLogFmt("[CarrierProbe:Step5:Hit] kind=%s tab=%d skillIndex=%d skillId=%d rel=(%d,%d) lUp=%d rUp=%d",
        ProbeHitKindName(hit.kind), hit.tab, hit.skillIndex, hit.skillId, hit.relX, hit.relY, lUp ? 1 : 0, rUp ? 1 : 0);
    if (hit.kind == NativePanelHit_PassiveTab && lUp) {
        g_probePanelState.activeTab = 0;
        g_probePanelState.scrollOffset = 0.0f;
        return true;
    }
    if (hit.kind == NativePanelHit_ActiveTab && lUp) {
        g_probePanelState.activeTab = 1;
        g_probePanelState.scrollOffset = 0.0f;
        return true;
    }

    std::vector<SkillEntry>& skills =
        (g_probePanelState.activeTab == 0) ? g_probePanelState.passiveSkills : g_probePanelState.activeSkills;
    if (hit.skillIndex >= 0 && hit.skillIndex < static_cast<int>(skills.size())) {
        SkillEntry& skill = skills[static_cast<size_t>(hit.skillIndex)];
        RetroSkillActionContext ctx = {};
        ctx.currentTab = g_probePanelState.activeTab;
        ctx.targetTab = g_probePanelState.activeTab;
        ctx.skillIndex = hit.skillIndex;
        ctx.skillId = skill.skillId;
        ctx.currentLevel = skill.level;
        ctx.baseLevel = skill.baseLevel;
        ctx.bonusLevel = skill.bonusLevel;
        ctx.maxLevel = skill.maxLevel;
        ctx.canUpgrade = skill.canUpgrade;
        if (hit.kind == NativePanelHit_PlusButton && lUp && skill.canUpgrade && g_probePanelState.superSkillPoints > 0) {
            WriteLogFmt("[CarrierProbe:Step5:Action] plus skillId=%d level=%d/%d sp=%d",
                skill.skillId, skill.level, skill.maxLevel, g_probePanelState.superSkillPoints);
            if (g_probePanelHooks.onPlusAction)
                g_probePanelHooks.onPlusAction(ctx, g_probePanelHooks.userData);
            return true;
        }
        if (hit.kind == NativePanelHit_SkillIcon && rUp && skill.canUse && skill.level > 0) {
            WriteLogFmt("[CarrierProbe:Step5:Action] use skillId=%d level=%d canUse=1", skill.skillId, skill.level);
            if (g_probePanelHooks.onSkillUse)
                g_probePanelHooks.onSkillUse(ctx, g_probePanelHooks.userData);
            return true;
        }
    }
    if (hit.kind == NativePanelHit_InitButton && lUp) {
        RetroSkillActionContext ctx = {};
        ctx.currentTab = g_probePanelState.activeTab;
        WriteLogFmt("[CarrierProbe:Step5:Action] init tab=%d", g_probePanelState.activeTab);
        if (g_probePanelHooks.onInitAction)
            g_probePanelHooks.onInitAction(ctx, g_probePanelHooks.userData);
        return true;
    }
    return true;
}

static bool ProbeDrawImageCandidate(DWORD* surface, const NativeImageCandidate& candidate)
{
    bool anyOk = false;
    for (int i = 0; i < 4 && candidate.paths[i]; ++i) {
        void* imageObj = nullptr;
        const unsigned short* path = reinterpret_cast<const unsigned short*>(candidate.paths[i]);
        bool ok = ProbeResolveNativeImage(path, &imageObj, "Step5:Candidate");
        WriteLogFmt("[CarrierProbe:Step5:Candidate] key=%S pathIndex=%d ok=%d path=%S",
            candidate.key, i, ok ? 1 : 0, candidate.paths[i]);
        if (ok && imageObj && surface && !anyOk) {
            VARIANTARG alphaVar = {};
            VariantInit(&alphaVar);
            alphaVar.vt = VT_I4;
            alphaVar.lVal = 255;
            __try {
                reinterpret_cast<tSurfaceDrawImage>(ADDR_401C90)(
                    surface, candidate.drawX, candidate.drawY, reinterpret_cast<int>(imageObj), reinterpret_cast<DWORD*>(&alphaVar));
                anyOk = true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                WriteLogFmt("[CarrierProbe:Step5:Candidate] draw EXCEPTION key=%S code=0x%08X",
                    candidate.key, GetExceptionCode());
            }
            ProbeClearGameVariant(&alphaVar);
        }
        ProbeReleaseUiObj(imageObj);
    }
    return anyOk;
}

static void ProbeRenderRowsSmoke(DWORD* surface)
{
    std::vector<SkillEntry>& skills =
        (g_probePanelState.activeTab == 0) ? g_probePanelState.passiveSkills : g_probePanelState.activeSkills;
    const int listX = 9;
    const int listY = 93;
    const int rowStep = 40;
    const int rowH = 35;
    const int visibleRows = 4;
    const int start = g_probePanelState.usesGameSkillData ? g_probePanelState.gameVisibleStartIndex : 0;
    WriteLogFmt("[CarrierProbe:Step5:Rows] tab=%d usesGame=%d start=%d skills=%d gameCount=%d sp=%d",
        g_probePanelState.activeTab,
        g_probePanelState.usesGameSkillData ? 1 : 0,
        start,
        static_cast<int>(skills.size()),
        g_probePanelState.gameSkillCount,
        g_probePanelState.superSkillPoints);
    for (int i = 0; i < visibleRows; ++i) {
        int index = start + i;
        int y = listY + i * rowStep;
        ProbeSurfaceClearRect(surface, listX, y, 140, rowH, static_cast<unsigned char>(0x18 + i), "Step5:RowBg");
        ProbeSurfaceClearRect(surface, listX + 3, y + 2, 32, 32, static_cast<unsigned char>(0x38 + i), "Step5:IconBox");
        ProbeSurfaceClearRect(surface, listX + 127, y + 22, 12, 10, static_cast<unsigned char>(0x58 + i), "Step5:PlusBox");
        if (index >= 0 && index < static_cast<int>(skills.size())) {
            const SkillEntry& skill = skills[static_cast<size_t>(index)];
            WriteLogFmt("[CarrierProbe:Step5:Row] row=%d skillIndex=%d skillId=%d name=%s level=%d base=%d bonus=%d max=%d upgrade=%d canUpgrade=%d canUse=%d iconId=%d color=0x%08X",
                i, index, skill.skillId, skill.name.c_str(), skill.level, skill.baseLevel, skill.bonusLevel,
                skill.maxLevel, skill.upgradeState, skill.canUpgrade ? 1 : 0, skill.canUse ? 1 : 0,
                skill.iconId, static_cast<unsigned int>(skill.iconColor));
        } else {
            WriteLogFmt("[CarrierProbe:Step5:Row] row=%d skillIndex=%d empty", i, index);
        }
    }
}

static bool ProbeRenderRetroSkillNativePanel(NativePanelRenderContext& ctx, const NativePanelInput* input, bool resolveCandidates)
{
    if (!ctx.surface)
        return false;
    ProbeEnsureNativePanelRuntime();
    SkillOverlayBridgeSyncRetroState(g_probePanelState);
    ProbeSurfaceClearRect(ctx.surface, 0, 0, ctx.width, ctx.height, 0x00, "Step5:ClearPanel");
    bool bgOk = ProbeNativeDrawImage(ctx.surface, 0, 0, reinterpret_cast<const unsigned short*>(ADDR_STR_SkillExMainBackgrnd), "Step5:BgSkillEx");
    if (!bgOk)
        bgOk = ProbeNativeDrawImage(ctx.surface, 0, 0, reinterpret_cast<const unsigned short*>(ADDR_STR_SkillMacroBackgrnd), "Step5:BgMacroFallback");
    if (input) {
        NativePanelHit hit = ProbeHitTestNativePanel(g_probePanelState, input->mouseX - ctx.panelX, input->mouseY - ctx.panelY);
        ProbeApplyNativePanelHit(hit, input->lUp, input->rUp);
    }
    ProbeRenderRowsSmoke(ctx.surface);
    if (resolveCandidates) {
        for (size_t i = 0; i < sizeof(kProbeImageCandidates) / sizeof(kProbeImageCandidates[0]); ++i)
            ProbeDrawImageCandidate(ctx.surface, kProbeImageCandidates[i]);
    }
    WriteLogFmt("[CarrierProbe:Step5:Draw] surface=0x%08X bg=%d candidates=%d panel=(%d,%d,%d,%d) source=%s",
        reinterpret_cast<DWORD>(ctx.surface),
        bgOk ? 1 : 0,
        resolveCandidates ? 1 : 0,
        ctx.panelX,
        ctx.panelY,
        ctx.width,
        ctx.height,
        SkillOverlayBridgeGetActiveSourceName() ? SkillOverlayBridgeGetActiveSourceName() : "(null)");
    return true;
}

static bool ProbeGetSurfaceForChild(uintptr_t child, DWORD** outSurface, const char* tag)
{
    if (outSurface) *outSurface = nullptr;
    if (!child || !outSurface)
        return false;
    DWORD* surface = nullptr;
    __try {
        reinterpret_cast<tGetSurface>(ADDR_435A50)(child, &surface);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLogFmt("[CarrierProbe:%s] EXCEPTION get surface child=0x%08X code=0x%08X",
            tag ? tag : "Surface", static_cast<DWORD>(child), GetExceptionCode());
        return false;
    }
    if (!surface) {
        WriteLogFmt("[CarrierProbe:%s] FAIL surface=null child=0x%08X", tag ? tag : "Surface", static_cast<DWORD>(child));
        return false;
    }
    *outSurface = surface;
    WriteLogFmt("[CarrierProbe:%s] OK child=0x%08X surface=0x%08X",
        tag ? tag : "Surface", static_cast<DWORD>(child), reinterpret_cast<DWORD>(surface));
    return true;
}

static void __fastcall ProbeSecondChildDraw(uintptr_t thisPtr, void* /*edxUnused*/, int* clipRegion)
{
    DWORD* surface = nullptr;
    bool gotSurface = ProbeGetSurfaceForChild(thisPtr, &surface, "VT1Draw");
    if (InterlockedDecrement(&g_probeDrawLogBudget) >= 0) {
        WriteLogFmt("[CarrierProbe:VT1Draw] child=0x%08X clip=0x%08X gotSurface=%d panel=(%d,%d) owner=0x%08X",
            static_cast<DWORD>(thisPtr), reinterpret_cast<DWORD>(clipRegion), gotSurface ? 1 : 0,
            g_probeCarrier.panelX, g_probeCarrier.panelY, static_cast<DWORD>(g_probeCarrier.child));
    }
    if (!gotSurface)
        return;
    NativePanelInput input = g_probePanelInput;
    g_probePanelInput = NativePanelInput{};
    NativePanelRenderContext ctx = {};
    ctx.surface = surface;
    ctx.panelX = g_probeCarrier.panelX;
    ctx.panelY = g_probeCarrier.panelY;
    ProbeRenderRetroSkillNativePanel(ctx, &input, false);
}

static bool ProbePatchVT1Draw(uintptr_t child)
{
    if (!ProbeIsOfficialSecondChild(child, false, false)) {
        WriteLogFmt("[CarrierProbe:Step2:VT1] skip invalid official child=0x%08X", static_cast<DWORD>(child));
        return false;
    }
    DWORD origVT1 = *reinterpret_cast<DWORD*>(child + 0x00);
    if (!origVT1 || SafeIsBadReadPtr(reinterpret_cast<void*>(origVT1), 256 * sizeof(DWORD))) {
        WriteLogFmt("[CarrierProbe:Step2:VT1] FAIL origVT1 unreadable 0x%08X", origVT1);
        return false;
    }
    if (!g_probeCustomVT1) {
        g_probeCustomVT1 = reinterpret_cast<DWORD*>(VirtualAlloc(nullptr, 256 * sizeof(DWORD), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!g_probeCustomVT1) {
            WriteLog("[CarrierProbe:Step2:VT1] FAIL VirtualAlloc custom VT1");
            return false;
        }
    }
    std::memcpy(g_probeCustomVT1, reinterpret_cast<void*>(origVT1), 256 * sizeof(DWORD));
    g_probeCustomVT1[11] = reinterpret_cast<DWORD>(&ProbeSecondChildDraw);
    *reinterpret_cast<DWORD*>(child + 0x00) = reinterpret_cast<DWORD>(g_probeCustomVT1);
    g_probeCarrier.vt1Patched = true;
    WriteLogFmt("[CarrierProbe:Step2:VT1] installed child=0x%08X orig=0x%08X custom=0x%08X draw=0x%08X slot11Old=0x%08X",
        static_cast<DWORD>(child), origVT1, reinterpret_cast<DWORD>(g_probeCustomVT1),
        reinterpret_cast<DWORD>(&ProbeSecondChildDraw), reinterpret_cast<DWORD*>(origVT1)[11]);
    return true;
}

static int __stdcall ProbeSecondChildMsgHook(DWORD msg, DWORD a2, DWORD a3, DWORD a4)
{
    const bool mouseUpCloseMsg = (msg == WM_LBUTTONUP || msg == WM_RBUTTONUP);
    uintptr_t slot = g_probeCarrier.skillWnd ? ProbeGetSecondChildPtr(g_probeCarrier.skillWnd) : 0;
    const bool ours = g_probeCarrier.child && slot == g_probeCarrier.child && slot == g_probeCustomVT2OwnerChild;
    if (mouseUpCloseMsg && ours) {
        g_probeCarrier.lastMouseMsg = msg;
        g_probeCarrier.lastGuardTick = GetTickCount();
        if (InterlockedDecrement(&g_probeMsgSwallowBudget) >= 0) {
            WriteLogFmt("[CarrierProbe:Step4:VT2] swallow msg=0x%04X child=0x%08X slot=0x%08X a=[%08X,%08X,%08X]",
                msg, static_cast<DWORD>(g_probeCarrier.child), static_cast<DWORD>(slot), a2, a3, a4);
        }
        return 0;
    }
    if (InterlockedDecrement(&g_probeMsgLogBudget) >= 0) {
        WriteLogFmt("[CarrierProbe:Step4:VT2] pass msg=0x%04X slot=0x%08X child=0x%08X ours=%d a=[%08X,%08X,%08X]",
            msg, static_cast<DWORD>(slot), static_cast<DWORD>(g_probeCarrier.child), ours ? 1 : 0, a2, a3, a4);
    }
    tSecondChildMsgFn orig = reinterpret_cast<tSecondChildMsgFn>(g_probeOriginalSecondChildMsgFn);
    return orig ? orig(msg, a2, a3, a4) : 0;
}

static bool ProbePatchVT2MouseGuard(uintptr_t child)
{
    if (!ProbeIsOfficialSecondChild(child, true, false)) {
        WriteLogFmt("[CarrierProbe:Step4:VT2] skip invalid child=0x%08X", static_cast<DWORD>(child));
        return false;
    }
    DWORD origVT2 = *reinterpret_cast<DWORD*>(child + 0x04);
    if (origVT2 != ADDR_VT_SkillWndSecondChild2) {
        WriteLogFmt("[CarrierProbe:Step4:VT2] skip vt2=0x%08X expected=0x%08X", origVT2, ADDR_VT_SkillWndSecondChild2);
        return false;
    }
    const int kVT2Entries = 64;
    if (SafeIsBadReadPtr(reinterpret_cast<void*>(origVT2), kVT2Entries * sizeof(DWORD))) {
        WriteLogFmt("[CarrierProbe:Step4:VT2] FAIL orig VT2 unreadable 0x%08X", origVT2);
        return false;
    }
    if (!g_probeCustomVT2) {
        g_probeCustomVT2 = reinterpret_cast<DWORD*>(VirtualAlloc(nullptr, kVT2Entries * sizeof(DWORD), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!g_probeCustomVT2) {
            WriteLog("[CarrierProbe:Step4:VT2] FAIL VirtualAlloc custom VT2");
            return false;
        }
        std::memcpy(g_probeCustomVT2, reinterpret_cast<void*>(origVT2), kVT2Entries * sizeof(DWORD));
    }
    DWORD origMsg = g_probeCustomVT2[2];
    if (origMsg && origMsg != reinterpret_cast<DWORD>(&ProbeSecondChildMsgHook))
        g_probeOriginalSecondChildMsgFn = origMsg;
    if (g_probeOriginalSecondChildMsgFn != ADDR_9D98F0) {
        WriteLogFmt("[CarrierProbe:Step4:VT2] WARN origMsg=0x%08X expected=0x%08X",
            g_probeOriginalSecondChildMsgFn, ADDR_9D98F0);
    }
    g_probeCustomVT2[2] = reinterpret_cast<DWORD>(&ProbeSecondChildMsgHook);
    *reinterpret_cast<DWORD*>(child + 0x04) = reinterpret_cast<DWORD>(g_probeCustomVT2);
    g_probeCustomVT2OwnerChild = child;
    g_probeCarrier.vt2Patched = true;
    WriteLogFmt("[CarrierProbe:Step4:VT2] installed child=0x%08X origVT2=0x%08X customVT2=0x%08X origMsg=0x%08X hook=0x%08X",
        static_cast<DWORD>(child), origVT2, reinterpret_cast<DWORD>(g_probeCustomVT2),
        g_probeOriginalSecondChildMsgFn, reinterpret_cast<DWORD>(&ProbeSecondChildMsgHook));
    return true;
}

static void ProbeRestoreVT2MouseGuard(uintptr_t child, const char* reason)
{
    if (!child || SafeIsBadReadPtr(reinterpret_cast<void*>(child + 0x04), sizeof(DWORD)))
        return;
    DWORD vt2 = *reinterpret_cast<DWORD*>(child + 0x04);
    if (g_probeCustomVT2 && vt2 == reinterpret_cast<DWORD>(g_probeCustomVT2)) {
        *reinterpret_cast<DWORD*>(child + 0x04) = ADDR_VT_SkillWndSecondChild2;
        WriteLogFmt("[CarrierProbe:Step4:VT2] restored child=0x%08X reason=%s",
            static_cast<DWORD>(child), reason ? reason : "unknown");
    }
    if (g_probeCustomVT2OwnerChild == child)
        g_probeCustomVT2OwnerChild = 0;
    g_probeCarrier.vt2Patched = false;
}

static bool ProbeRebuildNativeChildSurface(uintptr_t child, int x, int y, int width, int height, const char* tag)
{
    if (!child || width <= 0 || height <= 0)
        return false;
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
            mov ecx, [child]
            call [fnResize]
            mov [result], eax
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLogFmt("[CarrierProbe:%s] EXCEPTION B9AB50 child=0x%08X code=0x%08X",
            tag ? tag : "Resize", static_cast<DWORD>(child), GetExceptionCode());
        return false;
    }
    WriteLogFmt("[CarrierProbe:%s] B9AB50 ret=%d expectW=%d child=0x%08X xywh=(%d,%d,%d,%d) a6=%d a7=%d a8=%d",
        tag ? tag : "Resize", result, width, static_cast<DWORD>(child), x, y, width, height, a6, a7, a8);
    return true;
}

static bool ProbeMoveNativeChild(uintptr_t child, int x, int y, const char* tag)
{
    if (!child)
        return false;
    LONG result = 0;
    __try {
        result = reinterpret_cast<tMoveNativeWnd>(ADDR_56D630)(child, x, y);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLogFmt("[CarrierProbe:%s] EXCEPTION 56D630 child=0x%08X code=0x%08X",
            tag ? tag : "Move", static_cast<DWORD>(child), GetExceptionCode());
        return false;
    }
    WriteLogFmt("[CarrierProbe:%s] 56D630 ret=0x%08X child=0x%08X xy=(%d,%d)",
        tag ? tag : "Move", static_cast<DWORD>(result), static_cast<DWORD>(child), x, y);
    return result >= 0;
}

static void ProbeMarkDirty(uintptr_t child, const char* tag)
{
    if (!child)
        return;
    DWORD fnDirty = ADDR_B9A5D0;
    __try {
        __asm {
            push 0
            mov ecx, [child]
            call [fnDirty]
        }
        WriteLogFmt("[CarrierProbe:%s] B9A5D0 dirty child=0x%08X", tag ? tag : "Dirty", static_cast<DWORD>(child));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLogFmt("[CarrierProbe:%s] EXCEPTION B9A5D0 child=0x%08X code=0x%08X",
            tag ? tag : "Dirty", static_cast<DWORD>(child), GetExceptionCode());
    }
}

static uintptr_t ProbeCreateOfficialSecondChild(uintptr_t skillWndThis, DWORD flags, int panelX, int panelY)
{
    uintptr_t before = ProbeGetSecondChildPtr(skillWndThis);
    ProbeDumpSlot(skillWndThis, "Step2:BeforeCreate");
    if (before) {
        ProbeDumpCWndCore(before, "Step2:ExistingChild");
        if ((flags & ProbeFlag_AllowReplaceSlot) == 0) {
            WriteLogFmt("[CarrierProbe:Step2] slot busy, skip create because AllowReplaceSlot is off old=0x%08X", static_cast<DWORD>(before));
            return before;
        }
        WriteLogFmt("[CarrierProbe:Step2] replacing existing second-child via 9DC220 old=0x%08X", static_cast<DWORD>(before));
    }
    DWORD fnCreate = ADDR_9DC220;
    int mode = kProbeSecondChildMode;
    __try {
        __asm {
            push [mode]
            mov ecx, [skillWndThis]
            call [fnCreate]
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLogFmt("[CarrierProbe:Step2] EXCEPTION 9DC220 skillWnd=0x%08X mode=%d code=0x%08X",
            static_cast<DWORD>(skillWndThis), mode, GetExceptionCode());
        return 0;
    }
    uintptr_t child = ProbeGetSecondChildPtr(skillWndThis);
    ProbeDumpSlot(skillWndThis, "Step2:AfterCreate");
    if (!child) {
        WriteLog("[CarrierProbe:Step2] FAIL 9DC220 left slot null");
        return 0;
    }
    ProbeDumpCWndCore(child, "Step2:CreatedChild");
    WriteLogFmt("[CarrierProbe:Step2] official=%d mode=%d child=0x%08X",
        ProbeIsOfficialSecondChild(child, false, false) ? 1 : 0, mode, static_cast<DWORD>(child));
    if (panelX > -9000 && panelY > -9000) {
        ProbeRebuildNativeChildSurface(child, panelX, panelY, kProbePanelW, kProbePanelH, "Step2:Resize");
        CWnd_SetRenderPos(child, panelX, panelY);
        CWnd_SetComPos(child, panelX, panelY);
        ProbeMoveNativeChild(child, panelX, panelY, "Step2:Move");
        ProbeMarkDirty(child, "Step2:Dirty");
        ProbeDumpCWndCore(child, "Step2:AfterResizeMove");
    }
    g_probeCarrier.skillWnd = skillWndThis;
    g_probeCarrier.child = child;
    g_probeCarrier.lastSlot = child;
    g_probeCarrier.createdTick = GetTickCount();
    g_probeCarrier.lastSeenTick = g_probeCarrier.createdTick;
    g_probeCarrier.panelX = panelX;
    g_probeCarrier.panelY = panelY;
    g_probeCarrier.generation++;
    g_probeCarrier.alive = true;
    g_probeCarrier.lostByMouseUp = false;
    return child;
}

static bool ProbeReleaseSecondChild(uintptr_t skillWndThis, const char* reason)
{
    if (!skillWndThis)
        return false;
    uintptr_t child = ProbeGetSecondChildPtr(skillWndThis);
    if (!child) {
        WriteLogFmt("[CarrierProbe:Release] slot already null reason=%s", reason ? reason : "unknown");
        return false;
    }
    ProbeRestoreVT2MouseGuard(child, reason);
    WriteLogFmt("[CarrierProbe:Release] begin reason=%s child=0x%08X", reason ? reason : "unknown", static_cast<DWORD>(child));
    DWORD fnClose = ADDR_B9E880;
    __try {
        __asm {
            mov ecx, [child]
            call [fnClose]
        }
        WriteLogFmt("[CarrierProbe:Release] B9E880 OK child=0x%08X", static_cast<DWORD>(child));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLogFmt("[CarrierProbe:Release] EXCEPTION B9E880 child=0x%08X code=0x%08X", static_cast<DWORD>(child), GetExceptionCode());
    }
    uintptr_t childAfterClose = ProbeGetSecondChildPtr(skillWndThis);
    if (childAfterClose) {
        DWORD fnRelease = ADDR_9D93A0;
        uintptr_t wrapPtr = skillWndThis + 3044;
        int zero = 0;
        __try {
            __asm {
                push [zero]
                mov ecx, [wrapPtr]
                call [fnRelease]
            }
            WriteLogFmt("[CarrierProbe:Release] 9D93A0 OK wrap=0x%08X childAfterClose=0x%08X",
                static_cast<DWORD>(wrapPtr), static_cast<DWORD>(childAfterClose));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            WriteLogFmt("[CarrierProbe:Release] EXCEPTION 9D93A0 wrap=0x%08X code=0x%08X",
                static_cast<DWORD>(wrapPtr), GetExceptionCode());
        }
        if (!SafeIsBadReadPtr(reinterpret_cast<void*>(skillWndThis + 3048), sizeof(DWORD)))
            *reinterpret_cast<DWORD*>(skillWndThis + 3048) = 0;
    }
    g_probeCarrier.alive = false;
    g_probeCarrier.child = 0;
    g_probeCarrier.lastSlot = 0;
    ProbeDumpSlot(skillWndThis, "Release:After");
    return true;
}

static void ProbePollCarrier(uintptr_t skillWndThis, const char* reason)
{
    uintptr_t slot = ProbeGetSecondChildPtr(skillWndThis);
    uintptr_t old = g_probeCarrier.child;
    bool slotOfficial = ProbeIsOfficialSecondChild(slot, true, true);
    bool ours = old && slot == old;
    if (ours && slotOfficial) {
        g_probeCarrier.lastSeenTick = GetTickCount();
        g_probeCarrier.lastSlot = slot;
        g_probeCarrier.alive = true;
        WriteLogFmt("[CarrierProbe:Step3:Watch] ok reason=%s child=0x%08X vt1Patched=%d vt2Patched=%d lastMouse=0x%04X",
            reason ? reason : "poll",
            static_cast<DWORD>(slot),
            g_probeCarrier.vt1Patched ? 1 : 0,
            g_probeCarrier.vt2Patched ? 1 : 0,
            g_probeCarrier.lastMouseMsg);
        return;
    }
    if (!slot) {
        g_probeCarrier.alive = false;
        g_probeCarrier.lostByMouseUp =
            (g_probeCarrier.lastMouseMsg == WM_LBUTTONUP || g_probeCarrier.lastMouseMsg == WM_RBUTTONUP) &&
            GetTickCount() - g_probeCarrier.lastGuardTick < 2000;
        WriteLogFmt("[CarrierProbe:Step3:Watch] lost reason=%s old=0x%08X slot=0x00000000 lostByMouse=%d lastMouse=0x%04X",
            reason ? reason : "poll",
            static_cast<DWORD>(old),
            g_probeCarrier.lostByMouseUp ? 1 : 0,
            g_probeCarrier.lastMouseMsg);
        if (g_probeCustomVT2OwnerChild == old)
            g_probeCustomVT2OwnerChild = 0;
        return;
    }
    WriteLogFmt("[CarrierProbe:Step3:Watch] replaced/busy reason=%s old=0x%08X slot=0x%08X slotOfficial=%d",
        reason ? reason : "poll",
        static_cast<DWORD>(old),
        static_cast<DWORD>(slot),
        slotOfficial ? 1 : 0);
    ProbeDumpCWndCore(slot, "Step3:Watch:SlotObject");
}

static void ProbeDumpLayerState(const char* reason)
{
    WriteLogFmt("[CarrierProbe:Layer] reason=%s zHead=0x%08X zTail=0x%08X dirtyHead=0x%08X dirtyTail=0x%08X",
        reason ? reason : "dump",
        ProbeReadDwordOrZero(ADDR_ZOrderListHead),
        ProbeReadDwordOrZero(ADDR_ZOrderListTail),
        ProbeReadDwordOrZero(ADDR_DirtyListHead),
        ProbeReadDwordOrZero(ADDR_DirtyListTail));
    uintptr_t wndMan = 0;
    if (!SafeIsBadReadPtr(reinterpret_cast<void*>(ADDR_CWndMan), sizeof(DWORD)))
        wndMan = *reinterpret_cast<DWORD*>(ADDR_CWndMan);
    if (!wndMan) {
        WriteLog("[CarrierProbe:Layer] CWndMan=null");
        return;
    }
    if (SafeIsBadReadPtr(reinterpret_cast<void*>(wndMan + CWNDMAN_TOPLEVEL_OFF), sizeof(DWORD))) {
        WriteLogFmt("[CarrierProbe:Layer] CWndMan top-level unreadable wndMan=0x%08X", static_cast<DWORD>(wndMan));
        return;
    }
    uintptr_t vec = *reinterpret_cast<DWORD*>(wndMan + CWNDMAN_TOPLEVEL_OFF);
    if (!vec || vec < 4 || SafeIsBadReadPtr(reinterpret_cast<void*>(vec - 4), sizeof(DWORD))) {
        WriteLogFmt("[CarrierProbe:Layer] top-level vec invalid wndMan=0x%08X vec=0x%08X", static_cast<DWORD>(wndMan), static_cast<DWORD>(vec));
        return;
    }
    int count = *reinterpret_cast<int*>(vec - 4);
    if (count < 0 || count > 4096) {
        WriteLogFmt("[CarrierProbe:Layer] top-level count invalid count=%d vec=0x%08X", count, static_cast<DWORD>(vec));
        return;
    }
    WriteLogFmt("[CarrierProbe:Layer] wndMan=0x%08X topVec=0x%08X count=%d scanMax=80",
        static_cast<DWORD>(wndMan), static_cast<DWORD>(vec), count);
    int scanMax = count < 80 ? count : 80;
    for (int i = 0; i < scanMax; ++i) {
        uintptr_t wnd = 0;
        uintptr_t slotAddr = vec + i * sizeof(DWORD);
        if (!SafeIsBadReadPtr(reinterpret_cast<void*>(slotAddr), sizeof(DWORD)))
            wnd = *reinterpret_cast<DWORD*>(slotAddr);
        if (!wnd || SafeIsBadReadPtr(reinterpret_cast<void*>(wnd), 0x30))
            continue;
        int x = CWnd_GetRenderX(wnd);
        int y = CWnd_GetRenderY(wnd);
        int w = CWnd_GetWidth(wnd);
        int h = CWnd_GetHeight(wnd);
        int z = SafeIsBadReadPtr(reinterpret_cast<void*>(wnd + CWND_OFF_ZORDER * 4), sizeof(DWORD))
            ? -9999
            : *reinterpret_cast<int*>(wnd + CWND_OFF_ZORDER * 4);
        DWORD vt1 = ProbeReadDwordOrZero(wnd + 0x00);
        DWORD vt2 = ProbeReadDwordOrZero(wnd + 0x04);
        bool officialSecond = ProbeIsOfficialSecondChild(wnd, true, true);
        WriteLogFmt("[CarrierProbe:LayerItem] i=%d wnd=0x%08X vt=[%08X,%08X] rect=(%d,%d,%d,%d) z=%d officialSecond=%d isProbe=%d",
            i, static_cast<DWORD>(wnd), vt1, vt2, x, y, w, h, z,
            officialSecond ? 1 : 0, (wnd == g_probeCarrier.child) ? 1 : 0);
    }
}

static void ProbeAuditOverlayDowngrade(DWORD flags)
{
    WriteLogFmt("[CarrierProbe:Step6] desired official_second_child_primary=1 overlay_fallback=0 overlay_debug=0 flags=0x%08X", flags);
    WriteLog("[CarrierProbe:Step6] checklist: CreateSuperWnd official first; overlay only when fallback route enabled");
    WriteLog("[CarrierProbe:Step6] checklist: D3D8 ToggleSuperWnd must not early-return unless overlay fallback route is active");
    WriteLog("[CarrierProbe:Step6] checklist: Present/D3D8Present must not render overlay panel or suppress mouse under official primary");
    WriteLog("[CarrierProbe:Step6] checklist: UpdateSuperCWnd must move/dirty second-child, not return after overlay anchor");
    WriteLog("[CarrierProbe:Step6] checklist: WndProc native panel hit-test first, overlay handler fallback only");
    WriteLog("[CarrierProbe:Step6] checklist: reset/cleanup touches overlay only if fallback/debug overlay initialized");
}

static void ProbeAuditHideDecision(uintptr_t skillWndThis, uintptr_t child, const char* reason)
{
    bool skillReadable = skillWndThis && !SafeIsBadReadPtr(reinterpret_cast<void*>(skillWndThis), 0x30);
    bool childReadable = child && !SafeIsBadReadPtr(reinterpret_cast<void*>(child), 0x84);
    bool slotMatches = skillWndThis && child && ProbeGetSecondChildPtr(skillWndThis) == child;
    DWORD* surface = nullptr;
    bool surfaceOk = childReadable && ProbeGetSurfaceForChild(child, &surface, "HideDecisionSurface");
    int w = childReadable ? CWnd_GetWidth(child) : 0;
    int h = childReadable ? CWnd_GetHeight(child) : 0;
    bool hide = !skillReadable || !childReadable || !slotMatches || !surfaceOk || w <= 0 || h <= 0 || w > 4096 || h > 4096;
    WriteLogFmt("[CarrierProbe:HideDecision] reason=%s hide=%d skillReadable=%d childReadable=%d slotMatches=%d surfaceOk=%d childWh=(%d,%d)",
        reason ? reason : "audit",
        hide ? 1 : 0,
        skillReadable ? 1 : 0,
        childReadable ? 1 : 0,
        slotMatches ? 1 : 0,
        surfaceOk ? 1 : 0,
        w,
        h);
}

static void ProbeRunNativeSmokeRender(uintptr_t child, bool resolveCandidates)
{
    DWORD* surface = nullptr;
    if (!ProbeGetSurfaceForChild(child, &surface, "Step5:Surface"))
        return;
    NativePanelInput input = g_probePanelInput;
    g_probePanelInput = NativePanelInput{};
    NativePanelRenderContext ctx = {};
    ctx.surface = surface;
    ctx.panelX = g_probeCarrier.panelX;
    ctx.panelY = g_probeCarrier.panelY;
    ProbeRenderRetroSkillNativePanel(ctx, &input, resolveCandidates);
    ProbeMarkDirty(child, "Step5:DirtyAfterDraw");
}

static void ProbeStep1ModeAndPrereqs(uintptr_t skillWndThis, DWORD flags)
{
    WriteLogFmt("[CarrierProbe:Step1] begin skillWnd=0x%08X flags=0x%08X mode=official_second_child_primary",
        static_cast<DWORD>(skillWndThis), flags);
    ProbeDumpAddresses();
    ProbeDumpSlot(skillWndThis, "Step1:SlotSnapshot");
    int vtX = 0;
    int vtY = 0;
    int comX = 0;
    int comY = 0;
    bool hasVT = ProbeGetSkillWndAnchorPos(skillWndThis, &vtX, &vtY);
    bool hasCom = ProbeGetSkillWndComPos(skillWndThis, &comX, &comY);
    WriteLogFmt("[CarrierProbe:Step1] anchor vt=%d(%d,%d) com=%d(%d,%d) panelWh=(%d,%d) confidence vt=A/B com=B",
        hasVT ? 1 : 0, vtX, vtY, hasCom ? 1 : 0, comX, comY, kProbePanelW, kProbePanelH);
    WriteLog("[CarrierProbe:Step1] hook prerequisites: move=9D95A0 refresh=9E1770 draw=9DEE30 childDraw=VT1[11]; existing build gates must be moved from !overlay to officialPrimary");
}
} // namespace

extern "C" __declspec(dllexport) void __stdcall SSW_SecondChildCarrierProbe_RunOnce(
    DWORD skillWndThis32,
    DWORD flags,
    int explicitPanelX,
    int explicitPanelY)
{
    uintptr_t skillWndThis = skillWndThis32 ? static_cast<uintptr_t>(skillWndThis32) : ProbeReadGlobalSkillWnd();
    DWORD started = GetTickCount();
    WriteLogFmt("[CarrierProbe:Begin] version=one-shot-2026-04-11 skillWndArg=0x%08X resolvedSkillWnd=0x%08X flags=0x%08X explicitPanel=(%d,%d)",
        skillWndThis32,
        static_cast<DWORD>(skillWndThis),
        flags,
        explicitPanelX,
        explicitPanelY);

    if (!skillWndThis || SafeIsBadReadPtr(reinterpret_cast<void*>(skillWndThis), 0x30)) {
        WriteLog("[CarrierProbe:Begin] abort: SkillWnd unreadable. Open SkillWnd before running the probe.");
        return;
    }

    int panelX = -9999;
    int panelY = -9999;
    const char* panelSrc = "none";
    bool panelOk = ProbeComputePanelPos(skillWndThis, explicitPanelX, explicitPanelY, &panelX, &panelY, &panelSrc);
    WriteLogFmt("[CarrierProbe:PanelPos] ok=%d src=%s panel=(%d,%d) wh=(%d,%d)",
        panelOk ? 1 : 0,
        panelSrc,
        panelX,
        panelY,
        kProbePanelW,
        kProbePanelH);

    ProbeStep1ModeAndPrereqs(skillWndThis, flags);
    if (flags & ProbeFlag_DumpLayerState)
        ProbeDumpLayerState("before_create");

    uintptr_t child = ProbeGetSecondChildPtr(skillWndThis);
    if (flags & ProbeFlag_CreateCarrier) {
        child = ProbeCreateOfficialSecondChild(skillWndThis, flags, panelX, panelY);
    } else {
        WriteLog("[CarrierProbe:Step2] CreateCarrier flag is off; using existing slot only");
        ProbeDumpCWndCore(child, "Step2:ExistingOnly");
    }

    if (child && panelOk) {
        g_probeCarrier.skillWnd = skillWndThis;
        g_probeCarrier.child = child;
        g_probeCarrier.lastSlot = child;
        g_probeCarrier.panelX = panelX;
        g_probeCarrier.panelY = panelY;
        g_probeCarrier.alive = true;
        if (!g_probeCarrier.createdTick)
            g_probeCarrier.createdTick = GetTickCount();
    }

    if (child && (flags & ProbeFlag_PatchVT1Draw)) {
        ProbePatchVT1Draw(child);
        ProbeDumpCWndCore(child, "Step2:AfterVT1");
    }

    ProbePollCarrier(skillWndThis, "after_step2");

    if (child && (flags & ProbeFlag_PatchVT2MouseGuard)) {
        ProbePatchVT2MouseGuard(child);
        ProbeDumpCWndCore(child, "Step4:AfterVT2");
    } else {
        WriteLog("[CarrierProbe:Step4:VT2] PatchVT2MouseGuard flag is off; this run will only observe slot loss");
    }

    if (child && (flags & ProbeFlag_NativeSmokeRender))
        ProbeRunNativeSmokeRender(child, (flags & ProbeFlag_ResolveCandidates) != 0);

    ProbePollCarrier(skillWndThis, "after_step5");
    ProbeAuditHideDecision(skillWndThis, child, "after_step5");
    ProbeAuditOverlayDowngrade(flags);

    if (flags & ProbeFlag_DumpLayerState)
        ProbeDumpLayerState("after_all_steps");

    if ((flags & ProbeFlag_AutoReleaseCarrier) && child)
        ProbeReleaseSecondChild(skillWndThis, "auto_release_flag");

    WriteLogFmt("[CarrierProbe:End] elapsedMs=%u child=0x%08X slot=0x%08X alive=%d vt1=%d vt2=%d lastMouse=0x%04X",
        GetTickCount() - started,
        static_cast<DWORD>(g_probeCarrier.child),
        static_cast<DWORD>(ProbeGetSecondChildPtr(skillWndThis)),
        g_probeCarrier.alive ? 1 : 0,
        g_probeCarrier.vt1Patched ? 1 : 0,
        g_probeCarrier.vt2Patched ? 1 : 0,
        g_probeCarrier.lastMouseMsg);
}

extern "C" __declspec(dllexport) void __stdcall SSW_SecondChildCarrierProbe_ObserveWndProc(
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    if (!g_probeCarrier.skillWnd)
        g_probeCarrier.skillWnd = ProbeReadGlobalSkillWnd();
    uintptr_t slotBefore = g_probeCarrier.skillWnd ? ProbeGetSecondChildPtr(g_probeCarrier.skillWnd) : 0;
    bool interesting =
        msg == WM_MOUSEMOVE ||
        msg == WM_LBUTTONDOWN ||
        msg == WM_LBUTTONUP ||
        msg == WM_RBUTTONDOWN ||
        msg == WM_RBUTTONUP ||
        msg == WM_MOUSEWHEEL ||
        msg == WM_CAPTURECHANGED ||
        msg == WM_KILLFOCUS;

    if (interesting) {
        int mx = static_cast<short>(LOWORD(lParam));
        int my = static_cast<short>(HIWORD(lParam));
        g_probePanelInput.mouseX = mx;
        g_probePanelInput.mouseY = my;
        g_probePanelInput.lDown = msg == WM_LBUTTONDOWN;
        g_probePanelInput.lUp = msg == WM_LBUTTONUP;
        g_probePanelInput.rUp = msg == WM_RBUTTONUP;
        if (msg == WM_MOUSEWHEEL)
            g_probePanelInput.wheel = GET_WHEEL_DELTA_WPARAM(wParam);
        if (msg == WM_LBUTTONUP || msg == WM_RBUTTONUP)
            g_probeCarrier.lastMouseMsg = msg;
        WriteLogFmt("[CarrierProbe:WndProc] msg=0x%04X w=0x%08X l=0x%08X mouse=(%d,%d) panel=(%d,%d) slotBefore=0x%08X child=0x%08X",
            msg,
            static_cast<DWORD>(wParam),
            static_cast<DWORD>(lParam),
            mx,
            my,
            g_probeCarrier.panelX,
            g_probeCarrier.panelY,
            static_cast<DWORD>(slotBefore),
            static_cast<DWORD>(g_probeCarrier.child));
    }

    if (interesting && g_probeCarrier.skillWnd)
        ProbePollCarrier(g_probeCarrier.skillWnd, "wndproc_observe");
}

extern "C" __declspec(dllexport) void __stdcall SSW_SecondChildCarrierProbe_Poll(DWORD skillWndThis32, DWORD reasonCode)
{
    uintptr_t skillWndThis = skillWndThis32 ? static_cast<uintptr_t>(skillWndThis32) : g_probeCarrier.skillWnd;
    if (!skillWndThis)
        skillWndThis = ProbeReadGlobalSkillWnd();
    char reason[64] = {};
    std::snprintf(reason, sizeof(reason), "external_poll_%08X", reasonCode);
    ProbePollCarrier(skillWndThis, reason);
    ProbeAuditHideDecision(skillWndThis, g_probeCarrier.child, reason);
}

extern "C" __declspec(dllexport) void __stdcall SSW_SecondChildCarrierProbe_Release(DWORD skillWndThis32)
{
    uintptr_t skillWndThis = skillWndThis32 ? static_cast<uintptr_t>(skillWndThis32) : g_probeCarrier.skillWnd;
    if (!skillWndThis)
        skillWndThis = ProbeReadGlobalSkillWnd();
    ProbeReleaseSecondChild(skillWndThis, "external_release");
}
