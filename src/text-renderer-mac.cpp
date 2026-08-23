/*
font-meets-clock
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
#include "layout.hpp"
#include "text-renderer.hpp"
#include <CoreFoundation/CFArray.h>
#include <CoreFoundation/CFAttributedString.h>
#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/CFCGTypes.h>
#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFString.h>
#include <CoreGraphics/CGAffineTransform.h>
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGColor.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGFont.h>
#include <CoreGraphics/CGImage.h>
#include <CoreText/CTRun.h>
#include <CoreText/CTFont.h>
#include <CoreText/CTFontDescriptor.h>
#include <CoreText/CTLine.h>
#include <CoreText/CTStringAttributes.h>
#include <MacTypes.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct row_style {
	CTFontRef font = nullptr;
	CGColorRef color = nullptr;
};

constexpr double reference_point_size = 100;

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

CFPtr<CGColorRef> make_color(std::uint32_t abgr)
{
	CFPtr<CGColorSpaceRef> space(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
	if (!space) {
		return nullptr;
	}
	const CGFloat components[] = {
		static_cast<CGFloat>(abgr & 0xff) / 255.0,
		static_cast<CGFloat>((abgr >> 8) & 0xff) / 255.0,
		static_cast<CGFloat>((abgr >> 16) & 0xff) / 255.0,
		static_cast<CGFloat>((abgr >> 24) & 0xff) / 255.0,
	};
	return CFPtr<CGColorRef>(CGColorCreate(space.get(), components));
}

CFPtr<CTLineRef> make_line(const std::string &text, const row_style &row)
{
	CFPtr<CFStringRef> string = make_cfstring(text);
	if (!string) {
		return nullptr;
	}

	const void *keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
	const void *values[] = {row.font, row.color};
	CFPtr<CFDictionaryRef> attributes(CFDictionaryCreate(nullptr, keys, values, row.color ? 2 : 1,
							     &kCFTypeDictionaryKeyCallBacks,
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

ink_extents measure(CTLineRef line)
{
	const CGRect bounds = CTLineGetBoundsWithOptions(line, kCTLineBoundsUseGlyphPathBounds);

	return {.width = bounds.size.width,
		.ascent = bounds.origin.y + bounds.size.height,
		.descent = -bounds.origin.y,
		.left = bounds.origin.x};
}

std::array<ink_extents, 10> digit_extents(const row_style &row)
{
	std::array<ink_extents, 10> digits{};
	for (int i = 0; i <= 9; ++i) {
		CFPtr<CTLineRef> line = make_line(std::string(1, static_cast<char>('0' + i)), row);
		if (!line) {
			return {};
		}
		digits[i] = measure(line.get());
	}
	return digits;
}

ink_span digit_envelope(const std::array<ink_extents, 10> &digits)
{
	ink_span envelope;
	for (int i = 0; i <= 9; ++i) {
		const ink_extents extents = digits[i];
		envelope.ascent = std::max(envelope.ascent, extents.ascent);
		envelope.descent = std::max(envelope.descent, extents.descent);
	}
	return envelope;
}

double solve_point_size(const std::array<ink_extents, 10> &digits, const double target_height)
{
	const double reference_height = digit_envelope(digits).height();
	if (reference_height <= 0) {
		return 0;
	}
	return reference_point_size * target_height / reference_height;
}

row_extents date_reference_extents(const row_style &row, const date_format format)
{
	row_extents max_extents;
	int widest_month = 0;
	int widest_day = 0;

	double widest = 0;
	for (int month = 1; month <= 12; ++month) {
		for (int day = 1; day <= 31; ++day) {
			CFPtr<CTLineRef> line = make_line(format_date(format, month, day, 0), row);
			if (!line) {
				return {};
			}
			const ink_extents extents = measure(line.get());
			max_extents.width = std::max(max_extents.width, measure(line.get()).width);
			if (extents.width > widest) {
				widest = extents.width;
				widest_month = month;
				widest_day = day;
			}
		}
	}
	if (widest_month == 0) {
		return {};
	}

	for (int weekday = 1; weekday < 7; ++weekday) {
		CFPtr<CTLineRef> line = make_line(format_date(format, widest_month, widest_day, weekday), row);
		if (!line) {
			return {};
		}
		max_extents.width = std::max(max_extents.width, measure(line.get()).width);
	}

	std::string height_glyphs = "0123456789";
	for (const char *weekday : weekday_names) {
		height_glyphs += weekday;
	}
	for (const char *month : month_names) {
		height_glyphs += month;
	}

	CFPtr<CTLineRef> line = make_line(height_glyphs, row);
	if (!line) {
		return {};
	}
	const ink_extents extents = measure(line.get());
	max_extents.ascent = extents.ascent;
	max_extents.descent = extents.descent;
	return max_extents;
}

row_extents time_reference_extents(const row_style &row)
{
	row_extents max_extents;
	for (int hour = 0; hour < 24; ++hour) {
		for (int minute = 0; minute < 60; ++minute) {
			char text[6];
			std::snprintf(text, sizeof text, "%d:%02d", hour, minute);

			CFPtr<CTLineRef> line = make_line(text, row);
			if (!line) {
				return {};
			}
			max_extents.extend(measure(line.get()));
		}
	}
	return max_extents;
}

void draw_centered(CGContextRef context, CTLineRef line, const double reference_width, const double baseline_y,
		   const double colon_offset_px = 0)
{
	const ink_extents ink = measure(line);
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
	CFPtr<CTFontRef> date_font;
	CFPtr<CTFontRef> time_font;
	CFPtr<CGColorRef> color;
	CFPtr<CGColorSpaceRef> space;
	clock_frame frame;

	rendered_text render(const clock_content &content) const override
	{
		CFPtr<CTLineRef> date_line = make_line(content.date, {.font = date_font.get(), .color = color.get()});
		CFPtr<CTLineRef> time_line = make_line(content.time, {.font = time_font.get(), .color = color.get()});
		if (!date_line || !time_line) {
			return {};
		}

		rendered_text result = {.width = frame.width, .height = frame.height};
		result.pixels.assign(static_cast<std::size_t>(result.width) * result.height * 4, 0);

		const CGBitmapInfo bitmap_info =
			static_cast<CGBitmapInfo>(static_cast<std::uint32_t>(kCGImageAlphaPremultipliedLast) |
						  static_cast<std::uint32_t>(kCGBitmapByteOrder32Big));
		CFPtr<CGContextRef> context(CGBitmapContextCreate(result.pixels.data(), result.width, result.height, 8,
								  static_cast<std::size_t>(result.width) * 4,
								  space.get(), bitmap_info));
		if (!context) {
			return {};
		}

		CGContextSetShouldAntialias(context.get(), true);
		CGContextSetShouldSmoothFonts(context.get(), false);
		CGContextSetTextMatrix(context.get(), CGAffineTransformIdentity);
		CGContextSetFillColorWithColor(context.get(), color.get());

		draw_centered(context.get(), date_line.get(), frame.reference_width, frame.date_baseline_y);
		draw_centered(context.get(), time_line.get(), frame.reference_width, frame.time_baseline_y,
			      frame.colon_offset_px);

		return result;
	}
};

std::unique_ptr<prepared_clock> prepare_clock(const clock_style &style)
{
	CFPtr<CTFontRef> probe = make_font(style.font_face, style.font_style, reference_point_size);
	if (!probe) {
		return nullptr;
	}

	const std::array<ink_extents, 10> probe_digits = digit_extents({.font = probe.get()});

	const double date_point_size = solve_point_size(probe_digits, style.date_ink_height());
	const double time_point_size = solve_point_size(probe_digits, style.time_ink_height());
	if (date_point_size <= 0 || time_point_size <= 0) {
		return nullptr;
	}

	CFPtr<CTFontRef> date_font = make_font(style.font_face, style.font_style, date_point_size);
	CFPtr<CTFontRef> time_font = make_font(style.font_face, style.font_style, time_point_size);
	CFPtr<CGColorRef> color = make_color(style.color);
	CFPtr<CGColorSpaceRef> space(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
	if (!date_font || !time_font || !color || !space) {
		return nullptr;
	}

	row_extents date_extents = date_reference_extents({.font = date_font.get()}, style.format);
	row_extents time_extents = time_reference_extents({.font = time_font.get()});
	if (date_extents.width <= 0 || time_extents.width <= 0) {
		return nullptr;
	}
	const double reference_width = std::max(date_extents.width, time_extents.width) + style.margin() * 2;
	const double time_baseline_y = time_extents.descent;
	const double date_baseline_y =
		time_baseline_y + time_extents.ascent + style.date_and_time_spacing() + date_extents.descent;
	const double reference_height = date_baseline_y + date_extents.ascent + style.margin() * 2;

	auto clock = std::make_unique<mac_clock>();

	clock->date_font = std::move(date_font);
	clock->time_font = std::move(time_font);
	clock->color = std::move(color);
	clock->space = std::move(space);
	clock->frame = {.width = static_cast<std::uint32_t>(std::ceil(reference_width)),
			.height = static_cast<std::uint32_t>(std::ceil(reference_height)),
			.reference_width = reference_width,
			.date_baseline_y = date_baseline_y + style.margin(),
			.time_baseline_y = time_baseline_y + style.margin(),
			.colon_offset_px = style.colon_offset_px()};
	return clock;
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

	const row_style row = {.font = probe.get()};

	const ink_span digits = digit_envelope(digit_extents(row));
	if (digits.height() <= 0) {
		return 0;
	}

	CFPtr<CTLineRef> colon = make_line(":", row);
	if (!colon) {
		return 0;
	}
	const ink_extents colon_ink = measure(colon.get());

	const double digit_center = (digits.ascent - digits.descent) / 2;
	const double colon_center = (colon_ink.ascent - colon_ink.descent) / 2;
	return (digit_center - colon_center) / digits.height() - colon_optional_offset_ratio;
}
