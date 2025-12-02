/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkTypeface.h"
#include "include/private/base/SkTFitsIn.h"
#include "modules/skshaper/include/SkShaper.h"
#include "src/base/SkUTF.h"
#include <limits.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <locale>
#include <string>
#include <utility>

#if !defined(SK_DISABLE_LEGACY_SKSHAPER_FUNCTIONS)

#if defined(SK_SHAPER_HARFBUZZ_AVAILABLE)
#include "modules/skshaper/include/SkShaper_harfbuzz.h"
#endif

#if defined(SK_SHAPER_CORETEXT_AVAILABLE)
#include "modules/skshaper/include/SkShaper_coretext.h"
#endif

#endif  // !defined(SK_DISABLE_LEGACY_SKSHAPER_FUNCTIONS)

#if !defined(SK_DISABLE_LEGACY_SKSHAPER_FUNCTIONS)
std::unique_ptr<SkShaper> SkShaper::Make(sk_sp<SkFontMgr> fallback) {
#if defined(SK_SHAPER_HARFBUZZ_AVAILABLE) && defined(SK_SHAPER_UNICODE_AVAILABLE)
    std::unique_ptr<SkShaper> shaper = MakeShapeThenWrap(std::move(fallback));
    if (shaper) {
        return shaper;
    }
#elif defined(SK_SHAPER_CORETEXT_AVAILABLE)
    if (auto shaper = SkShapers::CT::CoreText()) {
        return shaper;
    }
#endif
    return SkShapers::Primitive::PrimitiveText();
}

void SkShaper::PurgeCaches() {
#if defined(SK_SHAPER_HARFBUZZ_AVAILABLE) && defined(SK_SHAPER_UNICODE_AVAILABLE)
    SkShapers::HB::PurgeCaches();
#endif
}

std::unique_ptr<SkShaper::BiDiRunIterator>
SkShaper::MakeBiDiRunIterator(const char* utf8, size_t utf8Bytes, uint8_t bidiLevel) {
#if defined(SK_SHAPER_UNICODE_AVAILABLE)
      std::unique_ptr<SkShaper::BiDiRunIterator> bidi = MakeIcuBiDiRunIterator(utf8, utf8Bytes, bidiLevel);
      if (bidi) {
          return bidi;
      }
#endif
    return std::make_unique<SkShaper::TrivialBiDiRunIterator>(bidiLevel, utf8Bytes);
}

std::unique_ptr<SkShaper::ScriptRunIterator>
SkShaper::MakeScriptRunIterator(const char* utf8, size_t utf8Bytes, SkFourByteTag scriptTag) {
#if defined(SK_SHAPER_HARFBUZZ_AVAILABLE) && defined(SK_SHAPER_UNICODE_AVAILABLE)
    std::unique_ptr<SkShaper::ScriptRunIterator> script =
            SkShapers::HB::ScriptRunIterator(utf8, utf8Bytes, scriptTag);
    if (script) {
        return script;
    }
#endif
    return std::make_unique<SkShaper::TrivialScriptRunIterator>(scriptTag, utf8Bytes);
}
#endif  // !defined(SK_DISABLE_LEGACY_SKSHAPER_FUNCTIONS)

SkShaper::SkShaper() {}
SkShaper::~SkShaper() {}

/** Replaces invalid utf-8 sequences with REPLACEMENT CHARACTER U+FFFD. */
static inline SkUnichar utf8_next(const char** ptr, const char* end) {
    SkUnichar val = SkUTF::NextUTF8(ptr, end);
    return val < 0 ? 0xFFFD : val;
}

class FontMgrRunIterator final : public SkShaper::FontRunIterator {
public:
    FontMgrRunIterator(const char* utf8, size_t utf8Bytes,
                       const SkFont& font, sk_sp<SkFontMgr> fallbackMgr,
                       const char* requestName, SkFontStyle requestStyle,
                       const SkShaper::LanguageRunIterator* lang)
        : fCurrent(utf8), fBegin(utf8), fEnd(fCurrent + utf8Bytes)
        , fFallbackMgr(std::move(fallbackMgr))
        , fFont(font)
        , fFallbackFont(fFont)
        , fCurrentFont(nullptr)
        , fRequestName(requestName)
        , fRequestStyle(requestStyle)
        , fLanguage(lang)
    {
        // If fallback is not wanted, clients should use TrivialFontRunIterator.
        SkASSERT(fFallbackMgr);
        fFont.setTypeface(font.refTypeface());
        fFallbackFont.setTypeface(nullptr);
    }
    FontMgrRunIterator(const char* utf8, size_t utf8Bytes,
                       const SkFont& font, sk_sp<SkFontMgr> fallbackMgr)
        : FontMgrRunIterator(utf8, utf8Bytes, font, std::move(fallbackMgr),
                             nullptr, font.getTypeface()->fontStyle(), nullptr)
    {}

