#pragma once

#include "imgui.h"
#include <string>
#include <vector>

struct SkillEntry {
    int skillId = 0;
    std::string name;
    int level = 0;
    int baseLevel = 0;
    int bonusLevel = 0;
    int maxLevel = 0;
    int upgradeState = 0;
    bool canUpgrade = false;
    ImU32 iconColor = IM_COL32(120, 120, 120, 255);
    int iconId = 0;
    bool enabled = true;
    bool isCustomInjected = false;
    bool isLearned = false;
    bool showDisabledIcon = false;
    bool canUse = false;
    bool canDrag = false;
    bool isPassive = false;
    bool isSuperSkill = false;
    bool hideFromNativeSkillWnd = false;
    bool allowNativeUpgradeFallback = false;
    bool isOnCooldown = false;
    int cooldownMs = 0;
    int remainingCooldownMs = 0;
    int superSpCost = 0;
    int superSpCarrierSkillId = 0;
    std::string tooltipPreview;
    std::string tooltipDescription;
    std::string tooltipDetail;
};

struct QuickSlotBinding {
    int skillId = 0;
    int skillTab = -1;
    std::string skillName;
};

// 游戏技能栏固定像素位置（用户实测值）
// 起点 (889, 701), 每格 34x34, 4列 x 2行
const int SKILL_BAR_ORIGIN_X = 885;
const int SKILL_BAR_ORIGIN_Y = 699;
const int SKILL_BAR_SLOT_SIZE = 34;
const int SKILL_BAR_COLS = 4;
const int SKILL_BAR_ROWS = 2;
const int SKILL_BAR_TOTAL_SLOTS = SKILL_BAR_COLS * SKILL_BAR_ROWS;

struct PanelMetrics {
    float shadow;
    float width;
    float height;
    float rounding;
    float titleHeight;
    float tabHeight;
    float tabWidth;
    float infoHeight;
    float rowHeight;
    float footerHeight;
    float outerPadding;
    float innerPadding;
    float iconSize;
};

inline PanelMetrics GetPanelMetrics(float mainScale)
{
    return {
        0.0f,
        174.0f * mainScale,
        299.0f * mainScale,
        0.0f,
        0.0f,
        20.0f * mainScale,
        76.0f * mainScale,
        24.0f * mainScale,
        36.0f * mainScale,
        24.0f * mainScale,
        0.0f,
        0.0f,
        32.0f * mainScale
    };
}

struct RetroSkillRuntimeState {
    std::vector<SkillEntry> passiveSkills;
    std::vector<SkillEntry> activeSkills;
    int superSkillPoints = 0;
    int superSkillCarrierSkillId = 0;
    bool hasSuperSkillData = false;

    int activeTab = 0;
    bool usesGameSkillData = false;
    int gameVisibleStartIndex = 0;
    int gameSkillCount = 0;
    float dragTitleHeight = 28.0f;

    bool isDraggingSkill = false;
    int dragSkillTab = -1;
    int dragSkillIndex = -1;
    ImVec2 dragSkillGrabOffset = ImVec2(0.0f, 0.0f);
    bool dragSkillStartedThisFrame = false;
    bool dragSkillIsClickMode = false;  // true=click-to-drag, false=hold-drag / release-drop

    bool isPressingUiButton = false;
    bool isHoldingActionButton = false;
    bool isHoveringActionButtons = false;
    bool actionHoverStoppedByClick = false;
    bool actionHoverInstantUseNormal1 = false;
    double actionButtonsHoverStartTime = 0.0;

    double minClickIntervalSeconds = 0.3;
    double lastAcceptedClickTime = -1.0;

    float scrollOffset = 0.0f;
    bool isScrollDragging = false;
    float scrollDragStartY = 0.0f;
    float scrollDragStartOffset = 0.0f;

    bool titlePressLockedUntilRelease = false;

    int quickSlotBarOriginX = SKILL_BAR_ORIGIN_X;
    int quickSlotBarOriginY = SKILL_BAR_ORIGIN_Y;
    int quickSlotBarSlotSize = SKILL_BAR_SLOT_SIZE;
    int quickSlotBarCols = SKILL_BAR_COLS;
    int quickSlotBarRows = SKILL_BAR_ROWS;
    bool quickSlotBarVisible = true;
    bool quickSlotBarAcceptDrop = true;
    bool quickSlotBarCollapsed = false;

    QuickSlotBinding quickSlots[8];
};

void ResetRetroSkillData(RetroSkillRuntimeState& state);
