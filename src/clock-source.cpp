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

#include "text-renderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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
constexpr const char *date_format_name = "date_format";
constexpr const char *font_display_name = "font_display";
constexpr const char *select_font_name = "select_font";
constexpr const char *font_face_name = "font_face";
constexpr const char *font_style_name = "font_style";
constexpr const char *size_name = "size";
constexpr const char *color_name = "color";
constexpr const char *shadow_name = "shadow";
constexpr const char *colon_offset_percent_name = "colon_offset_percent";
constexpr const int colon_offset_percent_min = -10;
constexpr const int colon_offset_percent_max = 50;
#ifdef _WIN32
constexpr const char *default_font_face = "Calibri";
#elif defined(__APPLE__)
constexpr const char *default_font_face = "Optima";
#else
constexpr const char *default_font_face = "Sans Serif";
#endif
} // namespace settings

struct clock_source {
	obs_source_t *source;

	std::unique_ptr<prepared_clock> clock;

	date_format format = date_format::month_day_weekday;
	clock_content content;
	std::time_t last_read = 0;

	gs_texture_t *texture = nullptr;
	std::uint32_t texture_width = 0;
	std::uint32_t texture_height = 0;
};

struct date_format_option {
	date_format value;
	const char *id;
};

constexpr date_format_option date_format_options[] = {
	{date_format::month_day_weekday, "month_day_weekday"},
	{date_format::day_month_weekday, "day_month_weekday"},
	{date_format::month_name_day, "month_name_day"},
	{date_format::day_month_name, "day_month_name"},
};

constexpr int sample_month = 1;
constexpr int sample_day = 23;
constexpr int sample_weekday = 5;

const char *clock_source_get_name(void *)
{
	return obs_module_text("ClockSource");
}

clock_content read_clock(std::time_t now, const date_format format)
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
	const int weekday = std::clamp(local.tm_wday, 0, 6);

	char time_text[6];
	std::snprintf(time_text, sizeof time_text, "%d:%02d", hour, minute);
	return {.date = format_date(format, month, day, weekday), .time = time_text};
}

bool refresh_content(clock_source *context)
{
	const std::time_t now = std::time(nullptr);
	if (now == context->last_read) {
		return false;
	}
	context->last_read = now;

	clock_content content = read_clock(now, context->format);
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
	if (context->texture && context->texture_width == bitmap.width && context->texture_height == bitmap.height) {
		gs_texture_set_image(context->texture, rows, bitmap.width * 4, false);
	} else {
		if (context->texture) {
			gs_texture_destroy(context->texture);
		}
		context->texture = gs_texture_create(bitmap.width, bitmap.height, GS_RGBA, 1, &rows, GS_DYNAMIC);
		context->texture_width = bitmap.width;
		context->texture_height = bitmap.height;
	}
	obs_leave_graphics();
}

date_format read_date_format(obs_data_t *settings)
{
	const char *stored = obs_data_get_string(settings, settings::date_format_name);
	for (const date_format_option &option : date_format_options) {
		if (std::strcmp(option.id, stored) == 0) {
			return option.value;
		}
	}
	return date_format_options[0].value;
}

int suggested_colon_offset_percent(const std::string &font_face, const std::string &font_style)
{
	const double suggested_colon_offset_ratio =
		suggest_colon_offset_ratio({.font_face = font_face, .font_style = font_style});
	return std::clamp(static_cast<int>(std::lround(suggested_colon_offset_ratio * 100)),
			  settings::colon_offset_percent_min, settings::colon_offset_percent_max);
}

void clock_source_update(void *data, obs_data_t *settings)
{
	auto *context = static_cast<clock_source *>(data);
	date_format format = read_date_format(settings);
	auto font_face = static_cast<std::string>(obs_data_get_string(settings, settings::font_face_name));
	auto font_style = static_cast<std::string>(obs_data_get_string(settings, settings::font_style_name));
	auto size = static_cast<double>(obs_data_get_int(settings, settings::size_name));
	auto color = static_cast<std::uint32_t>(obs_data_get_int(settings, settings::color_name));
	auto shadow = static_cast<bool>(obs_data_get_bool(settings, settings::shadow_name));

	const std::string font_display = font_style.empty() ? font_face : font_face + " " + font_style;
	obs_data_set_string(settings, settings::font_display_name, font_display.c_str());

	if (!obs_data_has_user_value(settings, settings::colon_offset_percent_name)) {
		const int colon_offset_percent = suggested_colon_offset_percent(font_face, font_style);
		obs_data_set_int(settings, settings::colon_offset_percent_name, colon_offset_percent);
	}
	auto colon_offset_percent =
		static_cast<double>(obs_data_get_int(settings, settings::colon_offset_percent_name));
	context->clock = prepare_clock({.format = format,
					.font_face = font_face,
					.font_style = font_style,
					.size = size,
					.color = color,
					.shadow = shadow,
					.colon_offset_ratio = colon_offset_percent / 100});

	context->format = format;
	context->last_read = 0;
	refresh_content(context);
	clock_source_rebuild_texture(context);
}

