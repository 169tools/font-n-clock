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

#include "text-renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

inline constexpr double reference_point_size = 100;
inline constexpr double colon_optional_offset_ratio = 0.02;
inline constexpr double negative_infinity = -std::numeric_limits<double>::infinity();

struct ink_extents {
	double width = 0;
	double ascent = 0;
	double descent = 0;
	double left = 0;

	double height() const { return ascent + descent; }
};

struct ink_span {
	double ascent = negative_infinity;
	double descent = negative_infinity;

	double height() const { return ascent + descent; }
};

struct row_extents {
	double width = 0;
	double ascent = negative_infinity;
	double descent = negative_infinity;

	double height() const { return ascent + descent; }

	void extend(const ink_extents &extents)
	{
		width = std::max(width, extents.width);
		ascent = std::max(ascent, extents.ascent);
		descent = std::max(descent, extents.descent);
	}
};

struct clock_frame {
	std::uint32_t width = 0;
	std::uint32_t height = 0;

	double reference_width = 0;
	double date_baseline_y = 0;
	double time_baseline_y = 0;
	double meridiem_baseline_y = 0;
	double colon_offset_px = 0;
};

struct shadow_style {
	static constexpr double opacity = 0.5;
	double offset = 0;
	double blur = 0;
};

class text_measurer {
public:
	virtual ~text_measurer() = default;
	virtual ink_extents measure(const std::string &text) const = 0;

protected:
	text_measurer() = default;
};

std::array<ink_extents, 10> digit_extents(const text_measurer &measurer);
ink_span digit_envelope(const std::array<ink_extents, 10> &digits);
double solve_point_size(const std::array<ink_extents, 10> &digits, double target_height);

row_extents date_reference_extents(const text_measurer &measurer, date_format format);
row_extents time_reference_extents(const text_measurer &measurer, bool twelve_hour);
row_extents meridiem_reference_extents(const text_measurer &measurer);

inline clock_frame solve_frame(const clock_style &style, const std::optional<row_extents> &date,
			       const row_extents &time, const std::optional<row_extents> &meridiem)
{
	double widest_row = time.width;
	double meridiem_baseline_y = 0;
	double time_baseline_y = time.descent;

	if (meridiem) {
		widest_row = std::max(widest_row, meridiem->width);
		meridiem_baseline_y = meridiem->descent;
		time_baseline_y = meridiem_baseline_y + meridiem->ascent + style.row_spacing() + time.descent;
	}

	double date_baseline_y = 0;
	double ink_top = time_baseline_y + time.ascent;
	if (date) {
		widest_row = std::max(widest_row, date->width);
		date_baseline_y = ink_top + style.row_spacing() + date->descent;
		ink_top = date_baseline_y + date->ascent;
	}

	const double reference_width = widest_row + style.horizontal_margin() * 2;
	const double reference_height = ink_top + style.top_margin() + style.bottom_margin();

	return {
		.width = static_cast<std::uint32_t>(std::ceil(reference_width)),
		.height = static_cast<std::uint32_t>(std::ceil(reference_height)),
		.reference_width = reference_width,
		.date_baseline_y = date_baseline_y + style.bottom_margin(),
		.time_baseline_y = time_baseline_y + style.bottom_margin(),
		.meridiem_baseline_y = meridiem_baseline_y + style.bottom_margin(),
		.colon_offset_px = style.colon_offset_px(),
	};
}
