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
#include <util/windows/ComPtr.hpp>

#include "text-renderer.hpp"

#include <Unknwnbase.h>
#include <WinNls.h>
#include <basetsd.h>
#include <cstddef>
#include <dwrite.h>
#include <memory>
#include <minwindef.h>
#include <string>
#include <stringapiset.h>
#include <winerror.h>

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

class glyph_collector : public IDWriteTextRenderer {};

std::unique_ptr<prepared_clock> prepare_clock(const clock_style &style)
{
	return nullptr;
}

double suggest_colon_offset_ratio(const clock_style &style)
{
	return 0;
}
