#include "retro_skill_panel.h"

#include "core/Common.h"
#include "retro_skill_app.h"
#include "retro_skill_text_dwrite.h"
#include "super_imgui_overlay.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const ImU32 kRetroSkillTextColor = IM_COL32(85, 85, 85, 255);
static const ImU32 kRetroPureBlackTextColor = IM_COL32(0, 0, 0, 255);
static const ImU32 kRetroOrangeTextColor = IM_COL32(0xFF, 0x99, 0x00, 255);
static const ImU32 kRetroPureWhiteTextColor = IM_COL32(255, 255, 255, 255);
static const ImU32 kRetroDisabledTextColor = IM_COL32(173, 173, 173, 255);
static const ImU32 kTooltipFillColor = IM_COL32(0x0E, 0x39, 0x5A, 0xCC);
static const ImU32 kTooltipFrameColor = IM_COL32(255, 255, 255, 255);

struct WrappedTooltipLine
{
    std::string text;
    bool justify = false;
};

static float MeasureWrappedTooltipBlockHeight(const std::string& text, float maxWidth, float fontSize, float glyphSpacing, float extraLineGap);

static void DrawOutlinedText(ImDrawList* dl, const ImVec2& pos, ImU32 color, const char* text, float mainScale, float fontSize = 0.0f, float glyphSpacing = 0.0f)
{
    const ImVec2 alignedPos(floorf(pos.x), floorf(pos.y));
    (void)mainScale;

    const float dwriteFontSize = (fontSize > 0.0f) ? fontSize : 0.0f;
    if (RetroSkillDWriteDrawTextEx(dl, alignedPos, color, text, dwriteFontSize, glyphSpacing))
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

static ImVec2 MeasureRetroText(const std::string& text, float fontSize, float glyphSpacing)
{
    if (text.empty())
        return ImVec2(0.0f, 0.0f);

    ImVec2 measured = RetroSkillDWriteMeasureTextEx(text.c_str(), fontSize, glyphSpacing);
    if (measured.x > 0.0f || measured.y > 0.0f)
        return measured;

    if (fontSize > 0.0f)
    {
        ImFont* font = ImGui::GetFont();
        if (font)
            return font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str());
    }

    return ImGui::CalcTextSize(text.c_str());
}

static float ResolveRetroLineHeight(float fontSize, float glyphSpacing)
{
    ImVec2 measured = RetroSkillDWriteMeasureTextEx(u8"技", fontSize, glyphSpacing);
    if (measured.y > 0.0f)
        return measured.y;
    if (fontSize > 0.0f)
        return fontSize;
    return ImGui::GetFontSize();
}

static size_t Utf8CodepointLength(unsigned char leadByte)
{
    if ((leadByte & 0x80u) == 0u)
        return 1;
    if ((leadByte & 0xE0u) == 0xC0u)
        return 2;
    if ((leadByte & 0xF0u) == 0xE0u)
        return 3;
    if ((leadByte & 0xF8u) == 0xF0u)
        return 4;
    return 1;
}

static void SplitUtf8Codepoints(const std::string& text, std::vector<std::string>& outCodepoints)
{
    outCodepoints.clear();
    for (size_t i = 0; i < text.size();)
    {
        const size_t codepointLength = Utf8CodepointLength(static_cast<unsigned char>(text[i]));
        const size_t safeLength = (i + codepointLength <= text.size()) ? codepointLength : 1;
        outCodepoints.push_back(text.substr(i, safeLength));
        i += safeLength;
    }
}

static std::string ConvertLiteralNewlines(const std::string& text)
{
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'n')
        {
            result.push_back('\n');
            ++i;
        }
        else
        {
            result.push_back(text[i]);
        }
    }
    return result;
}

struct ColorTextSegment
{
    std::string text;
    ImU32 color;
};

