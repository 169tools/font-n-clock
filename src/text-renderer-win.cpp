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

#include <cmath>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <util/base.h>
#include <util/windows/ComPtr.hpp>

#include "layout.hpp"
#include "text-renderer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <dwrite.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
	std::wstring wide(static_cast<std::size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), wide.data(), length);
	return wide;
}

ComPtr<IDWriteFactory> make_factory()
{
	ComPtr<IDWriteFactory> factory;
	if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
				       reinterpret_cast<IUnknown **>(&factory)))) {
		return nullptr;
	}
	return factory;
}

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
		std::wstring name(static_cast<std::size_t>(length) + 1, L'\0');
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

struct glyph_run {
	ComPtr<IDWriteFontFace> face;
	float em_size = 0;
	float baseline_x = 0;
	std::vector<std::uint16_t> indices;
	std::vector<float> advances;
	std::vector<DWRITE_GLYPH_OFFSET> offsets;
};

class glyph_collector : public IDWriteTextRenderer {
public:
	std::vector<glyph_run> runs;

	HRESULT STDMETHODCALLTYPE DrawGlyphRun(void *, FLOAT baseline_x, FLOAT, DWRITE_MEASURING_MODE,
					       const DWRITE_GLYPH_RUN *run, const DWRITE_GLYPH_RUN_DESCRIPTION *,
					       IUnknown *) noexcept override
	{
		glyph_run collected;
		collected.face = run->fontFace;
		collected.em_size = run->fontEmSize;
		collected.baseline_x = baseline_x;
		collected.indices.assign(run->glyphIndices, run->glyphIndices + run->glyphCount);
		collected.advances.assign(run->glyphAdvances, run->glyphAdvances + run->glyphCount);
		if (run->glyphOffsets) {
			collected.offsets.assign(run->glyphOffsets, run->glyphOffsets + run->glyphCount);
		} else {
			collected.offsets.resize(run->glyphCount);
		}
		runs.push_back(std::move(collected));
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DrawUnderline(void *, FLOAT, FLOAT, const DWRITE_UNDERLINE *,
						IUnknown *) noexcept override
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
		*matrix = {1, 0, 0, 1, 0, 0};
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

std::optional<ink_extents> measure_glyphs(const glyph_collector &collected)
{
	double min_x = 0;
	double max_x = 0;
	double min_y = 0;
	double max_y = 0;
	bool any = false;

	for (const glyph_run &run : collected.runs) {
		if (run.indices.empty() || !run.face) {
			continue;
		}

		std::vector<DWRITE_GLYPH_METRICS> metrics(run.indices.size());
		if (FAILED(run.face->GetDesignGlyphMetrics(run.indices.data(), static_cast<UINT32>(run.indices.size()),
							   metrics.data(), FALSE))) {
			return std::nullopt;
		}

		DWRITE_FONT_METRICS font_metrics = {};
		run.face->GetMetrics(&font_metrics);
		if (font_metrics.designUnitsPerEm == 0) {
			return std::nullopt;
		}
		const double scale = run.em_size / font_metrics.designUnitsPerEm;

		double cursor = run.baseline_x;
		for (std::size_t i = 0; i < run.indices.size(); ++i) {
			const DWRITE_GLYPH_METRICS &m = metrics[i];
			const double origin_x = cursor + run.offsets[i].advanceOffset;
			const double origin_y = run.offsets[i].ascenderOffset;
			cursor += run.advances[i];

			const double left = m.leftSideBearing;
			const double right = static_cast<double>(m.advanceWidth) - m.rightSideBearing;
			const double top = static_cast<double>(m.verticalOriginY) - m.topSideBearing;
			const double bottom =
				m.verticalOriginY - (static_cast<double>(m.advanceHeight) - m.bottomSideBearing);
			if (right <= left && top <= bottom) {
				continue;
			}

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
				max_y = std::max(max_y, t);
			}
		}
	}

	if (!any) {
		return ink_extents{};
	}
	return ink_extents{.width = max_x - min_x, .ascent = max_y, .descent = -min_y, .left = min_x};
}

bool collect_glyphs(IDWriteFactory *factory, IDWriteTextFormat *format, const std::string &text,
		    glyph_collector &collector)
{
	const std::wstring wide = to_wide(text);
	ComPtr<IDWriteTextLayout> layout;
	if (FAILED(factory->CreateTextLayout(wide.c_str(), static_cast<UINT32>(wide.size()), format, layout_limit,
					     layout_limit, &layout))) {
		return false;
	}
	return SUCCEEDED(layout->Draw(nullptr, &collector, 0, 0));
}

class dw_measurer : public text_measurer {
public:
	dw_measurer(IDWriteFactory *factory, IDWriteTextFormat *format) : factory(factory), format(format) {}

