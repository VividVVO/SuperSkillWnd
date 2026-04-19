#include "retro_skill_app.h"

#include "imgui.h"
#include "retro_skill_panel.h"

#include <cmath>

namespace
{
    struct DefaultBehaviorController
    {
        RetroSkillRuntimeState* state = nullptr;
    };

    RetroSkillActionDecision OnDefaultTabAction(const RetroSkillActionContext& context, void* userData)
    {
        DefaultBehaviorController* controller = static_cast<DefaultBehaviorController*>(userData);
        if (!controller || !controller->state)
            return RetroSkill_UseDefault;

        RetroSkillRuntimeState& state = *controller->state;
        state.activeTab = context.targetTab;
        state.scrollOffset = 0.0f;
        state.isDraggingSkill = false;
        state.dragSkillTab = -1;
        state.dragSkillIndex = -1;
        return RetroSkill_SuppressDefault;
    }

    RetroSkillActionDecision OnDefaultPlusAction(const RetroSkillActionContext& context, void* userData)
    {
        DefaultBehaviorController* controller = static_cast<DefaultBehaviorController*>(userData);
        if (!controller || !controller->state)
            return RetroSkill_UseDefault;

        RetroSkillRuntimeState& state = *controller->state;
        std::vector<SkillEntry>& skills = (state.activeTab == 0) ? state.passiveSkills : state.activeSkills;
        if (context.skillIndex < 0 || context.skillIndex >= (int)skills.size())
            return RetroSkill_SuppressDefault;

        SkillEntry& skill = skills[(size_t)context.skillIndex];
        if (skill.level < skill.maxLevel)
        {
            skill.level++;
            skill.baseLevel = skill.level;
            skill.bonusLevel = 0;
            skill.upgradeState = (skill.level < skill.maxLevel) ? 1 : -1;
            skill.canUpgrade = skill.level < skill.maxLevel;
            skill.showDisabledIcon = false;
            skill.isLearned = skill.level > 0;
            skill.canUse = skill.enabled && skill.isLearned && !skill.isOnCooldown && !skill.isPassive;
            skill.canDrag = skill.enabled && skill.isLearned && !skill.isPassive;
        }
        return RetroSkill_SuppressDefault;
    }

    RetroSkillActionDecision OnDefaultInitAction(const RetroSkillActionContext& context, void* userData)
    {
        DefaultBehaviorController* controller = static_cast<DefaultBehaviorController*>(userData);
        if (!controller || !controller->state)
            return RetroSkill_UseDefault;

        RetroSkillRuntimeState& state = *controller->state;
        std::vector<SkillEntry>& skills = (context.currentTab == 0) ? state.passiveSkills : state.activeSkills;
        for (SkillEntry& skill : skills)
        {
            skill.level = 0;
            skill.baseLevel = 0;
            skill.bonusLevel = 0;
            skill.upgradeState = 0;
            skill.canUpgrade = skill.maxLevel > 0;
            skill.showDisabledIcon = true;
            skill.isLearned = false;
            skill.canUse = false;
            skill.canDrag = false;
        }

        return RetroSkill_SuppressDefault;
    }

    ImVec2 GetRetroCursorFixedOffset(RetroSkillAssets& assets, UITexture* currentTex)
    {
        const ImVec2 normalBaselineOffset(0.0f, -4.0f);
        const ImVec2 hoverABaselineOffset(0.0f, -2.0f);
        UITexture* normal = GetRetroSkillTexture(assets, "mouse.normal");
        UITexture* hoverA = GetRetroSkillTexture(assets, "mouse.normal.1");
        UITexture* hoverB = GetRetroSkillTexture(assets, "mouse.normal.2");
        UITexture* pressed = GetRetroSkillTexture(assets, "mouse.pressed");

        if (!currentTex)
            return normalBaselineOffset;

        if (currentTex && hoverA && currentTex == hoverA)
            return hoverABaselineOffset;

        if (hoverB && currentTex == hoverB)
        {
            if (hoverA && hoverA->width > 0 && hoverA->height > 0 &&
                currentTex->width > 0 && currentTex->height > 0)
            {
                return ImVec2(
                    hoverABaselineOffset.x + (float)(hoverA->width - currentTex->width),
                    hoverABaselineOffset.y + (float)(hoverA->height - currentTex->height));
            }
            return hoverABaselineOffset;
        }

        if (pressed && currentTex == pressed)
        {
            if (normal && normal->width > 0 && normal->height > 0 &&
                currentTex->width > 0 && currentTex->height > 0)
            {
                return ImVec2(
                    normalBaselineOffset.x + (float)(normal->width - currentTex->width) - 3.0f,
                    normalBaselineOffset.y + (float)(normal->height - currentTex->height) - 3.0f);
            }
            return ImVec2(normalBaselineOffset.x - 3.0f, normalBaselineOffset.y - 3.0f);
        }

        return normalBaselineOffset;
    }

