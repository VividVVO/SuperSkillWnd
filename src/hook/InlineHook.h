#pragma once
//
// InlineHook.h — x86 inline hook 引擎（从原项目验证代码迁移）
//
#include <windows.h>
#include "hde32.h"

// ============================================================================
// x86指令长度计算（委托给 hde32 表驱动反汇编引擎）
// ============================================================================
inline size_t GetX86InstructionLength(const BYTE* code)
{
    unsigned int len = hde32_len(code);
    return (size_t)len;
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
// 最小拷贝长度计算（使用通用指令长度解析）
// ============================================================================
inline int CalcMinCopyLen(BYTE* p)
{
    int len = 0;
    while (len < 5) {
        size_t instrLen = GetX86InstructionLength(p + len);
        if (instrLen == 0) {
            // Unknown instruction — try minimum safe skip
            len += 1;
            continue;
        }
        len += (int)instrLen;
    }
    return len;
}
