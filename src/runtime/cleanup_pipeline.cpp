#include "runtime/cleanup_pipeline.h"

void SuperRuntimeRunCleanupPipeline(
    const SuperRuntimeCleanupOptions& options,
    const SuperRuntimeCleanupCallbacks& callbacks)
{
    if (callbacks.resetSuperRuntimeState)
        callbacks.resetSuperRuntimeState(false, options.reason ? options.reason : "cleanup");

    if (options.enableImguiOverlayPanel)
    {
        if (options.isD3D8Mode)
        {
            if (callbacks.shutdownD3D8Overlay)
                callbacks.shutdownD3D8Overlay();
        }
        else
        {
            if (callbacks.shutdownD3D9Overlay)
                callbacks.shutdownD3D9Overlay();
        }
    }

    if (callbacks.releaseAllD3D8Textures)
        callbacks.releaseAllD3D8Textures(options.reason ? options.reason : "cleanup");
    if (callbacks.releaseAllD3D9Textures)
        callbacks.releaseAllD3D9Textures(options.reason ? options.reason : "cleanup");
    if (callbacks.shutdownSkillOverlayBridge)
        callbacks.shutdownSkillOverlayBridge();

    if (callbacks.isInputSpoofInstalled && callbacks.isInputSpoofInstalled())
    {
        if (callbacks.setInputSpoofSuppressMouse)
            callbacks.setInputSpoofSuppressMouse(false);
        if (callbacks.uninstallInputSpoof)
            callbacks.uninstallInputSpoof();
    }
}