	std::optional<ink_extents> measure(const std::string &text) const override
	{
		glyph_collector collector;
		if (!collect_glyphs(factory, format, text, collector)) {
			return std::nullopt;
		}
		return measure_glyphs(collector);
	}

private:
	IDWriteFactory *factory = nullptr;
	IDWriteTextFormat *format = nullptr;
};

class win_clock : public prepared_clock {
public:
	ComPtr<IDWriteFactory> factory;
	ComPtr<IDWriteTextFormat> caption_format;
	ComPtr<IDWriteTextFormat> time_format;
	double color_rgb[3] = {1, 1, 1};
	double color_alpha = 1;
	clock_frame frame;

	rendered_text render(const clock_content &content) const override
	{
		rendered_text result = {.width = frame.width, .height = frame.height};
		result.pixels.assign(static_cast<std::size_t>(result.width) * result.height * 4, 0);

		if (!content.date.empty() &&
		    !draw_centered(result, content.date, caption_format.Get(), frame.date_baseline_y)) {
			return {};
		}
		if (!draw_centered(result, content.time, time_format.Get(), frame.time_baseline_y)) {
			return {};
		}
		if (!content.meridiem.empty() &&
		    !draw_centered(result, content.meridiem, caption_format.Get(), frame.meridiem_baseline_y)) {
			return {};
		}
		return result;
	}

private:
	bool draw_centered(rendered_text &target, const std::string &text, IDWriteTextFormat *format,
			   const double baseline_y) const
	{
		glyph_collector collector;
		if (!collect_glyphs(factory.Get(), format, text, collector)) {
			return false;
		}

		const std::optional<ink_extents> ink = measure_glyphs(collector);
		if (!ink) {
			return false;
		}

		const double origin_x = (frame.reference_width - ink->width) / 2 - ink->left;
		const double device_y = static_cast<double>(target.height) - baseline_y;

		for (const glyph_run &run : collector.runs) {
			if (run.indices.empty() || !run.face) {
				continue;
			}
			if (!draw_run(target, run, origin_x, device_y)) {
				return false;
			}
		}

		return true;
	}

	bool draw_run(rendered_text &target, const glyph_run &run, const double origin_x, const double device_y) const
	{
		DWRITE_GLYPH_RUN dwrite_run = {};
		dwrite_run.fontFace = run.face.Get();
		dwrite_run.fontEmSize = run.em_size;
		dwrite_run.glyphCount = static_cast<UINT32>(run.indices.size());
		dwrite_run.glyphIndices = run.indices.data();
		dwrite_run.glyphAdvances = run.advances.data();
		dwrite_run.glyphOffsets = run.offsets.data();

		ComPtr<IDWriteGlyphRunAnalysis> analysis;
		if (FAILED(factory->CreateGlyphRunAnalysis(
			    &dwrite_run, 1.0f, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
			    DWRITE_MEASURING_MODE_NATURAL, static_cast<FLOAT>(run.baseline_x + origin_x),
			    static_cast<FLOAT>(device_y), &analysis))) {
			return false;
		}

		RECT bounds = {};
		if (FAILED(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds))) {
			return false;
		}

		const long texture_width = bounds.right - bounds.left;
		const long texture_height = bounds.bottom - bounds.top;
		if (texture_width <= 0 || texture_height <= 0) {
			return true;
		}

		std::vector<std::uint8_t> alpha(static_cast<std::size_t>(texture_width) * texture_height * 3);
		if (FAILED(analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, alpha.data(),
							static_cast<UINT32>(alpha.size())))) {
			return false;
		}

