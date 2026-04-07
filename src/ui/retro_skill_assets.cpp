#include "retro_skill_assets.h"
#include "skill/skill_local_data.h"
#include "util/stb_image.h"

static bool CreateTextureFromRgba(LPDIRECT3DDEVICE9 device, const unsigned char* data, int width, int height, UITexture& outTexture)
{
    HRESULT hr = device->CreateTexture(
        width,
        height,
        1,
        0,
        D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED,
        &outTexture.texture,
        nullptr
    );

    if (FAILED(hr))
        return false;

    D3DLOCKED_RECT lockedRect;
    hr = outTexture.texture->LockRect(0, &lockedRect, nullptr, 0);
    if (SUCCEEDED(hr))
    {
        for (int y = 0; y < height; y++)
        {
            unsigned char* dstRow = (unsigned char*)lockedRect.pBits + y * lockedRect.Pitch;
            const unsigned char* srcRow = data + y * width * 4;

            for (int x = 0; x < width; x++)
            {
                dstRow[x * 4 + 0] = srcRow[x * 4 + 2];
                dstRow[x * 4 + 1] = srcRow[x * 4 + 1];
                dstRow[x * 4 + 2] = srcRow[x * 4 + 0];
                dstRow[x * 4 + 3] = srcRow[x * 4 + 3];
            }
        }

        outTexture.texture->UnlockRect(0);
    }

    outTexture.width = width;
    outTexture.height = height;
    return SUCCEEDED(hr);
}

static bool LoadTextureInternalFromMemory(LPDIRECT3DDEVICE9 device, const unsigned char* fileData, size_t fileSize, UITexture& outTexture)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load_from_memory(fileData, static_cast<int>(fileSize), &width, &height, &channels, 4);
    if (!data)
        return false;

    const bool ok = CreateTextureFromRgba(device, data, width, height, outTexture);
    stbi_image_free(data);
    return ok;
}

static bool LoadTextureInternal(LPDIRECT3DDEVICE9 device, const char* filename, UITexture& outTexture)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 4);
    if (!data)
        return false;

    const bool ok = CreateTextureFromRgba(device, data, width, height, outTexture);
    stbi_image_free(data);
    return ok;
}

bool LoadRetroSkillTexture(LPDIRECT3DDEVICE9 device, const char* filename, UITexture& outTexture)
{
    return LoadTextureInternal(device, filename, outTexture);
}

bool LoadRetroSkillTextureFromMemory(LPDIRECT3DDEVICE9 device, const unsigned char* data, size_t size, UITexture& outTexture)
{
    return LoadTextureInternalFromMemory(device, data, size, outTexture);
}

