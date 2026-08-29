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

#include "layout.hpp"

#include "text-renderer.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

std::array<ink_extents, 10> digit_extents(const text_measurer &measurer)
{
	std::array<ink_extents, 10> digits{};
	for (int i = 0; i <= 9; ++i) {
		digits[i] = measurer.measure(std::string(1, static_cast<char>('0' + i)));
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

row_extents date_reference_extents(const text_measurer &measurer, date_format format)
{
	row_extents max_extents;
	int widest_month = 0;
	int widest_day = 0;

	double widest = 0;
	for (int month = 1; month <= 12; ++month) {
		for (int day = 1; day <= 31; ++day) {
			const double width = measurer.measure(format_date(format, month, day, 0)).width;
			max_extents.width = std::max(max_extents.width, width);
			if (width > widest) {
				widest = width;
				widest_month = month;
				widest_day = day;
			}
		}
	}
	if (widest_month == 0) {
		return {};
	}

	for (int weekday = 1; weekday < 7; ++weekday) {
		const double width = measurer.measure(format_date(format, widest_month, widest_day, weekday)).width;
		max_extents.width = std::max(max_extents.width, width);
	}

	std::string height_glyphs = "0123456789";
	for (const char *weekday : weekday_names) {
		height_glyphs += weekday;
	}
	for (const char *month : month_names) {
		height_glyphs += month;
	}

	const ink_extents extents = measurer.measure(height_glyphs);
	max_extents.ascent = extents.ascent;
	max_extents.descent = extents.descent;
	return max_extents;
}

row_extents time_reference_extents(const text_measurer &measurer, const bool twelve_hour)
{
	row_extents max_extents;
	const int first_hour = twelve_hour ? 1 : 0;
	const int last_hour = twelve_hour ? 12 : 23;
	for (int hour = first_hour; hour <= last_hour; ++hour) {
		for (int minute = 0; minute < 60; ++minute) {
			char text[6];
			std::snprintf(text, sizeof text, "%d:%02d", hour, minute);
			max_extents.extend(measurer.measure(text));
		}
	}
	return max_extents;
}

row_extents meridiem_reference_extents(const text_measurer &measurer)
{
	row_extents max_extents;
	for (const char *meridiem : meridiem_names) {
		max_extents.extend(measurer.measure(meridiem));
	}
	return max_extents;
}