static void ParseColorSegments(const std::string& text, ImU32 defaultColor, std::vector<ColorTextSegment>& outSegments)
{
    outSegments.clear();
    size_t i = 0;
    std::string current;
    while (i < text.size())
    {
        if (text[i] == '#')
        {
            size_t closePos = text.find('#', i + 1);
            if (closePos != std::string::npos && closePos > i + 1)
            {
                unsigned char nextChar = (i + 1 < text.size()) ? static_cast<unsigned char>(text[i + 1]) : 0;
                bool isPlaceholder = (nextChar >= 'a' && nextChar <= 'z') || (nextChar >= 'A' && nextChar <= 'Z') || nextChar == '_';
                bool containsNonAscii = false;
                for (size_t j = i + 1; j < closePos; ++j)
                {
                    if (static_cast<unsigned char>(text[j]) >= 0x80)
                    {
                        containsNonAscii = true;
                        break;
                    }
                }
                if (!isPlaceholder || containsNonAscii)
                {
                    if (!current.empty())
                    {
                        ColorTextSegment seg;
                        seg.text = current;
                        seg.color = defaultColor;
                        outSegments.push_back(seg);
                        current.clear();
                    }
                    ColorTextSegment seg;
                    seg.text = text.substr(i + 1, closePos - i - 1);
                    seg.color = kRetroOrangeTextColor;
                    outSegments.push_back(seg);
                    i = closePos + 1;
                    continue;
                }
            }
        }
        current.push_back(text[i]);
        ++i;
    }
    if (!current.empty())
    {
        ColorTextSegment seg;
        seg.text = current;
        seg.color = defaultColor;
        outSegments.push_back(seg);
    }
}

static std::string StripColorMarkers(const std::string& text)
{
    std::string result;
    result.reserve(text.size());
    size_t i = 0;
    while (i < text.size())
    {
        if (text[i] == '#')
        {
            size_t closePos = text.find('#', i + 1);
            if (closePos != std::string::npos && closePos > i + 1)
            {
                unsigned char nextChar = static_cast<unsigned char>(text[i + 1]);
                bool isPlaceholder = (nextChar >= 'a' && nextChar <= 'z') || (nextChar >= 'A' && nextChar <= 'Z') || nextChar == '_';
                bool containsNonAscii = false;
                for (size_t j = i + 1; j < closePos; ++j)
                {
                    if (static_cast<unsigned char>(text[j]) >= 0x80)
                    {
                        containsNonAscii = true;
                        break;
                    }
                }
                if (!isPlaceholder || containsNonAscii)
                {
                    result.append(text, i + 1, closePos - i - 1);
                    i = closePos + 1;
                    continue;
                }
            }
        }
        result.push_back(text[i]);
        ++i;
    }
    return result;
}

static std::string StripMaxLevelPrefix(const std::string& text)
{
    if (text.empty())
        return text;

    size_t newlinePos = text.find('\n');
    if (newlinePos == std::string::npos)
        return text;

    const std::string firstLine = text.substr(0, newlinePos);
    if (firstLine.find(u8"最高等级") == std::string::npos)
        return text;

    size_t bodyPos = newlinePos + 1;
    while (bodyPos < text.size() && (text[bodyPos] == '\r' || text[bodyPos] == '\n'))
        ++bodyPos;
    return text.substr(bodyPos);
}

static std::string ResolveTooltipDescriptionText(const SkillEntry& skill)
{
    std::string description = StripMaxLevelPrefix(ConvertLiteralNewlines(skill.tooltipDescription));
    if (!description.empty())
        return description;
    if (!skill.tooltipPreview.empty())
        return ConvertLiteralNewlines(skill.tooltipPreview);
    return ConvertLiteralNewlines(skill.tooltipDetail);
}

static std::string ResolveTooltipInfoText(const SkillEntry& skill)
{
    if (!skill.tooltipDetail.empty())
        return ConvertLiteralNewlines(skill.tooltipDetail);
    if (!skill.tooltipPreview.empty())
        return ConvertLiteralNewlines(skill.tooltipPreview);
    return StripMaxLevelPrefix(ConvertLiteralNewlines(skill.tooltipDescription));
}

static bool ContainsUnresolvedTooltipToken(const std::string& text)
{
    for (size_t i = 0; i + 1 < text.size(); ++i)
    {
        if (text[i] != '#')
            continue;

        const unsigned char next = static_cast<unsigned char>(text[i + 1]);
        if ((next >= 'A' && next <= 'Z') ||
            (next >= 'a' && next <= 'z') ||
            next == '_')
        {
            return true;
        }
    }

    return false;
}

