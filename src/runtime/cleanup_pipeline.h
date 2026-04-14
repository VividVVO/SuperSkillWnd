#pragma once

struct SuperRuntimeCleanupCallbacks
{
    void (*resetSuperRuntimeState)(bool closeWnd, const char* reason);
    void (*shutdownD3D8Overlay)();
    void (*shutdownD3D9Overlay)();
    void (*releaseAllD3D8Textures)(const char* reason);
    void (*releaseAllD3D9Textures)(const char* reason);
    void (*shutdownSkillOverlayBridge)();
    bool (*isInputSpoofInstalled)();
    void (*setInputSpoofSuppressMouse)(bool suppress);
    void (*uninstallInputSpoof)();
};

struct SuperRuntimeCleanupOptions
{
    bool enableImguiOverlayPanel = true;
    bool isD3D8Mode = false;
    const char* reason = "cleanup";
};

void SuperRuntimeRunCleanupPipeline(
    const SuperRuntimeCleanupOptions& options,
    const SuperRuntimeCleanupCallbacks& callbacks);

