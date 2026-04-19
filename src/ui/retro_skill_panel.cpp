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
static const ImU32 kRetroPureYellowTextColor = IM_COL32(255, 255, 0, 255);
static const ImU32 kRetroDisabledTextColor = IM_COL32(173, 173, 173, 255);
static const ImU32 kTooltipFillColor = IM_COL32(0x0E, 0x39, 0x5A, 0xCC);
static const ImU32 kTooltipFrameColor = IM_COL32(255, 255, 255, 255);
static const ImU32 kTooltipIconBackplateColor = IM_COL32(0xBD, 0xCD, 0xDD, 255);

struct WrappedTooltipLine
{
    std::string text;
    std::string plainText;
    std::vector<std::string> codepoints;
    std::vector<ImU32> codepointColors;
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

static void DrawOutlinedTextWithStyleHint(
    ImDrawList* dl,
    const ImVec2& pos,
    ImU32 color,
    const char* styleHintText,
    const char* text,
    float mainScale,
    float fontSize = 0.0f,
    float glyphSpacing = 0.0f,
    bool largeText = false)
{
    const ImVec2 alignedPos(floorf(pos.x), floorf(pos.y));
    (void)mainScale;

    const float dwriteFontSize = (fontSize > 0.0f) ? fontSize : 0.0f;
    if (RetroSkillDWriteDrawTextWithStyleHintEx(dl, alignedPos, color, styleHintText, text, dwriteFontSize, glyphSpacing, largeText))
        return;

    DrawOutlinedText(dl, alignedPos, color, text, mainScale, fontSize, glyphSpacing);
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

static ImVec2 MeasureRetroTextWithStyleHint(const std::string& styleHintText, const std::string& text, float fontSize, float glyphSpacing, bool largeText = false)
{
    if (text.empty())
        return ImVec2(0.0f, 0.0f);

    ImVec2 measured = RetroSkillDWriteMeasureTextWithStyleHintEx(
        styleHintText.empty() ? nullptr : styleHintText.c_str(),
        text.c_str(),
        fontSize,
        glyphSpacing,
        largeText);
    if (measured.x > 0.0f || measured.y > 0.0f)
        return measured;

    return MeasureRetroText(text, fontSize, glyphSpacing);
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

enum TooltipSpacingClass
{
    TooltipSpacing_Unknown = 0,
    TooltipSpacing_Whitespace,
    TooltipSpacing_Latin,
    TooltipSpacing_Digit,
    TooltipSpacing_Cjk,
    TooltipSpacing_FullWidthComma,
    TooltipSpacing_FullWidthPeriod,
    TooltipSpacing_Bracket,
    TooltipSpacing_OtherPunctuation,
};

static unsigned int DecodeUtf8CodepointValue(const std::string& text)
{
    if (text.empty())
        return 0u;

    const unsigned char lead = static_cast<unsigned char>(text[0]);
    if ((lead & 0x80u) == 0u)
        return static_cast<unsigned int>(lead);

    if ((lead & 0xE0u) == 0xC0u && text.size() >= 2)
    {
        return ((unsigned int)(lead & 0x1Fu) << 6) |
            (unsigned int)(static_cast<unsigned char>(text[1]) & 0x3Fu);
    }

    if ((lead & 0xF0u) == 0xE0u && text.size() >= 3)
    {
        return ((unsigned int)(lead & 0x0Fu) << 12) |
            ((unsigned int)(static_cast<unsigned char>(text[1]) & 0x3Fu) << 6) |
            (unsigned int)(static_cast<unsigned char>(text[2]) & 0x3Fu);
    }

    if ((lead & 0xF8u) == 0xF0u && text.size() >= 4)
    {
        return ((unsigned int)(lead & 0x07u) << 18) |
            ((unsigned int)(static_cast<unsigned char>(text[1]) & 0x3Fu) << 12) |
            ((unsigned int)(static_cast<unsigned char>(text[2]) & 0x3Fu) << 6) |
            (unsigned int)(static_cast<unsigned char>(text[3]) & 0x3Fu);
    }

    return static_cast<unsigned int>(lead);
}

static bool IsTooltipCjkCodepoint(unsigned int codepoint)
{
    return (codepoint >= 0x3400u && codepoint <= 0x4DBFu) ||
        (codepoint >= 0x4E00u && codepoint <= 0x9FFFu) ||
        (codepoint >= 0xF900u && codepoint <= 0xFAFFu);
}

static bool IsTooltipAsciiPunctuation(unsigned int codepoint)
{
    return (codepoint >= 0x21u && codepoint <= 0x2Fu) ||
        (codepoint >= 0x3Au && codepoint <= 0x40u) ||
        (codepoint >= 0x5Bu && codepoint <= 0x60u) ||
        (codepoint >= 0x7Bu && codepoint <= 0x7Eu);
}

static TooltipSpacingClass ClassifyTooltipCodepoint(unsigned int codepoint)
{
    if (codepoint == 0u)
        return TooltipSpacing_Unknown;

    if (codepoint == 0x20u || codepoint == 0x09u)
        return TooltipSpacing_Whitespace;

    if ((codepoint >= (unsigned int)'a' && codepoint <= (unsigned int)'z') ||
        (codepoint >= (unsigned int)'A' && codepoint <= (unsigned int)'Z'))
    {
        return TooltipSpacing_Latin;
    }

    if (codepoint >= (unsigned int)'0' && codepoint <= (unsigned int)'9')
        return TooltipSpacing_Digit;

    if (codepoint == (unsigned int)'[' || codepoint == (unsigned int)']')
        return TooltipSpacing_Bracket;

    if (codepoint == 0xFF0Cu || codepoint == 0x3001u || codepoint == (unsigned int)',')
        return TooltipSpacing_FullWidthComma;

    if (codepoint == 0xFF1Au || codepoint == (unsigned int)':' || codepoint == (unsigned int)';')
        return TooltipSpacing_FullWidthComma;

    if (codepoint == 0x3002u || codepoint == 0xFF0Eu || codepoint == (unsigned int)'.')
        return TooltipSpacing_FullWidthPeriod;

    if (IsTooltipCjkCodepoint(codepoint))
        return TooltipSpacing_Cjk;

    if ((codepoint >= 0x3000u && codepoint <= 0x303Fu) ||
        (codepoint >= 0xFF01u && codepoint <= 0xFF65u) ||
        IsTooltipAsciiPunctuation(codepoint))
    {
        return TooltipSpacing_OtherPunctuation;
    }

    return TooltipSpacing_Unknown;
}

static float ResolveTooltipPairSpacing(const std::string& leftCodepoint, const std::string& rightCodepoint, float spacingUnit)
{
    if (leftCodepoint.empty() || rightCodepoint.empty() || spacingUnit <= 0.0f)
        return 0.0f;

    const TooltipSpacingClass leftClass = ClassifyTooltipCodepoint(DecodeUtf8CodepointValue(leftCodepoint));
    const TooltipSpacingClass rightClass = ClassifyTooltipCodepoint(DecodeUtf8CodepointValue(rightCodepoint));

    if (leftClass == TooltipSpacing_Whitespace || rightClass == TooltipSpacing_Whitespace)
        return 0.0f;

    if (rightClass == TooltipSpacing_FullWidthComma || rightClass == TooltipSpacing_FullWidthPeriod)
        return 0.0f;

    if (leftClass == TooltipSpacing_FullWidthPeriod)
        return 0.0f;

    if (leftClass == TooltipSpacing_FullWidthComma)
        return 0.0f;

    if (leftClass == TooltipSpacing_Latin && rightClass == TooltipSpacing_Latin)
        return 0.0f;

    if ((leftClass == TooltipSpacing_Latin && rightClass == TooltipSpacing_Digit) ||
        (leftClass == TooltipSpacing_Digit && rightClass == TooltipSpacing_Latin))
    {
        return 1.0f * spacingUnit;
    }

    if ((leftClass == TooltipSpacing_Latin && rightClass == TooltipSpacing_Cjk) ||
        (rightClass == TooltipSpacing_Latin && leftClass == TooltipSpacing_Cjk))
    {
        return 1.0f * spacingUnit;
    }

    return 1.0f * spacingUnit;
}

static float MeasureTooltipCodepointSequenceWidth(const std::vector<std::string>& codepoints, const std::string& styleHintText, float fontSize, float spacingUnit)
{
    if (codepoints.empty())
        return 0.0f;

    float width = 0.0f;
    for (size_t i = 0; i < codepoints.size(); ++i)
    {
        const float codepointWidth = MeasureRetroTextWithStyleHint(styleHintText, codepoints[i], fontSize, 0.0f).x;
        width += codepointWidth;
        if (i + 1 < codepoints.size())
            width += ResolveTooltipPairSpacing(codepoints[i], codepoints[i + 1], spacingUnit);
    }
    return width;
}

static float MeasureTooltipTextWidth(const std::string& text, float fontSize, float spacingUnit)
{
    if (text.empty())
        return 0.0f;

    std::vector<std::string> codepoints;
    SplitUtf8Codepoints(text, codepoints);
    return MeasureTooltipCodepointSequenceWidth(codepoints, text, fontSize, spacingUnit);
}

static ImVec2 MeasureTooltipText(const std::string& text, float fontSize, float spacingUnit)
{
    return ImVec2(
        MeasureTooltipTextWidth(text, fontSize, spacingUnit),
        ResolveRetroLineHeight(fontSize, spacingUnit));
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

static bool FindTooltipColorMarkerSpan(const std::string& text, size_t openPos, size_t* outTextStart, size_t* outClosePos)
{
    if (outTextStart)
        *outTextStart = std::string::npos;
    if (outClosePos)
        *outClosePos = std::string::npos;

    if (openPos >= text.size() || text[openPos] != '#')
        return false;

    if (openPos + 2 < text.size() && text[openPos + 1] == 'c')
    {
        const size_t closePos = text.find('#', openPos + 2);
        if (closePos == std::string::npos || closePos == openPos + 2)
            return false;

        if (outTextStart)
            *outTextStart = openPos + 2;
        if (outClosePos)
            *outClosePos = closePos;
        return true;
    }

    if (openPos + 1 >= text.size())
        return false;

    const size_t closePos = text.find('#', openPos + 1);
    if (closePos == std::string::npos || closePos == openPos + 1)
        return false;

    const unsigned char nextChar = static_cast<unsigned char>(text[openPos + 1]);
    const bool placeholderLike =
        (nextChar >= 'a' && nextChar <= 'z') ||
        (nextChar >= 'A' && nextChar <= 'Z') ||
        nextChar == '_';
    bool containsNonAscii = false;
    for (size_t i = openPos + 1; i < closePos; ++i)
    {
        if (static_cast<unsigned char>(text[i]) >= 0x80)
        {
            containsNonAscii = true;
            break;
        }
    }

    if (placeholderLike && !containsNonAscii)
        return false;

    if (outTextStart)
        *outTextStart = openPos + 1;
    if (outClosePos)
        *outClosePos = closePos;
    return true;
}

static void ParseColorSegments(const std::string& text, ImU32 defaultColor, std::vector<ColorTextSegment>& outSegments)
{
    outSegments.clear();
    size_t i = 0;
    std::string current;
    while (i < text.size())
    {
        if (text[i] == '#')
        {
            size_t textStart = std::string::npos;
            size_t closePos = std::string::npos;
            if (FindTooltipColorMarkerSpan(text, i, &textStart, &closePos))
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
                seg.text = text.substr(textStart, closePos - textStart);
                seg.color = kRetroOrangeTextColor;
                outSegments.push_back(seg);
                i = closePos + 1;
                continue;
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

static void BuildTooltipColorizedCodepoints(
    const std::string& text,
    ImU32 defaultColor,
    std::vector<std::string>& outCodepoints,
    std::vector<ImU32>& outCodepointColors)
{
    outCodepoints.clear();
    outCodepointColors.clear();

    if (text.empty())
        return;

    std::vector<ColorTextSegment> segments;
    ParseColorSegments(text, defaultColor, segments);
    for (size_t i = 0; i < segments.size(); ++i)
    {
        std::vector<std::string> segmentCodepoints;
        SplitUtf8Codepoints(segments[i].text, segmentCodepoints);
        for (size_t j = 0; j < segmentCodepoints.size(); ++j)
        {
            outCodepoints.push_back(segmentCodepoints[j]);
            outCodepointColors.push_back(segments[i].color);
        }
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
            size_t textStart = std::string::npos;
            size_t closePos = std::string::npos;
            if (FindTooltipColorMarkerSpan(text, i, &textStart, &closePos))
            {
                result.append(text, textStart, closePos - textStart);
                i = closePos + 1;
                continue;
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

    std::string description = StripMaxLevelPrefix(ConvertLiteralNewlines(skill.tooltipDescription));
    if (!description.empty())
        return description;

    if (!skill.tooltipPreview.empty())
        return ConvertLiteralNewlines(skill.tooltipPreview);

    return std::string();
}

static bool ContainsUnresolvedTooltipToken(const std::string& text)
{
    for (size_t i = 0; i + 1 < text.size(); ++i)
    {
        if (text[i] != '#')
            continue;

        size_t textStart = std::string::npos;
        size_t closePos = std::string::npos;
        if (FindTooltipColorMarkerSpan(text, i, &textStart, &closePos))
        {
            i = closePos;
            continue;
        }

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

    const std::string plainParagraph = StripColorMarkers(paragraph);
    std::vector<std::string> codepoints;
    std::vector<ImU32> codepointColors;
    BuildTooltipColorizedCodepoints(paragraph, kRetroPureWhiteTextColor, codepoints, codepointColors);
    if (codepoints.empty())
    {
        outLines.push_back(WrappedTooltipLine());
        return;
    }

    const size_t lineStartIndex = outLines.size();
    std::vector<std::string> currentCodepoints;
    std::vector<ImU32> currentColors;
    std::string currentPlainText;
    for (size_t i = 0; i < codepoints.size(); ++i)
    {
        std::vector<std::string> candidateCodepoints = currentCodepoints;
        candidateCodepoints.push_back(codepoints[i]);
        const std::string candidatePlainText = currentPlainText + codepoints[i];
        const float candidateWidth = MeasureTooltipCodepointSequenceWidth(candidateCodepoints, candidatePlainText, fontSize, glyphSpacing);
        if (currentCodepoints.empty() || candidateWidth <= maxWidth + 0.5f)
        {
            currentCodepoints.swap(candidateCodepoints);
            currentPlainText = candidatePlainText;
            currentColors.push_back(codepointColors[i]);
            continue;
        }

        WrappedTooltipLine line;
        line.text = currentPlainText;
        line.plainText = currentPlainText;
        line.codepoints.swap(currentCodepoints);
        line.codepointColors.swap(currentColors);
        line.justify = false;
        outLines.push_back(line);
        currentCodepoints.push_back(codepoints[i]);
        currentColors.push_back(codepointColors[i]);
        currentPlainText = codepoints[i];
    }

    if (!currentCodepoints.empty())
    {
        WrappedTooltipLine line;
        line.text = currentPlainText;
        line.plainText = currentPlainText;
        line.codepoints.swap(currentCodepoints);
        line.codepointColors.swap(currentColors);
        line.justify = false;
        outLines.push_back(line);
    }

    if (outLines.size() > lineStartIndex)
        outLines.back().justify = false;
}

static void BuildWrappedTooltipLines(const std::string& text, float maxWidth, float fontSize, float glyphSpacing, std::vector<WrappedTooltipLine>& outLines)
{
    outLines.clear();

    size_t start = 0;
    while (start <= text.size())
    {
        size_t newlinePos = text.find('\n', start);
        const std::string rawLine = (newlinePos == std::string::npos)
            ? text.substr(start)
            : text.substr(start, newlinePos - start);
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

    const std::string plainText = StripColorMarkers(text);
    std::vector<std::string> codepoints;
    std::vector<ImU32> codepointColors;
    BuildTooltipColorizedCodepoints(text, color, codepoints, codepointColors);
    if (codepoints.empty())
        return;

    std::vector<float> codepointWidths;
    codepointWidths.reserve(codepoints.size());

    float lineWidth = 0.0f;
    for (size_t i = 0; i < codepoints.size(); ++i)
    {
        const ImVec2 measured = MeasureRetroTextWithStyleHint(plainText, codepoints[i], fontSize, 0.0f);
        codepointWidths.push_back(measured.x);
        lineWidth += measured.x;
        if (i + 1 < codepoints.size())
            lineWidth += ResolveTooltipPairSpacing(codepoints[i], codepoints[i + 1], glyphSpacing);
    }

    float extraGap = 0.0f;
    if (justify && codepoints.size() >= 2 && maxWidth > lineWidth)
        extraGap = (maxWidth - lineWidth) / (float)(codepoints.size() - 1);

    float cursorX = pos.x;
    for (size_t i = 0; i < codepoints.size(); ++i)
    {
        DrawOutlinedTextWithStyleHint(
            drawList,
            ImVec2(cursorX, pos.y),
            codepointColors[i],
            plainText.c_str(),
            codepoints[i].c_str(),
            mainScale,
            fontSize,
            0.0f);
        cursorX += codepointWidths[i];
        if (i + 1 < codepoints.size())
            cursorX += ResolveTooltipPairSpacing(codepoints[i], codepoints[i + 1], glyphSpacing) + extraGap;
    }
}

static void DrawWrappedTooltipStyledLine(
    ImDrawList* drawList,
    const ImVec2& pos,
    const WrappedTooltipLine& line,
    float maxWidth,
    float mainScale,
    float fontSize,
    float glyphSpacing)
{
    if (line.codepoints.empty())
        return;

    float lineWidth = 0.0f;
    std::vector<float> codepointWidths;
    codepointWidths.reserve(line.codepoints.size());
    for (size_t i = 0; i < line.codepoints.size(); ++i)
    {
        const ImVec2 measured = MeasureRetroTextWithStyleHint(line.plainText, line.codepoints[i], fontSize, 0.0f).x > 0.0f
            ? MeasureRetroTextWithStyleHint(line.plainText, line.codepoints[i], fontSize, 0.0f)
            : ImVec2(0.0f, 0.0f);
        codepointWidths.push_back(measured.x);
        lineWidth += measured.x;
        if (i + 1 < line.codepoints.size())
            lineWidth += ResolveTooltipPairSpacing(line.codepoints[i], line.codepoints[i + 1], glyphSpacing);
    }

    float extraGap = 0.0f;
    if (line.justify && line.codepoints.size() >= 2 && maxWidth > lineWidth)
        extraGap = (maxWidth - lineWidth) / (float)(line.codepoints.size() - 1);

    float cursorX = pos.x;
    for (size_t i = 0; i < line.codepoints.size(); ++i)
    {
        const ImU32 color = (i < line.codepointColors.size()) ? line.codepointColors[i] : kRetroPureWhiteTextColor;
        DrawOutlinedTextWithStyleHint(
            drawList,
            ImVec2(cursorX, pos.y),
            color,
            line.plainText.c_str(),
            line.codepoints[i].c_str(),
            mainScale,
            fontSize,
            0.0f);
        cursorX += codepointWidths[i];
        if (i + 1 < line.codepoints.size())
            cursorX += ResolveTooltipPairSpacing(line.codepoints[i], line.codepoints[i + 1], glyphSpacing) + extraGap;
    }
}

static float ResolveTooltipPairAdvance(const std::string& leftCodepoint, const std::string& rightCodepoint, float spacingUnit, float rightInkExpansion)
{
    const float spacing = ResolveTooltipPairSpacing(leftCodepoint, rightCodepoint, spacingUnit);
    if (spacing <= 0.0f || rightInkExpansion <= 0.0f)
        return spacing;

    return spacing + rightInkExpansion;
}

static float MeasureTooltipSpacedLineWidth(const std::string& text, float fontSize, float glyphSpacing, float rightInkExpansion, bool largeText = false)
{
    if (text.empty())
        return 0.0f;

    const std::string plainText = StripColorMarkers(text);
    std::vector<std::string> codepoints;
    SplitUtf8Codepoints(plainText, codepoints);
    if (codepoints.empty())
        return 0.0f;

    float width = 0.0f;
    for (size_t i = 0; i < codepoints.size(); ++i)
    {
        width += MeasureRetroTextWithStyleHint(plainText, codepoints[i], fontSize, 0.0f, largeText).x;
        if (i + 1 < codepoints.size())
            width += ResolveTooltipPairAdvance(codepoints[i], codepoints[i + 1], glyphSpacing, rightInkExpansion);
    }

    if (rightInkExpansion > 0.0f)
        width += rightInkExpansion;

    return width;
}

static void DrawTooltipSpacedLine(
    ImDrawList* drawList,
    const ImVec2& pos,
    const std::string& text,
    ImU32 color,
    float mainScale,
    float fontSize,
    float glyphSpacing,
    float rightInkExpansion,
    bool largeText = false)
{
    if (text.empty())
        return;

    const std::string plainText = StripColorMarkers(text);
    std::vector<std::string> codepoints;
    std::vector<ImU32> codepointColors;
    BuildTooltipColorizedCodepoints(text, color, codepoints, codepointColors);
    if (codepoints.empty())
        return;

    float cursorX = pos.x;
    for (size_t i = 0; i < codepoints.size(); ++i)
    {
        const float codepointWidth = MeasureRetroTextWithStyleHint(plainText, codepoints[i], fontSize, 0.0f, largeText).x;
        DrawOutlinedTextWithStyleHint(
            drawList,
            ImVec2(cursorX, pos.y),
            codepointColors[i],
            plainText.c_str(),
            codepoints[i].c_str(),
            mainScale,
            fontSize,
            0.0f,
            largeText);

        if (rightInkExpansion > 0.0f)
        {
            DrawOutlinedTextWithStyleHint(
                drawList,
                ImVec2(cursorX + rightInkExpansion, pos.y),
                codepointColors[i],
                plainText.c_str(),
                codepoints[i].c_str(),
                mainScale,
                fontSize,
                0.0f,
                largeText);
        }

        cursorX += codepointWidth;
        if (i + 1 < codepoints.size())
            cursorX += ResolveTooltipPairAdvance(codepoints[i], codepoints[i + 1], glyphSpacing, rightInkExpansion);
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
        if (!lines[i].codepoints.empty())
            DrawWrappedTooltipStyledLine(drawList, ImVec2(pos.x, cursorY), lines[i], maxWidth, mainScale, fontSize, glyphSpacing);
        cursorY += lineAdvance;
    }

    return lines.empty() ? 0.0f : (cursorY - pos.y);
}

static void DrawBoldTooltipTitle(ImDrawList* drawList, const ImVec2& pos, const char* text, float mainScale, float fontSize, float glyphSpacing)
{
    if (!text || !text[0])
        return;

    DrawTooltipSpacedLine(drawList, pos, text, kRetroPureWhiteTextColor, mainScale, fontSize, glyphSpacing, 1.0f * mainScale, true);
}

static std::string BuildTooltipMaxLevelLabel(int maxLevel)
{
    char numberText[16] = {};
    sprintf_s(numberText, "%d", maxLevel);

    std::string label = u8"[最高等级：";
    label += numberText;
    label += "]";
    return label;
}

static void DrawTooltipMaxLevelLabel(ImDrawList* drawList, const ImVec2& pos, int maxLevel, float mainScale, float fontSize)
{
    if (!drawList)
        return;

    const std::string label = BuildTooltipMaxLevelLabel(maxLevel);
    DrawJustifiedTooltipLine(
        drawList,
        pos,
        label,
        0.0f,
        kRetroPureWhiteTextColor,
        mainScale,
        fontSize,
        1.0f * mainScale,
        false);
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
    const float lineGap = 4.0f * mainScale;
    const float smallGap = lineGap;
    const float sectionGap = 4.0f * mainScale;
    const float tooltipBodyRightPadding = floorf(10.0f * mainScale);
    const float descriptionTextLeft = floorf(92.0f * mainScale);
    const float infoTextLeft = floorf(7.0f * mainScale);
    const float contentOffsetY = floorf(2.0f * mainScale);
    const float descriptionInfoOffsetY = floorf(-4.0f * mainScale);
    const float titleOffsetY = floorf(10.0f * mainScale);
    const float iconTopOffsetY = floorf(34.0f * mainScale);
    const float descriptionTopOffsetY = floorf(37.0f * mainScale);

    std::string descriptionText = BuildTooltipMaxLevelLabel(skill.maxLevel);
    const std::string descriptionBodyText = ResolveTooltipDescriptionText(skill);
    if (!descriptionBodyText.empty())
    {
        descriptionText += "\n";
        descriptionText += descriptionBodyText;
    }
    const std::string fallbackInfoText = ResolveTooltipInfoText(skill);
    const std::string currentInfoText = !skill.tooltipCurrentDetail.empty() ? ConvertLiteralNewlines(skill.tooltipCurrentDetail) : fallbackInfoText;
    const std::string nextInfoText = !skill.tooltipNextDetail.empty() ? ConvertLiteralNewlines(skill.tooltipNextDetail) : fallbackInfoText;

    float info2Height = 0.0f;
    const float descriptionAvailableWidth = tooltipWidth - descriptionTextLeft - tooltipBodyRightPadding;
    const float info2AvailableWidth = tooltipWidth - infoTextLeft - tooltipBodyRightPadding;
    const float descriptionWrapWidth = (descriptionAvailableWidth > 1.0f) ? descriptionAvailableWidth : 1.0f;
    const float info2WrapWidth = (info2AvailableWidth > 1.0f) ? info2AvailableWidth : 1.0f;
    const float labelLineHeight = ResolveRetroLineHeight(bodyFontSize, glyphSpacing);
    const float descriptionHeight = MeasureWrappedTooltipBlockHeight(
        descriptionText,
        descriptionWrapWidth,
        bodyFontSize,
        glyphSpacing,
        lineGap);
    const float iconBottomOffsetY = floorf(iconTopOffsetY + 68.0f * mainScale);
    const float descriptionBottomOffsetY = floorf(descriptionTopOffsetY + descriptionHeight);
    const float topSectionBottomOffsetY = (iconBottomOffsetY > descriptionBottomOffsetY)
        ? iconBottomOffsetY
        : descriptionBottomOffsetY;
    const float dividerOffsetY = floorf(topSectionBottomOffsetY + (14.0f * mainScale));
    const float infoStartOffsetY = floorf(dividerOffsetY + (9.0f * mainScale));

    auto measureInfoSection = [&](const std::string& sectionText) {
        float height = labelLineHeight;
        if (!sectionText.empty())
            height += smallGap + MeasureWrappedTooltipBlockHeight(sectionText, info2WrapWidth, bodyFontSize, glyphSpacing, lineGap);
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

    const float tooltipHeight = floorf(contentOffsetY + infoStartOffsetY + info2Height + (10.0f * mainScale));
    const ImVec2 tooltipSize(tooltipWidth, tooltipHeight);

    ImGuiIO& io = ImGui::GetIO();
    const float edgePadding = 4.0f;
    const float followOffsetX = 0.0f;
    // Independent buff tooltip: keep the existing follow behavior, but lift it
    // 19px relative to the old placement so it doesn't sit too low.
    const float followOffsetY = -1.0f * mainScale;
    float tooltipX = floorf(io.MousePos.x + followOffsetX);
    if (tooltipX < edgePadding)
        tooltipX = edgePadding;
    if (tooltipX + tooltipSize.x > io.DisplaySize.x - edgePadding)
        tooltipX = floorf(io.DisplaySize.x - tooltipSize.x - edgePadding);
    if (tooltipX < edgePadding)
        tooltipX = edgePadding;

    float tooltipY = floorf(io.MousePos.y + followOffsetY);
    if (tooltipY < edgePadding)
        tooltipY = edgePadding;
    if (tooltipY + tooltipSize.y > io.DisplaySize.y - edgePadding)
        tooltipY = floorf(io.DisplaySize.y - tooltipSize.y - edgePadding);
    if (tooltipY < edgePadding)
        tooltipY = edgePadding;

    const ImVec2 tooltipMin(tooltipX, tooltipY);
    const ImVec2 tooltipMax(tooltipX + tooltipSize.x, tooltipY + tooltipSize.y);

    LogTooltipDrawState(skill, io.MousePos, tooltipMin, tooltipSize, currentInfoText, nextInfoText);
    DrawTooltipBorder(drawList, tooltipMin, tooltipMax);

    const ImVec2 titleSize(MeasureTooltipSpacedLineWidth(skill.name, titleFontSize, glyphSpacing, 1.0f * mainScale, true), ResolveRetroLineHeight(titleFontSize, glyphSpacing));
    const ImVec2 titlePos(
        floorf(tooltipMin.x + (tooltipWidth - titleSize.x) * 0.5f),
        floorf(tooltipMin.y + titleOffsetY + contentOffsetY));
    DrawBoldTooltipTitle(drawList, titlePos, skill.name.c_str(), mainScale, titleFontSize, glyphSpacing);

    UITexture* iconTex = nullptr;
    if (skill.showDisabledIcon)
        iconTex = GetRetroSkillSkillIconDisabledTexture(assets, skill.iconId);
    if ((!iconTex || !iconTex->texture) && skill.iconId > 0)
        iconTex = GetRetroSkillSkillIconTexture(assets, skill.iconId);

    const ImVec2 iconMin(
        floorf(tooltipMin.x + 16.0f * mainScale),
        floorf(tooltipMin.y + iconTopOffsetY - 1.0f * mainScale + contentOffsetY));
    const ImVec2 iconMax(iconMin.x + 64.0f * mainScale, iconMin.y + 64.0f * mainScale);
    const ImVec2 iconBackplateMin(
        floorf(tooltipMin.x + 14.0f * mainScale),
        floorf(tooltipMin.y + iconTopOffsetY - 3.0f * mainScale + contentOffsetY));
    const ImVec2 iconBackplateMax(iconBackplateMin.x + 68.0f * mainScale, iconBackplateMin.y + 68.0f * mainScale);
    drawList->AddRectFilled(iconBackplateMin, iconBackplateMax, kTooltipIconBackplateColor);

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
        ImVec2(floorf(tooltipMin.x + descriptionTextLeft), floorf(tooltipMin.y + descriptionTopOffsetY + contentOffsetY + descriptionInfoOffsetY)),
        descriptionWrapWidth,
        descriptionText,
        kRetroPureWhiteTextColor,
        mainScale,
        bodyFontSize,
        glyphSpacing,
        lineGap);

    const float dividerY = floorf(tooltipMin.y + dividerOffsetY + contentOffsetY);
    drawList->AddLine(
        ImVec2(floorf(tooltipMin.x + 7.0f * mainScale), dividerY),
        ImVec2(floorf(tooltipMin.x + 285.0f * mainScale), dividerY),
        kRetroPureWhiteTextColor,
        1.0f);

    float infoCursorY = floorf(tooltipMin.y + infoStartOffsetY + contentOffsetY + descriptionInfoOffsetY);
    auto drawInfoSection = [&](const char* label, const std::string& sectionText) {
        DrawJustifiedTooltipLine(
            drawList,
            ImVec2(floorf(tooltipMin.x + 13.0f * mainScale), floorf(infoCursorY)),
            label,
            0.0f,
            kRetroPureWhiteTextColor,
            mainScale,
            bodyFontSize,
            glyphSpacing,
            false);

        infoCursorY += labelLineHeight;
        if (!sectionText.empty())
        {
            infoCursorY += smallGap;
            infoCursorY += DrawWrappedTooltipBlock(
                drawList,
                ImVec2(floorf(tooltipMin.x + infoTextLeft), floorf(infoCursorY)),
                info2WrapWidth,
                sectionText,
                kRetroPureWhiteTextColor,
                mainScale,
                bodyFontSize,
                glyphSpacing,
                lineGap);
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

void RenderRetroCompactTooltipCard(const char* title, int iconId, const char* infoText, RetroSkillAssets& assets, float mainScale)
{
    if (!title || !title[0])
        return;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (!drawList)
        return;

    const float tooltipWidth = floorf(290.0f * mainScale);
    const float titleFontSize = floorf(14.0f * mainScale);
    const float bodyFontSize = floorf(12.0f * mainScale);
    const float glyphSpacing = 1.0f * mainScale;
    const float lineGap = 4.0f * mainScale;
    const float contentOffsetY = floorf(2.0f * mainScale);
    const float titleOffsetY = floorf(10.0f * mainScale);
    const float iconTopOffsetY = floorf(34.0f * mainScale);
    const float infoTextLeft = floorf(92.0f * mainScale);
    const float tooltipBodyRightPadding = floorf(10.0f * mainScale);

    const std::string compactInfoText = infoText ? ConvertLiteralNewlines(infoText) : std::string();
    const float infoWrapWidth = (tooltipWidth - infoTextLeft - tooltipBodyRightPadding > 1.0f)
        ? (tooltipWidth - infoTextLeft - tooltipBodyRightPadding)
        : 1.0f;
    const float infoHeight = compactInfoText.empty()
        ? 0.0f
        : MeasureWrappedTooltipBlockHeight(compactInfoText, infoWrapWidth, bodyFontSize, glyphSpacing, lineGap);
    const float bodyBottomOffsetY = floorf(iconTopOffsetY + (compactInfoText.empty() ? 64.0f : (infoHeight + 12.0f * mainScale)));
    const float tooltipHeight = (std::max)(
        floorf(contentOffsetY + bodyBottomOffsetY + (12.0f * mainScale)),
        floorf(128.0f * mainScale));
    const ImVec2 tooltipSize(tooltipWidth, tooltipHeight);

    ImGuiIO& io = ImGui::GetIO();
    const float edgePadding = 4.0f;
    const float followOffsetX = 0.0f;
    const float followOffsetY = 18.0f * mainScale;

    float tooltipX = floorf(io.MousePos.x + followOffsetX);
    if (tooltipX < edgePadding)
        tooltipX = edgePadding;
    if (tooltipX + tooltipSize.x > io.DisplaySize.x - edgePadding)
        tooltipX = floorf(io.DisplaySize.x - tooltipSize.x - edgePadding);
    if (tooltipX < edgePadding)
        tooltipX = edgePadding;

    float tooltipY = floorf(io.MousePos.y + followOffsetY);
    if (tooltipY < edgePadding)
        tooltipY = edgePadding;
    if (tooltipY + tooltipSize.y > io.DisplaySize.y - edgePadding)
        tooltipY = floorf(io.DisplaySize.y - tooltipSize.y - edgePadding);
    if (tooltipY < edgePadding)
        tooltipY = edgePadding;

    const ImVec2 tooltipMin(tooltipX, tooltipY);
    const ImVec2 tooltipMax(tooltipX + tooltipSize.x, tooltipY + tooltipSize.y);
    DrawTooltipBorder(drawList, tooltipMin, tooltipMax);

    const std::string titleText = title;
    const ImVec2 titleSize(
        MeasureTooltipSpacedLineWidth(titleText, titleFontSize, glyphSpacing, 1.0f * mainScale, true),
        ResolveRetroLineHeight(titleFontSize, glyphSpacing));
    const ImVec2 titlePos(
        floorf(tooltipMin.x + (tooltipWidth - titleSize.x) * 0.5f),
        floorf(tooltipMin.y + titleOffsetY + contentOffsetY));
    DrawBoldTooltipTitle(drawList, titlePos, titleText.c_str(), mainScale, titleFontSize, glyphSpacing);

    UITexture* iconTex = GetRetroSkillSkillIconTexture(assets, iconId);
    const ImVec2 iconMin(
        floorf(tooltipMin.x + 16.0f * mainScale),
        floorf(tooltipMin.y + iconTopOffsetY - 1.0f * mainScale + contentOffsetY));
    const ImVec2 iconMax(iconMin.x + 64.0f * mainScale, iconMin.y + 64.0f * mainScale);
    const ImVec2 iconBackplateMin(
        floorf(tooltipMin.x + 14.0f * mainScale),
        floorf(tooltipMin.y + iconTopOffsetY - 3.0f * mainScale + contentOffsetY));
    const ImVec2 iconBackplateMax(iconBackplateMin.x + 68.0f * mainScale, iconBackplateMin.y + 68.0f * mainScale);
    drawList->AddRectFilled(iconBackplateMin, iconBackplateMax, kTooltipIconBackplateColor);

    if (iconTex && iconTex->texture)
        drawList->AddImage((ImTextureID)iconTex->texture, iconMin, iconMax);
    else
        drawList->AddRectFilled(iconMin, iconMax, IM_COL32(82, 97, 120, 255), 2.0f * mainScale);

    if (!compactInfoText.empty())
    {
        DrawWrappedTooltipBlock(
            drawList,
            ImVec2(floorf(tooltipMin.x + infoTextLeft), floorf(tooltipMin.y + iconTopOffsetY + contentOffsetY)),
            infoWrapWidth,
            compactInfoText,
            kRetroPureWhiteTextColor,
            mainScale,
            bodyFontSize,
            glyphSpacing,
            lineGap);
    }
}

void RenderRetroBuffTooltipCard(const SkillEntry& skill, RetroSkillAssets& assets, float mainScale)
{
    if (skill.skillId <= 0 && skill.iconId <= 0 && skill.name.empty())
        return;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (!drawList)
        return;

    const float tooltipWidth = floorf(290.0f * mainScale);
    const float titleFontSize = floorf(14.0f * mainScale);
    const float bodyFontSize = floorf(12.0f * mainScale);
    const float glyphSpacing = 1.0f * mainScale;
    const float lineGap = 4.0f * mainScale;
    const float tooltipBodyRightPadding = floorf(10.0f * mainScale);
    const float descriptionTextLeft = floorf(92.0f * mainScale);
    const float contentOffsetY = floorf(2.0f * mainScale);
    const float descriptionInfoOffsetY = floorf(-4.0f * mainScale);
    const float titleOffsetY = floorf(10.0f * mainScale);
    const float iconTopOffsetY = floorf(34.0f * mainScale);
    const float descriptionTopOffsetY = floorf(37.0f * mainScale);

    std::string descriptionText;
    if (skill.maxLevel > 0)
        descriptionText = BuildTooltipMaxLevelLabel(skill.maxLevel);

    const std::string descriptionBodyText = ResolveTooltipDescriptionText(skill);
    if (!descriptionBodyText.empty())
    {
        if (!descriptionText.empty())
            descriptionText += "\n";
        descriptionText += descriptionBodyText;
    }

    const float descriptionAvailableWidth = tooltipWidth - descriptionTextLeft - tooltipBodyRightPadding;
    const float descriptionWrapWidth = (descriptionAvailableWidth > 1.0f) ? descriptionAvailableWidth : 1.0f;
    const float descriptionHeight = descriptionText.empty()
        ? 0.0f
        : MeasureWrappedTooltipBlockHeight(
            descriptionText,
            descriptionWrapWidth,
            bodyFontSize,
            glyphSpacing,
            lineGap);
    const float iconBottomOffsetY = floorf(iconTopOffsetY + 68.0f * mainScale);
    const float descriptionBottomOffsetY = floorf(descriptionTopOffsetY + descriptionHeight);
    const float bodyBottomOffsetY = (iconBottomOffsetY > descriptionBottomOffsetY)
        ? iconBottomOffsetY
        : descriptionBottomOffsetY;
    const float tooltipHeight = (std::max)(
        floorf(contentOffsetY + bodyBottomOffsetY + (12.0f * mainScale)),
        floorf(100.0f * mainScale));
    const ImVec2 tooltipSize(tooltipWidth, tooltipHeight);

    ImGuiIO& io = ImGui::GetIO();
    const float edgePadding = 4.0f;
    const float followOffsetX = 0.0f;
    const float followOffsetY = 18.0f * mainScale;
    float tooltipX = floorf(io.MousePos.x + followOffsetX);
    if (tooltipX < edgePadding)
        tooltipX = edgePadding;
    if (tooltipX + tooltipSize.x > io.DisplaySize.x - edgePadding)
        tooltipX = floorf(io.DisplaySize.x - tooltipSize.x - edgePadding);
    if (tooltipX < edgePadding)
        tooltipX = edgePadding;

    float tooltipY = floorf(io.MousePos.y + followOffsetY);
    if (tooltipY < edgePadding)
        tooltipY = edgePadding;
    if (tooltipY + tooltipSize.y > io.DisplaySize.y - edgePadding)
        tooltipY = floorf(io.DisplaySize.y - tooltipSize.y - edgePadding);
    if (tooltipY < edgePadding)
        tooltipY = edgePadding;

    const ImVec2 tooltipMin(tooltipX, tooltipY);
    const ImVec2 tooltipMax(tooltipX + tooltipSize.x, tooltipY + tooltipSize.y);
    DrawTooltipBorder(drawList, tooltipMin, tooltipMax);

    const std::string titleText = !skill.name.empty() ? skill.name : std::string();
    const ImVec2 titleSize(
        MeasureTooltipSpacedLineWidth(titleText, titleFontSize, glyphSpacing, 1.0f * mainScale, true),
        ResolveRetroLineHeight(titleFontSize, glyphSpacing));
    const ImVec2 titlePos(
        floorf(tooltipMin.x + (tooltipWidth - titleSize.x) * 0.5f),
        floorf(tooltipMin.y + titleOffsetY + contentOffsetY));
    DrawBoldTooltipTitle(drawList, titlePos, titleText.c_str(), mainScale, titleFontSize, glyphSpacing);

    UITexture* iconTex = GetRetroSkillSkillIconTexture(assets, skill.iconId);
    const ImVec2 iconMin(
        floorf(tooltipMin.x + 16.0f * mainScale),
        floorf(tooltipMin.y + iconTopOffsetY - 1.0f * mainScale + contentOffsetY));
    const ImVec2 iconMax(iconMin.x + 64.0f * mainScale, iconMin.y + 64.0f * mainScale);
    const ImVec2 iconBackplateMin(
        floorf(tooltipMin.x + 14.0f * mainScale),
        floorf(tooltipMin.y + iconTopOffsetY - 3.0f * mainScale + contentOffsetY));
    const ImVec2 iconBackplateMax(iconBackplateMin.x + 68.0f * mainScale, iconBackplateMin.y + 68.0f * mainScale);
    drawList->AddRectFilled(iconBackplateMin, iconBackplateMax, kTooltipIconBackplateColor);

    if (iconTex && iconTex->texture)
        drawList->AddImage((ImTextureID)iconTex->texture, iconMin, iconMax);
    else
        drawList->AddRectFilled(iconMin, iconMax, IM_COL32(82, 97, 120, 255), 2.0f * mainScale);

    if (!descriptionText.empty())
    {
        DrawWrappedTooltipBlock(
            drawList,
            ImVec2(floorf(tooltipMin.x + descriptionTextLeft), floorf(tooltipMin.y + descriptionTopOffsetY + contentOffsetY + descriptionInfoOffsetY)),
            descriptionWrapWidth,
            descriptionText,
            kRetroPureWhiteTextColor,
            mainScale,
            bodyFontSize,
            glyphSpacing,
            lineGap);
    }
}

static int CalculateSuperSkillResetSpentSp(const std::vector<SkillEntry>& skills)
{
    long long total = 0;
    for (size_t i = 0; i < skills.size(); ++i)
    {
        const SkillEntry& skill = skills[i];
        if (!skill.isSuperSkill || skill.level <= 0)
            continue;

        const int pointCost = skill.superSpCost > 0 ? skill.superSpCost : 1;
        total += (long long)skill.level * (long long)pointCost;
        if (total > 0x7FFFFFFFLL)
            return 0x7FFFFFFF;
    }
    return (int)total;
}

static int CalculateSuperSkillResetSpentSp(const RetroSkillRuntimeState& state)
{
    long long total = CalculateSuperSkillResetSpentSp(state.passiveSkills);
    total += CalculateSuperSkillResetSpentSp(state.activeSkills);
    if (total > 0x7FFFFFFFLL)
        return 0x7FFFFFFF;
    return (int)total;
}

static std::string FormatMesoText(int meso)
{
    if (meso < 0)
        meso = 0;

    char raw[32] = {};
    sprintf_s(raw, "%d", meso);

    std::string text = raw;
    for (int insertPos = (int)text.size() - 3; insertPos > 0; insertPos -= 3)
        text.insert((size_t)insertPos, ",");
    return text;
}

static void DrawResetNoticeCenteredLine(
    ImDrawList* drawList,
    const ImVec2& noticeMin,
    float noticeWidth,
    float y,
    const std::string& text,
    bool bold,
    float mainScale)
{
    if (!drawList || text.empty())
        return;

    const float fontSize = floorf(12.0f * mainScale);
    const float glyphSpacing = 1.0f * mainScale;
    const float rightInkExpansion = bold ? (1.0f * mainScale) : 0.0f;
    // Confirm notice bold text should only look thicker, not inherit tooltip-title largeText cell widths.
    const bool useLargeTextCells = false;
    const float width = MeasureTooltipSpacedLineWidth(text, fontSize, glyphSpacing, rightInkExpansion, useLargeTextCells);
    const float x = floorf(noticeMin.x + (noticeWidth - width) * 0.5f);
    const bool isTitleLine = (y - noticeMin.y) <= floorf(24.0f * mainScale);
    const ImU32 lineColor = isTitleLine ? kRetroPureYellowTextColor : kRetroPureWhiteTextColor;

    DrawTooltipSpacedLine(
        drawList,
        ImVec2(x, floorf(y)),
        text,
        lineColor,
        mainScale,
        fontSize,
        glyphSpacing,
        rightInkExpansion,
        useLargeTextCells);
}

static ImGuiID g_resetNoticeActiveButtonId = 0;

static bool DrawResetNoticeButton(
    ImDrawList* drawList,
    RetroSkillAssets& assets,
    const char* id,
    const char* normalKey,
    const char* hoverKey,
    const char* pressedKey,
    const char* disabledKey,
    const ImVec2& pos,
    float mainScale,
    bool enabled,
    bool* outHovered,
    bool* outHeld)
{
    if (outHovered) *outHovered = false;
    if (outHeld) *outHeld = false;

    ImGuiIO& io = ImGui::GetIO();
    UITexture* normal = GetRetroSkillTexture(assets, normalKey);
    UITexture* hover = GetRetroSkillTexture(assets, hoverKey);
    UITexture* pressed = GetRetroSkillTexture(assets, pressedKey);
    UITexture* disabled = (disabledKey && disabledKey[0]) ? GetRetroSkillTexture(assets, disabledKey) : nullptr;
    UITexture* base = normal ? normal : (hover ? hover : (pressed ? pressed : disabled));
    if (!drawList || !base || !base->texture)
        return false;

    const ImVec2 size(base->width * mainScale, base->height * mainScale);
    const ImVec2 buttonMax(pos.x + size.x, pos.y + size.y);
    const ImGuiID buttonId = ImGui::GetID(id);
    const bool hoveredRect = ImGui::IsMouseHoveringRect(pos, buttonMax, false);
    bool clicked = false;
    bool hovered = enabled && hoveredRect;
    bool held = false;

    if (enabled && hoveredRect && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        g_resetNoticeActiveButtonId = buttonId;

    held = enabled &&
           io.MouseDown[ImGuiMouseButton_Left] &&
           g_resetNoticeActiveButtonId == buttonId;

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        clicked = enabled && hoveredRect && g_resetNoticeActiveButtonId == buttonId;
        if (g_resetNoticeActiveButtonId == buttonId)
            g_resetNoticeActiveButtonId = 0;
    }
    else if (!io.MouseDown[ImGuiMouseButton_Left] && g_resetNoticeActiveButtonId == buttonId)
    {
        g_resetNoticeActiveButtonId = 0;
    }

    UITexture* texture = normal;
    if (!enabled && disabled && disabled->texture)
        texture = disabled;
    else if (held && pressed && pressed->texture)
        texture = pressed;
    else if (hovered && hover && hover->texture)
        texture = hover;
    if (!texture || !texture->texture)
        texture = base;

    drawList->AddImage(
        (ImTextureID)texture->texture,
        pos,
        ImVec2(pos.x + texture->width * mainScale, pos.y + texture->height * mainScale));

    if (outHovered) *outHovered = hovered;
    if (outHeld) *outHeld = held;
    return clicked;
}

static void CloseResetConfirmWindow(RetroSkillRuntimeState& state)
{
    g_resetNoticeActiveButtonId = 0;
    state.superSkillResetConfirmVisible = false;
    state.superSkillResetConfirmOpenRequested = false;
    state.superSkillResetConfirmSpentSp = 0;
    state.superSkillResetConfirmCostMeso = 0;
    state.superSkillResetConfirmCostPending = false;
    state.superSkillResetConfirmPreviewRequestRevision = 0;
    state.superSkillResetConfirmPreviewRequestTick = 0;
    state.lastAcceptedClickTime = -1.0;
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
    bool skillUseTriggeredByDoubleClick = false;

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

    auto shouldSuppressInitPreviewAction = [&]() {
        if (!hooks || !hooks->onInitPreviewAction)
            return false;
        RetroSkillActionContext context;
        context.currentTab = state.activeTab;
        context.targetTab = state.activeTab;
        return hooks->onInitPreviewAction(context, hooks->userData) == RetroSkill_SuppressDefault;
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

    auto fireSkillUseById = [&](int skillId) {
        if (!hooks || !hooks->onSkillUse || skillId <= 0)
            return;

        RetroSkillActionContext context;
        context.currentTab = state.activeTab;
        context.targetTab = state.activeTab;
        context.skillId = skillId;

        auto fillFromSkillList = [&](const std::vector<SkillEntry>& list, int tab) {
            for (size_t i = 0; i < list.size(); ++i)
            {
                const SkillEntry& skill = list[i];
                if (skill.skillId != skillId)
                    continue;

                context.currentTab = tab;
                context.targetTab = tab;
                context.skillIndex = (int)i;
                context.currentLevel = skill.level;
                context.baseLevel = skill.baseLevel;
                context.bonusLevel = skill.bonusLevel;
                context.maxLevel = skill.maxLevel;
                context.canUpgrade = skill.canUpgrade;
                return true;
            }
            return false;
        };

        fillFromSkillList(state.activeSkills, 1) || fillFromSkillList(state.passiveSkills, 0);
        hooks->onSkillUse(context, hooks->userData);
    };

    auto canAcceptActionClick = [&]() {
        if (state.superSkillResetConfirmVisible || state.superSkillResetConfirmOpenRequested)
            return false;
        double now = ImGui::GetTime();
        if (state.lastAcceptedClickTime < 0.0)
            return true;
        return (now - state.lastAcceptedClickTime) >= state.minClickIntervalSeconds;
    };

    auto markActionClickAccepted = [&]() {
        state.lastAcceptedClickTime = ImGui::GetTime();
    };

    auto canStartSkillDrag = [&](const SkillEntry& skill) {
        if (state.superSkillResetConfirmVisible || state.superSkillResetConfirmOpenRequested)
            return false;
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
                        const float footY = floorf(superSpPos.y + charSize.y - 2.0f);
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
                const bool skillIconHeld = ImGui::IsItemActive() && io.MouseDown[0];

                if (skillIconHeld && !state.isDraggingSkill)
                    state.isPressingUiButton = true;

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

            const bool doubleClickedThisSkill =
                !state.superSkillResetConfirmVisible &&
                !state.superSkillResetConfirmOpenRequested &&
                !plusHovered &&
                isRowHovered &&
                ImGui::IsMouseDoubleClicked(0) &&
                skill.canUse &&
                skill.level > 0 &&
                (!state.isDraggingSkill ||
                 (state.dragSkillTab == state.activeTab && state.dragSkillIndex == (int)i));
            if (doubleClickedThisSkill)
            {
                state.isDraggingSkill = false;
                state.dragSkillTab = -1;
                state.dragSkillIndex = -1;
                state.dragSkillStartedThisFrame = false;
                state.dragSkillIsClickMode = false;
                fireSkillUse((int)i, skill);
                skillUseTriggeredByDoubleClick = true;
                WriteLogFmt("[RetroSkillPanel] double-click use skillId=%d level=%d tab=%d",
                    skill.skillId,
                    skill.level,
                    state.activeTab);
            }

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
                const int localSpentSp = CalculateSuperSkillResetSpentSp(state);
                const unsigned int requestRevision = state.superSkillResetPreviewRevision;
                const bool previewRequested = shouldSuppressInitPreviewAction();
                if (previewRequested)
                {
                    state.superSkillResetConfirmSpentSp = localSpentSp;
                    state.superSkillResetConfirmPreviewRequestRevision = requestRevision;
                    state.superSkillResetConfirmCostMeso = 0;
                    state.superSkillResetConfirmCostPending = true;
                    state.superSkillResetConfirmPreviewRequestTick = GetTickCount();
                    state.superSkillResetConfirmVisible = false;
                    state.superSkillResetConfirmOpenRequested = true;
                    state.isDraggingSkill = false;
                    state.dragSkillTab = -1;
                    state.dragSkillIndex = -1;
                    state.dragSkillIsClickMode = false;
                    WriteLogFmt(
                        "[RetroSkillPanel] reset confirm requested localSpentSp=%d requestRev=%u previewRequested=%d pending=%d",
                        localSpentSp,
                        requestRevision,
                        1,
                        1);
                    markActionClickAccepted();
                }
                else
                {
                    WriteLogFmt(
                        "[RetroSkillPanel] reset confirm skipped localSpentSp=%d requestRev=%u previewRequested=0",
                        localSpentSp,
                        requestRevision);
                }
            }
        }

        if (state.superSkillResetConfirmOpenRequested && !state.superSkillResetConfirmCostPending)
        {
            state.superSkillResetConfirmVisible = true;
            state.superSkillResetConfirmOpenRequested = false;
            state.superSkillResetConfirmPreviewRequestTick = 0;
        }

        if (state.superSkillResetConfirmVisible)
        {
            UITexture* noticeBg = GetRetroSkillTexture(assets, "initial.backgrnd");
            const float noticeWidth = (noticeBg && noticeBg->width > 0) ? noticeBg->width * mainScale : 260.0f * mainScale;
            const float noticeHeight = (noticeBg && noticeBg->height > 0) ? noticeBg->height * mainScale : 131.0f * mainScale;
            const ImVec2 noticeSize(noticeWidth, noticeHeight);
            ImVec2 noticePos(
                floorf(panelPos.x + (m.width - noticeSize.x) * 0.5f),
                floorf(panelPos.y + (m.height - noticeSize.y) * 0.5f));

            if (noticePos.x < 0.0f)
                noticePos.x = 0.0f;
            if (noticePos.y < 0.0f)
                noticePos.y = 0.0f;
            if (noticePos.x + noticeSize.x > io.DisplaySize.x)
                noticePos.x = floorf(io.DisplaySize.x - noticeSize.x);
            if (noticePos.y + noticeSize.y > io.DisplaySize.y)
                noticePos.y = floorf(io.DisplaySize.y - noticeSize.y);
            if (noticePos.x < 0.0f)
                noticePos.x = 0.0f;
            if (noticePos.y < 0.0f)
                noticePos.y = 0.0f;

            ImGui::SetNextWindowPos(noticePos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(noticeSize, ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

            const ImGuiWindowFlags noticeFlags =
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBackground;

            if (ImGui::Begin("##SuperSkillResetConfirmNotice", nullptr, noticeFlags))
            {
                ImDrawList* noticeDrawList = ImGui::GetWindowDrawList();
                const ImVec2 windowPos = ImGui::GetWindowPos();
                const ImVec2 noticeMax(windowPos.x + noticeSize.x, windowPos.y + noticeSize.y);
                if (noticeBg && noticeBg->texture)
                {
                    noticeDrawList->AddImage(
                        (ImTextureID)noticeBg->texture,
                        windowPos,
                        ImVec2(windowPos.x + noticeBg->width * mainScale, windowPos.y + noticeBg->height * mainScale));
                }
                else
                {
                    noticeDrawList->AddRectFilled(windowPos, ImVec2(windowPos.x + noticeSize.x, windowPos.y + noticeSize.y), IM_COL32(239, 232, 210, 255));
                    noticeDrawList->AddRect(windowPos, ImVec2(windowPos.x + noticeSize.x, windowPos.y + noticeSize.y), IM_COL32(80, 70, 55, 255));
                }

                const float noticeFontSize = floorf(12.0f * mainScale);
                const float noticeGlyphSpacing = 1.0f * mainScale;
                const float noticeLineAdvance = floorf(noticeFontSize + 2.0f * mainScale);
                float textY = floorf(windowPos.y + 20.0f * mainScale);

                DrawResetNoticeCenteredLine(noticeDrawList, windowPos, noticeSize.x, textY, u8"超级技能技能点初始化", true, mainScale);
                textY = floorf(textY + noticeLineAdvance);
                DrawResetNoticeCenteredLine(noticeDrawList, windowPos, noticeSize.x, textY, u8"重复进行初始化时，费用会逐渐增加。", false, mainScale);
                textY = floorf(textY + noticeLineAdvance);
                DrawResetNoticeCenteredLine(noticeDrawList, windowPos, noticeSize.x, textY, u8"费用最高不超过5千万金币。", false, mainScale);
                textY = floorf(textY + noticeLineAdvance);

                std::string costLine;
                if (state.superSkillResetConfirmCostPending)
                    costLine = u8"当前初始化费用：服务器计算中...";
                else if (state.superSkillResetConfirmSpentSp <= 0)
                    costLine = u8"当前没有需要初始化的超级技能";
                else
                    costLine = std::string(u8"当前初始化费用：") + FormatMesoText(state.superSkillResetConfirmCostMeso) + u8"金币";
                DrawResetNoticeCenteredLine(noticeDrawList, windowPos, noticeSize.x, textY, costLine, true, mainScale);

                if (ImGui::IsMouseHoveringRect(windowPos, noticeMax, false) && io.MouseDown[0])
                    state.isPressingUiButton = true;

                bool yesHovered = false;
                bool yesHeld = false;
                const bool yesClicked = DrawResetNoticeButton(
                    noticeDrawList,
                    assets,
                    "reset_yes",
                    "Notice.btYes.normal.0",
                    "Notice.btYes.mouseOver.0",
                    "Notice.btYes.pressed.0",
                    "Notice.btYes.disabled.0",
                    ImVec2(floorf(windowPos.x + 157.0f * mainScale), floorf(windowPos.y + 101.0f * mainScale)),
                    mainScale,
                    state.superSkillResetConfirmSpentSp > 0,
                    &yesHovered,
                    &yesHeld);

                bool noHovered = false;
                bool noHeld = false;
                const bool noClicked = DrawResetNoticeButton(
                    noticeDrawList,
                    assets,
                    "reset_no",
                    "Notice.btNo.normal.0",
                    "Notice.btNo.mouseOver.0",
                    "Notice.btNo.pressed.0",
                    nullptr,
                    ImVec2(floorf(windowPos.x + 199.0f * mainScale), floorf(windowPos.y + 101.0f * mainScale)),
                    mainScale,
                    true,
                    &noHovered,
                    &noHeld);

                if (yesHovered || noHovered || yesHeld || noHeld)
                    isHoveringActionButtonsThisFrame = true;
                if ((yesHeld || noHeld) && io.MouseDown[0])
                    state.isPressingUiButton = true;

                if (yesClicked)
                {
                    actionButtonsClickedThisFrame = true;
                    shouldSuppressInitAction();
                    CloseResetConfirmWindow(state);
                }
                else if (noClicked)
                {
                    actionButtonsClickedThisFrame = true;
                    CloseResetConfirmWindow(state);
                }

                ImGui::End();
            }
            else
            {
                ImGui::End();
            }

            ImGui::PopStyleVar(3);
        }

        if (!skillUseTriggeredByDoubleClick && state.isDraggingSkill && state.dragSkillTab == state.activeTab && state.dragSkillIndex >= 0 && state.dragSkillIndex < (int)skills.size())
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

        if (!skillUseTriggeredByDoubleClick &&
            !state.superSkillResetConfirmVisible &&
            !state.superSkillResetConfirmOpenRequested &&
            !state.isDraggingSkill &&
            !state.isScrollDragging &&
            state.quickSlotBarVisible &&
            state.quickSlotBarAcceptDrop &&
            ImGui::IsMouseDoubleClicked(0))
        {
            float barX = 0.0f;
            float barY = 0.0f;
            float barW = 0.0f;
            float barH = 0.0f;
            if (getQuickSlotBarRect(&barX, &barY, &barW, &barH) &&
                io.MousePos.x >= barX && io.MousePos.x < barX + barW &&
                io.MousePos.y >= barY && io.MousePos.y < barY + barH)
            {
                const int col = (int)((io.MousePos.x - barX) / (float)state.quickSlotBarSlotSize);
                const int row = (int)((io.MousePos.y - barY) / (float)state.quickSlotBarSlotSize);
                const int slotIndex = row * state.quickSlotBarCols + col;
                if (col >= 0 && col < state.quickSlotBarCols &&
                    row >= 0 && row < state.quickSlotBarRows &&
                    slotIndex >= 0 && slotIndex < SKILL_BAR_TOTAL_SLOTS)
                {
                    const int slotSkillId = state.quickSlots[slotIndex].skillId;
                    if (slotSkillId > 0)
                    {
                        fireSkillUseById(slotSkillId);
                        skillUseTriggeredByDoubleClick = true;
                        WriteLogFmt("[RetroSkillPanel] double-click quickSlot[%d] use skillId=%d",
                            slotIndex,
                            slotSkillId);
                    }
                }
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

        if (hoveredSkill && hasHoveredSkillRect &&
            !state.isDraggingSkill &&
            !state.superSkillResetConfirmVisible &&
            !state.superSkillResetConfirmOpenRequested)
            RenderSkillTooltip(*hoveredSkill, assets, mainScale, hoveredSkillMin, hoveredSkillMax, panelPos, m.width);
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
}
