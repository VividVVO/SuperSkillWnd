#include "retro_skill_panel.h"

#include "retro_skill_app.h"
#include "retro_skill_text_dwrite.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

static const ImU32 kRetroSkillTextColor = IM_COL32(85, 85, 85, 255);

static void DrawOutlinedText(ImDrawList* dl, const ImVec2& pos, ImU32 color, const char* text, float mainScale, float fontSize = 0.0f, float glyphSpacing = 0.0f)
{
    const ImVec2 alignedPos(floorf(pos.x), floorf(pos.y));
    (void)mainScale;

    const float resolvedFontSize = (fontSize > 0.0f) ? fontSize : ImGui::GetFontSize();
    if (RetroSkillDWriteDrawTextEx(dl, alignedPos, color, text, resolvedFontSize, glyphSpacing))
        return;

    if (fontSize > 0.0f)
    {
        ImFont* font = ImGui::GetFont();
        dl->AddText(font, fontSize, alignedPos, color, text);
        return;
    }

    dl->AddText(alignedPos, color, text);
}

static void DrawPlainImGuiText(ImDrawList* dl, const ImVec2& pos, ImU32 color, const char* text, float fontSize = 0.0f)
{
    if (!dl || !text || !text[0])
        return;

    const ImVec2 alignedPos(floorf(pos.x), floorf(pos.y));
    if (fontSize > 0.0f)
    {
        ImFont* font = ImGui::GetFont();
        dl->AddText(font, fontSize, alignedPos, color, text);
        return;
    }

    dl->AddText(alignedPos, color, text);
}

static void RenderSkillTooltip(const SkillEntry& skill, RetroSkillAssets& assets, float mainScale)
{
    ImGui::SetNextWindowBgAlpha(0.96f);
    ImGui::SetNextWindowPos(ImVec2(
        ImGui::GetIO().MousePos.x + 14.0f * mainScale,
        ImGui::GetIO().MousePos.y + 18.0f * mainScale),
        ImGuiCond_Always);

    if (!ImGui::BeginTooltip())
        return;

    UITexture* iconTex = nullptr;
    if (skill.showDisabledIcon)
        iconTex = GetRetroSkillSkillIconDisabledTexture(assets, skill.iconId);
    if ((!iconTex || !iconTex->texture) && skill.iconId > 0)
        iconTex = GetRetroSkillSkillIconTexture(assets, skill.iconId);

    if (iconTex && iconTex->texture)
    {
        ImGui::Image((ImTextureID)iconTex->texture, ImVec2(32.0f * mainScale, 32.0f * mainScale));
        ImGui::SameLine();
    }

    ImGui::BeginGroup();
    ImGui::TextUnformatted(skill.name.c_str());
    ImGui::Text("Lv. %d / %d", skill.level, skill.maxLevel);
    ImGui::TextUnformatted(skill.isPassive ? "Passive Skill" : "Active Skill");
    if (skill.isSuperSkill)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.24f, 1.0f), "Super Skill");
        if (skill.superSpCost > 0)
            ImGui::Text("Super SP Cost: %d", skill.superSpCost);
    }
    ImGui::EndGroup();

    if (!skill.tooltipPreview.empty() || !skill.tooltipDescription.empty() || !skill.tooltipDetail.empty())
    {
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 300.0f * mainScale);

        if (!skill.tooltipPreview.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.90f, 0.55f, 1.0f), "%s", skill.tooltipPreview.c_str());
            ImGui::Spacing();
        }

        if (!skill.tooltipDescription.empty())
        {
            ImGui::TextWrapped("%s", skill.tooltipDescription.c_str());
            if (!skill.tooltipDetail.empty())
                ImGui::Spacing();
        }

        if (!skill.tooltipDetail.empty())
            ImGui::TextColored(ImVec4(0.72f, 0.95f, 0.72f, 1.0f), "%s", skill.tooltipDetail.c_str());

        ImGui::PopTextWrapPos();
    }

    ImGui::EndTooltip();
}