static void LogTooltipDrawState(
    const SkillEntry& skill,
    const ImVec2& mousePos,
    const ImVec2& tooltipMin,
    const ImVec2& tooltipSize,
    const std::string& currentInfoText,
    const std::string& nextInfoText)
{
    static DWORD s_lastLogTick = 0;
    static int s_lastSkillId = 0;

    const DWORD now = GetTickCount();
    const bool currentUnresolved = ContainsUnresolvedTooltipToken(currentInfoText);
    const bool nextUnresolved = ContainsUnresolvedTooltipToken(nextInfoText);
    const bool shouldLog = currentUnresolved ||
                           nextUnresolved ||
                           s_lastSkillId != skill.skillId ||
                           (now - s_lastLogTick) >= 1000;

    if (!shouldLog)
        return;

    s_lastLogTick = now;
    s_lastSkillId = skill.skillId;
    WriteLogFmt(
        "[TooltipDraw] skill=%d level=%d/%d mouse=(%d,%d) box=(%d,%d,%d,%d) currentLen=%d nextLen=%d unresolvedCurrent=%d unresolvedNext=%d",
        skill.skillId,
        skill.level,
        skill.maxLevel,
        static_cast<int>(floorf(mousePos.x)),
        static_cast<int>(floorf(mousePos.y)),
        static_cast<int>(floorf(tooltipMin.x)),
        static_cast<int>(floorf(tooltipMin.y)),
        static_cast<int>(floorf(tooltipSize.x)),
        static_cast<int>(floorf(tooltipSize.y)),
        static_cast<int>(currentInfoText.size()),
        static_cast<int>(nextInfoText.size()),
        currentUnresolved ? 1 : 0,
        nextUnresolved ? 1 : 0);
}

static void AppendWrappedParagraph(const std::string& paragraph, float maxWidth, float fontSize, float glyphSpacing, std::vector<WrappedTooltipLine>& outLines)
{
    if (paragraph.empty())
    {
        outLines.push_back(WrappedTooltipLine());
        return;
    }

    std::vector<std::string> codepoints;
    SplitUtf8Codepoints(paragraph, codepoints);
    if (codepoints.empty())
    {
        outLines.push_back(WrappedTooltipLine());
        return;
    }

    const size_t lineStartIndex = outLines.size();
    std::string currentLine;
    for (size_t i = 0; i < codepoints.size(); ++i)
    {
        const std::string candidate = currentLine + codepoints[i];
        const ImVec2 candidateSize = MeasureRetroText(candidate, fontSize, glyphSpacing);
        if (currentLine.empty() || candidateSize.x <= maxWidth + 0.5f)
        {
            currentLine = candidate;
            continue;
        }

        WrappedTooltipLine line;
        line.text = currentLine;
        line.justify = true;
        outLines.push_back(line);
        currentLine = codepoints[i];
    }

    if (!currentLine.empty())
    {
        WrappedTooltipLine line;
        line.text = currentLine;
        line.justify = false;
        outLines.push_back(line);
    }

    if (outLines.size() > lineStartIndex)
        outLines.back().justify = false;
}

static void BuildWrappedTooltipLines(const std::string& text, float maxWidth, float fontSize, float glyphSpacing, std::vector<WrappedTooltipLine>& outLines)
{
    outLines.clear();

    std::string cleanText = StripColorMarkers(text);
    size_t start = 0;
    while (start <= cleanText.size())
    {
        size_t newlinePos = cleanText.find('\n', start);
        const std::string rawLine = (newlinePos == std::string::npos)
            ? cleanText.substr(start)
            : cleanText.substr(start, newlinePos - start);
        const std::string paragraph = (!rawLine.empty() && rawLine[rawLine.size() - 1] == '\r')
            ? rawLine.substr(0, rawLine.size() - 1)
            : rawLine;

        AppendWrappedParagraph(paragraph, maxWidth, fontSize, glyphSpacing, outLines);

        if (newlinePos == std::string::npos)
            break;

        start = newlinePos + 1;
    }
}

static void AddTooltipPixel(ImDrawList* drawList, float x, float y, ImU32 color)
{
    if (!drawList)
        return;

    const float px = floorf(x);
    const float py = floorf(y);
    drawList->AddRectFilled(ImVec2(px, py), ImVec2(px + 1.0f, py + 1.0f), color);
}