bool LoadAllRetroSkillAssets(RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, const char* assetPath)
{
    auto loadOne = [&](const char* key, const char* fileName) {
        UITexture texture;
        std::string path = std::string(assetPath) + fileName;
        if (LoadTextureInternal(device, path.c_str(), texture))
            assets.textures[key] = texture;
    };

    auto loadSkillIcon = [&](int skillId) {
        std::vector<unsigned char> pngBytes;
        if (SkillLocalDataGetIconPngBytes(skillId, pngBytes) && !pngBytes.empty())
        {
            UITexture texture;
            if (LoadTextureInternalFromMemory(device, &pngBytes[0], pngBytes.size(), texture))
                assets.skillIcons[skillId] = texture;
        }

        std::vector<unsigned char> mouseOverBytes;
        if (SkillLocalDataGetIconMouseOverPngBytes(skillId, mouseOverBytes) && !mouseOverBytes.empty())
        {
            UITexture texture;
            if (LoadTextureInternalFromMemory(device, &mouseOverBytes[0], mouseOverBytes.size(), texture))
                assets.skillIconsMouseOver[skillId] = texture;
        }

        std::vector<unsigned char> disabledBytes;
        if (SkillLocalDataGetIconDisabledPngBytes(skillId, disabledBytes) && !disabledBytes.empty())
        {
            UITexture texture;
            if (LoadTextureInternalFromMemory(device, &disabledBytes[0], disabledBytes.size(), texture))
                assets.skillIconsDisabled[skillId] = texture;
        }
    };

    loadOne("main", "SkillEx.main.png");
    loadOne("Passive.0", "SkillEx.Passive.0.png");
    loadOne("Passive.1", "SkillEx.Passive.1.png");
    loadOne("ActivePassive.0", "SkillEx.ActivePassive.0.png");
    loadOne("ActivePassive.1", "SkillEx.ActivePassive.1.png");
    loadOne("initial.normal", "SkillEx.initial.normal.png");
    loadOne("initial.mouseOver", "SkillEx.initial.mouseOver.png");
    loadOne("initial.pressed", "SkillEx.inital.pressed.png");
    loadOne("scroll", "OptionMenu.scroll.2.png");
    loadOne("scroll.pressed", "OptionMenu.scroll.1.png");
    loadOne("BtSpUp.normal", "SkillEx.main.BtSpUp.normal.0.png");
    loadOne("BtSpUp.disabled", "SkillEx.main.BtSpUp.disabled.0.png");
    loadOne("BtSpUp.pressed", "SkillEx.main.BtSpUp.pressed.0.png");
    loadOne("BtSpUp.mouseOver", "SkillEx.main.BtSpUp.mouseOver.0.png");
    loadOne("skill0", "Skill.main.skill0.png");
    loadOne("skill1", "Skill.main.skill1.png");
    loadOne("TypeIcon.0", "SkillEx.TypeIcon.0.png");
    loadOne("TypeIcon.2", "SkillEx.TypeIcon.2.png");
    loadOne("surpe.normal", "SkillEx.surpe.normal.png");
    loadOne("surpe.mouseOver", "SkillEx.surpe.mouseOver.png");
    loadOne("surpe.pressed", "SkillEx.surpe.pressed.png");
    loadOne("surpe.disabled", "SkillEx.surpe.disabled.png");
    loadOne("mouse.normal", "System.mouse.normal.png");
    loadOne("mouse.normal.1", "System.mouse.normal.1.png");
    loadOne("mouse.normal.2", "System.mouse.normal.2.png");
    loadOne("mouse.pressed", "System.mouse.pressed.png");
    loadOne("mouse.drag", "System.mouse.Drag.png");

    SkillLocalDataInitialize();
    std::vector<int> skillIds;
    SkillLocalDataGetSkillIds(skillIds);
    for (size_t i = 0; i < skillIds.size(); ++i)
        loadSkillIcon(skillIds[i]);

    return true;
}

void CleanupRetroSkillAssets(RetroSkillAssets& assets)
{
    for (auto& pair : assets.textures)
    {
        if (pair.second.texture)
            pair.second.texture->Release();
    }
    assets.textures.clear();

    for (auto& pair : assets.skillIcons)
    {
        if (pair.second.texture)
            pair.second.texture->Release();
    }
    assets.skillIcons.clear();

    for (auto& pair : assets.skillIconsMouseOver)
    {
        if (pair.second.texture)
            pair.second.texture->Release();
    }
    assets.skillIconsMouseOver.clear();

    for (auto& pair : assets.skillIconsDisabled)
    {
        if (pair.second.texture)
            pair.second.texture->Release();
    }
    assets.skillIconsDisabled.clear();
}

UITexture* GetRetroSkillTexture(RetroSkillAssets& assets, const char* name)
{
    auto it = assets.textures.find(name);
    if (it != assets.textures.end())
        return &it->second;
    return nullptr;
}

UITexture* GetRetroSkillSkillIconTexture(RetroSkillAssets& assets, int skillId)
{
    std::map<int, UITexture>::iterator it = assets.skillIcons.find(skillId);
    if (it != assets.skillIcons.end())
        return &it->second;
    return nullptr;
}

UITexture* GetRetroSkillSkillIconMouseOverTexture(RetroSkillAssets& assets, int skillId)
{
    std::map<int, UITexture>::iterator it = assets.skillIconsMouseOver.find(skillId);
    if (it != assets.skillIconsMouseOver.end())
        return &it->second;
    return nullptr;
}

UITexture* GetRetroSkillSkillIconDisabledTexture(RetroSkillAssets& assets, int skillId)
{
    std::map<int, UITexture>::iterator it = assets.skillIconsDisabled.find(skillId);
    if (it != assets.skillIconsDisabled.end())
        return &it->second;
    return nullptr;
}
