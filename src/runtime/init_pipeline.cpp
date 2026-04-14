#include "runtime/init_pipeline.h"

#include "core/Common.h"

namespace
{
    bool RunOptionalStep(bool (*fn)(), const char* failLog)
    {
        if (!fn || !fn())
        {
            if (failLog && failLog[0])
                WriteLog(failLog);
            return false;
        }
        return true;
    }

    bool RunRequiredStep(bool (*fn)(), const char* failLog)
    {
        if (fn && fn())
            return true;
        if (failLog && failLog[0])
            WriteLog(failLog);
        return false;
    }
}

bool SuperRuntimeRunInstallPipeline(
    const SuperRuntimeInstallOptions& options,
    const SuperRuntimeInstallCallbacks& callbacks,
    SuperRuntimeInstallResult* outResult)
{
    if (outResult)
        *outResult = SuperRuntimeInstallResult{};

    if (options.isD3D8Mode)
    {
        WriteLog("[D3D] Detected D3D8 mode (d3d8.dll loaded)");
        if (!RunRequiredStep(callbacks.setupD3D8Hook, "D3D8 hook FAILED"))
            return false;
    }
    else
    {
        WriteLog("[D3D] Using D3D9 mode");
        if (!RunRequiredStep(callbacks.setupD3D9Hook, "D3D9 hook FAILED"))
            return false;
    }

    RunOptionalStep(callbacks.setupNativeButtonAssetPathHook, "BtnSkinHook failed (non-fatal)");
    RunOptionalStep(callbacks.setupNativeButtonResolveHook, "BtnResolveHook failed (non-fatal)");
    RunOptionalStep(callbacks.setupNativeButtonDrawHook, "BtnDrawHook failed (non-fatal)");
    RunOptionalStep(callbacks.setupNativeButtonMetricHooks, "BtnMetricHook failed (non-fatal)");
    RunOptionalStep(callbacks.setupPacketHook, "PacketHook failed (non-fatal)");
    RunOptionalStep(callbacks.setupSkillReleaseClassifierHook, "SkillReleaseHook failed (non-fatal)");
    RunOptionalStep(callbacks.setupSkillPresentationHook, "SkillVisualHook failed (non-fatal)");
    RunOptionalStep(callbacks.setupSkillNativeIdGateHooks, "SkillGate hooks failed (non-fatal)");
    RunOptionalStep(callbacks.setupSkillLevelLookupHooks, "SkillLevel hooks failed (non-fatal)");

    WriteLog("[NativeText] disabled: using self renderer");

    bool childDrawHookOk = true;
    bool moveHookOk = true;
    bool refreshHookOk = true;
    if (!options.enableImguiOverlayPanel)
    {
        childDrawHookOk = callbacks.setupSuperChildDrawHook && callbacks.setupSuperChildDrawHook();
        moveHookOk = callbacks.setupSkillWndMoveHook && callbacks.setupSkillWndMoveHook();
        refreshHookOk = callbacks.setupSkillWndRefreshHook && callbacks.setupSkillWndRefreshHook();

        if (!childDrawHookOk)
            WriteLog("Super child draw route failed (route-B blocked)");
        if (!moveHookOk)
            WriteLog("SkillWnd move hook failed (route-B degraded)");
        if (!refreshHookOk)
            WriteLog("SkillWnd refresh hook failed (route-B degraded)");
    }

    RunOptionalStep(callbacks.setupSkillWndHook, "SkillWnd hook failed (non-fatal)");
    RunOptionalStep(callbacks.setupSkillWndDrawHook, "SkillWnd draw hook failed (non-fatal)");
    RunOptionalStep(callbacks.setupPostB9F6E0TimingTestHook, "PostUiTimingTest hook failed (non-fatal)");
    RunOptionalStep(callbacks.setupSkillListBuildFilterHook, "SkillListFilter hook failed (non-fatal)");
    RunOptionalStep(callbacks.setupSkillWndDtorHook, "SkillWnd dtor hook failed (non-fatal)");
    RunOptionalStep(callbacks.setupMsgHook, "MsgHook failed (non-fatal)");

    if (options.isD3D8Mode && options.enableImguiOverlayPanel)
    {
        WriteLog("[WndProc] deferred install until SkillWnd becomes active in D3D8 mode");
        WriteLog("[InputSpoof] deferred install until SkillWnd becomes active in D3D8 mode");
    }
    else
    {
        if (!RunRequiredStep(callbacks.setupWndProcHook, "WndProc hook FAILED"))
            return false;
        if (!(callbacks.installInputSpoof && callbacks.installInputSpoof()))
            WriteLog("[InputSpoof] install failed (non-fatal)");
    }

    if (outResult)
    {
        outResult->isD3D8Mode = options.isD3D8Mode;
        outResult->superChildHooksReady = childDrawHookOk && moveHookOk && refreshHookOk;
    }

    return true;
}