static void DrawTooltipBorder(ImDrawList* drawList, const ImVec2& minPos, const ImVec2& maxPos)
{
    if (!drawList)
        return;

    // Match the native Maple tooltip frame:
    // - outer 1px outline in tooltip background color, without the 4 corner pixels
    // - inner 1px ring stays transparent
    // - the 4 inner-corner pixels are filled with the tooltip background
    const float left = floorf(minPos.x);
    const float top = floorf(minPos.y);
    const float right = floorf(maxPos.x) - 1.0f;
    const float bottom = floorf(maxPos.y) - 1.0f;

    if (right - left < 4.0f || bottom - top < 4.0f)
        return;

    // Fill the core area, leaving a transparent 1px inset ring.
    drawList->AddRectFilled(
        ImVec2(left + 2.0f, top + 2.0f),
        ImVec2(right - 1.0f, bottom - 1.0f),
        kTooltipFillColor);

    // Restore the 4 inner-corner pixels with background color.
    AddTooltipPixel(drawList, left + 1.0f, top + 1.0f, kTooltipFillColor);
    AddTooltipPixel(drawList, right - 1.0f, top + 1.0f, kTooltipFillColor);
    AddTooltipPixel(drawList, left + 1.0f, bottom - 1.0f, kTooltipFillColor);
    AddTooltipPixel(drawList, right - 1.0f, bottom - 1.0f, kTooltipFillColor);

    // Draw the outer 1px border while keeping the 4 corner pixels transparent.
    drawList->AddRectFilled(
        ImVec2(left + 1.0f, top),
        ImVec2(right, top + 1.0f),
        kTooltipFillColor);
    drawList->AddRectFilled(
        ImVec2(left + 1.0f, bottom),
        ImVec2(right, bottom + 1.0f),
        kTooltipFillColor);
    drawList->AddRectFilled(
        ImVec2(left, top + 1.0f),
        ImVec2(left + 1.0f, bottom),
        kTooltipFillColor);
    drawList->AddRectFilled(
        ImVec2(right, top + 1.0f),
        ImVec2(right + 1.0f, bottom),
        kTooltipFillColor);
}

static void DrawJustifiedTooltipLine(
    ImDrawList* drawList,
    const ImVec2& pos,
    const std::string& text,
    float maxWidth,
    ImU32 color,
    float mainScale,
    float fontSize,
    float glyphSpacing,
    bool justify)
{
    if (text.empty())
        return;

    std::string plainText = StripColorMarkers(text);
    std::vector<std::string> codepoints;
    SplitUtf8Codepoints(plainText, codepoints);

    if (!justify || codepoints.size() < 2)
    {
        std::vector<ColorTextSegment> segments;
        ParseColorSegments(text, color, segments);
        float cursorX = pos.x;
        for (size_t i = 0; i < segments.size(); ++i)
        {
            DrawOutlinedText(drawList, ImVec2(cursorX, pos.y), segments[i].color, segments[i].text.c_str(), mainScale, fontSize, glyphSpacing);
            cursorX += MeasureRetroText(segments[i].text, fontSize, glyphSpacing).x;
        }
        return;
    }

    std::vector<ColorTextSegment> segments;
    ParseColorSegments(text, color, segments);

    std::vector<ImU32> codepointColors;
    codepointColors.reserve(codepoints.size());
    {
        size_t cpIndex = 0;
        for (size_t s = 0; s < segments.size() && cpIndex < codepoints.size(); ++s)
        {
            std::vector<std::string> segCodepoints;
            SplitUtf8Codepoints(segments[s].text, segCodepoints);
            for (size_t j = 0; j < segCodepoints.size() && cpIndex < codepoints.size(); ++j, ++cpIndex)
                codepointColors.push_back(segments[s].color);
        }
        while (codepointColors.size() < codepoints.size())
            codepointColors.push_back(color);
    }

    std::vector<float> codepointWidths;
    codepointWidths.reserve(codepoints.size());

    float lineWidth = 0.0f;
    for (size_t i = 0; i < codepoints.size(); ++i)
    {
        const ImVec2 measured = MeasureRetroText(codepoints[i], fontSize, 0.0f);
        codepointWidths.push_back(measured.x);
        lineWidth += measured.x;
    }

    lineWidth += (float)(codepoints.size() - 1) * glyphSpacing;
    const float extraWidth = (maxWidth > lineWidth) ? (maxWidth - lineWidth) : 0.0f;
    const float extraGap = extraWidth / (float)(codepoints.size() - 1);

    float cursorX = pos.x;
    for (size_t i = 0; i < codepoints.size(); ++i)
    {
        DrawOutlinedText(drawList, ImVec2(cursorX, pos.y), codepointColors[i], codepoints[i].c_str(), mainScale, fontSize, 0.0f);
        cursorX += codepointWidths[i];
        if (i + 1 < codepoints.size())
            cursorX += glyphSpacing + extraGap;
    }
}

