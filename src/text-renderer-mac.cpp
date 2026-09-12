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

#include "cf-ptr.hpp"
#include "compositor.hpp"
#include "layout.hpp"
#include "text-renderer.hpp"

#include <CoreFoundation/CFArray.h>
#include <CoreFoundation/CFAttributedString.h>
#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/CFCGTypes.h>
#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFSet.h>
#include <CoreFoundation/CFString.h>
#include <CoreGraphics/CGAffineTransform.h>
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGColor.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGFont.h>
#include <CoreGraphics/CGGeometry.h>
#include <CoreGraphics/CGImage.h>
#include <CoreGraphics/CGPath.h>
#include <CoreText/CTFont.h>
#include <CoreText/CTFontDescriptor.h>
#include <CoreText/CTFontManager.h>
#include <CoreText/CTLine.h>
#include <CoreText/CTRun.h>
#include <CoreText/CTStringAttributes.h>
#include <MacTypes.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct row_style {
	CTFontRef font = nullptr;
	double tracking_em = 0;
};

std::string to_utf8(CFStringRef value)
{
	if (!value) {
		return {};
	}
	const CFIndex capacity = CFStringGetMaximumSizeForEncoding(CFStringGetLength(value), kCFStringEncodingUTF8) + 1;
	std::string utf8(static_cast<std::size_t>(capacity), '\0');
	if (!CFStringGetCString(value, utf8.data(), capacity, kCFStringEncodingUTF8)) {
		return {};
	}
	utf8.resize(std::strlen(utf8.c_str()));
	return utf8;
}

CFPtr<CFStringRef> make_cfstring(const std::string &value)
{
	return CFPtr<CFStringRef>(CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(value.data()),
							  static_cast<CFIndex>(value.size()), kCFStringEncodingUTF8,
							  false));
}

CFPtr<CTFontRef> make_font(const std::string &face, const std::string &style, const double point_size)
{
	CFPtr<CFStringRef> font_face = make_cfstring(face);
	if (!font_face) {
		return nullptr;
	}

	CFPtr<CFMutableDictionaryRef> attributes(CFDictionaryCreateMutable(nullptr, 2, &kCFTypeDictionaryKeyCallBacks,
									   &kCFTypeDictionaryValueCallBacks));
	if (!attributes) {
		return nullptr;
	}
	CFDictionarySetValue(attributes.get(), kCTFontFamilyNameAttribute, font_face.get());

	CFPtr<CFStringRef> font_style = make_cfstring(style);
	if (!font_style) {
		return nullptr;
	}
	CFDictionarySetValue(attributes.get(), kCTFontStyleNameAttribute, font_style.get());

	CFPtr<CTFontDescriptorRef> descriptor(CTFontDescriptorCreateWithAttributes(attributes.get()));
	if (!descriptor) {
		return nullptr;
	}
	return CFPtr<CTFontRef>(CTFontCreateWithFontDescriptor(descriptor.get(), point_size, nullptr));
}

CFPtr<CTLineRef> make_line(const std::string &text, const row_style &row)
{
	CFPtr<CFStringRef> string = make_cfstring(text);
	if (!string) {
		return nullptr;
	}

	const double tracking_points = row.tracking_em * CTFontGetSize(row.font);
	CFPtr<CFNumberRef> tracking(CFNumberCreate(nullptr, kCFNumberDoubleType, &tracking_points));
	if (!tracking) {
		return nullptr;
	}

	const void *keys[] = {kCTFontAttributeName, kCTTrackingAttributeName, kCTForegroundColorAttributeName};
	const void *values[] = {row.font, tracking.get()};
	CFPtr<CFDictionaryRef> attributes(CFDictionaryCreate(nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
							     &kCFTypeDictionaryValueCallBacks));
	if (!attributes) {
		return nullptr;
	}

	CFPtr<CFAttributedStringRef> attributed(CFAttributedStringCreate(nullptr, string.get(), attributes.get()));
	if (!attributed) {
		return nullptr;
	}
	return CFPtr<CTLineRef>(CTLineCreateWithAttributedString(attributed.get()));
}

ink_extents measure_line(CTLineRef line)
{
	const CGRect bounds = CTLineGetBoundsWithOptions(line, kCTLineBoundsUseGlyphPathBounds);

	return {
		.width = bounds.size.width,
		.ascent = bounds.origin.y + bounds.size.height,
		.descent = -bounds.origin.y,
		.left = bounds.origin.x,
	};
}

