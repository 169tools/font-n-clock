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

#include "text-renderer.hpp"
#include <obs.h>
#include <obs-data.h>
#include <obs-module.h>
#include <obs-properties.h>
#include <obs-source.h>

#include <cstdint>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <string>

struct clock_source {
	obs_source_t *src;
	std::string text;
	vec4 color;

	gs_texture_t *tex = nullptr;
	std::uint32_t tex_width = 0;
	std::uint32_t tex_height = 0;
};

const char *clock_source_get_name(void *)
{
	return obs_module_text("ClockSource");
}

static void clock_source_rebuild_texture(clock_source *context)
{
	const rendered_text bitmap = render_text({.font_face = "Tsukushi B Round Gothic", .font_style = "Bold"});
	const std::uint8_t *rows = bitmap.pixels.data();

	obs_enter_graphics();
	if (context->tex) {
		gs_texture_destroy(context->tex);
	}
	context->tex = gs_texture_create(bitmap.width, bitmap.height, GS_RGBA, 1, &rows, GS_DYNAMIC);
	obs_leave_graphics();

	context->tex_width = bitmap.width;
	context->tex_height = bitmap.height;
}

void clock_source_update(void *data, obs_data_t *settings)
{
	auto *context = static_cast<clock_source *>(data);
	context->text = static_cast<std::string>(obs_data_get_string(settings, "text"));
	std::uint32_t color = static_cast<std::uint32_t>(obs_data_get_int(settings, "color"));
	vec4_from_rgba(&context->color, color);

	clock_source_rebuild_texture(context);
}

void *clock_source_create(obs_data_t *settings, obs_source_t *source)
{
	clock_source *context = new clock_source();
	context->src = source;
	clock_source_update(context, settings);
	return context;
}

void clock_source_destroy(void *data)
{
	auto *context = static_cast<clock_source *>(data);
	if (context->tex) {
		obs_enter_graphics();
		gs_texture_destroy(context->tex);
		obs_leave_graphics();
	}
	delete context;
}

std::uint32_t clock_source_get_width(void *data)
{
	return static_cast<clock_source *>(data)->tex_width;
}

std::uint32_t clock_source_get_height(void *data)
{
	return static_cast<clock_source *>(data)->tex_height;
}

void clock_source_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "text", "12:34");
	obs_data_set_default_int(settings, "color", 0xFFAAA500);
}

void clock_source_render(void *data, gs_effect *)
{
	auto *context = static_cast<clock_source *>(data);
	if (!context->tex) {
		return;
	}

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_PREMULTIPLIED_ALPHA);
	gs_technique_t *tech = gs_effect_get_technique(effect, "Draw");

	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);

	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);

	gs_effect_set_texture_srgb(gs_effect_get_param_by_name(effect, "image"), context->tex);
	gs_draw_sprite(0, 0, context->tex_width, context->tex_height);

	gs_technique_end_pass(tech);
	gs_technique_end(tech);

	gs_enable_framebuffer_srgb(previous);
}

obs_properties_t *clock_source_get_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_color(props, "color", obs_module_text("ClockSource.Color"));
	return props;
}

obs_source_info info = {
	.id = "font-meets-clock",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB,
	.get_name = clock_source_get_name,
	.create = clock_source_create,
	.destroy = clock_source_destroy,
	.get_width = clock_source_get_width,
	.get_height = clock_source_get_height,
	.get_defaults = clock_source_get_defaults,
	.get_properties = clock_source_get_properties,
	.update = clock_source_update,
	.video_render = clock_source_render,
	.icon_type = OBS_ICON_TYPE_COLOR,
};

void register_clock_source()
{
	obs_register_source(&info);
};
