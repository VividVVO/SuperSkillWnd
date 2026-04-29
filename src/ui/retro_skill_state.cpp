#include "retro_skill_state.h"

namespace
{
    SkillEntry MakeSampleSkill(
        int skillId,
        const char* name,
        int level,
        int maxLevel,
        bool canUpgrade,
        ImU32 iconColor)
    {
        SkillEntry entry;
        entry.skillId = skillId;
        entry.name = name;
        entry.level = level;
        entry.baseLevel = level;
        entry.bonusLevel = 0;
        entry.maxLevel = maxLevel;
        entry.upgradeState = canUpgrade ? 1 : (level > 0 ? -1 : 0);
        entry.canUpgrade = canUpgrade;
        entry.iconColor = iconColor;
        entry.enabled = true;
        entry.isCustomInjected = false;
        entry.isLearned = level > 0;
        entry.showDisabledIcon = (entry.upgradeState == 0);
        entry.canUse = entry.enabled && entry.isLearned;
        entry.canDrag = entry.canUse;
        entry.isOnCooldown = false;
        entry.cooldownMs = 0;
        entry.remainingCooldownMs = 0;
        return entry;
    }
}

void ResetRetroSkillData(RetroSkillRuntimeState& state)
{
    state.passiveSkills = {
        MakeSampleSkill(1001001, "Attack Boost", 0, 1, false, IM_COL32(165, 108, 214, 255)),
        MakeSampleSkill(1001002, "White Eye", 1, 1, false, IM_COL32(148,  74, 194, 255)),
        MakeSampleSkill(1001003, "Extra Strike", 0, 1, false, IM_COL32(191, 107, 107, 255)),
        MakeSampleSkill(1001004, "Spirit Duration", 1, 1, false, IM_COL32(217, 138, 226, 255)),
        MakeSampleSkill(1001005, "Swift Soul", 0, 1, false, IM_COL32(226, 170, 170, 255)),
        MakeSampleSkill(1001006, "Spirit Cooldown", 1, 1, false, IM_COL32(180, 132, 232, 255))
    };

    state.activeSkills = {
        MakeSampleSkill(2001001, "Five Elements", 0, 1, true, IM_COL32(203, 132,  67, 255)),
        MakeSampleSkill(2001002, "Spirit Summon", 0, 1, true, IM_COL32( 92, 132, 194, 255)),
        MakeSampleSkill(2001003, "Break Magic", 0, 1, true, IM_COL32(120, 164, 120, 255)),
        MakeSampleSkill(2001004, "Element Burst", 0, 1, true, IM_COL32(180, 140, 200, 255)),
        MakeSampleSkill(2001005, "Flash Step", 0, 1, true, IM_COL32(150, 100, 180, 255)),
        MakeSampleSkill(2001006, "Guard Field", 0, 1, true, IM_COL32(100, 150, 200, 255)),
        MakeSampleSkill(2001007, "Flame Storm", 0, 1, true, IM_COL32(200, 100, 100, 255)),
        MakeSampleSkill(2001008, "Frozen World", 0, 1, true, IM_COL32(100, 200, 220, 255))
    };

    state.activeTab = 0;
    state.superSkillPoints = 0;
    state.superSkillCarrierSkillId = 0;
    state.hasSuperSkillData = false;
    state.usesGameSkillData = false;
    state.gameVisibleStartIndex = 0;
    state.gameSkillCount = 0;
    state.dragTitleHeight = 28.0f;

    state.isDraggingSkill = false;
    state.dragSkillTab = -1;
    state.dragSkillIndex = -1;
    state.dragSkillGrabOffset = ImVec2(0.0f, 0.0f);
    state.dragSkillStartedThisFrame = false;

    state.isPressingUiButton = false;
    state.isHoldingActionButton = false;
    state.isHoveringActionButtons = false;
    state.actionHoverStoppedByClick = false;
    state.actionHoverInstantUseNormal1 = false;
    state.actionButtonsHoverStartTime = 0.0;

    state.minClickIntervalSeconds = 0.0;
    state.lastAcceptedClickTime = -1.0;

    state.scrollOffset = 0.0f;
    state.isScrollDragging = false;
    state.scrollDragStartY = 0.0f;
    state.scrollDragStartOffset = 0.0f;

    state.titlePressLockedUntilRelease = false;

    state.quickSlotBarOriginX = SKILL_BAR_ORIGIN_X;
    state.quickSlotBarOriginY = SKILL_BAR_ORIGIN_Y;
    state.quickSlotBarSlotSize = SKILL_BAR_SLOT_SIZE;
    state.quickSlotBarCols = SKILL_BAR_COLS;
    state.quickSlotBarRows = SKILL_BAR_ROWS;
    state.quickSlotBarVisible = true;
    state.quickSlotBarAcceptDrop = true;
    state.quickSlotBarCollapsed = false;

    state.superSkillResetConfirmVisible = false;
    state.superSkillResetConfirmOpenRequested = false;
    state.superSkillResetConfirmSpentSp = 0;
    state.superSkillResetConfirmCostMeso = 0;
    state.superSkillResetConfirmCurrentMeso = 0;
    state.superSkillResetConfirmHasCurrentMeso = false;
    state.superSkillResetConfirmCostPending = false;
    state.superSkillResetConfirmPreviewRequestRevision = 0;
    state.superSkillResetConfirmPreviewRequestTick = 0;
    state.superSkillResetPreviewSpentSp = 0;
    state.superSkillResetPreviewCostMeso = 0;
    state.superSkillResetPreviewCurrentMeso = 0;
    state.superSkillResetPreviewHasCurrentMeso = false;
    state.superSkillResetPreviewRevision = 0;
}