    void consume() override {
        SkASSERT(fCurrent < fEnd);
        SkASSERT(!fLanguage || this->endOfCurrentRun() <= fLanguage->endOfCurrentRun());
        SkUnichar u = utf8_next(&fCurrent, fEnd);
        
        SkString primaryTypefaceName;
        if (fFont.getTypeface()) {
            fFont.getTypeface()->getFamilyName(&primaryTypefaceName);
        }
        
        // If the starting typeface can handle this character, use it.
        if (fFont.unicharToGlyph(u)) {
            fCurrentFont = &fFont;
        // If the current fallback can handle this character, use it.
        } else if (fFallbackFont.getTypeface() && fFallbackFont.unicharToGlyph(u)) {
            fCurrentFont = &fFallbackFont;
            SkString fallbackTypefaceName;
            fFallbackFont.getTypeface()->getFamilyName(&fallbackTypefaceName);
        // If not, try to find a fallback typeface
        } else {
            const char* language = fLanguage ? fLanguage->currentLanguage() : nullptr;
            int languageCount = fLanguage ? 1 : 0;
            
            // First try with requestName (SVG font-family)
            sk_sp<SkTypeface> candidate(fFallbackMgr->matchFamilyStyleCharacter(
                fRequestName, fRequestStyle, &language, languageCount, u));
            
            // If that fails, try with nullptr (system default fallback) - like Chromium
            if (!candidate) {
                candidate = fFallbackMgr->matchFamilyStyleCharacter(
                    nullptr, fRequestStyle, &language, languageCount, u);
            }
            
            // If still fails, try without language constraints
            if (!candidate && language) {
                candidate = fFallbackMgr->matchFamilyStyleCharacter(
                    nullptr, fRequestStyle, nullptr, 0, u);
            }
            
            if (candidate) {
                fFallbackFont.setTypeface(std::move(candidate));
                fCurrentFont = &fFallbackFont;
                SkString candidateTypefaceName;
                fFallbackFont.getTypeface()->getFamilyName(&candidateTypefaceName);
            } else {
                // Even if no fallback found, we should still try to render with primary font
                // The glyph might be missing but we don't want to skip the character
                fCurrentFont = &fFont;
            }
        }

        // Continue extending the run with consecutive characters that use the same font
        while (fCurrent < fEnd) {
            const char* prev = fCurrent;
            u = utf8_next(&fCurrent, fEnd);

            // End run if not using initial typeface and initial typeface has this character.
            if (fCurrentFont->getTypeface() != fFont.getTypeface() && fFont.unicharToGlyph(u)) {
                fCurrent = prev;
                return;
            }

            // End run if current typeface does not have this character and some other font does.
            if (!fCurrentFont->unicharToGlyph(u)) {
                SkString currentTypefaceName;
                if (fCurrentFont->getTypeface()) {
                    fCurrentFont->getTypeface()->getFamilyName(&currentTypefaceName);
                }
                const char* language = fLanguage ? fLanguage->currentLanguage() : nullptr;
                int languageCount = fLanguage ? 1 : 0;
                
                // First try with requestName
                sk_sp<SkTypeface> candidate(fFallbackMgr->matchFamilyStyleCharacter(
                    fRequestName, fRequestStyle, &language, languageCount, u));
                
                // If that fails, try with nullptr (system default)
                if (!candidate) {
                    candidate = fFallbackMgr->matchFamilyStyleCharacter(
                        nullptr, fRequestStyle, &language, languageCount, u);
                }
                
                // If still fails, try without language constraints
                if (!candidate && language) {
                    candidate = fFallbackMgr->matchFamilyStyleCharacter(
                        nullptr, fRequestStyle, nullptr, 0, u);
                }
                
                if (candidate) {
                    SkString candidateTypefaceName;
                    candidate->getFamilyName(&candidateTypefaceName);
                    // End the current run before this character, so SkShaper will create a new run
                    // with the new font for this character
                    fCurrent = prev;
                    return;
                } else {
                    // Continue with current font even though it doesn't have the glyph
                    // This allows the character to be rendered (possibly as missing glyph)
                }
            } else {
            }
        }
    }
    size_t endOfCurrentRun() const override {
        size_t end = fCurrent - fBegin;
        SkString fontName;
        if (fCurrentFont && fCurrentFont->getTypeface()) {
            fCurrentFont->getTypeface()->getFamilyName(&fontName);
        }
        return end;
    }
    bool atEnd() const override {
        return fCurrent == fEnd;
    }

