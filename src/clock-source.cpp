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
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>
#include <obs.h>
#include <obs-data.h>
#include <obs-module.h>
#include <obs-properties.h>
#include <obs-source.h>

#include <cstdint>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <string>
#include <utility>

namespace settings {
constexpr const char *size = "size";
constexpr const char *color = "color";
} // namespace settings

struct clock_source {
	obs_source_t *src;

	std::unique_ptr<prepared_clock> clock;

	clock_content content;
	std::time_t last_read = 0;

	gs_texture_t *tex = nullptr;
	std::uint32_t tex_width = 0;
	std::uint32_t tex_height = 0;
};

const char *clock_source_get_name(void *)
{
	return obs_module_text("ClockSource");
}

clock_content read_clock(std::time_t now)
{
	std::tm local = {};
#ifdef _WIN32
	localtime_s(&local, &now);
#else
	localtime_r(&now, &local);
#endif
	const int month = std::clamp(local.tm_mon + 1, 1, 12);
	const int day = std::clamp(local.tm_mday, 1, 31);
	const int hour = std::clamp(local.tm_hour, 0, 23);
	const int minute = std::clamp(local.tm_min, 0, 59);
	const char *weekday = weekday_names[std::clamp(local.tm_wday, 0, 6)];

	char date_text[10];
	char time_text[6];
	std::snprintf(date_text, sizeof date_text, "%d/%d %s", month, day, weekday);
	std::snprintf(time_text, sizeof time_text, "%d:%02d", hour, minute);

	return {.date = date_text, .time = time_text};
}

bool refresh_content(clock_source *context)
{
	const std::time_t now = std::time(nullptr);
	if (now == context->last_read) {
		return false;
	}
	context->last_read = now;

	clock_content content = read_clock(now);
	if (content.date == context->content.date && content.time == context->content.time) {
		return false;
	}

	context->content = std::move(content);
	return true;
}

static void clock_source_rebuild_texture(clock_source *context)
{
	const rendered_text bitmap = context->clock ? context->clock->render(context->content) : rendered_text{};
	if (!bitmap.valid()) {
		return;
	}
	const std::uint8_t *rows = bitmap.pixels.data();

	obs_enter_graphics();
	if (context->tex && context->tex_width == bitmap.width && context->tex_height == bitmap.height) {
		gs_texture_set_image(context->tex, rows, bitmap.width * 4, false);
	} else {
		if (context->tex) {
			gs_texture_destroy(context->tex);
		}
		context->tex = gs_texture_create(bitmap.width, bitmap.height, GS_RGBA, 1, &rows, GS_DYNAMIC);
		context->tex_width = bitmap.width;
		context->tex_height = bitmap.height;
	}
	obs_leave_graphics();
}

void clock_source_update(void *data, obs_data_t *settings)
{
	auto *context = static_cast<clock_source *>(data);
	auto size = static_cast<std::uint32_t>(obs_data_get_int(settings, settings::size));
	auto color = static_cast<std::uint32_t>(obs_data_get_int(settings, settings::color));
	context->clock = prepare_clock(
		{.font_face = "Tsukushi A Round Gothic", .font_style = "Bold", .size = size, .color = color});

	refresh_content(context);
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
	obs_data_set_default_int(settings, settings::size, 50);
	obs_data_set_default_int(settings, settings::color, 0xFFFFFFFF);
}

void clock_source_video_tick(void *data, float)
{
	auto *context = static_cast<clock_source *>(data);
	if (!refresh_content(context)) {
		return;
	}
	clock_source_rebuild_texture(context);
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
	obs_properties_add_int_slider(props, settings::size, obs_module_text("ClockSource.Size"), 20, 200, 1);
	obs_properties_add_color(props, settings::color, obs_module_text("ClockSource.Color"));
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
	.video_tick = clock_source_video_tick,
	.video_render = clock_source_render,
	.icon_type = OBS_ICON_TYPE_COLOR,
};

void register_clock_source()
{
	obs_register_source(&info);
};
