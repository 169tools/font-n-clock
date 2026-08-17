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

#include <obs.h>
#include <obs-data.h>
#include <obs-module.h>
#include <obs-properties.h>
#include <obs-source.h>

#include <cstdint>
#include <graphics/graphics.h>
#include <graphics/vec4.h>

struct color_source {
	vec4 color;
	vec4 color_srgb;

	std::uint32_t width;
	std::uint32_t height;

	obs_source_t *src;
};

const char *color_source_get_name(void *)
{
	return obs_module_text("ClockSource");
}

void color_source_update(void *data, obs_data_t *settings)
{
	auto *context = static_cast<color_source *>(data);
	std::uint32_t color = static_cast<std::uint32_t>(obs_data_get_int(settings, "color"));
	vec4_from_rgba(&context->color, color);
	vec4_from_rgba_srgb(&context->color_srgb, color);
	context->width = static_cast<std::uint32_t>(obs_data_get_int(settings, "width"));
	context->height = static_cast<std::uint32_t>(obs_data_get_int(settings, "height"));
}

void *color_source_create(obs_data_t *settings, obs_source_t *source)
{
	color_source *context = new color_source();
	context->src = source;
	color_source_update(context, settings);
	return context;
}

void color_source_destroy(void *data)
{
	auto *context = static_cast<color_source *>(data);
	delete context;
}

std::uint32_t color_source_get_width(void *data)
{
	return static_cast<color_source *>(data)->width;
}

std::uint32_t color_source_get_height(void *data)
{
	return static_cast<color_source *>(data)->height;
}

void color_source_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "color", 0xFFAAA500);
	obs_data_set_default_int(settings, "width", 1920);
	obs_data_set_default_int(settings, "height", 1080);
}

void color_source_render_helper(color_source *context, vec4 *colorVal)
{
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");

	gs_effect_set_vec4(color, colorVal);

	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);

	gs_draw_sprite(0, 0, context->width, context->height);

	gs_technique_end_pass(tech);
	gs_technique_end(tech);
}

void color_source_render(void *data, gs_effect *)
{
	auto *context = static_cast<color_source *>(data);
	const bool linear_srgb = gs_get_linear_srgb() || (context->color.w < 1.0f);
	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(linear_srgb);

	if (linear_srgb) {
		color_source_render_helper(context, &context->color_srgb);
	} else {
		color_source_render_helper(context, &context->color);
	}
	gs_enable_framebuffer_srgb(previous);
}

obs_properties_t *color_source_get_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_color(props, "color", obs_module_text("ColorSource.Color"));
	obs_properties_add_int(props, "width", obs_module_text("ColorSource.Width"), 0, 1920, 10);
	obs_properties_add_int(props, "height", obs_module_text("ColorSource.Height"), 0, 1080, 10);
	return props;
}

obs_source_info info = {
	.id = "font-meets-clock",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB,
	.get_name = color_source_get_name,
	.create = color_source_create,
	.destroy = color_source_destroy,
	.get_width = color_source_get_width,
	.get_height = color_source_get_height,
	.get_defaults = color_source_get_defaults,
	.get_properties = color_source_get_properties,
	.update = color_source_update,
	.video_render = color_source_render,
	.icon_type = OBS_ICON_TYPE_COLOR,
};

void register_color_source()
{
	obs_register_source(&info);
};