static float DrawWrappedTooltipBlock(
    ImDrawList* drawList,
    const ImVec2& pos,
    float maxWidth,
    const std::string& text,
    ImU32 color,
    float mainScale,
    float fontSize,
    float glyphSpacing,
    float extraLineGap)
{
    std::vector<WrappedTooltipLine> lines;
    BuildWrappedTooltipLines(text, maxWidth, fontSize, glyphSpacing, lines);
    const float lineAdvance = ResolveRetroLineHeight(fontSize, glyphSpacing) + extraLineGap;

    float cursorY = pos.y;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (!lines[i].text.empty())
            DrawJustifiedTooltipLine(drawList, ImVec2(pos.x, cursorY), lines[i].text, maxWidth, color, mainScale, fontSize, glyphSpacing, lines[i].justify);
        cursorY += lineAdvance;
    }

    return lines.empty() ? 0.0f : (cursorY - pos.y);
}

static void DrawBoldTooltipTitle(ImDrawList* drawList, const ImVec2& pos, const char* text, float mainScale, float fontSize)
{
    if (!text || !text[0])
        return;

    DrawOutlinedText(drawList, pos, kRetroPureWhiteTextColor, text, mainScale, fontSize, 1.0f * mainScale);
    DrawOutlinedText(drawList, ImVec2(pos.x + 1.0f * mainScale, pos.y), kRetroPureWhiteTextColor, text, mainScale, fontSize, 1.0f * mainScale);
}

static void DrawTooltipMaxLevelLabel(ImDrawList* drawList, const ImVec2& pos, int maxLevel, float mainScale, float fontSize)
{
    if (!drawList)
        return;

    const float prefixSpacing = 1.0f * mainScale;
    const std::string prefix = u8"[最高等级：";

    char numberText[16] = {};
    sprintf_s(numberText, "%d", maxLevel);

    const std::string numberString = numberText;
    const std::string suffix = "]";

    DrawOutlinedText(drawList, pos, kRetroPureWhiteTextColor, prefix.c_str(), mainScale, fontSize, prefixSpacing);

    float cursorX = pos.x + MeasureRetroText(prefix, fontSize, prefixSpacing).x;
    cursorX += 8.0f * mainScale;
    const float numberSpacing = 1.0f * mainScale;
    DrawOutlinedText(drawList, ImVec2(floorf(cursorX), pos.y), kRetroPureWhiteTextColor, numberString.c_str(), mainScale, fontSize, numberSpacing);

    cursorX += MeasureRetroText(numberString, fontSize, numberSpacing).x;
    cursorX += ((numberString.size() <= 1) ? 3.0f : 2.0f) * mainScale;

    DrawOutlinedText(drawList, ImVec2(floorf(cursorX), pos.y), kRetroPureWhiteTextColor, suffix.c_str(), mainScale, fontSize, 0.0f);
}

