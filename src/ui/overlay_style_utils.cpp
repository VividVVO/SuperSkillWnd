#include "ui/overlay_style_utils.h"

#include "third_party/imgui/imgui.h"

#include <windows.h>
#include <string>

void OverlayConfigureImGuiStyle(float mainScale)
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(mainScale);
    style.FontScaleDpi = 1.0f;
    style.WindowRounding = 0.0f;
    style.FrameRounding = 2.0f * mainScale;
    style.ScrollbarRounding = 2.0f * mainScale;
    style.WindowBorderSize = 0.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_Border] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_Button] = ImVec4(0, 0, 0, 0);
}

void OverlayLoadMainAndConsolasFonts(float mainScale, ImFont** outMainFont, ImFont** outConsolasFont)
{
    if (outMainFont)
        *outMainFont = nullptr;
    if (outConsolasFont)
        *outConsolasFont = nullptr;

    ImGuiIO& io = ImGui::GetIO();
    char winDir[MAX_PATH] = {};
    GetWindowsDirectoryA(winDir, MAX_PATH);
    std::string fontPath = std::string(winDir) + "\\Fonts\\msyh.ttc";

    ImFontConfig fontConfig = {};
    fontConfig.OversampleH = 1;
    fontConfig.OversampleV = 1;
    fontConfig.PixelSnapH = true;

    ImFont* mainFont = io.Fonts->AddFontFromFileTTF(
        fontPath.c_str(),
        14.0f * mainScale,
        &fontConfig,
        io.Fonts->GetGlyphRangesChineseFull());

    if (!mainFont)
        mainFont = io.Fonts->AddFontDefault();

    std::string consolasPath = std::string(winDir) + "\\Fonts\\times.ttf";
    ImFontConfig consolasCfg = {};
    consolasCfg.OversampleH = 1;
    consolasCfg.OversampleV = 1;
    consolasCfg.PixelSnapH = true;
    static const ImWchar digitRanges[] = { 0x20, 0x7E, 0 };
    ImFont* consolasFont = io.Fonts->AddFontFromFileTTF(
        consolasPath.c_str(),
        14.0f * mainScale,
        &consolasCfg,
        digitRanges);

    if (outMainFont)
        *outMainFont = mainFont;
    if (outConsolasFont)
        *outConsolasFont = consolasFont;
}