    const SkFont& currentFont() const override {
        return *fCurrentFont;
    }

private:
    char const * fCurrent;
    char const * const fBegin;
    char const * const fEnd;
    sk_sp<SkFontMgr> const fFallbackMgr;
    SkFont fFont;
    SkFont fFallbackFont;
    SkFont* fCurrentFont;
    char const * const fRequestName;
    SkFontStyle const fRequestStyle;
    SkShaper::LanguageRunIterator const * const fLanguage;
};

std::unique_ptr<SkShaper::FontRunIterator>
SkShaper::MakeFontMgrRunIterator(const char* utf8, size_t utf8Bytes,
                                 const SkFont& font, sk_sp<SkFontMgr> fallback)
{
    return std::make_unique<FontMgrRunIterator>(utf8, utf8Bytes, font, std::move(fallback));
}

std::unique_ptr<SkShaper::FontRunIterator>
SkShaper::MakeFontMgrRunIterator(const char* utf8, size_t utf8Bytes, const SkFont& font,
                                 sk_sp<SkFontMgr> fallback,
                                 const char* requestName, SkFontStyle requestStyle,
                                 const SkShaper::LanguageRunIterator* language)
{
    return std::make_unique<FontMgrRunIterator>(utf8, utf8Bytes, font, std::move(fallback),
                                                requestName, requestStyle, language);
}

std::unique_ptr<SkShaper::LanguageRunIterator>
SkShaper::MakeStdLanguageRunIterator(const char* utf8, size_t utf8Bytes) {
    return std::make_unique<TrivialLanguageRunIterator>(std::locale().name().c_str(), utf8Bytes);
}

void SkTextBlobBuilderRunHandler::beginLine() {
    fCurrentPosition = fOffset;
    fMaxRunAscent = 0;
    fMaxRunDescent = 0;
    fMaxRunLeading = 0;
}
void SkTextBlobBuilderRunHandler::runInfo(const RunInfo& info) {
    SkFontMetrics metrics;
    info.fFont.getMetrics(&metrics);
    fMaxRunAscent = std::min(fMaxRunAscent, metrics.fAscent);
    fMaxRunDescent = std::max(fMaxRunDescent, metrics.fDescent);
    fMaxRunLeading = std::max(fMaxRunLeading, metrics.fLeading);
}

void SkTextBlobBuilderRunHandler::commitRunInfo() {
    fCurrentPosition.fY -= fMaxRunAscent;
}

SkShaper::RunHandler::Buffer SkTextBlobBuilderRunHandler::runBuffer(const RunInfo& info) {
    int glyphCount = SkTFitsIn<int>(info.glyphCount) ? info.glyphCount : INT_MAX;
    int utf8RangeSize = SkTFitsIn<int>(info.utf8Range.size()) ? info.utf8Range.size() : INT_MAX;

    const auto& runBuffer = fBuilder.allocRunTextPos(info.fFont, glyphCount, utf8RangeSize);
    if (runBuffer.utf8text && fUtf8Text) {
        memcpy(runBuffer.utf8text, fUtf8Text + info.utf8Range.begin(), utf8RangeSize);
    }
    fClusters = runBuffer.clusters;
    fGlyphCount = glyphCount;
    fClusterOffset = info.utf8Range.begin();

    return { runBuffer.glyphs,
             runBuffer.points(),
             nullptr,
             runBuffer.clusters,
             fCurrentPosition };
}

void SkTextBlobBuilderRunHandler::commitRunBuffer(const RunInfo& info) {
    SkASSERT(0 <= fClusterOffset);
    for (int i = 0; i < fGlyphCount; ++i) {
        SkASSERT(fClusters[i] >= (unsigned)fClusterOffset);
        fClusters[i] -= fClusterOffset;
    }
    fCurrentPosition += info.fAdvance;
}
void SkTextBlobBuilderRunHandler::commitLine() {
    fOffset += { 0, fMaxRunDescent + fMaxRunLeading - fMaxRunAscent };
}

sk_sp<SkTextBlob> SkTextBlobBuilderRunHandler::makeBlob() {
    return fBuilder.make();
}