    static DefaultBehaviorController g_defaultBehaviorController;

}

enum MouseState { MS_NORMAL, MS_PRESSED, MS_DRAG, MS_HOVER_INSTANT, MS_HOVER_LOOP_A, MS_HOVER_LOOP_B };

namespace
{
    bool IsPressedNativeCursorState(int state)
    {
        return state == 9 || state == 10 || state == 12;
    }

    bool IsHoverNativeCursorState(int state)
    {
        switch (state)
        {
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 11:
        case 14:
        case 15:
            return true;
        default:
            return false;
        }
    }

    bool IsDragNativeCursorState(int state)
    {
        return state == 16;
    }

    int ResolveOverlayNativeCursorState(
        bool wantsHover,
        bool wantsPressed,
        bool wantsDrag,
        int observedState)
    {
        if (wantsDrag)
            return IsDragNativeCursorState(observedState) ? observedState : 16;
        if (wantsPressed)
            return IsPressedNativeCursorState(observedState) ? observedState : 12;
        if (wantsHover)
            return IsHoverNativeCursorState(observedState) ? observedState : 4;
        return observedState;
    }

    bool TryResolveMouseStateFromNativeCursorState(int nativeState, MouseState* outMouseState)
    {
        if (!outMouseState)
            return false;

        switch (nativeState)
        {
        case 0:
            *outMouseState = MS_NORMAL;
            return true;
        case 4:
        case 7:
        case 14:
            *outMouseState = MS_HOVER_LOOP_A;
            return true;
        case 5:
        case 11:
            *outMouseState = MS_HOVER_INSTANT;
            return true;
        case 6:
        case 8:
        case 15:
            *outMouseState = MS_HOVER_LOOP_B;
            return true;
        case 9:
        case 10:
        case 12:
            *outMouseState = MS_PRESSED;
            return true;
        case 16:
            *outMouseState = MS_DRAG;
            return true;
        default:
            return false;
        }
    }
}

void InitializeRetroSkillApp(RetroSkillRuntimeState& state, RetroSkillAssets& assets, const RetroDeviceRef& deviceRef, const char* assetPath)
{
    ResetRetroSkillData(state);
    LoadAllRetroSkillAssets(assets, deviceRef, assetPath);
}

void ShutdownRetroSkillApp(RetroSkillAssets& assets)
{
    CleanupRetroSkillAssets(assets);
}

void ConfigureRetroSkillDefaultBehaviorHooks(RetroSkillBehaviorHooks& hooks, RetroSkillRuntimeState& state)
{
    g_defaultBehaviorController.state = &state;
    hooks.userData = &g_defaultBehaviorController;
    hooks.onTabAction = OnDefaultTabAction;
    hooks.onPlusAction = OnDefaultPlusAction;
    hooks.onInitAction = OnDefaultInitAction;
}

void RenderRetroSkillScene(RetroSkillRuntimeState& state, RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, float mainScale)
{
    RenderRetroSkillSceneEx(state, assets, device, mainScale, nullptr);
}

