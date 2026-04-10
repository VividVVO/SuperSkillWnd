#include "retro_skill_text_dwrite.h"

#include "core/Common.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace
{
    struct FontResourceKey
    {
        std::wstring faceName;
        int pixelHeight = 0;
        int weight = 0;

        bool operator<(const FontResourceKey& other) const
        {
            if (faceName != other.faceName)
                return faceName < other.faceName;
            if (pixelHeight != other.pixelHeight)
                return pixelHeight < other.pixelHeight;
            return weight < other.weight;
        }
    };

    struct TextStyleKey
    {
        bool numeric = false;
        int pixelHeight = 0;
        int numericCellWidth = 0;
        int numericCellHeight = 0;
        int glyphSpacing = 0;

        bool operator<(const TextStyleKey& other) const
        {
            if (numeric != other.numeric)
                return numeric < other.numeric;
            if (pixelHeight != other.pixelHeight)
                return pixelHeight < other.pixelHeight;
            if (numericCellWidth != other.numericCellWidth)
                return numericCellWidth < other.numericCellWidth;
            if (numericCellHeight != other.numericCellHeight)
                return numericCellHeight < other.numericCellHeight;
            return glyphSpacing < other.glyphSpacing;
        }
    };

    struct GlyphCacheKey
    {
        TextStyleKey style;
        wchar_t codepoint = 0;

        bool operator<(const GlyphCacheKey& other) const
        {
            if (style < other.style)
                return true;
            if (other.style < style)
                return false;
            return codepoint < other.codepoint;
        }
    };

    struct FontResource
    {
        HFONT font = nullptr;
        TEXTMETRICW metrics = {};
        std::wstring resolvedFaceName;
    };

    struct GlyphTexture
    {
        IDirect3DTexture9* texture = nullptr;
        int width = 0;
        int height = 0;
        int advance = 0;
        int offsetX = 0;
        int offsetY = 0;
        bool whitespace = false;
    };

    struct RendererState
    {
        LPDIRECT3DDEVICE9 device = nullptr;
        HDC dc = nullptr;
        bool initialized = false;
        std::map<FontResourceKey, FontResource> fonts;
        std::map<GlyphCacheKey, GlyphTexture> glyphs;
    };

    RendererState g_renderer;

    MAT2 MakeIdentityMatrix()
    {
        MAT2 mat = {};
        mat.eM11.value = 1;
        mat.eM22.value = 1;
        return mat;
    }

    int ClampInt(int value, int minValue, int maxValue)
    {
        if (value < minValue)
            return minValue;
        if (value > maxValue)
            return maxValue;
        return value;
    }

    int MaxInt(int a, int b)
    {
        return (a > b) ? a : b;
    }

    enum GlyphShapeClass
    {
        GlyphShape_Cjk = 0,
        GlyphShape_Digit = 1,
        GlyphShape_Latin = 2,
        GlyphShape_Punctuation = 3,
    };

    GlyphShapeClass ClassifyGlyphShape(wchar_t ch)
    {
        if (ch >= L'0' && ch <= L'9')
            return GlyphShape_Digit;

        if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z'))
            return GlyphShape_Latin;

        if ((ch >= 0x4E00 && ch <= 0x9FFF) || (ch >= 0x3400 && ch <= 0x4DBF))
            return GlyphShape_Cjk;

        return GlyphShape_Punctuation;
    }

    void ResolveTargetGlyphSize(const TextStyleKey& style, wchar_t ch, int sourceWidth, int sourceHeight, int* outWidth, int* outHeight)
    {
        if (!outWidth || !outHeight)
            return;

        const GlyphShapeClass shape = ClassifyGlyphShape(ch);
        if (style.numeric)
        {
            if (shape == GlyphShape_Digit)
            {
                if (style.numericCellWidth >= 6)
                    *outWidth = (ch == L'1') ? 5 : style.numericCellWidth;
                else
                    *outWidth = (ch == L'1') ? 3 : style.numericCellWidth;
                *outHeight = style.numericCellHeight > 0 ? style.numericCellHeight : 8;
                return;
            }

            *outWidth = MaxInt(1, sourceWidth);
            *outHeight = style.numericCellHeight > 0 ? style.numericCellHeight : 8;
            return;
        }

        if (shape == GlyphShape_Cjk)
        {
            *outWidth = ClampInt(sourceWidth, 1, 11);
            *outHeight = ClampInt(sourceHeight, 1, 12);
            return;
        }

        if (shape == GlyphShape_Latin)
        {
            *outWidth = ClampInt(sourceWidth, 1, 6);
            *outHeight = ClampInt(sourceHeight, 1, 12);
            return;
        }

        *outWidth = MaxInt(1, sourceWidth);
        *outHeight = ClampInt(sourceHeight, 1, 12);
    }

    std::vector<unsigned int> ResampleNearest(const std::vector<unsigned int>& sourcePixels, int sourceWidth, int sourceHeight, int targetWidth, int targetHeight)
    {
        std::vector<unsigned int> targetPixels;
        if (sourceWidth <= 0 || sourceHeight <= 0 || targetWidth <= 0 || targetHeight <= 0)
            return targetPixels;

        targetPixels.resize((size_t)targetWidth * (size_t)targetHeight, 0);
        for (int y = 0; y < targetHeight; ++y)
        {
            const int sourceY = (y * sourceHeight) / targetHeight;
            for (int x = 0; x < targetWidth; ++x)
            {
                const int sourceX = (x * sourceWidth) / targetWidth;
                targetPixels[(size_t)y * (size_t)targetWidth + (size_t)x] =
                    sourcePixels[(size_t)sourceY * (size_t)sourceWidth + (size_t)sourceX];
            }
        }

        return targetPixels;
    }

    std::vector<unsigned int> PlacePixelsIntoCanvas(const std::vector<unsigned int>& sourcePixels, int sourceWidth, int sourceHeight, int canvasWidth, int canvasHeight, int offsetX, int offsetY)
    {
        std::vector<unsigned int> canvasPixels;
        if (sourceWidth <= 0 || sourceHeight <= 0 || canvasWidth <= 0 || canvasHeight <= 0)
            return canvasPixels;

        canvasPixels.resize((size_t)canvasWidth * (size_t)canvasHeight, 0);
        for (int y = 0; y < sourceHeight; ++y)
        {
            const int dstY = offsetY + y;
            if (dstY < 0 || dstY >= canvasHeight)
                continue;

            for (int x = 0; x < sourceWidth; ++x)
            {
                const int dstX = offsetX + x;
                if (dstX < 0 || dstX >= canvasWidth)
                    continue;

                canvasPixels[(size_t)dstY * (size_t)canvasWidth + (size_t)dstX] =
                    sourcePixels[(size_t)y * (size_t)sourceWidth + (size_t)x];
            }
        }

        return canvasPixels;
    }

    bool IsNearlyGray(ImU32 color)
    {
        const int r = (int)((color >> IM_COL32_R_SHIFT) & 0xFF);
        const int g = (int)((color >> IM_COL32_G_SHIFT) & 0xFF);
        const int b = (int)((color >> IM_COL32_B_SHIFT) & 0xFF);
        return abs(r - g) <= 2 && abs(r - b) <= 2 && abs(g - b) <= 2;
    }

    ImU32 MakeToneColor(unsigned int rgb, unsigned int alpha)
    {
        return (alpha << IM_COL32_A_SHIFT) |
            (((rgb >> 16) & 0xFF) << IM_COL32_R_SHIFT) |
            (((rgb >> 8) & 0xFF) << IM_COL32_G_SHIFT) |
            ((rgb & 0xFF) << IM_COL32_B_SHIFT);
    }

    void ResolveToneColors(ImU32 inputColor, ImU32* outTopColor, ImU32* outBottomColor)
    {
        if (!outTopColor || !outBottomColor)
            return;

        const unsigned int alpha = (inputColor >> IM_COL32_A_SHIFT) & 0xFF;
        if (!IsNearlyGray(inputColor))
        {
            *outTopColor = inputColor;
            *outBottomColor = inputColor;
            return;
        }

        const ImU32 unifiedGray = MakeToneColor(0x555555, alpha);
        *outTopColor = unifiedGray;
        *outBottomColor = unifiedGray;
    }

    void AddGlyphQuad(ImDrawList* drawList, IDirect3DTexture9* texture, const ImVec2& minPos, const ImVec2& maxPos, ImU32 topColor, ImU32 bottomColor)
    {
        if (!drawList || !texture)
            return;

        drawList->PushTextureID((ImTextureID)texture);
        drawList->PrimReserve(6, 4);
        drawList->PrimWriteIdx((ImDrawIdx)(drawList->_VtxCurrentIdx + 0));
        drawList->PrimWriteIdx((ImDrawIdx)(drawList->_VtxCurrentIdx + 1));
        drawList->PrimWriteIdx((ImDrawIdx)(drawList->_VtxCurrentIdx + 2));
        drawList->PrimWriteIdx((ImDrawIdx)(drawList->_VtxCurrentIdx + 0));
        drawList->PrimWriteIdx((ImDrawIdx)(drawList->_VtxCurrentIdx + 2));
        drawList->PrimWriteIdx((ImDrawIdx)(drawList->_VtxCurrentIdx + 3));
        drawList->PrimWriteVtx(ImVec2(minPos.x, minPos.y), ImVec2(0.0f, 0.0f), topColor);
        drawList->PrimWriteVtx(ImVec2(maxPos.x, minPos.y), ImVec2(1.0f, 0.0f), topColor);
        drawList->PrimWriteVtx(ImVec2(maxPos.x, maxPos.y), ImVec2(1.0f, 1.0f), bottomColor);
        drawList->PrimWriteVtx(ImVec2(minPos.x, maxPos.y), ImVec2(0.0f, 1.0f), bottomColor);
        drawList->PopTextureID();
    }

    void ReleaseGlyphTexture(GlyphTexture& glyph)
    {
        if (glyph.texture)
        {
            glyph.texture->Release();
            glyph.texture = nullptr;
        }
        glyph.width = 0;
        glyph.height = 0;
        glyph.advance = 0;
        glyph.offsetX = 0;
        glyph.offsetY = 0;
        glyph.whitespace = false;
    }

    void ClearGlyphCache()
    {
        for (std::map<GlyphCacheKey, GlyphTexture>::iterator it = g_renderer.glyphs.begin();
             it != g_renderer.glyphs.end();
             ++it)
        {
            ReleaseGlyphTexture(it->second);
        }
        g_renderer.glyphs.clear();
    }

    void ClearFontCache()
    {
        for (std::map<FontResourceKey, FontResource>::iterator it = g_renderer.fonts.begin();
             it != g_renderer.fonts.end();
             ++it)
        {
            if (it->second.font)
            {
                DeleteObject(it->second.font);
                it->second.font = nullptr;
            }
        }
        g_renderer.fonts.clear();
    }

    bool EnsureDeviceAndDc(LPDIRECT3DDEVICE9 device)
    {
        if (device)
            g_renderer.device = device;

        if (!g_renderer.device)
            return false;

        if (!g_renderer.dc)
        {
            g_renderer.dc = CreateCompatibleDC(nullptr);
            if (!g_renderer.dc)
            {
                WriteLog("[RetroText] CreateCompatibleDC failed");
                return false;
            }
            SetBkMode(g_renderer.dc, TRANSPARENT);
            SetMapMode(g_renderer.dc, MM_TEXT);
        }

        return true;
    }

    std::wstring Utf8ToWide(const char* text)
    {
        if (!text || !text[0])
            return std::wstring();

        int wideLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
        if (wideLen <= 0)
            wideLen = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
        if (wideLen <= 0)
            wideLen = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
        if (wideLen <= 0)
            return std::wstring();

        std::wstring wideText;
        wideText.resize((size_t)wideLen - 1);
        if (!wideText.empty())
        {
            if (!MultiByteToWideChar(CP_UTF8, 0, text, -1, &wideText[0], wideLen))
            {
                MultiByteToWideChar(CP_ACP, 0, text, -1, &wideText[0], wideLen);
            }
        }
        return wideText;
    }

    bool IsNumericLikeCharacter(wchar_t ch)
    {
        if (ch >= L'0' && ch <= L'9')
            return true;

        switch (ch)
        {
        case L' ':
        case L'+':
        case L'-':
        case L'(':
        case L')':
        case L'/':
        case L'.':
        case L':':
        case L'%':
            return true;
        default:
            return false;
        }
    }

    bool IsNumericLikeText(const std::wstring& text)
    {
        if (text.empty())
            return false;

        for (size_t i = 0; i < text.size(); ++i)
        {
            const wchar_t ch = text[i];
            if (ch == L'\r' || ch == L'\n' || ch == 0)
                continue;
            if (!IsNumericLikeCharacter(ch))
                return false;
        }
        return true;
    }

    int ResolvePixelHeight(bool numeric, float fontSize)
    {
        if (!numeric)
            return 12;

        return (fontSize > 0.0f && fontSize <= 9.5f) ? 12 : 11;
    }

    TextStyleKey ResolveStyle(const std::wstring& text, float fontSize, float glyphSpacing)
    {
        TextStyleKey style = {};
        style.numeric = IsNumericLikeText(text);
        style.pixelHeight = ResolvePixelHeight(style.numeric, fontSize);
        style.glyphSpacing = (int)floorf(glyphSpacing + 0.5f);
        if (style.numeric)
        {
            if (fontSize > 0.0f && fontSize <= 9.5f)
            {
                style.numericCellWidth = 6;
                style.numericCellHeight = 9;
            }
            else
            {
                style.numericCellWidth = 5;
                style.numericCellHeight = 8;
            }
        }
        return style;
    }

    std::vector<std::wstring> GetCandidateFaces(const TextStyleKey& style)
    {
        std::vector<std::wstring> faces;
        if (style.numeric)
        {
            faces.push_back(L"Tahoma");
            faces.push_back(L"NSimSun");
            faces.push_back(L"SimSun");
        }
        else
        {
            faces.push_back(L"SimSun");
            faces.push_back(L"NSimSun");
            faces.push_back(L"MS Gothic");
        }
        return faces;
    }

    FontResource* AcquireFont(const std::wstring& faceName, int pixelHeight, int weight)
    {
        const FontResourceKey key = { faceName, pixelHeight, weight };
        std::map<FontResourceKey, FontResource>::iterator found = g_renderer.fonts.find(key);
        if (found != g_renderer.fonts.end())
            return &found->second;

        FontResource resource = {};
        resource.font = CreateFontW(
            -pixelHeight,
            0,
            0,
            0,
            weight,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_TT_ONLY_PRECIS,
            CLIP_DEFAULT_PRECIS,
            NONANTIALIASED_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            faceName.c_str());

        if (!resource.font)
        {
            resource.font = CreateFontW(
                -pixelHeight,
                0,
                0,
                0,
                weight,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                NONANTIALIASED_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE,
                faceName.c_str());
        }

        if (!resource.font)
            return nullptr;

        HGDIOBJ oldObject = SelectObject(g_renderer.dc, resource.font);
        GetTextMetricsW(g_renderer.dc, &resource.metrics);

        wchar_t resolvedFace[LF_FACESIZE] = {};
        if (GetTextFaceW(g_renderer.dc, LF_FACESIZE, resolvedFace) > 0)
            resource.resolvedFaceName = resolvedFace;
        else
            resource.resolvedFaceName = faceName;

        SelectObject(g_renderer.dc, oldObject);

        std::pair<std::map<FontResourceKey, FontResource>::iterator, bool> inserted =
            g_renderer.fonts.insert(std::make_pair(key, resource));
        return inserted.second ? &inserted.first->second : nullptr;
    }

    int MeasureWhitespaceAdvance(FontResource* font, wchar_t ch)
    {
        if (!font)
            return 0;

        wchar_t measureChar = (ch == L'\t') ? L' ' : ch;
        HGDIOBJ oldObject = SelectObject(g_renderer.dc, font->font);
        SIZE size = {};
        GetTextExtentPoint32W(g_renderer.dc, &measureChar, 1, &size);
        SelectObject(g_renderer.dc, oldObject);

        int advance = size.cx;
        if (ch == L'\t')
            advance *= 4;
        if (advance <= 0)
            advance = MaxInt(1, font->metrics.tmAveCharWidth);
        return advance;
    }

    bool CreateTextureFromArgbPixels(const std::vector<unsigned int>& pixels, int width, int height, IDirect3DTexture9** outTexture)
    {
        if (!outTexture || !g_renderer.device || width <= 0 || height <= 0)
            return false;

        *outTexture = nullptr;

        IDirect3DTexture9* texture = nullptr;
        if (FAILED(g_renderer.device->CreateTexture(
            width,
            height,
            1,
            0,
            D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED,
            &texture,
            nullptr)))
        {
            return false;
        }

        D3DLOCKED_RECT locked = {};
        if (FAILED(texture->LockRect(0, &locked, nullptr, 0)))
        {
            texture->Release();
            return false;
        }

        for (int y = 0; y < height; ++y)
        {
            unsigned char* dstRow = (unsigned char*)locked.pBits + y * locked.Pitch;
            const unsigned char* srcRow = (const unsigned char*)&pixels[(size_t)y * (size_t)width];
            memcpy(dstRow, srcRow, (size_t)width * sizeof(unsigned int));
        }

        texture->UnlockRect(0);
        *outTexture = texture;
        return true;
    }

    bool TryCreateGlyphWithFont(const TextStyleKey& style, FontResource* font, wchar_t ch, GlyphTexture* outGlyph)
    {
        if (!font || !outGlyph)
            return false;

        outGlyph->advance = MeasureWhitespaceAdvance(font, ch);
        if (ch == L' ' || ch == L'\t')
        {
            outGlyph->whitespace = true;
            return true;
        }

        HGDIOBJ oldObject = SelectObject(g_renderer.dc, font->font);
        GLYPHMETRICS metrics = {};
        const MAT2 identity = MakeIdentityMatrix();

        DWORD bufferSize = GetGlyphOutlineW(g_renderer.dc, ch, GGO_BITMAP, &metrics, 0, nullptr, &identity);
        if (bufferSize == GDI_ERROR)
        {
            SelectObject(g_renderer.dc, oldObject);
            return false;
        }

        if (bufferSize == 0 || metrics.gmBlackBoxX == 0 || metrics.gmBlackBoxY == 0)
        {
            SelectObject(g_renderer.dc, oldObject);
            outGlyph->whitespace = true;
            if (outGlyph->advance <= 0)
                outGlyph->advance = MaxInt(1, metrics.gmCellIncX);
            return true;
        }

        std::vector<unsigned char> glyphBuffer;
        glyphBuffer.resize((size_t)bufferSize);
        if (GetGlyphOutlineW(g_renderer.dc, ch, GGO_BITMAP, &metrics, bufferSize, &glyphBuffer[0], &identity) == GDI_ERROR)
        {
            SelectObject(g_renderer.dc, oldObject);
            return false;
        }
        SelectObject(g_renderer.dc, oldObject);

        const unsigned int rowPitchBytes = ((metrics.gmBlackBoxX + 31u) / 32u) * 4u;
        int minX = (int)metrics.gmBlackBoxX;
        int minY = (int)metrics.gmBlackBoxY;
        int maxX = -1;
        int maxY = -1;

        for (unsigned int y = 0; y < metrics.gmBlackBoxY; ++y)
        {
            const unsigned char* row = &glyphBuffer[(size_t)y * rowPitchBytes];
            for (unsigned int x = 0; x < metrics.gmBlackBoxX; ++x)
            {
                const unsigned char byteValue = row[x >> 3];
                const unsigned char mask = (unsigned char)(0x80u >> (x & 7u));
                if ((byteValue & mask) == 0)
                    continue;
                if ((int)x < minX) minX = (int)x;
                if ((int)y < minY) minY = (int)y;
                if ((int)x > maxX) maxX = (int)x;
                if ((int)y > maxY) maxY = (int)y;
            }
        }

        if (maxX < minX || maxY < minY)
        {
            outGlyph->whitespace = true;
            if (outGlyph->advance <= 0)
                outGlyph->advance = MaxInt(1, metrics.gmCellIncX);
            return true;
        }

        const int cropWidth = maxX - minX + 1;
        const int cropHeight = maxY - minY + 1;
        std::vector<unsigned int> pixels;
        pixels.resize((size_t)cropWidth * (size_t)cropHeight, 0);

        for (int y = 0; y < cropHeight; ++y)
        {
            const unsigned char* srcRow = &glyphBuffer[(size_t)(minY + y) * rowPitchBytes];
            for (int x = 0; x < cropWidth; ++x)
            {
                const unsigned int sourceX = (unsigned int)(minX + x);
                const unsigned char byteValue = srcRow[sourceX >> 3];
                const unsigned char mask = (unsigned char)(0x80u >> (sourceX & 7u));
                if ((byteValue & mask) == 0)
                    continue;

                pixels[(size_t)y * (size_t)cropWidth + (size_t)x] =
                    0xFFFFFFFFu;
            }
        }

        int targetWidth = cropWidth;
        int targetHeight = cropHeight;
        ResolveTargetGlyphSize(style, ch, cropWidth, cropHeight, &targetWidth, &targetHeight);
        if (targetWidth <= 0)
            targetWidth = cropWidth;
        if (targetHeight <= 0)
            targetHeight = cropHeight;

        if (targetWidth != cropWidth || targetHeight != cropHeight)
        {
            pixels = ResampleNearest(pixels, cropWidth, cropHeight, targetWidth, targetHeight);
        }

        int finalTextureWidth = targetWidth;
        int finalTextureHeight = targetHeight;
        if (style.numeric && ClassifyGlyphShape(ch) == GlyphShape_Digit)
        {
            const int numericCellWidth = style.numericCellWidth > 0 ? style.numericCellWidth : 5;
            const int numericCellHeight = style.numericCellHeight > 0 ? style.numericCellHeight : 8;
            const int centeredOffsetX = (numericCellWidth - targetWidth) / 2;
            const int bottomOffsetY = numericCellHeight - targetHeight;
            pixels = PlacePixelsIntoCanvas(
                pixels,
                targetWidth,
                targetHeight,
                numericCellWidth,
                numericCellHeight,
                centeredOffsetX,
                bottomOffsetY);
            finalTextureWidth = numericCellWidth;
            finalTextureHeight = numericCellHeight;
        }

        if (!CreateTextureFromArgbPixels(pixels, finalTextureWidth, finalTextureHeight, &outGlyph->texture))
            return false;

        outGlyph->width = finalTextureWidth;
        outGlyph->height = finalTextureHeight;
        outGlyph->advance = MaxInt(1, finalTextureWidth);
        if (outGlyph->advance <= 0)
            outGlyph->advance = finalTextureWidth;
        outGlyph->offsetX = 0;
        outGlyph->offsetY = MaxInt(0, font->metrics.tmAscent - finalTextureHeight);
        outGlyph->whitespace = false;
        return true;
    }

    GlyphTexture* AcquireGlyph(const TextStyleKey& style, wchar_t ch)
    {
        GlyphCacheKey cacheKey = {};
        cacheKey.style = style;
        cacheKey.codepoint = ch;

        std::map<GlyphCacheKey, GlyphTexture>::iterator found = g_renderer.glyphs.find(cacheKey);
        if (found != g_renderer.glyphs.end())
            return &found->second;

        GlyphTexture glyph = {};
        const std::vector<std::wstring> faces = GetCandidateFaces(style);
        for (size_t i = 0; i < faces.size(); ++i)
        {
            FontResource* font = AcquireFont(faces[i], style.pixelHeight, FW_NORMAL);
            if (!font)
                continue;
            if (TryCreateGlyphWithFont(style, font, ch, &glyph))
                break;
        }

        if (!glyph.texture && !glyph.whitespace)
        {
            if (ch != L'?')
                return AcquireGlyph(style, L'?');

            glyph.whitespace = true;
            glyph.advance = MaxInt(1, style.pixelHeight / 2);
        }

        std::pair<std::map<GlyphCacheKey, GlyphTexture>::iterator, bool> inserted =
            g_renderer.glyphs.insert(std::make_pair(cacheKey, glyph));
        return inserted.second ? &inserted.first->second : nullptr;
    }

    int ResolveLineHeight(const TextStyleKey& style)
    {
        if (style.numeric && style.numericCellHeight > 0)
            return style.numericCellHeight;

        const std::vector<std::wstring> faces = GetCandidateFaces(style);
        for (size_t i = 0; i < faces.size(); ++i)
        {
            FontResource* font = AcquireFont(faces[i], style.pixelHeight, FW_NORMAL);
            if (font && font->metrics.tmHeight > 0)
                return MaxInt(font->metrics.tmHeight, style.numeric ? 8 : 12);
        }
        return style.numeric ? 8 : 12;
    }
}

