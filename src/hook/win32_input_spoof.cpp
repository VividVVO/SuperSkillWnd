#include "hook/win32_input_spoof.h"

#include "core/Common.h"

#include <stdint.h>

namespace
{
    typedef BOOL (WINAPI *tGetCursorPosFn)(LPPOINT);
    typedef BOOL (WINAPI *tScreenToClientFn)(HWND, LPPOINT);
    typedef SHORT (WINAPI *tGetAsyncKeyStateFn)(int);
    typedef SHORT (WINAPI *tGetKeyStateFn)(int);

    struct IatHookSlot
    {
        uintptr_t* slot = nullptr;
        uintptr_t original = 0;
    };

    struct SpoofState
    {
        bool installed = false;
        bool suppressMouse = false;
        IatHookSlot getCursorPos;
        IatHookSlot screenToClient;
        IatHookSlot getAsyncKeyState;
        IatHookSlot getKeyState;
    };

    SpoofState g_spoof;

    uintptr_t* FindIatSlot(HMODULE module, const char* dllName, const char* apiName)
    {
        if (!module || !dllName || !apiName)
            return nullptr;

        BYTE* base = reinterpret_cast<BYTE*>(module);
        IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return nullptr;

        IMAGE_NT_HEADERS* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return nullptr;

        IMAGE_DATA_DIRECTORY& importDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!importDir.VirtualAddress)
            return nullptr;

        IMAGE_IMPORT_DESCRIPTOR* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importDir.VirtualAddress);
        while (imp->Name)
        {
            const char* importedDll = reinterpret_cast<const char*>(base + imp->Name);
            if (_stricmp(importedDll, dllName) == 0)
            {
                IMAGE_THUNK_DATA* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
                IMAGE_THUNK_DATA* origThunk = imp->OriginalFirstThunk
                    ? reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk)
                    : thunk;

                while (origThunk->u1.AddressOfData)
                {
                    if (!(origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG))
                    {
                        IMAGE_IMPORT_BY_NAME* byName =
                            reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + origThunk->u1.AddressOfData);
                        if (strcmp(reinterpret_cast<const char*>(byName->Name), apiName) == 0)
                            return reinterpret_cast<uintptr_t*>(&thunk->u1.Function);
                    }
                    ++thunk;
                    ++origThunk;
                }
            }
            ++imp;
        }

        return nullptr;
    }

    bool PatchSlot(IatHookSlot* hook, uintptr_t replacement)
    {
        if (!hook || !hook->slot)
            return false;

        DWORD oldProtect = 0;
        if (!VirtualProtect(hook->slot, sizeof(uintptr_t), PAGE_READWRITE, &oldProtect))
            return false;

        *hook->slot = replacement;
        VirtualProtect(hook->slot, sizeof(uintptr_t), oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), hook->slot, sizeof(uintptr_t));
        return true;
    }

    inline bool ShouldSpoofButtonVk(int vk)
    {
        return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
    }

    BOOL WINAPI hkGetCursorPos(LPPOINT pt)
    {
        tGetCursorPosFn original = reinterpret_cast<tGetCursorPosFn>(g_spoof.getCursorPos.original);
        BOOL ok = original ? original(pt) : FALSE;
        if (ok && pt && g_spoof.suppressMouse)
        {
            pt->x = -10000;
            pt->y = -10000;
        }
        return ok;
    }

    BOOL WINAPI hkScreenToClient(HWND hwnd, LPPOINT pt)
    {
        tScreenToClientFn original = reinterpret_cast<tScreenToClientFn>(g_spoof.screenToClient.original);
        BOOL ok = original ? original(hwnd, pt) : FALSE;
        if (ok && pt && g_spoof.suppressMouse)
        {
            pt->x = -10000;
            pt->y = -10000;
        }
        return ok;
    }

    SHORT WINAPI hkGetAsyncKeyState(int vk)
    {
        if (g_spoof.suppressMouse && ShouldSpoofButtonVk(vk))
            return 0;
        tGetAsyncKeyStateFn original = reinterpret_cast<tGetAsyncKeyStateFn>(g_spoof.getAsyncKeyState.original);
        return original ? original(vk) : 0;
    }

    SHORT WINAPI hkGetKeyState(int vk)
    {
        if (g_spoof.suppressMouse && ShouldSpoofButtonVk(vk))
            return 0;
        tGetKeyStateFn original = reinterpret_cast<tGetKeyStateFn>(g_spoof.getKeyState.original);
        return original ? original(vk) : 0;
    }
}

