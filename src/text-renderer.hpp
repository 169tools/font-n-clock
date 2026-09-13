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

#include "clock-format.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct clock_style {
	static constexpr int default_size = 50;

	date_format format = date_format::month_day_weekday;
	bool twelve_hour = false;
	std::string font_face;
	std::string font_style;
	double size = default_size;
	double colon_offset_ratio = 0;
	double tracking_em = 0;
	std::uint32_t color = 0xffffffff;
	std::uint32_t outline_width = 0;
	std::uint32_t outline_color = 0xff8c857e;
	bool shadow = false;

	double caption_ink_height() const noexcept { return size * 0.4; }
	double time_ink_height() const noexcept { return size; }
	double caption_tracking_em() const noexcept { return tracking_em * 0.8; }
	double row_spacing() const noexcept { return size * 0.24; }
	double top_margin() const noexcept { return size * 0.36; }
	double bottom_margin() const noexcept { return size * 0.4; }
	double horizontal_margin() const noexcept { return size * 0.38; }
	double outline_width_px() const noexcept { return (outline_width / 2.0) * size / default_size; }
	double caption_outline_width_px() const noexcept { return outline_width_px() * 0.75; }
	double colon_offset_px() const noexcept { return time_ink_height() * colon_offset_ratio; }
	double shadow_offset_px() const noexcept { return time_ink_height() * 0.02; }
	double shadow_blur_px() const noexcept { return time_ink_height() * 0.1; }
};

struct rendered_text {
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::vector<std::uint8_t> pixels;

	bool valid() const noexcept { return width > 0 && height > 0 && !pixels.empty(); }
};

class prepared_clock {
public:
	virtual ~prepared_clock() = default;

	prepared_clock(const prepared_clock &) = delete;
	prepared_clock &operator=(const prepared_clock &) = delete;

	virtual rendered_text render(const clock_strings &clock_strings) const = 0;

protected:
	prepared_clock() = default;
};

std::unique_ptr<prepared_clock> prepare_clock(const clock_style &style);
std::vector<std::string> available_font_families();
std::vector<std::string> available_font_styles(const std::string &family);
double suggest_colon_offset_ratio(const clock_style &style);
