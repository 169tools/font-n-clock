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

CFStringRef make_cfstring(const std::string &value)
{
	// TODO: Create 後の解放を行う（他の箇所も同じ）
	return CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(value.data()),
				       static_cast<CFIndex>(value.size()), kCFStringEncodingUTF8, false);
}

CTFontRef make_font()
{
	CFStringRef face = make_cfstring("Tsukushi B Round Gothic");
	CFMutableDictionaryRef attributes(CFDictionaryCreateMutable(nullptr, 2, &kCFTypeDictionaryKeyCallBacks,
								    &kCFTypeDictionaryValueCallBacks));
	CFDictionarySetValue(attributes, kCTFontFamilyNameAttribute, face);

	CFStringRef style = make_cfstring("Bold");
	CFDictionarySetValue(attributes, kCTFontStyleNameAttribute, style);

	CTFontDescriptorRef descriptor(CTFontDescriptorCreateWithAttributes(attributes));
	double point_size = 96;
	return CTFontCreateWithFontDescriptor(descriptor, point_size, nullptr);
}

struct row_style {
	CTFontRef font = nullptr;
	CGColorRef color = nullptr;
};

CGColorRef make_color(std::uint32_t rgba)
{
	CGColorSpaceRef space(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
	const CGFloat components[] = {
		static_cast<CGFloat>(rgba & 0xff) / 255.0,
		static_cast<CGFloat>((rgba >> 8) & 0xff) / 255.0,
		static_cast<CGFloat>((rgba >> 16) & 0xff) / 255.0,
		static_cast<CGFloat>((rgba >> 24) & 0xff) / 255.0,
	};
	return CGColorRef(CGColorCreate(space, components));
}

CTLineRef make_line(const std::string &text, const row_style &row)
{
	CFStringRef string = make_cfstring(text);
	const void *keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
	const void *values[] = {row.font, row.color};
	CFDictionaryRef attributes(CFDictionaryCreate(nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
						      &kCFTypeDictionaryValueCallBacks));
	CFAttributedStringRef attributed(CFAttributedStringCreate(nullptr, string, attributes));
	return CTLineRef(CTLineCreateWithAttributedString(attributed));
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

rendered_text render_text()
{
	CTFontRef font = make_font();
	CGColorRef color = make_color(0xffaaa500);
	CTLineRef line = make_line("12:34", row_style(font, color));

	rendered_text result;
	ink_extents ink = measure(line);
	result.width = static_cast<std::uint32_t>(std::ceil(ink.width));
	result.height = static_cast<std::uint32_t>(std::ceil(ink.height()));
	result.pixels.assign(static_cast<std::size_t>(result.width) * result.height * 4, 0);

	CGColorSpaceRef space(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
	const CGBitmapInfo bitmap_info =
		static_cast<CGBitmapInfo>(static_cast<std::uint32_t>(kCGImageAlphaPremultipliedLast) |
					  static_cast<std::uint32_t>(kCGBitmapByteOrder32Big));

	CGContextRef context(CGBitmapContextCreate(result.pixels.data(), result.width, result.height, 8,
						   static_cast<std::size_t>(result.width) * 4, space, bitmap_info));

	CGContextSetShouldAntialias(context, true);
	CGContextSetShouldSmoothFonts(context, false);
	CGContextSetTextMatrix(context, CGAffineTransformIdentity);
	CGContextSetTextPosition(context, -ink.left, ink.descent);
	CTLineDraw(line, context);

	return result;
}