bool RetroSkillDWriteInitialize(LPDIRECT3DDEVICE9 device)
{
    if (!EnsureDeviceAndDc(device))
        return false;

    g_renderer.initialized = true;
    WriteLog("[RetroText] initialized self renderer");
    return true;
}

void RetroSkillDWriteShutdown()
{
    ClearGlyphCache();
    ClearFontCache();

    if (g_renderer.dc)
    {
        DeleteDC(g_renderer.dc);
        g_renderer.dc = nullptr;
    }

    g_renderer.device = nullptr;
    g_renderer.initialized = false;
}

void RetroSkillDWriteOnDeviceLost()
{
}

void RetroSkillDWriteOnDeviceReset(LPDIRECT3DDEVICE9 device)
{
    if (device && g_renderer.device && device != g_renderer.device)
        ClearGlyphCache();
    if (device)
        g_renderer.device = device;
}

bool RetroSkillDWriteDrawTextEx(ImDrawList* drawList, const ImVec2& pos, ImU32 color, const char* text, float fontSize, float glyphSpacing)
{
    if (!drawList || !text || !text[0] || !g_renderer.initialized || !EnsureDeviceAndDc(nullptr))
        return false;

    const std::wstring wideText = Utf8ToWide(text);
    if (wideText.empty())
        return false;

    const TextStyleKey style = ResolveStyle(wideText, fontSize, glyphSpacing);
    float cursorX = floorf(pos.x);
    float cursorY = floorf(pos.y);
    const float startX = cursorX;
    const float lineHeight = (float)ResolveLineHeight(style);
    bool renderedAnything = false;

    for (size_t i = 0; i < wideText.size(); ++i)
    {
        const wchar_t ch = wideText[i];
        if (ch == L'\r')
            continue;

        if (ch == L'\n')
        {
            cursorX = startX;
            cursorY += lineHeight;
            continue;
        }

        GlyphTexture* glyph = AcquireGlyph(style, ch);
        if (!glyph)
            continue;

        if (!glyph->whitespace && glyph->texture && glyph->width > 0 && glyph->height > 0)
        {
            const ImVec2 glyphMin(
                floorf(cursorX + (float)glyph->offsetX),
                floorf(cursorY + (float)glyph->offsetY));
            const ImVec2 glyphMax(
                glyphMin.x + (float)glyph->width,
                glyphMin.y + (float)glyph->height);

            ImU32 topColor = color;
            ImU32 bottomColor = color;
            ResolveToneColors(color, &topColor, &bottomColor);
            AddGlyphQuad(drawList, glyph->texture, glyphMin, glyphMax, topColor, bottomColor);
            renderedAnything = true;
        }

        cursorX += (float)glyph->advance + (float)style.glyphSpacing;
    }

    return renderedAnything;
}

bool RetroSkillDWriteDrawText(ImDrawList* drawList, const ImVec2& pos, ImU32 color, const char* text, float fontSize)
{
    return RetroSkillDWriteDrawTextEx(drawList, pos, color, text, fontSize, 0.0f);
}

void RetroSkillDWriteRegisterNativeGlyphLookup(void* /*trampoline*/)
{
}

void RetroSkillDWriteObserveGlyphLookup(void* /*fontCache*/, unsigned int /*codepoint*/)
{
}