void draw_centered(CGContextRef context, CTLineRef line, const double reference_width, const double baseline_y,
		   const double colon_offset_px = 0)
{
	const ink_extents ink = measure_line(line);
	const double origin_x = (reference_width - ink.width) / 2 - ink.left;

	CFArrayRef runs = CTLineGetGlyphRuns(line);
	const CFIndex run_count = runs ? CFArrayGetCount(runs) : 0;
	for (CFIndex r = 0; r < run_count; ++r) {
		CTRunRef run = static_cast<CTRunRef>(CFArrayGetValueAtIndex(runs, r));
		const CFIndex count = CTRunGetGlyphCount(run);
		if (count < 0) {
			continue;
		}

		CFDictionaryRef attributes = CTRunGetAttributes(run);
		CTFontRef run_font =
			attributes ? static_cast<CTFontRef>(CFDictionaryGetValue(attributes, kCTFontAttributeName))
				   : nullptr;
		if (!run_font) {
			continue;
		}

		const UniChar colon = ':';
		CGGlyph colon_glyph = 0;
		const bool has_colon = CTFontGetGlyphsForCharacters(run_font, &colon, &colon_glyph, 1);

		const auto size = static_cast<std::size_t>(count);
		std::vector<CGGlyph> glyphs(size);
		std::vector<CGPoint> positions(size);
		const CFRange all = CFRangeMake(0, count);
		CTRunGetGlyphs(run, all, glyphs.data());
		CTRunGetPositions(run, all, positions.data());

		for (std::size_t i = 0; i < size; ++i) {
			positions[i].x += origin_x;
			positions[i].y += baseline_y;
			if (colon_offset_px != 0 && has_colon && glyphs[i] == colon_glyph) {
				positions[i].y += colon_offset_px;
			}
		}
		CTFontDrawGlyphs(run_font, glyphs.data(), positions.data(), size, context);
	}
}

class mac_clock : public prepared_clock {
public:
	CFPtr<CTFontRef> caption_font;
	CFPtr<CTFontRef> time_font;
	double caption_tracking_em = 0;
	double time_tracking_em = 0;
	double outline_width_px = 0;
	double caption_outline_width_px = 0;
	clock_frame frame;
	composite_style composite;

	rendered_text render(const clock_strings &clock_strings) const override
	{
		const row_style caption_row = {.font = caption_font.get(), .tracking_em = caption_tracking_em};
		const row_style time_row = {.font = time_font.get(), .tracking_em = time_tracking_em};

		CFPtr<CTLineRef> date_line;
		if (!clock_strings.date.empty()) {
			date_line = make_line(clock_strings.date, caption_row);
			if (!date_line) {
				return {};
			}
		}

		CFPtr<CTLineRef> time_line = make_line(clock_strings.time, time_row);
		if (!time_line) {
			return {};
		}

		CFPtr<CTLineRef> meridiem_line;
		if (!clock_strings.meridiem.empty()) {
			meridiem_line = make_line(clock_strings.meridiem, caption_row);
			if (!meridiem_line) {
				return {};
			}
		}

		std::vector<std::uint8_t> alpha(static_cast<std::size_t>(frame.width) * frame.height, 0);
		CFPtr<CGContextRef> context(CGBitmapContextCreate(alpha.data(), frame.width, frame.height, 8,
								  frame.width, nullptr, kCGImageAlphaOnly));
		if (!context) {
			return {};
		}

		CGContextSetShouldAntialias(context.get(), true);
		CGContextSetShouldSmoothFonts(context.get(), false);
		CGContextSetAllowsFontSubpixelQuantization(context.get(), false);
		CGContextSetTextMatrix(context.get(), CGAffineTransformIdentity);
		CGContextSetGrayFillColor(context.get(), 1, 1);

		std::vector<text_layer> layers;
		const auto add_layer = [&](CTLineRef line, const double baseline_y, const double outline_width_px,
					   const double colon_offset_px = 0) {
			std::fill(alpha.begin(), alpha.end(), 0);
			draw_centered(context.get(), line, frame.reference_width, baseline_y, colon_offset_px);
			text_layer layer = {
				.coverage = {.width = frame.width, .height = frame.height},
				.outline_width_px = outline_width_px,
			};
			layer.coverage.pixels.resize(alpha.size());
			for (std::size_t i = 0; i < alpha.size(); ++i) {
				layer.coverage.pixels[i] = alpha[i] / 255.0f;
			}
			layers.push_back(std::move(layer));
		};

		if (date_line) {
			add_layer(date_line.get(), frame.date_baseline_y, caption_outline_width_px);
		}
		add_layer(time_line.get(), frame.time_baseline_y, outline_width_px, frame.colon_offset_px);
		if (meridiem_line) {
			add_layer(meridiem_line.get(), frame.meridiem_baseline_y, caption_outline_width_px);
		}
		return composite_text(layers, composite);
	}
};

class ct_measurer : public text_measurer {
public:
	explicit ct_measurer(const row_style &row) : row(row) {}