void RenderRetroSkillCursorOverlay(
    RetroSkillRuntimeState& state,
    RetroSkillAssets& assets,
    float mainScale,
    bool extraHoverAnimation,
    bool extraPressed,
    uint64_t extraHoverStartTick,
    bool extraHoverInstantUseNormal1,
    int observedNativeCursorState)
{
    ImGuiIO& io = ImGui::GetIO();

    if (state.isDraggingSkill && ImGui::IsMouseClicked(0) && !state.dragSkillStartedThisFrame)
    {
        state.isDraggingSkill = false;
        state.dragSkillTab = -1;
        state.dragSkillIndex = -1;
    }

    bool isMouseDown = io.MouseDown[0];
    const bool stateHoverAnimation = state.isHoveringActionButtons && !state.actionHoverStoppedByClick;
    MouseState mouseState = MS_NORMAL;
    const bool wantsDrag = state.isDraggingSkill;
    const bool wantsPressed = isMouseDown && (state.isPressingUiButton || extraPressed);
    const bool wantsHover = stateHoverAnimation || extraHoverAnimation;
    const int effectiveNativeCursorState = ResolveOverlayNativeCursorState(
        wantsHover,
        wantsPressed,
        wantsDrag,
        observedNativeCursorState);

    if (TryResolveMouseStateFromNativeCursorState(effectiveNativeCursorState, &mouseState))
    {
    }
    else if (state.isDraggingSkill)
    {
        mouseState = MS_DRAG;
    }
    else if (wantsPressed)
    {
        mouseState = MS_PRESSED;
    }
    else if (wantsHover)
    {
        double hoverElapsed = 0.0;
        bool hoverInstantUseNormal1 = false;
        if (stateHoverAnimation)
        {
            hoverElapsed = ImGui::GetTime() - state.actionButtonsHoverStartTime;
            hoverInstantUseNormal1 = state.actionHoverInstantUseNormal1;
        }
        else
        {
            const uint64_t nowTick = static_cast<uint64_t>(GetTickCount64());
            const uint64_t hoverStartTick = extraHoverStartTick ? extraHoverStartTick : nowTick;
            hoverElapsed = (double)(nowTick - hoverStartTick) / 1000.0;
            hoverInstantUseNormal1 = extraHoverInstantUseNormal1;
        }
        if (hoverElapsed < 0.5)
            mouseState = hoverInstantUseNormal1 ? MS_HOVER_LOOP_A : MS_HOVER_LOOP_B;
        else
        {
            double loopTime = hoverElapsed - 0.5;
            int loopFrame = (int)floor(loopTime / 0.5);
            mouseState = (loopFrame % 2 == 0) ? MS_HOVER_LOOP_A : MS_HOVER_LOOP_B;
        }
    }

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 mousePos = io.MousePos;

    UITexture* mouseTex = nullptr;
    switch (mouseState)
    {
        case MS_NORMAL: mouseTex = GetRetroSkillTexture(assets, "mouse.normal"); break;
        case MS_PRESSED: mouseTex = GetRetroSkillTexture(assets, "mouse.pressed"); break;
        case MS_DRAG: mouseTex = GetRetroSkillTexture(assets, "mouse.drag"); break;
        case MS_HOVER_INSTANT: mouseTex = GetRetroSkillTexture(assets, "mouse.normal.2"); break;
        case MS_HOVER_LOOP_A: mouseTex = GetRetroSkillTexture(assets, "mouse.normal.1"); break;
        case MS_HOVER_LOOP_B: mouseTex = GetRetroSkillTexture(assets, "mouse.normal.2"); break;
    }

    if ((!mouseTex || !mouseTex->texture) && mouseState == MS_HOVER_INSTANT)
        mouseTex = GetRetroSkillTexture(assets, "mouse.normal");
    if ((!mouseTex || !mouseTex->texture) && mouseState == MS_HOVER_LOOP_A)
        mouseTex = GetRetroSkillTexture(assets, "mouse.normal");

    if (mouseTex && mouseTex->texture)
    {
        const ImVec2 extraOffset = GetRetroCursorFixedOffset(assets, mouseTex);
        ImVec2 cursorMin(
            mousePos.x + extraOffset.x * mainScale,
            mousePos.y + extraOffset.y * mainScale);
        ImVec2 cursorMax(
            cursorMin.x + mouseTex->width * mainScale,
            cursorMin.y + mouseTex->height * mainScale);
        dl->AddImage((ImTextureID)mouseTex->texture, cursorMin, cursorMax);
    }
}

void RenderRetroSkillSceneEx(RetroSkillRuntimeState& state, RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, float mainScale, const RetroSkillBehaviorHooks* hooks)
{
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("Scene", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoBackground);

    RenderRetroSkillPanel(state, assets, device, mainScale, hooks);
    RenderRetroSkillCursorOverlay(state, assets, mainScale);

    ImGui::End();
    ImGui::PopStyleVar(2);
}
