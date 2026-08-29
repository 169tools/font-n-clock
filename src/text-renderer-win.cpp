/*
Font-n-Clock
Copyright (C) 2026 mizznoff <mizznoff@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "layout.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <util/windows/ComPtr.hpp>

#include "text-renderer.hpp"

#include <Unknwnbase.h>
#include <WinNls.h>
#include <basetsd.h>
#include <cstddef>
#include <dcommon.h>
#include <dwrite.h>
#include <memory>
#include <minwindef.h>
#include <string>
#include <stringapiset.h>
#include <winerror.h>
#include <winnt.h>

constexpr float layout_limit = 1 << 20;

std::wstring to_wide(const std::string &value)
{
	if (value.empty()) {
		return {};
	}
	const int length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
	if (length <= 0) {
		return {};
	}
	// L は何？
	std::wstring wide(static_cast<std::size_t>(length), L'0');
	MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), wide.data(), length);
	return wide;
}

// factory の役割は？
ComPtr<IDWriteFactory> make_factory()
{
	ComPtr<IDWriteFactory> factory;
	if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
				       reinterpret_cast<IUnknown **>(&factory)))) {
		return nullptr;
	}
	return factory;
}

// この関数はなんでこんな回りくどいことやってるの
bool matches_face_name(IDWriteFont *font, const std::wstring &style)
{
	ComPtr<IDWriteLocalizedStrings> names;
	if (FAILED(font->GetFaceNames(&names))) {
		return false;
	}

	for (UINT32 i = 0; i < names->GetCount(); ++i) {
		UINT32 length = 0;
		if (FAILED(names->GetStringLength(i, &length))) {
			continue;
		}
		std::wstring name(static_cast<std::size_t>(length) + 1, L'0');
		if (FAILED(names->GetString(i, name.data(), length + 1))) {
			continue;
		}
		name.resize(length);
		if (name == style) {
			return true;
		}
	}
	return false;
}

ComPtr<IDWriteFont> find_font(IDWriteFactory *factory, const std::string &face, const std::string &style)
{
	ComPtr<IDWriteFontCollection> collection;
	if (FAILED(factory->GetSystemFontCollection(&collection))) {
		return nullptr;
	}

	UINT32 index = 0;
	BOOL exists = FALSE;
	if (FAILED(collection->FindFamilyName(to_wide(face).c_str(), &index, &exists)) || !exists) {
		return nullptr;
	}

	ComPtr<IDWriteFontFamily> family;
	if (FAILED(collection->GetFontFamily(index, &family))) {
		return nullptr;
	}

	const std::wstring wanted = to_wide(style);
	for (UINT32 i = 0; i < family->GetFontCount(); ++i) {
		ComPtr<IDWriteFont> font;
		if (SUCCEEDED(family->GetFont(i, &font)) && matches_face_name(font.Get(), wanted)) {
			return font;
		}
	}

	ComPtr<IDWriteFont> fallback;
	if (FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
						DWRITE_FONT_STYLE_NORMAL, &fallback))) {
		return nullptr;
	}
	return fallback;
}

ComPtr<IDWriteTextFormat> make_format(IDWriteFactory *factory, IDWriteFont *font, const std::string &face,
				      const double point_size)
{
	ComPtr<IDWriteTextFormat> format;
	if (FAILED(factory->CreateTextFormat(to_wide(face).c_str(), nullptr, font->GetWeight(), font->GetStyle(),
					     font->GetStretch(), static_cast<FLOAT>(point_size), L"en-us", &format))) {
		return nullptr;
	}
	format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
	return format;
}

struct glyph_position {
	std::uint16_t index = 0; // ここを UINT16 にしないのは意図的？
	double x = 0;
	double y = 0;
};

class glyph_collector : public IDWriteTextRenderer {
public:
	std::vector<glyph_position> glyphs;
	ComPtr<IDWriteFontFace> face;

	HRESULT STDMETHODCALLTYPE DrawGlyphRun(void *, FLOAT baseline_x, FLOAT baseline_y, DWRITE_MEASURING_MODE,
					       const DWRITE_GLYPH_RUN *run, const DWRITE_GLYPH_RUN_DESCRIPTION *,
					       IUnknown *) noexcept override
	{
		face = run->fontFace;
		double cursor = baseline_x;
		for (UINT32 i = 0; i < run->glyphCount; ++i) {
			const DWRITE_GLYPH_OFFSET offset = run->glyphOffsets ? run->glyphOffsets[i]
									     : DWRITE_GLYPH_OFFSET{};
			glyphs.push_back({
				.index = run->glyphIndices[i],
				.x = cursor + offset.advanceOffset,
				.y = baseline_y - offset.ascenderOffset,
			});
			cursor += run->glyphAdvances[i];
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE
	DrawUnderline(void *, FLOAT, FLOAT, const DWRITE_UNDERLINE *,
		      IUnknown *) noexcept override // OBS のフォントセレクタに underline があるのってこれ由来？
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE DrawStrikethrough(void *, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH *,
						    IUnknown *) noexcept override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE DrawInlineObject(void *, FLOAT, FLOAT, IDWriteInlineObject *, BOOL, BOOL,
						   IUnknown *) noexcept override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void *, BOOL *disabled) noexcept override
	{
		*disabled = true;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetCurrentTransform(void *, DWRITE_MATRIX *matrix) noexcept override
	{
		*matrix = {1, 0, 0, 1, 0, 0}; // これは何
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void *, FLOAT *pixels_per_dip) noexcept override
	{
		*pixels_per_dip = 1.0f;
		return S_OK;
	}

	ULONG STDMETHODCALLTYPE AddRef() noexcept override { return 1; }
	ULONG STDMETHODCALLTYPE Release() noexcept override { return 1; }

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) noexcept override
	{
		if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWritePixelSnapping) ||
		    iid == __uuidof(IDWriteTextRenderer)) {
			*object = this;
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}
};

ink_extents measure_glyphs(const glyph_collector &collected, const double em_size)
{
	if (collected.glyphs.empty() || !collected.face) {
		return {};
	}

	std::vector<std::uint16_t> indices;
	indices.reserve(collected.glyphs.size()); // size が分かっているなら array でなく vector にする意味は？
	for (const glyph_position &glyph : collected.glyphs) {
		indices.push_back(glyph.index);
	}

	std::vector<DWRITE_GLYPH_METRICS> metrics(indices.size());
	if (FAILED(collected.face->GetDesignGlyphMetrics(indices.data(), static_cast<UINT32>(indices.size()),
							 metrics.data(), FALSE))) {
		return {};
	}

	DWRITE_FONT_METRICS font_metrics = {};
	collected.face->GetMetrics(&font_metrics);
	if (font_metrics.designUnitsPerEm == 0) {
		return {};
	}
	const double scale = em_size / font_metrics.designUnitsPerEm;

	double min_x = 0;
	double max_x = 0;
	double min_y = 0;
	double max_y = 0;
	bool any = false;

	for (std::size_t i = 0; i < indices.size(); ++i) {
		const DWRITE_GLYPH_METRICS &m = metrics[i];
		const double left = m.leftSideBearing;
		const double right = m.advanceWidth - m.rightSideBearing; // cast 不要では？
		const double top = m.verticalOriginY - m.topSideBearing;
		const double bottom = m.verticalOriginY - m.advanceHeight - m.bottomSideBearing;
		if (right <= left && top <= bottom) {
			continue;
		}

		const double origin_x = collected.glyphs[i].x;
		const double origin_y = collected.glyphs[i].y;
		const double l = origin_x + left * scale;
		const double r = origin_x + right * scale;
		const double b = origin_y + bottom * scale;
		const double t = origin_y + top * scale;

		if (!any) {
			min_x = l;
			max_x = r;
			min_y = b;
			max_y = t;
			any = true;
		} else {
			min_x = std::min(min_x, l);
			max_x = std::max(max_x, r);
			min_y = std::min(min_y, b);
			max_y = std::max(max_x, t);
		}
	}
	if (!any) {
		return {};
	}

	return {.width = max_x - min_x, .ascent = max_y, .descent = -min_y, .left = min_x};
}

std::unique_ptr<prepared_clock> prepare_clock(const clock_style &style)
{
	return nullptr;
}

double suggest_colon_offset_ratio(const clock_style &style)
{
	return 0;
}