		for (long y = 0; y < texture_height; ++y) {
			const long target_y = bounds.top + y;
			if (target_y < 0 || target_y >= static_cast<long>(target.height)) {
				continue;
			}
			for (long x = 0; x < texture_width; ++x) {
				const long target_x = bounds.left + x;
				if (target_x < 0 || target_x >= static_cast<long>(target.width)) {
					continue;
				}

				const std::size_t source = (static_cast<std::size_t>(y) * texture_width + x) * 3;
				const double coverage =
					(alpha[source] + alpha[source + 1] + alpha[source + 2]) / (3.0 * 255.0);
				if (coverage <= 0) {
					continue;
				}
				blend_pixel(target, target_x, target_y, coverage);
			}
		}
		return true;
	}

	void blend_pixel(rendered_text &target, const long x, const long y, const double coverage) const
	{
		const double a = coverage * color_alpha;
		const double inverse = 1.0 - a;
		const std::size_t pixel = (static_cast<std::size_t>(y) * target.width + x) * 4;

		for (int channel = 0; channel < 3; ++channel) {
			const double blended =
				color_rgb[channel] * a + target.pixels[pixel + channel] / 255.0 * inverse;
			target.pixels[pixel + channel] =
				static_cast<std::uint8_t>(std::clamp(std::lround(blended * 255), 0L, 255L));
		}
		const double blended_alpha = a + target.pixels[pixel + 3] / 255.0 * inverse;
		target.pixels[pixel + 3] =
			static_cast<std::uint8_t>(std::clamp(std::lround(blended_alpha * 255), 0L, 255L));
	}
};

std::unique_ptr<prepared_clock> prepare_clock(const clock_style &style)
{
	ComPtr<IDWriteFactory> factory = make_factory();
	if (!factory) {
		return nullptr;
	}

	ComPtr<IDWriteFont> font = find_font(factory.Get(), style.font_face, style.font_style);
	if (!font) {
		return nullptr;
	}

	ComPtr<IDWriteTextFormat> probe_format =
		make_format(factory.Get(), font.Get(), style.font_face, reference_point_size);
	if (!probe_format) {
		return nullptr;
	}

	const std::array<ink_extents, 10> probe_digits = digit_extents(dw_measurer(factory.Get(), probe_format.Get()));

	const double caption_point_size = solve_point_size(probe_digits, style.caption_ink_height());
	const double time_point_size = solve_point_size(probe_digits, style.time_ink_height());
	if (caption_point_size <= 0 || time_point_size <= 0) {
		return nullptr;
	}

	ComPtr<IDWriteTextFormat> caption_format =
		make_format(factory.Get(), font.Get(), style.font_face, caption_point_size);
	ComPtr<IDWriteTextFormat> time_format =
		make_format(factory.Get(), font.Get(), style.font_face, time_point_size);
	if (!caption_format || !time_format) {
		return nullptr;
	}

	const dw_measurer caption_measurer(factory.Get(), caption_format.Get());
	const dw_measurer time_measurer(factory.Get(), time_format.Get());

	const row_extents time_extents = time_reference_extents(time_measurer, style.twelve_hour);
	if (time_extents.width <= 0) {
		return nullptr;
	}

	std::optional<row_extents> meridiem_extents;
	if (style.twelve_hour) {
		meridiem_extents = meridiem_reference_extents(caption_measurer);
		if (meridiem_extents->width <= 0) {
			return nullptr;
		}
	}

	std::optional<row_extents> date_extents;
	if (style.format != date_format::none) {
		date_extents = date_reference_extents(caption_measurer, style.format);
		if (date_extents->width <= 0) {
			return nullptr;
		}
	}

	auto clock = std::make_unique<win_clock>();
	clock->factory = std::move(factory);
	clock->caption_format = std::move(caption_format);
	clock->time_format = std::move(time_format);
	clock->color_rgb[0] = static_cast<double>(style.color & 0xff) / 255.0;
	clock->color_rgb[1] = static_cast<double>((style.color >> 8) & 0xff) / 255.0;
	clock->color_rgb[2] = static_cast<double>((style.color >> 16) & 0xff) / 255.0;
	clock->color_alpha = static_cast<double>((style.color >> 24) & 0xff) / 255.0;
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
		make_format(factory.Get(), font.Get(), style.font_face, reference_point_size);
	if (!probe_format) {
		return 0;
	}

	const dw_measurer probe_measurer(factory.Get(), probe_format.Get());
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
