#pragma once

#include <d3d9.h>
#include <map>
#include <string>
#include <vector>

struct UITexture {
    LPDIRECT3DTEXTURE9 texture = nullptr;
    int width = 0;
    int height = 0;
};

struct RetroSkillAssets {
    std::map<std::string, UITexture> textures;
    std::map<int, UITexture> skillIcons;
    std::map<int, UITexture> skillIconsMouseOver;
    std::map<int, UITexture> skillIconsDisabled;
};

bool LoadRetroSkillTexture(LPDIRECT3DDEVICE9 device, const char* filename, UITexture& outTexture);
bool LoadRetroSkillTextureFromMemory(LPDIRECT3DDEVICE9 device, const unsigned char* data, size_t size, UITexture& outTexture);
bool LoadAllRetroSkillAssets(RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, const char* assetPath);
void CleanupRetroSkillAssets(RetroSkillAssets& assets);
UITexture* GetRetroSkillTexture(RetroSkillAssets& assets, const char* name);
UITexture* GetRetroSkillSkillIconTexture(RetroSkillAssets& assets, int skillId);
UITexture* GetRetroSkillSkillIconMouseOverTexture(RetroSkillAssets& assets, int skillId);
UITexture* GetRetroSkillSkillIconDisabledTexture(RetroSkillAssets& assets, int skillId);