static void RenderSkillTooltipCard(const SkillEntry& skill, RetroSkillAssets& assets, float mainScale, const ImVec2& hoveredMin, const ImVec2& hoveredMax, const ImVec2& panelPos, float panelWidth)
{
    (void)hoveredMin;
    (void)hoveredMax;
    (void)panelPos;
    (void)panelWidth;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (!drawList)
        return;

    const float tooltipWidth = floorf(290.0f * mainScale);
    const float titleFontSize = floorf(14.0f * mainScale);
    const float bodyFontSize = floorf(12.0f * mainScale);
    const float glyphSpacing = 1.0f * mainScale;
    const float smallGap = 2.0f * mainScale;
    const float sectionGap = 4.0f * mainScale;

    const std::string descriptionText = ResolveTooltipDescriptionText(skill);
    const std::string fallbackInfoText = ResolveTooltipInfoText(skill);
    const std::string currentInfoText = !skill.tooltipCurrentDetail.empty() ? ConvertLiteralNewlines(skill.tooltipCurrentDetail) : fallbackInfoText;
    const std::string nextInfoText = !skill.tooltipNextDetail.empty() ? ConvertLiteralNewlines(skill.tooltipNextDetail) : fallbackInfoText;

    float info2Height = 0.0f;
    const float descriptionWidth = floorf((271.0f - 95.0f) * mainScale);
    const float info2Width = floorf((278.0f - 10.0f) * mainScale);
    const float labelLineHeight = ResolveRetroLineHeight(bodyFontSize, glyphSpacing);
    const float descriptionHeight = MeasureWrappedTooltipBlockHeight(
        descriptionText,
        descriptionWidth,
        bodyFontSize,
        glyphSpacing,
        1.0f * mainScale);
    const float iconBottomOffsetY = floorf((34.0f + 68.0f) * mainScale);
    const float descriptionBottomOffsetY = floorf((37.0f * mainScale) + descriptionHeight);
    const float topSectionBottomOffsetY = (iconBottomOffsetY > descriptionBottomOffsetY)
        ? iconBottomOffsetY
        : descriptionBottomOffsetY;
    const float dividerOffsetY = floorf(topSectionBottomOffsetY + (14.0f * mainScale));
    const float infoStartOffsetY = floorf(dividerOffsetY + (9.0f * mainScale));

    auto measureInfoSection = [&](const std::string& sectionText) {
        float height = labelLineHeight;
        if (!sectionText.empty())
            height += smallGap + MeasureWrappedTooltipBlockHeight(sectionText, info2Width, bodyFontSize, glyphSpacing, 1.0f * mainScale);
        return height;
    };

    if (skill.level <= 0)
    {
        info2Height = measureInfoSection(nextInfoText);
    }
    else if (skill.level < skill.maxLevel)
    {
        info2Height = measureInfoSection(currentInfoText) + sectionGap + measureInfoSection(nextInfoText);
    }
    else
    {
        info2Height = measureInfoSection(currentInfoText);
    }

    const float tooltipHeight = floorf(infoStartOffsetY + info2Height + (10.0f * mainScale));
    const ImVec2 tooltipSize(tooltipWidth, tooltipHeight);

    ImGuiIO& io = ImGui::GetIO();
    const float followOffsetX = 18.0f * mainScale;
    const float followOffsetY = 18.0f * mainScale;
    float tooltipX = floorf(io.MousePos.x + followOffsetX);
    if (tooltipX + tooltipSize.x > io.DisplaySize.x - 4.0f)
        tooltipX = floorf(io.MousePos.x - tooltipSize.x - followOffsetX);
    if (tooltipX < 4.0f)
        tooltipX = 4.0f;
    if (tooltipX + tooltipSize.x > io.DisplaySize.x - 4.0f)
        tooltipX = floorf(io.DisplaySize.x - tooltipSize.x - 4.0f);

    float tooltipY = floorf(io.MousePos.y + followOffsetY);
    if (tooltipY + tooltipSize.y > io.DisplaySize.y - 4.0f)
        tooltipY = floorf(io.MousePos.y - tooltipSize.y - followOffsetY);
    if (tooltipY < 4.0f)
        tooltipY = 4.0f;
    if (tooltipY + tooltipSize.y > io.DisplaySize.y - 4.0f)
        tooltipY = floorf(io.DisplaySize.y - tooltipSize.y - 4.0f);

    const ImVec2 tooltipMin(tooltipX, tooltipY);
    const ImVec2 tooltipMax(tooltipX + tooltipSize.x, tooltipY + tooltipSize.y);

    LogTooltipDrawState(skill, io.MousePos, tooltipMin, tooltipSize, currentInfoText, nextInfoText);
    DrawTooltipBorder(drawList, tooltipMin, tooltipMax);

    const ImVec2 titleSize = MeasureRetroText(skill.name, titleFontSize, glyphSpacing);
    const ImVec2 titlePos(
        floorf(tooltipMin.x + (tooltipWidth - titleSize.x) * 0.5f),
        floorf(tooltipMin.y + 10.0f * mainScale));
    DrawBoldTooltipTitle(drawList, titlePos, skill.name.c_str(), mainScale, titleFontSize);

    UITexture* iconTex = nullptr;
    if (skill.showDisabledIcon)
        iconTex = GetRetroSkillSkillIconDisabledTexture(assets, skill.iconId);
    if ((!iconTex || !iconTex->texture) && skill.iconId > 0)
        iconTex = GetRetroSkillSkillIconTexture(assets, skill.iconId);

    const ImVec2 iconMin(floorf(tooltipMin.x + 15.0f * mainScale), floorf(tooltipMin.y + 34.0f * mainScale));
    const ImVec2 iconMax(iconMin.x + 68.0f * mainScale, iconMin.y + 68.0f * mainScale);
    if (iconTex && iconTex->texture)
    {
        drawList->AddImage((ImTextureID)iconTex->texture, iconMin, iconMax);
    }
    else
    {
        drawList->AddRectFilled(iconMin, iconMax, skill.iconColor);
    }

    DrawWrappedTooltipBlock(
        drawList,
        ImVec2(floorf(tooltipMin.x + 95.0f * mainScale), floorf(tooltipMin.y + 37.0f * mainScale)),
        descriptionWidth,
        descriptionText,
        kRetroPureWhiteTextColor,
        mainScale,
        bodyFontSize,
        glyphSpacing,
        1.0f * mainScale);

    const float dividerY = floorf(tooltipMin.y + dividerOffsetY);
    drawList->AddLine(
        ImVec2(floorf(tooltipMin.x + 7.0f * mainScale), dividerY),
        ImVec2(floorf(tooltipMin.x + 285.0f * mainScale), dividerY),
        kRetroPureWhiteTextColor,
        1.0f);

    float infoCursorY = floorf(tooltipMin.y + infoStartOffsetY);
    auto drawInfoSection = [&](const char* label, const std::string& sectionText) {
        DrawOutlinedText(
            drawList,
            ImVec2(floorf(tooltipMin.x + 13.0f * mainScale), floorf(infoCursorY)),
            kRetroPureWhiteTextColor,
            label,
            mainScale,
            bodyFontSize,
            glyphSpacing);

        infoCursorY += labelLineHeight;
        if (!sectionText.empty())
        {
            infoCursorY += smallGap;
            infoCursorY += DrawWrappedTooltipBlock(
                drawList,
                ImVec2(floorf(tooltipMin.x + 10.0f * mainScale), floorf(infoCursorY)),
                info2Width,
                sectionText,
                kRetroPureWhiteTextColor,
                mainScale,
                bodyFontSize,
                glyphSpacing,
                1.0f * mainScale);
        }
    };

    if (skill.level <= 0)
    {
        char nextLevelLabel[64] = {};
        sprintf_s(nextLevelLabel, u8"[下次等级 %d]", 1);
        drawInfoSection(nextLevelLabel, nextInfoText);
    }
    else if (skill.level < skill.maxLevel)
    {
        char currentLevelLabel[64] = {};
        char nextLevelLabel[64] = {};
        sprintf_s(currentLevelLabel, u8"[现在等级 %d]", skill.level);
        sprintf_s(nextLevelLabel, u8"[下次等级 %d]", skill.level + 1);
        drawInfoSection(currentLevelLabel, currentInfoText);
        infoCursorY += sectionGap;
        drawInfoSection(nextLevelLabel, nextInfoText);
    }
    else
    {
        char currentLevelLabel[64] = {};
        sprintf_s(currentLevelLabel, u8"[现在等级 %d] ", skill.level);
        drawInfoSection(currentLevelLabel, currentInfoText);
    }
}

