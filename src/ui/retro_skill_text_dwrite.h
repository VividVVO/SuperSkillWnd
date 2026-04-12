#pragma once

#include "ui/retro_render_backend.h"
#include "imgui.h"

bool RetroSkillDWriteInitialize(const RetroDeviceRef& deviceRef);
void RetroSkillDWriteShutdown();
void RetroSkillDWriteOnDeviceLost();
void RetroSkillDWriteOnDeviceReset(const RetroDeviceRef& deviceRef);
bool RetroSkillDWriteDrawText(ImDrawList* drawList, const ImVec2& pos, ImU32 color, const char* text, float fontSize);
bool RetroSkillDWriteDrawTextEx(ImDrawList* drawList, const ImVec2& pos, ImU32 color, const char* text, float fontSize, float glyphSpacing);
bool RetroSkillDWriteDrawTextWithStyleHintEx(ImDrawList* drawList, const ImVec2& pos, ImU32 color, const char* styleHintText, const char* text, float fontSize, float glyphSpacing, bool largeText = false);
ImVec2 RetroSkillDWriteMeasureText(const char* text, float fontSize);
ImVec2 RetroSkillDWriteMeasureTextEx(const char* text, float fontSize, float glyphSpacing);
ImVec2 RetroSkillDWriteMeasureTextWithStyleHintEx(const char* styleHintText, const char* text, float fontSize, float glyphSpacing, bool largeText = false);
void RetroSkillDWriteRegisterNativeGlyphLookup(void* trampoline);
void RetroSkillDWriteObserveGlyphLookup(void* fontCache, unsigned int codepoint);
