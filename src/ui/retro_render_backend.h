#pragma once

#include <windows.h>

enum RetroRenderBackend
{
    RetroRenderBackend_D3D9 = 0,
    RetroRenderBackend_D3D8 = 1,
};

struct RetroDeviceRef
{
    void* device = nullptr;
    RetroRenderBackend backend = RetroRenderBackend_D3D9;
};

bool RetroCreateBackendTextureFromRgba(
    const RetroDeviceRef& deviceRef,
    const unsigned char* rgba,
    int width,
    int height,
    void** outTexture);

bool RetroCreateBackendTextureFromArgb32(
    const RetroDeviceRef& deviceRef,
    const unsigned int* argbPixels,
    int width,
    int height,
    void** outTexture);

void RetroReleaseBackendTexture(void*& texture, RetroRenderBackend backend);
