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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

enum class date_format {
	month_day_weekday, // 12/31 WED
	day_month_weekday, // 31/12 WED
	month_name_day,    // DEC 31
	day_month_name,    // 31 DEC
};

struct clock_style {
	date_format format;
	std::string font_face;
	std::string font_style;
	double size = 50;
	std::uint32_t color = 0xffffffff;
	bool shadow = false;
	double colon_offset_ratio = 0;

	double date_ink_height() const noexcept { return size * 0.4; }
	double time_ink_height() const noexcept { return size; }
	double date_and_time_spacing() const noexcept { return size * 0.24; }
	double top_margin() const noexcept { return size * 0.36; }
	double bottom_margin() const noexcept { return size * 0.4; }
	double horizontal_margin() const noexcept { return size * 0.38; }
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

struct clock_content {
	std::string date;
	std::string time;
};

constexpr const char *month_names[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
				       "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
constexpr const char *weekday_names[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

inline std::string format_date(const date_format format, const int month, const int day, const int weekday)
{
	const int m = std::clamp(month, 1, 12);
	const int d = std::clamp(day, 1, 31);
	const char *month_name = month_names[m - 1];
	const char *weekday_name = weekday_names[std::clamp(weekday, 0, 6)];

	char text[10] = "";
	switch (format) {
	case date_format::month_day_weekday:
		std::snprintf(text, sizeof text, "%d/%d %s", m, d, weekday_name);
		break;
	case date_format::day_month_weekday:
		std::snprintf(text, sizeof text, "%d/%d %s", d, m, weekday_name);
		break;
	case date_format::month_name_day:
		std::snprintf(text, sizeof text, "%s %d", month_name, d);
		break;
	case date_format::day_month_name:
		std::snprintf(text, sizeof text, "%d %s", d, month_name);
		break;
	}
	return text;
}

class prepared_clock {
public:
	virtual ~prepared_clock() = default;

	prepared_clock(const prepared_clock &) = delete;
	prepared_clock &operator=(const prepared_clock &) = delete;

	virtual rendered_text render(const clock_content &content) const = 0;

protected:
	prepared_clock() = default;
};

std::unique_ptr<prepared_clock> prepare_clock(const clock_style &style);
double suggest_colon_offset_ratio(const clock_style &style);
