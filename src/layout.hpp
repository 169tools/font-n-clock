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

#pragma once

struct ink_extents {
	double width = 0.0;
	double ascent = 0.0;
	double descent = 0.0;
	double left = 0.0;

	double height() const { return ascent + descent; }
};

struct ink_span {
	double ascent = 0.0;
	double descent = 0.0;

	double height() const { return ascent + descent; }
};
