#include "ui/retro_render_backend.h"

#include "d3d8/d3d8_renderer.h"

#include <d3d9.h>

#include <cstring>
#include <vector>

namespace
{
    bool CreateD3D9TextureFromRgba(
        IDirect3DDevice9* device,
        const unsigned char* rgba,
        int width,
        int height,
        void** outTexture)
    {
        if (!device || !rgba || !outTexture || width <= 0 || height <= 0)
            return false;

        *outTexture = nullptr;

        IDirect3DTexture9* texture = nullptr;
        HRESULT hr = device->CreateTexture(
            width,
            height,
            1,
            0,
            D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED,
            &texture,
            nullptr);
        if (FAILED(hr) || !texture)
            return false;

        D3DLOCKED_RECT lockedRect = {};
        hr = texture->LockRect(0, &lockedRect, nullptr, 0);
        if (FAILED(hr))
        {
            texture->Release();
            return false;
        }

        for (int y = 0; y < height; ++y)
        {
            unsigned char* dstRow = static_cast<unsigned char*>(lockedRect.pBits) + y * lockedRect.Pitch;
            const unsigned char* srcRow = rgba + y * width * 4;

            for (int x = 0; x < width; ++x)
            {
                dstRow[x * 4 + 0] = srcRow[x * 4 + 2];
                dstRow[x * 4 + 1] = srcRow[x * 4 + 1];
                dstRow[x * 4 + 2] = srcRow[x * 4 + 0];
                dstRow[x * 4 + 3] = srcRow[x * 4 + 3];
            }
        }

        texture->UnlockRect(0);
        *outTexture = texture;
        return true;
    }

    bool CreateD3D9TextureFromArgb32(
        IDirect3DDevice9* device,
        const unsigned int* argbPixels,
        int width,
        int height,
        void** outTexture)
    {
        if (!device || !argbPixels || !outTexture || width <= 0 || height <= 0)
            return false;

        *outTexture = nullptr;

        IDirect3DTexture9* texture = nullptr;
        HRESULT hr = device->CreateTexture(
            width,
            height,
            1,
            0,
            D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED,
            &texture,
            nullptr);
        if (FAILED(hr) || !texture)
            return false;

        D3DLOCKED_RECT lockedRect = {};
        hr = texture->LockRect(0, &lockedRect, nullptr, 0);
        if (FAILED(hr))
        {
            texture->Release();
            return false;
        }

        for (int y = 0; y < height; ++y)
        {
            unsigned char* dstRow = static_cast<unsigned char*>(lockedRect.pBits) + y * lockedRect.Pitch;
            const unsigned char* srcRow = reinterpret_cast<const unsigned char*>(argbPixels + (size_t)y * (size_t)width);
            memcpy(dstRow, srcRow, (size_t)width * sizeof(unsigned int));
        }

        texture->UnlockRect(0);
        *outTexture = texture;
        return true;
    }

    bool CreateD3D8TextureFromRgba(
        void* device8,
        const unsigned char* rgba,
        int width,
        int height,
        void** outTexture)
    {
        if (!device8 || !rgba || !outTexture || width <= 0 || height <= 0)
            return false;

        *outTexture = nullptr;
        D3D8Texture texture = D3D8_CreateTextureFromRGBA(device8, rgba, width, height);
        if (!texture.pTexture8)
            return false;

        *outTexture = texture.pTexture8;
        return true;
    }

    bool CreateD3D8TextureFromArgb32(
        void* device8,
        const unsigned int* argbPixels,
        int width,
        int height,
        void** outTexture)
    {
        if (!device8 || !argbPixels || !outTexture || width <= 0 || height <= 0)
            return false;

        std::vector<unsigned char> rgba;
        rgba.resize((size_t)width * (size_t)height * 4u);

        for (int i = 0; i < width * height; ++i)
        {
            const unsigned int argb = argbPixels[i];
            rgba[(size_t)i * 4u + 0u] = (unsigned char)((argb >> 16) & 0xFFu);
            rgba[(size_t)i * 4u + 1u] = (unsigned char)((argb >> 8) & 0xFFu);
            rgba[(size_t)i * 4u + 2u] = (unsigned char)(argb & 0xFFu);
            rgba[(size_t)i * 4u + 3u] = (unsigned char)((argb >> 24) & 0xFFu);
        }

        return CreateD3D8TextureFromRgba(device8, rgba.data(), width, height, outTexture);
    }
}

bool RetroCreateBackendTextureFromRgba(
    const RetroDeviceRef& deviceRef,
    const unsigned char* rgba,
    int width,
    int height,
    void** outTexture)
{
    if (!outTexture)
        return false;

    switch (deviceRef.backend)
    {
    case RetroRenderBackend_D3D8:
        return CreateD3D8TextureFromRgba(deviceRef.device, rgba, width, height, outTexture);
    case RetroRenderBackend_D3D9:
    default:
        return CreateD3D9TextureFromRgba(
            static_cast<IDirect3DDevice9*>(deviceRef.device),
            rgba,
            width,
            height,
            outTexture);
    }
}

bool RetroCreateBackendTextureFromArgb32(
    const RetroDeviceRef& deviceRef,
    const unsigned int* argbPixels,
    int width,
    int height,
    void** outTexture)
{
    if (!outTexture)
        return false;

    switch (deviceRef.backend)
    {
    case RetroRenderBackend_D3D8:
        return CreateD3D8TextureFromArgb32(deviceRef.device, argbPixels, width, height, outTexture);
    case RetroRenderBackend_D3D9:
    default:
        return CreateD3D9TextureFromArgb32(
            static_cast<IDirect3DDevice9*>(deviceRef.device),
            argbPixels,
            width,
            height,
            outTexture);
    }
}

void RetroReleaseBackendTexture(void*& texture, RetroRenderBackend /*backend*/)
{
    if (!texture)
        return;

    IUnknown* unknown = reinterpret_cast<IUnknown*>(texture);
    unknown->Release();
    texture = nullptr;
}