void *clock_source_create(obs_data_t *settings, obs_source_t *source)
{
	clock_source *context = new clock_source();
	context->source = source;
	clock_source_update(context, settings);
	return context;
}

void clock_source_destroy(void *data)
{
	auto *context = static_cast<clock_source *>(data);
	if (context->texture) {
		obs_enter_graphics();
		gs_texture_destroy(context->texture);
		obs_leave_graphics();
	}
	delete context;
}

std::uint32_t clock_source_get_width(void *data)
{
	return static_cast<clock_source *>(data)->texture_width;
}

std::uint32_t clock_source_get_height(void *data)
{
	return static_cast<clock_source *>(data)->texture_height;
}

void clock_source_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, settings::font_face_name, settings::default_font_face);
	obs_data_set_default_string(settings, settings::font_style_name, "Bold");
	obs_data_set_default_string(settings, settings::date_format_name, date_format_options[0].id);
	obs_data_set_default_int(settings, settings::size_name, 50);
	obs_data_set_default_int(settings, settings::colon_offset_percent_name, 0);
	obs_data_set_default_int(settings, settings::color_name, 0xFFFFFFFF);
	obs_data_set_default_bool(settings, settings::shadow_name, false);
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
	if (!context->texture) {
		return;
	}

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_PREMULTIPLIED_ALPHA);
	while (gs_effect_loop(effect, "Draw")) {
		obs_source_draw(context->texture, 0, 0, 0, 0, false);
	}
}

bool clock_source_select_font(obs_properties_t *, obs_property_t *, void *data)
{
	auto *context = static_cast<clock_source *>(data);

	obs_data_t *settings = obs_source_get_settings(context->source);

	// TODO: フォント選択ダイアログを表示する
	const char *font_face = "Optima";
	const char *font_style = "Bold";
	obs_data_set_string(settings, settings::font_face_name, font_face);
	obs_data_set_string(settings, settings::font_style_name, font_style);

	const int colon_offset_percent = suggested_colon_offset_percent(font_face, font_style);
	obs_data_set_int(settings, settings::colon_offset_percent_name, colon_offset_percent);

	obs_source_update(context->source, settings);
	obs_data_release(settings);

	return true;
}

obs_properties_t *clock_source_get_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_text(props, settings::font_display_name, obs_module_text("ClockSource.FontDisplay"),
				OBS_TEXT_INFO);
	obs_properties_add_button2(props, settings::select_font_name, obs_module_text("ClockSource.SelectFont"),
				   clock_source_select_font, data);

	obs_property_t *list = obs_properties_add_list(props, settings::date_format_name,
						       obs_module_text("ClockSource.DateFormat"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_STRING);
	for (const date_format_option &option : date_format_options) {
		obs_property_list_add_string(
			list, format_date(option.value, sample_month, sample_day, sample_weekday).c_str(), option.id);
	}

	obs_properties_add_int_slider(props, settings::size_name, obs_module_text("ClockSource.Size"), 20, 200, 1);
	obs_properties_add_int_slider(props, settings::colon_offset_percent_name,
				      obs_module_text("ClockSource.ColonOffsetPercent"),
				      settings::colon_offset_percent_min, settings::colon_offset_percent_max, 1);
	obs_properties_add_color(props, settings::color_name, obs_module_text("ClockSource.Color"));
	obs_properties_add_bool(props, settings::shadow_name, obs_module_text("ClockSource.Shadow"));
	return props;
}

obs_source_info info = {
	.id = "font-n-clock",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
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
	.icon_type = OBS_ICON_TYPE_TEXT,
};

void register_clock_source()
{
	obs_register_source(&info);
};
