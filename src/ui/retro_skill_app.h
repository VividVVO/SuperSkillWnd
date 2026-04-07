#pragma once

#include "retro_skill_assets.h"
#include "retro_skill_state.h"

#include <d3d9.h>

enum RetroSkillActionDecision {
    RetroSkill_UseDefault = 0,
    RetroSkill_SuppressDefault = 1
};

struct RetroSkillActionContext {
    int currentTab = 0;
    int targetTab = -1;
    int skillIndex = -1;
    int skillId = 0;
    int currentLevel = 0;
    int baseLevel = 0;
    int bonusLevel = 0;
    int maxLevel = 0;
    bool canUpgrade = false;
    float dropX = 0.0f;
    float dropY = 0.0f;
    bool droppedOutsidePanel = false;
    int dropSlotIndex = -1;  // 技能栏 slot 索引 (0~7), -1 = 未命中
};

typedef RetroSkillActionDecision (*RetroSkillTabActionCallback)(const RetroSkillActionContext& context, void* userData);
typedef RetroSkillActionDecision (*RetroSkillPlusActionCallback)(const RetroSkillActionContext& context, void* userData);
typedef RetroSkillActionDecision (*RetroSkillInitActionCallback)(const RetroSkillActionContext& context, void* userData);
typedef RetroSkillActionDecision (*RetroSkillSkillActionCallback)(const RetroSkillActionContext& context, void* userData);
typedef RetroSkillActionDecision (*RetroSkillSkillDragEndCallback)(const RetroSkillActionContext& context, void* userData);
typedef RetroSkillActionDecision (*RetroSkillSkillUseCallback)(const RetroSkillActionContext& context, void* userData);

struct RetroSkillBehaviorHooks {
    RetroSkillTabActionCallback onTabAction = nullptr;
    RetroSkillPlusActionCallback onPlusAction = nullptr;
    RetroSkillInitActionCallback onInitAction = nullptr;
    RetroSkillSkillActionCallback onSkillDragBegin = nullptr;
    RetroSkillSkillDragEndCallback onSkillDragEnd = nullptr;
    RetroSkillSkillUseCallback onSkillUse = nullptr;
    void* userData = nullptr;
};

void InitializeRetroSkillApp(RetroSkillRuntimeState& state, RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, const char* assetPath);
void ShutdownRetroSkillApp(RetroSkillAssets& assets);
void ConfigureRetroSkillDefaultBehaviorHooks(RetroSkillBehaviorHooks& hooks, RetroSkillRuntimeState& state);
void RenderRetroSkillCursorOverlay(RetroSkillRuntimeState& state, RetroSkillAssets& assets, float mainScale);
void RenderRetroSkillScene(RetroSkillRuntimeState& state, RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, float mainScale);
void RenderRetroSkillSceneEx(RetroSkillRuntimeState& state, RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, float mainScale, const RetroSkillBehaviorHooks* hooks);