bool Win32InputSpoofInstall()
{
    if (g_spoof.installed)
        return true;

    HMODULE mainModule = GetModuleHandleA(nullptr);
    if (!mainModule)
        return false;

    g_spoof.getCursorPos.slot = FindIatSlot(mainModule, "user32.dll", "GetCursorPos");
    g_spoof.screenToClient.slot = FindIatSlot(mainModule, "user32.dll", "ScreenToClient");
    g_spoof.getAsyncKeyState.slot = FindIatSlot(mainModule, "user32.dll", "GetAsyncKeyState");
    g_spoof.getKeyState.slot = FindIatSlot(mainModule, "user32.dll", "GetKeyState");

    if (!g_spoof.getCursorPos.slot || !g_spoof.screenToClient.slot ||
        !g_spoof.getAsyncKeyState.slot || !g_spoof.getKeyState.slot)
    {
        WriteLog("[InputSpoof] FAIL: IAT slot missing");
        return false;
    }

    g_spoof.getCursorPos.original = *g_spoof.getCursorPos.slot;
    g_spoof.screenToClient.original = *g_spoof.screenToClient.slot;
    g_spoof.getAsyncKeyState.original = *g_spoof.getAsyncKeyState.slot;
    g_spoof.getKeyState.original = *g_spoof.getKeyState.slot;

    if (!PatchSlot(&g_spoof.getCursorPos, reinterpret_cast<uintptr_t>(&hkGetCursorPos)) ||
        !PatchSlot(&g_spoof.screenToClient, reinterpret_cast<uintptr_t>(&hkScreenToClient)) ||
        !PatchSlot(&g_spoof.getAsyncKeyState, reinterpret_cast<uintptr_t>(&hkGetAsyncKeyState)) ||
        !PatchSlot(&g_spoof.getKeyState, reinterpret_cast<uintptr_t>(&hkGetKeyState)))
    {
        Win32InputSpoofUninstall();
        WriteLog("[InputSpoof] FAIL: patch slot");
        return false;
    }

    g_spoof.installed = true;
    g_spoof.suppressMouse = false;
    WriteLog("[InputSpoof] Installed");
    return true;
}

void Win32InputSpoofUninstall()
{
    if (g_spoof.getCursorPos.slot && g_spoof.getCursorPos.original)
        PatchSlot(&g_spoof.getCursorPos, g_spoof.getCursorPos.original);
    if (g_spoof.screenToClient.slot && g_spoof.screenToClient.original)
        PatchSlot(&g_spoof.screenToClient, g_spoof.screenToClient.original);
    if (g_spoof.getAsyncKeyState.slot && g_spoof.getAsyncKeyState.original)
        PatchSlot(&g_spoof.getAsyncKeyState, g_spoof.getAsyncKeyState.original);
    if (g_spoof.getKeyState.slot && g_spoof.getKeyState.original)
        PatchSlot(&g_spoof.getKeyState, g_spoof.getKeyState.original);

    g_spoof = SpoofState{};
    WriteLog("[InputSpoof] Uninstalled");
}

void Win32InputSpoofSetSuppressMouse(bool suppress)
{
    g_spoof.suppressMouse = suppress;
}

bool Win32InputSpoofIsInstalled()
{
    return g_spoof.installed;
}

bool Win32InputSpoofIsSuppressing()
{
    return g_spoof.suppressMouse;
}

LPARAM Win32InputSpoofMakeOffscreenMouseLParam()
{
    const short off = -10000;
    return MAKELPARAM((WORD)off, (WORD)off);
}
