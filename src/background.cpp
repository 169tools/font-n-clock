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

#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <obs-module.h>
#include <obs-source.h>
#include <util/base.h>
#include <util/bmem.h>

#include "plugin-support.h"
#include "text-renderer.hpp"

#include <algorithm>

namespace {
vec4 corner_radii(const double radius_px, const edge_flags &edges, const float width, const float height)
{
	const auto radius = static_cast<float>(std::min({radius_px, width / 2.0, height / 2.0}));

	// 左上、右上、左下、右下
	struct vec4 radii;
	vec4_set(&radii, !edges.top && !edges.left ? radius : 0, !edges.top && !edges.right ? radius : 0,
		 !edges.bottom && !edges.left ? radius : 0, !edges.bottom && !edges.right ? radius : 0);
	return radii;
}
} // namespace

gs_effect_t *load_background_effect()
{
	char *path = obs_module_file("background.effect");
	if (!path) {
		obs_log(LOG_ERROR, "background.effect not found");
		return nullptr;
	}

	char *errors = nullptr;
	gs_effect_t *effect = gs_effect_create_from_file(path, nullptr);
	if (!effect) {
		obs_log(LOG_ERROR, "failed to load %s: %s", path, errors ? errors : "(no details)");
	}
	bfree(errors);
	bfree(path);
	return effect;
}

void draw_background(gs_effect_t *effect, gs_texture_t *texture, const fill_style &fill, const edge_flags &edges)
{
	const auto width = static_cast<float>(gs_texture_get_width(texture));
	const auto height = static_cast<float>(gs_texture_get_height(texture));

	struct vec4 color;
	if (gs_get_linear_srgb()) {
		vec4_from_rgba_srgb(&color, fill.color);
	} else {
		vec4_from_rgba(&color, fill.color);
	}

	struct vec2 size;
	vec2_set(&size, width, height);

	// 画面端に接している辺は余白が大きく見えるので、文字をずらして均等に見えるようにする
	const auto bias = static_cast<float>(fill.bias_px);
	struct vec2 offset;
	vec2_set(&offset, (edges.right ? bias : 0) - (edges.left ? bias : 0),
		 (edges.bottom ? bias : 0) - (edges.top ? bias : 0));

	const struct vec4 radii = corner_radii(fill.radius_px, edges, width, height);

	gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "fill_color"), &color);
	gs_effect_set_vec2(gs_effect_get_param_by_name(effect, "box_size"), &size);
	gs_effect_set_vec2(gs_effect_get_param_by_name(effect, "text_offset"), &offset);
	gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "corner_radius"), &radii);

	while (gs_effect_loop(effect, "Draw")) {
		obs_source_draw(texture, 0, 0, 0, 0, false);
	}
}
