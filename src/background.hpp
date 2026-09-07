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

#include <cstdint>

struct fill_style {
	std::uint32_t color = 0;
	double radius_px = 0;
	double bias_px = 0;
	edge_flags edges;
};

void apply_fill(rendered_text &bitmap, const fill_style &fill);
void shift_content(rendered_text &bitmap, const int dx, const int dy);
