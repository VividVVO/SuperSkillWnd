#pragma once

struct SuperRuntimeInstallCallbacks
{
    bool (*setupD3D8Hook)();
    bool (*setupD3D9Hook)();
    bool (*setupNativeButtonAssetPathHook)();
    bool (*setupNativeButtonResolveHook)();
    bool (*setupNativeButtonDrawHook)();
    bool (*setupNativeButtonMetricHooks)();
    bool (*setupPacketHook)();
    bool (*setupSkillReleaseClassifierHook)();
    bool (*setupSkillPresentationHook)();
    bool (*setupSkillNativeIdGateHooks)();
    bool (*setupSkillLevelLookupHooks)();
    bool (*setupSuperChildDrawHook)();
    bool (*setupSkillWndMoveHook)();
    bool (*setupSkillWndRefreshHook)();
    bool (*setupSkillWndHook)();
    bool (*setupSkillWndDrawHook)();
    bool (*setupPostB9F6E0TimingTestHook)();
    bool (*setupSkillListBuildFilterHook)();
    bool (*setupSkillWndDtorHook)();
    bool (*setupMsgHook)();
    bool (*setupWndProcHook)();
    bool (*installInputSpoof)();
};

struct SuperRuntimeInstallOptions
{
    bool isD3D8Mode = false;
    bool enableImguiOverlayPanel = true;
};

struct SuperRuntimeInstallResult
{
    bool isD3D8Mode = false;
    bool superChildHooksReady = false;
};

bool SuperRuntimeRunInstallPipeline(
    const SuperRuntimeInstallOptions& options,
    const SuperRuntimeInstallCallbacks& callbacks,
    SuperRuntimeInstallResult* outResult);

