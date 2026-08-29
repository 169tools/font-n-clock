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
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <util/base.h>
#include <util/windows/ComPtr.hpp>

#include "layout.hpp"
#include "text-renderer.hpp"

#include <Unknwnbase.h>
#include <WinNls.h>
#include <algorithm>
#include <array>
#include <basetsd.h>
#include <cstddef>
#include <cstdint>
#include <dcommon.h>
#include <dwrite.h>
#include <memory>
#include <minwindef.h>
#include <optional>
#include <string>
#include <stringapiset.h>
#include <vector>
#include <winerror.h>
#include <winnt.h>

constexpr float layout_limit = 1 << 20; // これは何

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

		blog(LOG_INFO, "[win] g=%u upem=%u lsb=%d aw=%u rsb=%d | tsb=%d ah=%u bsb=%d voy=%d", indices[i],
		     font_metrics.designUnitsPerEm, m.leftSideBearing, m.advanceWidth, m.rightSideBearing,
		     m.topSideBearing, m.advanceHeight, m.bottomSideBearing, m.verticalOriginY);

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

class dw_measurer : public text_measurer {
public:
	dw_measurer(IDWriteFactory *factory, IDWriteTextFormat *format, const double em_size)
		: factory(factory),
		  format(format),
		  em_size(em_size)
	{
	}

	std::optional<ink_extents> measure(const std::string &text) const override
	{
		const std::wstring wide = to_wide(text);
		ComPtr<IDWriteTextLayout> layout;
		if (FAILED(factory->CreateTextLayout(wide.c_str(), static_cast<UINT32>(wide.size()), format,
						     layout_limit, layout_limit, &layout))) {
			return std::nullopt;
		}

		glyph_collector collector;
		if (FAILED(layout->Draw(nullptr, &collector, 0, 0))) {
			return std::nullopt;
		}

		const HRESULT hr = layout->Draw(nullptr, &collector, 0, 0);
		blog(LOG_INFO, "[win] measure '%s' hr=0x%08lx glyphs=%zu face=%p", text.c_str(), hr,
		     collector.glyphs.size(), static_cast<void *>(collector.face.Get()));

		return measure_glyphs(collector, em_size);
	}

private:
	IDWriteFactory *factory = nullptr;
	IDWriteTextFormat *format = nullptr;
	double em_size = 0;
};

class win_clock : public prepared_clock {
public:
	clock_frame frame;

	rendered_text render(const clock_content &) const override { return {}; }
};

std::unique_ptr<prepared_clock> prepare_clock(const clock_style &style)
{
	blog(LOG_INFO, "[win] prepare_clock face='%s' style='%s' size=%f", style.font_face.c_str(),
	     style.font_style.c_str(), style.size);

	ComPtr<IDWriteFactory> factory = make_factory();
	if (!factory) {
		blog(LOG_WARNING, "[win] make_factory failed");
		return nullptr;
	}

	ComPtr<IDWriteFont> font = find_font(factory.Get(), style.font_face, style.font_style);
	if (!font) {
		blog(LOG_WARNING, "[win] find_font failed");
		return nullptr;
	}

	ComPtr<IDWriteTextFormat> probe_format =
		make_format(factory.Get(), font.Get(), style.font_face, reference_point_size);
	if (!font) {
		blog(LOG_WARNING, "[win] make_format failed");
		return nullptr;
	}

	const std::array<ink_extents, 10> probe_digits =
		digit_extents(dw_measurer(factory.Get(), probe_format.Get(), reference_point_size));

	const double caption_point_size = solve_point_size(probe_digits, style.caption_ink_height());
	const double time_point_size = solve_point_size(probe_digits, style.time_ink_height());
	if (caption_point_size <= 0 || time_point_size <= 0) {
		blog(LOG_WARNING, "[win] solve_point_size failed");
		return nullptr;
	}

	ComPtr<IDWriteTextFormat> caption_format =
		make_format(factory.Get(), font.Get(), style.font_face, caption_point_size);
	ComPtr<IDWriteTextFormat> time_format =
		make_format(factory.Get(), font.Get(), style.font_face, time_point_size);
	if (!caption_format || !time_format) {
		blog(LOG_WARNING, "[win] make_format failed");
		return nullptr;
	}

	const dw_measurer caption_measurer(factory.Get(), caption_format.Get(), caption_point_size);
	const dw_measurer time_measurer(factory.Get(), time_format.Get(), time_point_size);

	const row_extents time_extents = time_reference_extents(time_measurer, style.twelve_hour);
	if (time_extents.width <= 0) {
		blog(LOG_WARNING, "[win] time_reference_extents failed");
		return nullptr;
	}

	std::optional<row_extents> meridiem_extents;
	if (style.twelve_hour) {
		meridiem_extents = meridiem_reference_extents(caption_measurer);
		if (meridiem_extents->width <= 0) {
			blog(LOG_WARNING, "[win] meridiem_reference_extents failed");
			return nullptr;
		}
	}

	std::optional<row_extents> date_extents;
	if (style.format != date_format::none) {
		date_extents = date_reference_extents(caption_measurer, style.format);
		if (date_extents->width <= 0) {
			blog(LOG_WARNING, "[win] date_reference_extents failed");
			return nullptr;
		}
	}

	auto clock = std::make_unique<win_clock>();
	clock->frame = solve_frame(style, date_extents, time_extents, meridiem_extents);

	blog(LOG_INFO, "[ref] pt=%.6f/%.6f frame=%ux%u ref_w=%.6f date=%.6f time=%.6f mer=%.6f", caption_point_size,
	     time_point_size, clock->frame.width, clock->frame.height, clock->frame.reference_width,
	     clock->frame.date_baseline_y, clock->frame.time_baseline_y, clock->frame.meridiem_baseline_y);

	return clock;
}

double suggest_colon_offset_ratio(const clock_style &style)
{
	if (style.time_ink_height() <= 0) {
		return 0;
	}

	ComPtr<IDWriteFactory> factory = make_factory();
	if (!factory) {
		return 0;
	}
	ComPtr<IDWriteFont> font = find_font(factory.Get(), style.font_face, style.font_style);
	if (!font) {
		return 0;
	}
	ComPtr<IDWriteTextFormat> probe_format =
		make_format(factory.Get(), font.Get(), style.font_style, reference_point_size);
	if (!probe_format) {
		return 0;
	}

	const dw_measurer probe_measurer(factory.Get(), probe_format.Get(), reference_point_size);
	const ink_span digits = digit_envelope(digit_extents(probe_measurer));
	if (digits.height() <= 0) {
		return 0;
	}

	const std::optional<ink_extents> colon_ink = probe_measurer.measure(":");
	if (!colon_ink) {
		return 0;
	}

	const double digit_center = (digits.ascent - digits.descent) / 2;
	const double colon_center = (colon_ink->ascent - colon_ink->descent) / 2;
	return (digit_center - colon_center) / digits.height() - colon_optional_offset_ratio;
}