void RenderRetroSkillPanel(RetroSkillRuntimeState& state, RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, float mainScale, const RetroSkillBehaviorHooks* hooks)
{
    ImGuiIO& io = ImGui::GetIO();
    const PanelMetrics m = GetPanelMetrics(mainScale);
    state.dragTitleHeight = 20.0f * mainScale;

    state.dragSkillStartedThisFrame = false;
    state.isPressingUiButton = false;

    bool isHoveringActionButtonsThisFrame = false;
    bool actionButtonsClickedThisFrame = false;

    auto shouldSuppressTabAction = [&](int targetTab) {
        if (!hooks || !hooks->onTabAction)
            return false;
        RetroSkillActionContext context;
        context.currentTab = state.activeTab;
        context.targetTab = targetTab;
        return hooks->onTabAction(context, hooks->userData) == RetroSkill_SuppressDefault;
    };

    auto shouldSuppressPlusAction = [&](int skillIndex, const SkillEntry& skill) {
        if (!hooks || !hooks->onPlusAction)
            return false;
        RetroSkillActionContext context;
        context.currentTab = state.activeTab;
        context.targetTab = state.activeTab;
        context.skillIndex = skillIndex;
        context.skillId = skill.skillId;
        context.currentLevel = skill.level;
        context.baseLevel = skill.baseLevel;
        context.bonusLevel = skill.bonusLevel;
        context.maxLevel = skill.maxLevel;
        context.canUpgrade = skill.canUpgrade;
        return hooks->onPlusAction(context, hooks->userData) == RetroSkill_SuppressDefault;
    };

    auto shouldSuppressInitAction = [&]() {
        if (!hooks || !hooks->onInitAction)
            return false;
        RetroSkillActionContext context;
        context.currentTab = state.activeTab;
        context.targetTab = state.activeTab;
        return hooks->onInitAction(context, hooks->userData) == RetroSkill_SuppressDefault;
    };

    auto shouldSuppressDragBegin = [&](int skillIndex, const SkillEntry& skill) {
        if (!hooks || !hooks->onSkillDragBegin)
            return false;
        RetroSkillActionContext context;
        context.currentTab = state.activeTab;
        context.targetTab = state.activeTab;
        context.skillIndex = skillIndex;
        context.skillId = skill.skillId;
        context.currentLevel = skill.level;
        context.baseLevel = skill.baseLevel;
        context.bonusLevel = skill.bonusLevel;
        context.maxLevel = skill.maxLevel;
        context.canUpgrade = skill.canUpgrade;
        return hooks->onSkillDragBegin(context, hooks->userData) == RetroSkill_SuppressDefault;
    };

    auto getQuickSlotBarRect = [&](float* outX, float* outY, float* outW, float* outH) {
        if (!outX || !outY || !outW || !outH)
            return false;
        if (!state.quickSlotBarVisible || state.quickSlotBarCols <= 0 || state.quickSlotBarRows <= 0 || state.quickSlotBarSlotSize <= 0)
            return false;

        *outX = (float)state.quickSlotBarOriginX;
        *outY = (float)state.quickSlotBarOriginY;
        *outW = (float)(state.quickSlotBarCols * state.quickSlotBarSlotSize);
        *outH = (float)(state.quickSlotBarRows * state.quickSlotBarSlotSize);
        return true;
    };

    auto fireDragEnd = [&](int skillIndex, const SkillEntry& skill, float dropScreenX, float dropScreenY, bool outsidePanel) {
        if (!hooks || !hooks->onSkillDragEnd)
            return;
        RetroSkillActionContext context;
        context.currentTab = state.activeTab;
        context.targetTab = state.activeTab;
        context.skillIndex = skillIndex;
        context.skillId = skill.skillId;
        context.currentLevel = skill.level;
        context.baseLevel = skill.baseLevel;
        context.bonusLevel = skill.bonusLevel;
        context.maxLevel = skill.maxLevel;
        context.canUpgrade = skill.canUpgrade;
        context.dropX = dropScreenX;
        context.dropY = dropScreenY;
        context.droppedOutsidePanel = outsidePanel;

        // 计算 drop 到技能栏的 slot 索引
        context.dropSlotIndex = -1;
        if (outsidePanel && state.quickSlotBarAcceptDrop)
        {
            float barX = 0.0f;
            float barY = 0.0f;
            float barW = 0.0f;
            float barH = 0.0f;
            if (getQuickSlotBarRect(&barX, &barY, &barW, &barH) &&
                dropScreenX >= barX && dropScreenX < barX + barW &&
                dropScreenY >= barY && dropScreenY < barY + barH)
            {
                int col = (int)((dropScreenX - barX) / (float)state.quickSlotBarSlotSize);
                int row = (int)((dropScreenY - barY) / (float)state.quickSlotBarSlotSize);
                if (col >= 0 && col < state.quickSlotBarCols && row >= 0 && row < state.quickSlotBarRows)
                    context.dropSlotIndex = row * state.quickSlotBarCols + col;
            }
        }

        hooks->onSkillDragEnd(context, hooks->userData);
    };

    auto fireSkillUse = [&](int skillIndex, const SkillEntry& skill) {
        if (!hooks || !hooks->onSkillUse)
            return;
        RetroSkillActionContext context;
        context.currentTab = state.activeTab;
        context.targetTab = state.activeTab;
        context.skillIndex = skillIndex;
        context.skillId = skill.skillId;
        context.currentLevel = skill.level;
        context.baseLevel = skill.baseLevel;
        context.bonusLevel = skill.bonusLevel;
        context.maxLevel = skill.maxLevel;
        context.canUpgrade = skill.canUpgrade;
        hooks->onSkillUse(context, hooks->userData);
    };

    auto canAcceptActionClick = [&]() {
        double now = ImGui::GetTime();
        if (state.lastAcceptedClickTime < 0.0)
            return true;
        return (now - state.lastAcceptedClickTime) >= state.minClickIntervalSeconds;
    };

    auto markActionClickAccepted = [&]() {
        state.lastAcceptedClickTime = ImGui::GetTime();
    };

    auto canStartSkillDrag = [&](const SkillEntry& skill) {
        if (state.isScrollDragging)
            return false;
        return skill.canDrag;
    };

    ImGui::SetNextWindowSize(ImVec2(m.width, m.height), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("Super Skill Panel", nullptr, flags))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 panelPos = wp;

        ImGui::SetCursorScreenPos(panelPos);
        ImGui::PushID("title_drag_zone");
        ImGui::InvisibleButton("title_drag_zone", ImVec2(m.width, state.dragTitleHeight));
        bool titleHovered = ImGui::IsItemHovered();
        bool titleHeld = ImGui::IsItemActive() && io.MouseDown[0];
        ImGui::PopID();

        if (!io.MouseDown[0])
            state.titlePressLockedUntilRelease = false;

        if (titleHeld && !titleHovered)
            state.titlePressLockedUntilRelease = true;

        if (titleHeld && titleHovered && !state.titlePressLockedUntilRelease)
            state.isPressingUiButton = true;

        UITexture* mainTex = GetRetroSkillTexture(assets, "main");
        if (mainTex && mainTex->texture)
        {
            dl->AddImage((ImTextureID)mainTex->texture,
                        panelPos,
                        ImVec2(panelPos.x + mainTex->width * mainScale,
                               panelPos.y + mainTex->height * mainScale));
        }

        ImGui::SetCursorScreenPos(ImVec2(panelPos.x, panelPos.y + state.dragTitleHeight));
        ImGui::PushID("panel_body_drag_zone");
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("panel_body_drag_zone", ImVec2(m.width, m.height - state.dragTitleHeight));
        bool panelBodyHovered = ImGui::IsItemHovered();
        ImGui::PopID();

        if (panelBodyHovered)
            state.isPressingUiButton = state.isPressingUiButton || io.MouseDown[0];

        ImVec2 passiveTabPos(panelPos.x + 10.0f * mainScale, panelPos.y + 27.0f * mainScale);
        ImVec2 activeTabPos(panelPos.x + 89.0f * mainScale, panelPos.y + 27.0f * mainScale);

        ImGui::SetCursorScreenPos(passiveTabPos);
        ImGui::PushID("passive_tab");
        ImGui::InvisibleButton("passive_tab", ImVec2(76.0f * mainScale, 20.0f * mainScale));
        bool passiveClicked = ImGui::IsItemClicked();
        bool passiveHeld = ImGui::IsItemActive() && io.MouseDown[0];
        ImGui::PopID();

        ImGui::SetCursorScreenPos(activeTabPos);
        ImGui::PushID("active_tab");
        ImGui::InvisibleButton("active_tab", ImVec2(76.0f * mainScale, 20.0f * mainScale));
        bool activeClicked = ImGui::IsItemClicked();
        bool activeHeld = ImGui::IsItemActive() && io.MouseDown[0];
        ImGui::PopID();

        if (passiveHeld || activeHeld)
            state.isPressingUiButton = true;

        if (passiveClicked)
        {
            if (canAcceptActionClick())
            {
                bool suppressDefault = shouldSuppressTabAction(0);
                if (!suppressDefault)
                {
                    state.activeTab = 0;
                    state.scrollOffset = 0.0f;
                    state.isDraggingSkill = false;
                    state.dragSkillTab = -1;
                    state.dragSkillIndex = -1;
                }
                markActionClickAccepted();
            }
        }

        if (activeClicked)
        {
            if (canAcceptActionClick())
            {
                bool suppressDefault = shouldSuppressTabAction(1);
                if (!suppressDefault)
                {
                    state.activeTab = 1;
                    state.scrollOffset = 0.0f;
                    state.isDraggingSkill = false;
                    state.dragSkillTab = -1;
                    state.dragSkillIndex = -1;
                }
                markActionClickAccepted();
            }
        }

        if (state.activeTab == 0)
        {
            UITexture* passiveActiveTex = GetRetroSkillTexture(assets, "Passive.1");
            if (passiveActiveTex && passiveActiveTex->texture)
            {
                dl->AddImage((ImTextureID)passiveActiveTex->texture,
                            passiveTabPos,
                            ImVec2(passiveTabPos.x + passiveActiveTex->width * mainScale,
                                   passiveTabPos.y + passiveActiveTex->height * mainScale));
            }
        }
        else
        {
            UITexture* passiveNormalTex = GetRetroSkillTexture(assets, "Passive.0");
            if (passiveNormalTex && passiveNormalTex->texture)
            {
                dl->AddImage((ImTextureID)passiveNormalTex->texture,
                            passiveTabPos,
                            ImVec2(passiveTabPos.x + passiveNormalTex->width * mainScale,
                                   passiveTabPos.y + passiveNormalTex->height * mainScale));
            }
        }

        if (state.activeTab == 1)
        {
            UITexture* activeActiveTex = GetRetroSkillTexture(assets, "ActivePassive.1");
            if (activeActiveTex && activeActiveTex->texture)
            {
                ImVec2 activeActivePos(activeTabPos.x - 1.0f * mainScale, activeTabPos.y);
                dl->AddImage((ImTextureID)activeActiveTex->texture,
                            activeActivePos,
                            ImVec2(activeActivePos.x + activeActiveTex->width * mainScale,
                                   activeActivePos.y + activeActiveTex->height * mainScale));
            }
        }
        else
        {
            UITexture* activeNormalTex = GetRetroSkillTexture(assets, "ActivePassive.0");
            if (activeNormalTex && activeNormalTex->texture)
            {
                ImVec2 activeNormalPos(activeTabPos.x - 1.0f * mainScale, activeTabPos.y);
                dl->AddImage((ImTextureID)activeNormalTex->texture,
                            activeNormalPos,
                            ImVec2(activeNormalPos.x + activeNormalTex->width * mainScale,
                                   activeNormalPos.y + activeNormalTex->height * mainScale));
            }
        }

        const char* typeIconKey = (state.activeTab == 0) ? "TypeIcon.2" : "TypeIcon.0";
        UITexture* typeIconTex = GetRetroSkillTexture(assets, typeIconKey);
        if (typeIconTex && typeIconTex->texture)
        {
            ImVec2 typeIconPos(panelPos.x + 15.0f * mainScale, panelPos.y + 55.0f * mainScale);
            dl->AddImage((ImTextureID)typeIconTex->texture,
                        typeIconPos,
                        ImVec2(typeIconPos.x + typeIconTex->width * mainScale,
                               typeIconPos.y + typeIconTex->height * mainScale));
        }

        if (state.hasSuperSkillData)
        {
            char superSpValue[32] = {};
            sprintf_s(superSpValue, "%d", state.superSkillPoints);

            // The current ImGui atlas renders a 9px request closer to ~6px visually,
            // so we oversize the draw call to land near a true 9px visible height.
            const float superSpFontSizePx = 13.5f;
            const float superSpRightX = panelPos.x + 160.0f * mainScale;
            const float superSpCenterY = panelPos.y + (258.0f + 3.0f) * mainScale;
            ImFont* font = ImGui::GetFont();
            const ImVec2 superSpTextSize = font
                ? font->CalcTextSizeA(superSpFontSizePx, FLT_MAX, 0.0f, superSpValue)
                : ImGui::CalcTextSize(superSpValue);
            const ImVec2 superSpPos(
                floorf(superSpRightX - superSpTextSize.x),
                floorf(superSpCenterY - superSpTextSize.y * 0.5f));

            DrawPlainImGuiText(
                dl,
                superSpPos,
                kRetroSkillTextColor,
                superSpValue,
                superSpFontSizePx);
        }

        auto& skills = (state.activeTab == 0) ? state.passiveSkills : state.activeSkills;
        const SkillEntry* hoveredSkill = nullptr;
        float listStartY = panelPos.y + 93.0f * mainScale;
        float listEndY = panelPos.y + 248.0f * mainScale;
        float listX = panelPos.x + 9.0f * mainScale;
        float skillBoxHeight = 35.0f * mainScale;
        float skillGap = 5.0f * mainScale;

        int totalSkills = (int)skills.size();
        float totalSkillHeight = skillBoxHeight + skillGap;
        float listHeight = listEndY - listStartY;
        int visibleSkills = (int)floorf((listHeight + skillGap) / totalSkillHeight);
        if (visibleSkills < 1)
            visibleSkills = 1;
        float maxScrollOffset = (float)((totalSkills > visibleSkills) ? (totalSkills - visibleSkills) * totalSkillHeight : 0.0f);
        int maxScrollIndex = (totalSkills > visibleSkills) ? (totalSkills - visibleSkills) : 0;

        if (state.scrollOffset < 0.0f) state.scrollOffset = 0.0f;
        if (state.scrollOffset > maxScrollOffset) state.scrollOffset = maxScrollOffset;

        bool isMouseOverList = io.MousePos.x >= listX && io.MousePos.x <= (listX + 140.0f * mainScale) &&
                               io.MousePos.y >= listStartY && io.MousePos.y <= listEndY;
        const float effectiveScrollOffset = state.usesGameSkillData
            ? (float)state.gameVisibleStartIndex * totalSkillHeight
            : state.scrollOffset;

        if (!state.usesGameSkillData && isMouseOverList && !state.isScrollDragging && io.MouseWheel != 0.0f && maxScrollIndex > 0)
        {
            int scrollIndex = (int)round(state.scrollOffset / totalSkillHeight);
            if (io.MouseWheel > 0.0f)
                scrollIndex--;
            else if (io.MouseWheel < 0.0f)
                scrollIndex++;

            if (scrollIndex < 0) scrollIndex = 0;
            if (scrollIndex > maxScrollIndex) scrollIndex = maxScrollIndex;
            state.scrollOffset = (float)scrollIndex * totalSkillHeight;
        }

        dl->PushClipRect(
            ImVec2(floorf(listX), floorf(listStartY)),
            ImVec2(floorf(listX + 140.0f * mainScale), floorf(listEndY)),
            true);

        for (size_t i = 0; i < skills.size(); ++i)
        {
            SkillEntry& skill = skills[i];
            float skillY = listStartY + i * totalSkillHeight - effectiveScrollOffset;

            if (skillY < listStartY || (skillY + skillBoxHeight) > listEndY)
                continue;

            ImVec2 skillBoxPos(listX, skillY);
            const char* skillFrameKey = (skill.upgradeState != 0) ? "skill1" : "skill0";

            UITexture* skillBoxTex = GetRetroSkillTexture(assets, skillFrameKey);
            if (skillBoxTex && skillBoxTex->texture)
            {
                dl->AddImage((ImTextureID)skillBoxTex->texture,
                            skillBoxPos,
                            ImVec2(skillBoxPos.x + skillBoxTex->width * mainScale,
                                   skillBoxPos.y + skillBoxTex->height * mainScale));
            }

            ImVec2 iconPos(skillBoxPos.x + 3.0f * mainScale, skillBoxPos.y + 2.0f * mainScale);
            ImVec2 iconSize(32.0f * mainScale, 32.0f * mainScale);

            if (!state.isScrollDragging)
            {
                ImGui::SetCursorScreenPos(iconPos);
                ImGui::PushID(("skill_drag_" + std::to_string(i)).c_str());
                ImGui::InvisibleButton("skill_drag", iconSize);

                // 点击 icon 立即开始拖拽，再次按下鼠标结束
                if (ImGui::IsItemClicked(0) && !state.isDraggingSkill && canStartSkillDrag(skill))
                {
                    bool suppressDefaultDrag = shouldSuppressDragBegin((int)i, skill);
                    if (!suppressDefaultDrag)
                    {
                        state.isDraggingSkill = true;
                        state.dragSkillTab = state.activeTab;
                        state.dragSkillIndex = (int)i;
                        state.dragSkillGrabOffset = ImVec2(iconSize.x * 0.5f, iconSize.y * 0.5f);
                        state.dragSkillStartedThisFrame = true;
                        state.dragSkillIsClickMode = true;
                    }
                }

                // 右键点击技能 icon -> 使用技能
                if (ImGui::IsItemClicked(1) && !state.isDraggingSkill && skill.canUse && skill.level > 0)
                {
                    fireSkillUse((int)i, skill);
                }

                ImGui::PopID();
            }

            bool isThisSkillDragging = state.isDraggingSkill && (state.dragSkillTab == state.activeTab) && (state.dragSkillIndex == (int)i);

            // Determine icon state: disabled > mouseOver > normal
            bool isRowHovered = io.MousePos.x >= skillBoxPos.x && io.MousePos.x < (skillBoxPos.x + 140.0f * mainScale) &&
                                io.MousePos.y >= skillBoxPos.y && io.MousePos.y < (skillBoxPos.y + skillBoxHeight);
            bool isSkillDisabled = skill.showDisabledIcon;

            if (isRowHovered)
                hoveredSkill = &skill;

            UITexture* skillIconTex = nullptr;
            if (isSkillDisabled)
                skillIconTex = GetRetroSkillSkillIconDisabledTexture(assets, skill.iconId);
            else if (isRowHovered && !state.isDraggingSkill)
                skillIconTex = GetRetroSkillSkillIconMouseOverTexture(assets, skill.iconId);

            // Fallback to normal icon
            if (!skillIconTex || !skillIconTex->texture)
                skillIconTex = GetRetroSkillSkillIconTexture(assets, skill.iconId);

            if (skillIconTex && skillIconTex->texture)
            {
                dl->AddImage(
                    (ImTextureID)skillIconTex->texture,
                    iconPos,
                    ImVec2(iconPos.x + iconSize.x, iconPos.y + iconSize.y));
            }
            else
            {
                dl->AddRectFilled(iconPos, ImVec2(iconPos.x + iconSize.x, iconPos.y + iconSize.y),
                                 skill.iconColor, 4.0f * mainScale);
                dl->AddRect(iconPos, ImVec2(iconPos.x + iconSize.x, iconPos.y + iconSize.y),
                           IM_COL32(25, 27, 31, 255), 4.0f * mainScale, 0, 1.0f * mainScale);
            }

            ImVec2 textPos(skillBoxPos.x + 41.0f * mainScale, skillBoxPos.y + 3.0f * mainScale);
            DrawOutlinedText(dl, textPos, kRetroSkillTextColor, skill.name.c_str(), mainScale, 0.0f, 1.0f * mainScale);

            char levelText[32];
            sprintf_s(levelText, "%d", skill.level);
            DrawOutlinedText(dl, ImVec2(floorf(textPos.x), floorf(textPos.y + 19.0f * mainScale)), kRetroSkillTextColor, levelText, mainScale, floorf(12.0f * mainScale));

            if (skill.bonusLevel > 0)
            {
                char bonusText[32];
                sprintf_s(bonusText, "(+%d)", skill.bonusLevel);
                DrawOutlinedText(
                    dl,
                    ImVec2(floorf(textPos.x + 17.0f * mainScale), floorf(textPos.y + 19.0f * mainScale)),
                    IM_COL32(33, 153, 33, 255),
                    bonusText,
                    mainScale,
                    floorf(9.0f * mainScale));
            }

            UITexture* plusNormalTex = GetRetroSkillTexture(assets, "BtSpUp.normal");
            ImVec2 plusPos(skillBoxPos.x + 125.0f * mainScale, skillBoxPos.y + 20.0f * mainScale);
            ImVec2 plusVisualSize(
                ((plusNormalTex && plusNormalTex->width > 0) ? plusNormalTex->width : 12) * mainScale,
                ((plusNormalTex && plusNormalTex->height > 0) ? plusNormalTex->height : 12) * mainScale);
            const float plusHitInsetX = 2.0f * mainScale;
            const float plusHitInsetY = 2.0f * mainScale;
            ImVec2 plusHitPos(plusPos.x + plusHitInsetX, plusPos.y + plusHitInsetY);
            ImVec2 plusHitSize(plusVisualSize.x - plusHitInsetX * 2.0f, plusVisualSize.y - plusHitInsetY * 2.0f);
            if (plusHitSize.x < 4.0f * mainScale) plusHitSize.x = 4.0f * mainScale;
            if (plusHitSize.y < 4.0f * mainScale) plusHitSize.y = 4.0f * mainScale;

            ImGui::SetCursorScreenPos(plusHitPos);
            ImGui::PushID(("plus" + std::to_string(i)).c_str());
            ImGui::InvisibleButton("plus", plusHitSize);
            bool plusHovered = ImGui::IsItemHovered();
            bool plusHeld = ImGui::IsItemActive() && plusHovered;
            bool plusReleased = ImGui::IsItemDeactivated();
            bool plusReleasedWithClick = plusReleased && plusHovered;
            ImGui::PopID();

            isHoveringActionButtonsThisFrame = isHoveringActionButtonsThisFrame || plusHovered;
            actionButtonsClickedThisFrame = actionButtonsClickedThisFrame || plusReleasedWithClick;

            if (plusHeld)
                state.isPressingUiButton = true;

            UITexture* plusTex = nullptr;
            if (!skill.canUpgrade)
                plusTex = GetRetroSkillTexture(assets, "BtSpUp.disabled");
            else if (plusHeld)
                plusTex = GetRetroSkillTexture(assets, "BtSpUp.pressed");
            else if (plusHovered)
                plusTex = GetRetroSkillTexture(assets, "BtSpUp.mouseOver");
            else
                plusTex = GetRetroSkillTexture(assets, "BtSpUp.normal");

            if (plusTex && plusTex->texture)
            {
                dl->AddImage((ImTextureID)plusTex->texture,
                            plusPos,
                            ImVec2(plusPos.x + plusTex->width * mainScale,
                                   plusPos.y + plusTex->height * mainScale));
            }

            if (plusReleasedWithClick && skill.canUpgrade)
            {
                if (canAcceptActionClick())
                {
                    shouldSuppressPlusAction((int)i, skill);
                    markActionClickAccepted();
                }
            }

            float lineY = skillY + skillBoxHeight + skillGap / 2.0f;
            if (i < skills.size() - 1 && lineY < listEndY)
            {
                ImVec2 lineStart(listX, lineY);
                ImVec2 lineEnd(listX + 140.0f * mainScale, lineY);
                dl->AddLine(lineStart, lineEnd, IM_COL32(153, 153, 153, 255), 1.0f * mainScale);
            }
        }

        dl->PopClipRect();

        if (hoveredSkill && !state.isDraggingSkill)
            RenderSkillTooltip(*hoveredSkill, assets, mainScale);

        float scrollbarTrackTop = panelPos.y + 104.0f * mainScale;
        float scrollbarTrackHeight = 133.0f * mainScale;

        UITexture* scrollCheckTex = GetRetroSkillTexture(assets, "scroll");
        if (scrollCheckTex && scrollCheckTex->texture)
        {
            float scrollThumbHeight = (float)scrollCheckTex->height;
            float scrollRatio = 0.0f;
            if (maxScrollOffset > 0.0f)
                scrollRatio = effectiveScrollOffset / maxScrollOffset;

            if (scrollRatio < 0.0f) scrollRatio = 0.0f;
            if (scrollRatio > 1.0f) scrollRatio = 1.0f;

            float availableTrackHeight = scrollbarTrackHeight - scrollThumbHeight;
            float thumbY = scrollbarTrackTop + scrollRatio * availableTrackHeight;
            ImVec2 scrollPos(panelPos.x + 159.0f * mainScale, thumbY);

            ImVec2 scrollMin(scrollPos.x - (float)scrollCheckTex->width / 2.0f, thumbY);
            ImVec2 scrollMax(scrollPos.x + (float)scrollCheckTex->width / 2.0f, thumbY + scrollThumbHeight);

            bool isMouseOverThumb = io.MousePos.x >= scrollMin.x && io.MousePos.x <= scrollMax.x &&
                                    io.MousePos.y >= scrollMin.y && io.MousePos.y <= scrollMax.y;

            if (isMouseOverThumb && ImGui::IsMouseClicked(0))
            {
                state.isScrollDragging = true;
                state.scrollDragStartY = io.MousePos.y;
                state.scrollDragStartOffset = state.scrollOffset;
            }

            if (!io.MouseDown[0])
                state.isScrollDragging = false;

            if (!state.usesGameSkillData && state.isScrollDragging)
            {
                float mouseDelta = io.MousePos.y - state.scrollDragStartY;

                if (availableTrackHeight > 0.0f && maxScrollOffset > 0.0f)
                {
                    float offsetDelta = (mouseDelta / availableTrackHeight) * maxScrollOffset;
                    float newScrollOffset = state.scrollDragStartOffset + offsetDelta;
                    int scrollIndex = (int)round(newScrollOffset / totalSkillHeight);

                    if (scrollIndex < 0) scrollIndex = 0;
                    if (scrollIndex > maxScrollIndex) scrollIndex = maxScrollIndex;

                    state.scrollOffset = (float)scrollIndex * totalSkillHeight;
                }
            }

            bool isPressed = state.isScrollDragging || (isMouseOverThumb && io.MouseDown[0]);
            UITexture* scrollTex = nullptr;

            if (isPressed)
            {
                scrollTex = GetRetroSkillTexture(assets, "scroll.pressed");
                if (!scrollTex || !scrollTex->texture)
                    scrollTex = GetRetroSkillTexture(assets, "scroll");
            }
            else
            {
                scrollTex = GetRetroSkillTexture(assets, "scroll");
            }

            if (scrollTex && scrollTex->texture)
            {
                ImVec2 roundedMin(floor(scrollMin.x), floor(scrollMin.y));
                ImVec2 roundedMax(floor(scrollMax.x), floor(scrollMax.y));

                dl->AddImage((ImTextureID)scrollTex->texture, roundedMin, roundedMax);
            }
        }

        UITexture* initNormalTex = GetRetroSkillTexture(assets, "initial.normal");
        ImVec2 initPos(panelPos.x + m.width - 60.0f * mainScale,
                      panelPos.y + m.height - 26.0f * mainScale);
        ImVec2 initVisualSize(
            ((initNormalTex && initNormalTex->width > 0) ? initNormalTex->width : 50) * mainScale,
            ((initNormalTex && initNormalTex->height > 0) ? initNormalTex->height : 16) * mainScale);
        const float initHitInsetX = 4.0f * mainScale;
        const float initHitInsetY = 2.0f * mainScale;
        ImVec2 initHitPos(initPos.x + initHitInsetX, initPos.y + initHitInsetY);
        ImVec2 initHitSize(initVisualSize.x - initHitInsetX * 2.0f, initVisualSize.y - initHitInsetY * 2.0f);
        if (initHitSize.x < 8.0f * mainScale) initHitSize.x = 8.0f * mainScale;
        if (initHitSize.y < 4.0f * mainScale) initHitSize.y = 4.0f * mainScale;

        ImGui::SetCursorScreenPos(initHitPos);
        ImGui::PushID("init_button");
        ImGui::InvisibleButton("init_button", initHitSize);
        bool initHovered = ImGui::IsItemHovered();
        bool initHeld = ImGui::IsItemActive() && initHovered;
        bool initReleased = ImGui::IsItemDeactivated();
        bool initReleasedWithClick = initReleased && initHovered;
        ImGui::PopID();

        isHoveringActionButtonsThisFrame = isHoveringActionButtonsThisFrame || initHovered;
        actionButtonsClickedThisFrame = actionButtonsClickedThisFrame || initReleasedWithClick;

        if (initHeld)
            state.isPressingUiButton = true;

        UITexture* initTex;
        if (initHeld)
            initTex = GetRetroSkillTexture(assets, "initial.pressed");
        else if (initHovered)
            initTex = GetRetroSkillTexture(assets, "initial.mouseOver");
        else
            initTex = GetRetroSkillTexture(assets, "initial.normal");

        if (initTex && initTex->texture)
        {
            dl->AddImage((ImTextureID)initTex->texture,
                        initPos,
                        ImVec2(initPos.x + initTex->width * mainScale,
                               initPos.y + initTex->height * mainScale));
        }

        if (initReleasedWithClick)
        {
            if (canAcceptActionClick())
            {
                shouldSuppressInitAction();
                markActionClickAccepted();
            }
        }

        if (state.isDraggingSkill && state.dragSkillTab == state.activeTab && state.dragSkillIndex >= 0 && state.dragSkillIndex < (int)skills.size())
        {
            const SkillEntry& draggedSkill = skills[(size_t)state.dragSkillIndex];
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            ImVec2 dragIconPos(io.MousePos.x - state.dragSkillGrabOffset.x, io.MousePos.y - state.dragSkillGrabOffset.y);
            ImVec2 dragIconSize(32.0f * mainScale, 32.0f * mainScale);
            UITexture* draggedIconTex = GetRetroSkillSkillIconTexture(assets, draggedSkill.iconId);
            if (draggedIconTex && draggedIconTex->texture)
            {
                fg->AddImage(
                    (ImTextureID)draggedIconTex->texture,
                    dragIconPos,
                    ImVec2(dragIconPos.x + dragIconSize.x, dragIconPos.y + dragIconSize.y),
                    ImVec2(0.0f, 0.0f),
                    ImVec2(1.0f, 1.0f),
                    IM_COL32(255, 255, 255, 112));
            }
            else
            {
                fg->AddRectFilled(dragIconPos, ImVec2(dragIconPos.x + dragIconSize.x, dragIconPos.y + dragIconSize.y),
                                  IM_COL32(
                                      (draggedSkill.iconColor >> IM_COL32_R_SHIFT) & 0xFF,
                                      (draggedSkill.iconColor >> IM_COL32_G_SHIFT) & 0xFF,
                                      (draggedSkill.iconColor >> IM_COL32_B_SHIFT) & 0xFF,
                                      112),
                                  4.0f * mainScale);
            }
            fg->AddRect(dragIconPos, ImVec2(dragIconPos.x + dragIconSize.x, dragIconPos.y + dragIconSize.y),
                        IM_COL32(25, 27, 31, 180), 4.0f * mainScale, 0, 1.0f * mainScale);

            bool outsidePanelNow = io.MousePos.x < wp.x || io.MousePos.x >= (wp.x + m.width) ||
                                   io.MousePos.y < wp.y || io.MousePos.y >= (wp.y + m.height);
            if (outsidePanelNow)
            {
                float barX = 0.0f;
                float barY = 0.0f;
                float barW = 0.0f;
                float barH = 0.0f;
                const bool hasQuickSlotBar = getQuickSlotBarRect(&barX, &barY, &barW, &barH);
                const bool canDropToQuickSlotBar = hasQuickSlotBar && state.quickSlotBarAcceptDrop;
                bool overSkillBar = canDropToQuickSlotBar &&
                                    (io.MousePos.x >= barX && io.MousePos.x < barX + barW &&
                                     io.MousePos.y >= barY && io.MousePos.y < barY + barH);

                // 计算鼠标悬浮的具体 slot
                int hoverSlotCol = -1;
                int hoverSlotRow = -1;
                int hoverSlotIndex = -1;
                if (overSkillBar)
                {
                    hoverSlotCol = (int)((io.MousePos.x - barX) / (float)state.quickSlotBarSlotSize);
                    hoverSlotRow = (int)((io.MousePos.y - barY) / (float)state.quickSlotBarSlotSize);
                    if (hoverSlotCol >= 0 && hoverSlotCol < state.quickSlotBarCols &&
                        hoverSlotRow >= 0 && hoverSlotRow < state.quickSlotBarRows)
                        hoverSlotIndex = hoverSlotRow * state.quickSlotBarCols + hoverSlotCol;
                }

                ImDrawList* bg = ImGui::GetBackgroundDrawList();
                if (overSkillBar)
                {
                    // 高亮整个技能栏区域
                    bg->AddRectFilled(
                        ImVec2(barX, barY),
                        ImVec2(barX + barW, barY + barH),
                        IM_COL32(0, 200, 80, 25));
                    bg->AddRect(
                        ImVec2(barX, barY),
                        ImVec2(barX + barW, barY + barH),
                        IM_COL32(0, 255, 100, 80), 0.0f, 0, 2.0f * mainScale);

                    // 高亮具体 slot
                    if (hoverSlotIndex >= 0)
                    {
                        float slotX = barX + hoverSlotCol * (float)state.quickSlotBarSlotSize;
                        float slotY = barY + hoverSlotRow * (float)state.quickSlotBarSlotSize;
                        bg->AddRectFilled(
                            ImVec2(slotX, slotY),
                            ImVec2(slotX + (float)state.quickSlotBarSlotSize, slotY + (float)state.quickSlotBarSlotSize),
                            IM_COL32(0, 255, 100, 60));
                        bg->AddRect(
                            ImVec2(slotX, slotY),
                            ImVec2(slotX + (float)state.quickSlotBarSlotSize, slotY + (float)state.quickSlotBarSlotSize),
                            IM_COL32(255, 255, 255, 160), 0.0f, 0, 1.0f * mainScale);
                    }
                }

                // Hint text below dragged icon
                ImVec2 hintPos(dragIconPos.x, dragIconPos.y + dragIconSize.y + 4.0f * mainScale);
                const char* hintText = overSkillBar ? "Release to assign" :
                    (state.quickSlotBarCollapsed ? "Skill bar collapsed" : "Drag to skill bar");
                ImU32 hintColor = overSkillBar ? IM_COL32(0, 255, 100, 255) :
                    (state.quickSlotBarCollapsed ? IM_COL32(255, 120, 120, 255) : IM_COL32(255, 200, 50, 255));
                float hintBgWidth = overSkillBar ? 108.0f : (state.quickSlotBarCollapsed ? 118.0f : 100.0f);

                fg->AddRectFilled(
                    ImVec2(hintPos.x - 2.0f * mainScale, hintPos.y - 1.0f * mainScale),
                    ImVec2(hintPos.x + hintBgWidth * mainScale, hintPos.y + 14.0f * mainScale),
                    IM_COL32(0, 0, 0, 160), 3.0f * mainScale);
                DrawOutlinedText(fg, hintPos, hintColor, hintText, mainScale, floorf(10.0f * mainScale));
            }

            // Drag-end: 再次按下鼠标结束拖拽（跳过启动当帧）
            bool shouldEndDrag = false;
            if (!state.dragSkillStartedThisFrame)
            {
                shouldEndDrag = state.dragSkillIsClickMode
                    ? io.MouseClicked[0]
                    : ImGui::IsMouseReleased(0);
            }

            if (shouldEndDrag)
            {
                bool outsidePanel = io.MousePos.x < wp.x || io.MousePos.x >= (wp.x + m.width) ||
                                    io.MousePos.y < wp.y || io.MousePos.y >= (wp.y + m.height);
                // 2) Simulated PostMessage drag events pass through to the game
                int savedDragIndex = state.dragSkillIndex;
                state.isDraggingSkill = false;
                state.dragSkillTab = -1;
                state.dragSkillIndex = -1;
                state.dragSkillIsClickMode = false;

                fireDragEnd(savedDragIndex, draggedSkill, io.MousePos.x, io.MousePos.y, outsidePanel);
            }
        }

        if (!isHoveringActionButtonsThisFrame)
        {
            state.isHoveringActionButtons = false;
            state.actionHoverStoppedByClick = false;
            state.actionButtonsHoverStartTime = 0.0;
        }
        else
        {
            if (!state.isHoveringActionButtons)
            {
                state.actionButtonsHoverStartTime = ImGui::GetTime();
                state.actionHoverInstantUseNormal1 = ((GetTickCount64() & 1ULL) != 0ULL);
            }
            state.isHoveringActionButtons = true;
            if (actionButtonsClickedThisFrame)
            {
                state.actionHoverStoppedByClick = true;
                state.isHoldingActionButton = true;
            }
        }

        if (!io.MouseDown[0])
            state.isHoldingActionButton = false;

        if (state.isHoldingActionButton && !isHoveringActionButtonsThisFrame)
            state.isPressingUiButton = false;
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
}
