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
#include <CoreGraphics/CGImage.h>
#include <CoreText/CTFont.h>
#include <CoreText/CTFontDescriptor.h>
#include <CoreText/CTLine.h>
#include <CoreText/CTStringAttributes.h>
#include <MacTypes.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

CFPtr<CFStringRef> make_cfstring(const std::string &value)
{
	return CFPtr<CFStringRef>(CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(value.data()),
							  static_cast<CFIndex>(value.size()), kCFStringEncodingUTF8,
							  false));
}

CFPtr<CTFontRef> make_font(const std::string &style, const std::string &face)
{
	CFPtr<CFStringRef> font_face = make_cfstring(style);
	if (!font_face) {
		return {};
	}

	CFPtr<CFMutableDictionaryRef> attributes(CFDictionaryCreateMutable(nullptr, 2, &kCFTypeDictionaryKeyCallBacks,
									   &kCFTypeDictionaryValueCallBacks));
	if (!attributes) {
		return {};
	}
	CFDictionarySetValue(attributes.get(), kCTFontFamilyNameAttribute, font_face.get());

	CFPtr<CFStringRef> font_style = make_cfstring(style);
	if (!font_style) {
		return {};
	}
	CFDictionarySetValue(attributes.get(), kCTFontStyleNameAttribute, font_style.get());

	CFPtr<CTFontDescriptorRef> descriptor(CTFontDescriptorCreateWithAttributes(attributes.get()));
	if (!descriptor) {
		return {};
	}
	double point_size = 96;
	return CFPtr<CTFontRef>(CTFontCreateWithFontDescriptor(descriptor.get(), point_size, nullptr));
}

struct row_style {
	CTFontRef font = nullptr;
	CGColorRef color = nullptr;
};

CFPtr<CGColorRef> make_color(std::uint32_t abgr)
{
	CFPtr<CGColorSpaceRef> space(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
	if (!space) {
		return {};
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
		return {};
	}

	const void *keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
	const void *values[] = {row.font, row.color};
	CFPtr<CFDictionaryRef> attributes(CFDictionaryCreate(nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
							     &kCFTypeDictionaryValueCallBacks));
	if (!attributes) {
		return {};
	}

	CFPtr<CFAttributedStringRef> attributed(CFAttributedStringCreate(nullptr, string.get(), attributes.get()));
	if (!attributed) {
		return {};
	}
	return CFPtr<CTLineRef>(CTLineCreateWithAttributedString(attributed.get()));
}

ink_extents measure(CTLineRef line)
{
	const CGRect bounds = CTLineGetBoundsWithOptions(line, kCTLineBoundsUseGlyphPathBounds);

	ink_extents extents;
	extents.width = bounds.size.width;
	extents.left = bounds.origin.x;
	extents.ascent = bounds.origin.y + bounds.size.height;
	extents.descent = -bounds.origin.y;
	return extents;
};

rendered_text render_text(const clock_style &style)
{
	CFPtr<CTFontRef> font = make_font(style.font_face, style.font_style);
	if (!font) {
		return {};
	}

	CFPtr<CGColorRef> color = make_color(style.color);
	if (!color) {
		return {};
	}

	CFPtr<CTLineRef> line = make_line("12:34", row_style(font.get(), color.get()));
	if (!line) {
		return {};
	}

	rendered_text result;
	ink_extents ink = measure(line.get());
	result.width = static_cast<std::uint32_t>(std::ceil(ink.width));
	result.height = static_cast<std::uint32_t>(std::ceil(ink.height()));
	result.pixels.assign(static_cast<std::size_t>(result.width) * result.height * 4, 0);

	CFPtr<CGColorSpaceRef> space(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
	if (!space) {
		return {};
	}

	const CGBitmapInfo bitmap_info =
		static_cast<CGBitmapInfo>(static_cast<std::uint32_t>(kCGImageAlphaPremultipliedLast) |
					  static_cast<std::uint32_t>(kCGBitmapByteOrder32Big));

	CFPtr<CGContextRef> context(CGBitmapContextCreate(result.pixels.data(), result.width, result.height, 8,
							  static_cast<std::size_t>(result.width) * 4, space.get(),
							  bitmap_info));
	if (!context) {
		return {};
	}

	CGContextSetShouldAntialias(context.get(), true);
	CGContextSetShouldSmoothFonts(context.get(), false);
	CGContextSetTextMatrix(context.get(), CGAffineTransformIdentity);
	CGContextSetTextPosition(context.get(), -ink.left, ink.descent);
	CTLineDraw(line.get(), context.get());

	return result;
}
