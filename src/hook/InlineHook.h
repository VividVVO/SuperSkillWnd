#pragma once
//
// InlineHook.h — x86 inline hook 引擎（从原项目验证代码迁移）
//
#include <windows.h>

// ============================================================================
// x86指令长度计算（简易反汇编）
// ============================================================================
inline size_t GetX86InstructionLength(const BYTE* code)
{
    auto modrm_disp_len = [](BYTE modrm, bool hasSib) -> size_t {
        BYTE mod = (modrm >> 6) & 0x3;
        BYTE rm  = modrm & 0x7;
        size_t len = 0;
        if (hasSib) len += 1;
        if (mod == 0) {
            if (rm == 5) len += 4;
        } else if (mod == 1) {
            len += 1;
        } else if (mod == 2) {
            len += 4;
        }
        return len;
    };

    switch (code[0]) {
    case 0x50: case 0x51: case 0x52: case 0x53:
    case 0x55: case 0x56: case 0x57:
    case 0x58: case 0x59: case 0x5A: case 0x5B:
    case 0x5D: case 0x5E: case 0x5F:
    case 0xCC: return 1;
    case 0x6A: case 0xEB: return 2;
    case 0x33: return 2;
    case 0x68: case 0xA1: case 0xE8: case 0xE9: return 5;
    case 0x81: return 6;
    case 0x83: return 3;
    case 0x89:
    case 0x8B:
    case 0x8D:
    {
        BYTE modrm = code[1];
        bool hasSib = ((modrm & 0x07) == 0x04) && (((modrm >> 6) & 0x3) != 0x3);
        return 2 + modrm_disp_len(modrm, hasSib);
    }
    case 0x64:
        if (code[1] == 0xA1 || code[1] == 0xA3) return 6;
        break;
    case 0xC7:
        if (code[1] == 0x45) return 7;
        if (code[1] == 0x44) return 8;
        break;
    case 0xC6:
        if (code[1] == 0x45) return 4;
        if (code[1] == 0x44) return 5;
        break;
    }
    return 0;
}

inline size_t CalculateRelocatedByteCount(const BYTE* code, size_t minBytes)
{
    size_t total = 0;
    while (total < minBytes) {
        size_t len = GetX86InstructionLength(code + total);
        if (len == 0) return 0;
        total += len;
    }
    return total;
}

// ============================================================================
// 跟随JMP链找到真实入口
// ============================================================================
inline BYTE* FollowJmpChain(void* addr)
{
    BYTE* p = (BYTE*)addr;
    for (int i = 0; i < 16; i++) {
        if (p[0] == 0xE9)      p = p + 5 + *(int*)(p + 1);
        else if (p[0] == 0xEB) p = p + 2 + (signed char)p[1];
        else break;
    }
    return p;
}

// ============================================================================
// 通用5字节inline hook
// 返回trampoline指针（调用原函数用），失败返回nullptr
// ============================================================================
inline void* GenericInlineHook5(BYTE* pTarget, void* myFunc, int copyLen = 5)
{
    if (copyLen < 5) return nullptr;

    void* trampoline = VirtualAlloc(NULL, copyLen + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!trampoline) return nullptr;

    // 复制原始字节到trampoline
    memcpy(trampoline, pTarget, copyLen);
    // trampoline末尾跳回原函数
    *((BYTE*)trampoline + copyLen) = 0xE9;
    *(DWORD*)((BYTE*)trampoline + copyLen + 1) = (DWORD)(pTarget + copyLen) - ((DWORD)trampoline + copyLen) - 5;

    // 覆写原函数入口为jmp到我们的函数
    DWORD oldProtect;
    VirtualProtect(pTarget, copyLen, PAGE_EXECUTE_READWRITE, &oldProtect);
    pTarget[0] = 0xE9;
    *(DWORD*)(pTarget + 1) = (DWORD)myFunc - (DWORD)pTarget - 5;
    for (int i = 5; i < copyLen; i++) pTarget[i] = 0x90;
    VirtualProtect(pTarget, copyLen, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), pTarget, copyLen);

    return trampoline;
}

// ============================================================================
// 安装inline hook到指定地址
// 自动跟随JMP链、计算安全拷贝长度
// 返回trampoline指针
// ============================================================================
inline void* InstallInlineHook(DWORD targetAddr, void* hookFunc)
{
    BYTE* pTarget = FollowJmpChain((void*)targetAddr);

    size_t relocBytes = CalculateRelocatedByteCount(pTarget, 5);
    if (relocBytes == 0) return nullptr;

    void* trampoline = VirtualAlloc(NULL, relocBytes + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!trampoline) return nullptr;

    memcpy(trampoline, pTarget, relocBytes);
    BYTE* pTramp = (BYTE*)trampoline + relocBytes;
    pTramp[0] = 0xE9;
    *(int*)(pTramp + 1) = (int)((uintptr_t)pTarget + relocBytes - ((uintptr_t)pTramp + 5));

    DWORD oldProtect;
    VirtualProtect(pTarget, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    BYTE hook[5];
    hook[0] = 0xE9;
    *(int*)(hook + 1) = (int)((uintptr_t)hookFunc - (uintptr_t)pTarget - 5);
    memcpy(pTarget, hook, 5);
    VirtualProtect(pTarget, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), pTarget, 5);

    return trampoline;
}

// ============================================================================
// 最小拷贝长度计算（D3D9函数常用前言）
// ============================================================================
inline int CalcMinCopyLen(BYTE* p)
{
    int len = 0;
    while (len < 5) {
        BYTE b = p[len];
        if ((b >= 0x50 && b <= 0x5F) || b == 0x90 || b == 0xC3 || b == 0xCC)
            { len += 1; continue; }
        if (b == 0x8B || b == 0x33 || b == 0x3B || b == 0x85 || b == 0x89 || b == 0x31 || b == 0x29)
            { len += 2; continue; }
        if (b == 0x83) { len += 3; continue; }
        if (b == 0x81) { len += 6; continue; }
        if (b >= 0xB8 && b <= 0xBF) { len += 5; continue; }
        if (b == 0x6A) { len += 2; continue; }
        if (b == 0x68) { len += 5; continue; }
        if (b == 0x0F) { len += 2; continue; }
        if (b == 0x8D) { len += 3; continue; }
        len += 2;
    }
    return len;
}