	std::optional<ink_extents> measure(const std::string &text) const override
	{
		CFPtr<CTLineRef> line = make_line(text, row);
		if (!line) {
			return std::nullopt;
		}
		return measure_line(line.get());
	}

private:
	row_style row;
};

std::unique_ptr<prepared_clock> prepare_clock(const clock_style &style)
{
	CFPtr<CTFontRef> probe = make_font(style.font_face, style.font_style, reference_point_size);
	if (!probe) {
		return nullptr;
	}

	const std::array<ink_extents, 10> probe_digits = digit_extents(ct_measurer({.font = probe.get()}));

	const double caption_point_size = solve_point_size(probe_digits, style.caption_ink_height());
	const double time_point_size = solve_point_size(probe_digits, style.time_ink_height());
	if (caption_point_size <= 0 || time_point_size <= 0) {
		return nullptr;
	}

	CFPtr<CTFontRef> caption_font = make_font(style.font_face, style.font_style, caption_point_size);
	CFPtr<CTFontRef> time_font = make_font(style.font_face, style.font_style, time_point_size);
	if (!caption_font || !time_font) {
		return nullptr;
	}

	const ct_measurer caption_measurer({.font = caption_font.get(), .tracking_em = style.caption_tracking_em()});
	const ct_measurer time_measurer({.font = time_font.get(), .tracking_em = style.tracking_em});
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
	if (style.date_format != date_format::none) {
		date_extents = date_reference_extents(caption_measurer, style.date_format);
		if (date_extents->width <= 0) {
			return nullptr;
		}
	}

	auto clock = std::make_unique<mac_clock>();
	clock->caption_font = std::move(caption_font);
	clock->time_font = std::move(time_font);
	clock->caption_tracking_em = style.caption_tracking_em();
	clock->time_tracking_em = style.tracking_em;
	clock->outline_width_px = style.outline_width_px();
	clock->caption_outline_width_px = style.caption_outline_width_px();
	clock->frame = solve_frame(style, date_extents, time_extents, meridiem_extents);
	clock->composite = {.color = style.color, .outline_color = style.outline_color};
	if (style.shadow) {
		clock->composite.shadow = shadow_style{
			.offset = style.shadow_offset_px(),
			.blur = style.shadow_blur_px(),
		};
	}
	return clock;
}

std::vector<std::string> available_font_families()
{
	CFPtr<CFArrayRef> names(CTFontManagerCopyAvailableFontFamilyNames());
	if (!names) {
		return {};
	}

	std::vector<std::string> families;
	for (CFIndex i = 0; i < CFArrayGetCount(names.get()); ++i) {
		std::string name = to_utf8(static_cast<CFStringRef>(CFArrayGetValueAtIndex(names.get(), i)));
		// システム内部用のフォントも除外する
		if (name.empty() || name.front() == '.') {
			continue;
		}
		families.push_back(std::move(name));
	}
	return families;
}

std::vector<std::string> available_font_styles(const std::string &face)
{
	CFPtr<CFStringRef> family_name = make_cfstring(face);
	if (!family_name) {
		return {};
	}

	const void *keys[] = {kCTFontFamilyNameAttribute};
	const void *values[] = {family_name.get()};
	CFPtr<CFDictionaryRef> attributes(CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
							     &kCFTypeDictionaryValueCallBacks));
	if (!attributes) {
		return {};
	}

	CFPtr<CTFontDescriptorRef> descriptor(CTFontDescriptorCreateWithAttributes(attributes.get()));
	CFPtr<CFSetRef> mandatory(CFSetCreate(nullptr, keys, 1, &kCFTypeSetCallBacks));
	if (!descriptor || !mandatory) {
		return {};
	}

	CFPtr<CFArrayRef> matches(CTFontDescriptorCreateMatchingFontDescriptors(descriptor.get(), mandatory.get()));
	if (!matches) {
		return {};
	}

	std::vector<std::string> styles;
	for (CFIndex i = 0; i < CFArrayGetCount(matches.get()); ++i) {
		auto match = static_cast<CTFontDescriptorRef>(CFArrayGetValueAtIndex(matches.get(), i));
		CFPtr<CFStringRef> style(
			static_cast<CFStringRef>(CTFontDescriptorCopyAttribute(match, kCTFontStyleNameAttribute)));
		std::string name = to_utf8(style.get());
		// バリアブルフォントは静的フォントと重複しうる
		if (name.empty() || std::find(styles.begin(), styles.end(), name) != styles.end()) {
			continue;
		}
		styles.push_back(std::move(name));
	}
	return styles;
}

double suggest_colon_offset_ratio(const clock_style &style)
{
	if (style.time_ink_height() <= 0) {
		return 0;
	}
	CFPtr<CTFontRef> probe = make_font(style.font_face, style.font_style, reference_point_size);
	if (!probe) {
		return 0;
	}

	const ct_measurer probe_measurer({.font = probe.get()});
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
