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

#pragma once

#include "layout.hpp"
#include "text-renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

struct text_coverage {
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::vector<float> pixels;

	bool valid() const noexcept
	{
		return width > 0 && height > 0 && pixels.size() == static_cast<std::size_t>(width) * height;
	}
};

struct text_layer {
	text_coverage coverage;
	double outline_width_px = 0;
};

struct composite_style {
	std::uint32_t color = 0xffffffff;
	std::uint32_t outline_color = 0xff8c857e;
	std::optional<shadow_style> shadow;
};

rendered_text composite_text(const std::vector<text_layer> &layers, const composite_style &style);
