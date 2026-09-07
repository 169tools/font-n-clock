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

#include "background.hpp"

#include "text-renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace {
corner_flags rounded_corners(const edge_flags &edges)
{
	return {
		.top_left = !edges.top && !edges.left,
		.top_right = !edges.top && !edges.right,
		.bottom_left = !edges.bottom && !edges.left,
		.bottom_right = !edges.bottom && !edges.right,
	};
}

double corner_distance(const double x, const double y, const double width, const double height,
		       const corner_flags &corners, const double radius)
{
	struct corner {
		bool enabled;
		double dx;
		double dy;
	};

	const corner list[] = {
		{corners.top_left, radius - x, radius - y},
		{corners.top_right, x - (width - radius), radius - y},
		{corners.bottom_left, radius - x, y - (height - radius)},
		{corners.bottom_right, x - (width - radius), y - (height - radius)},
	};

	for (const corner &c : list) {
		if (c.dx <= 0 || c.dy <= 0) {
			continue;
		}
		return c.enabled ? std::hypot(c.dx, c.dy) - radius : -1;
	}
	return -1;
}

std::uint8_t to_byte(const double value)
{
	return static_cast<std::uint8_t>(std::clamp(std::lround(value * 255), 0L, 255L));
}
} // namespace

void apply_fill(rendered_text &bitmap, const fill_style &fill)
{
	const double width = bitmap.width;
	const double height = bitmap.height;
	const double radius = std::min({fill.radius_px, width / 2, height / 2});

	const double fill_a = ((fill.color >> 24) & 0xff) / 255.0;
	const double fill_b = ((fill.color >> 16) & 0xff) / 255.0;
	const double fill_g = ((fill.color >> 8) & 0xff) / 255.0;
	const double fill_r = (fill.color & 0xff) / 255.0;

	const corner_flags corners = rounded_corners(fill.edges);

	for (std::uint32_t y = 0; y < bitmap.height; ++y) {
		for (std::uint32_t x = 0; x < bitmap.width; ++x) {
			const double distance = corner_distance(x + 0.5, y + 0.5, width, height, corners, radius);
			const double coverage = std::clamp(0.5 - distance, 0.0, 1.0);
			if (coverage <= 0) {
				continue;
			}

			std::uint8_t *pixel = &bitmap.pixels[(static_cast<std::size_t>(y) * bitmap.width + x) * 4];
			const double text_alpha = pixel[3] / 255.0;
			const double back_alpha = fill_a * coverage * (1 - text_alpha);

			pixel[0] = to_byte(pixel[0] / 255.0 + fill_r * back_alpha);
			pixel[1] = to_byte(pixel[1] / 255.0 + fill_g * back_alpha);
			pixel[2] = to_byte(pixel[2] / 255.0 + fill_b * back_alpha);
			pixel[3] = to_byte(text_alpha + back_alpha);
		}
	}
}

void shift_content(rendered_text &bitmap, const int dx, const int dy)
{
	if (dx == 0 && dy == 0) {
		return;
	}

	const long width = bitmap.width;
	const long height = bitmap.height;

	std::vector<std::uint8_t> shifted(bitmap.pixels.size(), 0);

	const long first_x = std::max<long>(0, dx);
	const long count = std::min<long>(width, width + dx) - first_x;
	if (count <= 0) {
		return;
	}

	for (long y = std::max<long>(0, dy); y < std::min<long>(height, height + dy); ++y) {
		std::memcpy(&shifted[static_cast<std::size_t>(y * width + first_x) * 4],
			    &bitmap.pixels[static_cast<std::size_t>((y - dy) * width + first_x - dx) * 4],
			    static_cast<std::size_t>(count) * 4);
	}
	bitmap.pixels = std::move(shifted);
}