static void RenderSkillTooltip(const SkillEntry& skill, RetroSkillAssets& assets, float mainScale, const ImVec2& hoveredMin, const ImVec2& hoveredMax, const ImVec2& panelPos, float panelWidth)
{
    RenderSkillTooltipCard(skill, assets, mainScale, hoveredMin, hoveredMax, panelPos, panelWidth);
}

static float MeasureWrappedTooltipBlockHeight(const std::string& text, float maxWidth, float fontSize, float glyphSpacing, float extraLineGap)
{
    std::vector<WrappedTooltipLine> lines;
    BuildWrappedTooltipLines(text, maxWidth, fontSize, glyphSpacing, lines);
    if (lines.empty())
        return 0.0f;

    return (ResolveRetroLineHeight(fontSize, glyphSpacing) + extraLineGap) * (float)lines.size();
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

        const char* skillModeLabel = (state.activeTab == 0)
            ? u8"技能强化(被动)"
            : u8"攻击/增益(主动)";
        const float panelLabelFontSize = floorf(12.0f * mainScale);
        const float panelLabelGlyphSpacing = 1.0f * mainScale;
        const float panelLabelCenterX = floorf(panelPos.x + 104.0f * mainScale);
        const ImVec2 panelLabelSize = MeasureRetroText(skillModeLabel, panelLabelFontSize, panelLabelGlyphSpacing);
        DrawOutlinedText(
            dl,
            ImVec2(floorf(panelLabelCenterX - panelLabelSize.x * 0.5f), floorf(panelPos.y + 65.0f * mainScale)),
            kRetroPureWhiteTextColor,
            skillModeLabel,
            mainScale,
            panelLabelFontSize,
            panelLabelGlyphSpacing);

        if (state.hasSuperSkillData)
        {
            char superSpValue[32] = {};
            sprintf_s(superSpValue, "%d", state.superSkillPoints);

            const float superSpFontSizePx = 13.5f;
            const float superSpRightX = panelPos.x + 160.0f * mainScale;
            const float superSpCenterY = panelPos.y + (258.0f + 4.0f) * mainScale;
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
                kRetroPureBlackTextColor,
                superSpValue,
                superSpFontSizePx);

            if (font)
            {
                float charX = superSpPos.x;
                for (const char* p = superSpValue; *p; ++p)
                {
                    const ImVec2 charSize = font->CalcTextSizeA(superSpFontSizePx, FLT_MAX, 0.0f, p, p + 1);
                    if (*p == '1')
                    {
                        const float footY = floorf(superSpPos.y + charSize.y - 1.0f);
                        const float footLeft = floorf(charX);
                        const float footRight = floorf(charX + charSize.x);
                        dl->AddLine(ImVec2(footLeft, footY), ImVec2(footRight, footY), kRetroPureBlackTextColor, 1.0f);
                    }
                    charX += charSize.x;
                }
            }
        }

        auto& skills = (state.activeTab == 0) ? state.passiveSkills : state.activeSkills;
        const SkillEntry* hoveredSkill = nullptr;
        bool hasHoveredSkillRect = false;
        ImVec2 hoveredSkillMin(0.0f, 0.0f);
        ImVec2 hoveredSkillMax(0.0f, 0.0f);
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
            bool isSkillDisabled = skill.showDisabledIcon || (!skill.isLearned && !skill.canUpgrade);

            if (isRowHovered)
            {
                hoveredSkill = &skill;
                hoveredSkillMin = skillBoxPos;
                hoveredSkillMax = ImVec2(skillBoxPos.x + 140.0f * mainScale, skillBoxPos.y + skillBoxHeight);
                hasHoveredSkillRect = true;
            }

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
            const ImU32 primaryTextColor = isSkillDisabled ? kRetroDisabledTextColor : kRetroSkillTextColor;
            const ImU32 bonusTextColor = isSkillDisabled ? kRetroDisabledTextColor : IM_COL32(33, 153, 33, 255);
            const float skillNameFontSize = floorf(12.0f * mainScale);
            DrawOutlinedText(dl, textPos, primaryTextColor, skill.name.c_str(), mainScale, skillNameFontSize, 1.0f * mainScale);

            char levelText[32];
            sprintf_s(levelText, "%d", skill.level);
            DrawOutlinedText(dl, ImVec2(floorf(textPos.x), floorf(textPos.y + 17.0f * mainScale)), primaryTextColor, levelText, mainScale, floorf(12.0f * mainScale));

            if (skill.bonusLevel > 0)
            {
                char bonusText[32];
                sprintf_s(bonusText, "(+%d)", skill.bonusLevel);
                DrawOutlinedText(
                    dl,
                    ImVec2(floorf(textPos.x + 17.0f * mainScale), floorf(textPos.y + 17.0f * mainScale)),
                    bonusTextColor,
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

            const bool plusEffectiveDisabled = !skill.canUpgrade || state.superSkillPoints <= 0;

            UITexture* plusTex = nullptr;
            if (plusEffectiveDisabled)
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

            if (plusReleasedWithClick && !plusEffectiveDisabled)
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

        if (hoveredSkill && hasHoveredSkillRect && !state.isDraggingSkill)
            RenderSkillTooltip(*hoveredSkill, assets, mainScale, hoveredSkillMin, hoveredSkillMax, panelPos, m.width);
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
}
