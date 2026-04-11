#pragma once

#include "ui/retro_render_backend.h"

#include <map>
#include <string>
#include <vector>

struct UITexture {
    void* texture = nullptr;
    RetroRenderBackend backend = RetroRenderBackend_D3D9;
    int width = 0;
    int height = 0;
};

struct RetroSkillAssets {
    std::map<std::string, UITexture> textures;
    std::map<int, UITexture> skillIcons;
    std::map<int, UITexture> skillIconsMouseOver;
    std::map<int, UITexture> skillIconsDisabled;
};

bool LoadRetroSkillTexture(const RetroDeviceRef& deviceRef, const char* filename, UITexture& outTexture);
bool LoadRetroSkillTextureFromMemory(const RetroDeviceRef& deviceRef, const unsigned char* data, size_t size, UITexture& outTexture);
bool LoadAllRetroSkillAssets(RetroSkillAssets& assets, const RetroDeviceRef& deviceRef, const char* assetPath);
void CleanupRetroSkillAssets(RetroSkillAssets& assets);
UITexture* GetRetroSkillTexture(RetroSkillAssets& assets, const char* name);
UITexture* GetRetroSkillSkillIconTexture(RetroSkillAssets& assets, int skillId);
UITexture* GetRetroSkillSkillIconMouseOverTexture(RetroSkillAssets& assets, int skillId);
UITexture* GetRetroSkillSkillIconDisabledTexture(RetroSkillAssets& assets, int skillId);
