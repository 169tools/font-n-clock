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

#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <obs-data.h>
#include <obs-module.h>
#include <obs-properties.h>
#include <obs-source.h>
#include <obs.h>

#include "font-dialog.hpp"
#include "text-renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>

namespace settings {
constexpr const char *date_format_name = "date_format";
constexpr const char *twelve_hour_name = "twelve_hour";
constexpr const char *font_display_name = "font_display";
constexpr const char *select_font_name = "select_font";
constexpr const char *font_face_name = "font_face";
constexpr const char *font_style_name = "font_style";
constexpr const char *size_name = "size";
constexpr const char *colon_offset_percent_name = "colon_offset_percent";
constexpr const char *tracking_percent_name = "tracking_percent";
constexpr const char *color_name = "color";
constexpr const char *shadow_name = "shadow";
constexpr const int colon_offset_percent_min = -10;
constexpr const int colon_offset_percent_max = 50;
#if defined(_WIN32) || defined(__APPLE__)
constexpr const char *default_font_face = "Impact";
#else
constexpr const char *default_font_face = "Sans Serif";
#endif
constexpr const char *default_font_style = "Regular";
} // namespace settings

struct clock_texture {
	gs_texture_t *texture = nullptr;
	std::uint32_t texture_width = 0;
	std::uint32_t texture_height = 0;
	void clear();
};

void clock_texture::clear()
{
	if (texture) {
		obs_enter_graphics();
		gs_texture_destroy(texture);
		obs_leave_graphics();
		texture = nullptr;
	}
	texture_width = 0;
	texture_height = 0;
}

struct clock_source {
	obs_source_t *source = nullptr;

	clock_style clock_style;
	std::unique_ptr<prepared_clock> prepared_clock;

	clock_strings clock_strings;
	std::time_t last_read_time = 0;

	clock_texture clock_texture;
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
	{date_format::none, "none"},
};

const char *clock_source_get_name(void *);
void *clock_source_create(obs_data_t *settings, obs_source_t *source);
void clock_source_destroy(void *data);
std::uint32_t clock_source_get_width(void *data);
std::uint32_t clock_source_get_height(void *data);
void clock_source_get_defaults(obs_data_t *settings);
obs_properties_t *clock_source_get_properties(void *data);
void clock_source_update(void *data, obs_data_t *settings);
void clock_source_video_tick(void *data, float);
void clock_source_render(void *data, gs_effect *);

std::string font_display_text(const std::string &font_face, const std::string &font_style);
bool clock_source_select_font(obs_properties_t *, obs_property_t *, void *data);
int suggested_colon_offset_percent(const std::string &font_face, const std::string &font_style);
static void clock_source_rebuild_texture(clock_source *context);
bool refresh_content(clock_source *context);

const char *clock_source_get_name(void *)
{
	return obs_module_text("ClockSource");
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
	context->clock_texture.clear();
	delete context;
}

std::uint32_t clock_source_get_width(void *data)
{
	return static_cast<clock_source *>(data)->clock_texture.texture_width;
}

std::uint32_t clock_source_get_height(void *data)
{
	return static_cast<clock_source *>(data)->clock_texture.texture_height;
}

void clock_source_get_defaults(obs_data_t *settings)
{
	const std::string font_display = font_display_text(settings::default_font_face, settings::default_font_style);
	const int colon_offset_percent =
		suggested_colon_offset_percent(settings::default_font_face, settings::default_font_style);

	obs_data_set_default_string(settings, settings::date_format_name, date_format_options[0].id);
	obs_data_set_default_bool(settings, settings::twelve_hour_name, false);
	obs_data_set_default_string(settings, settings::font_face_name, settings::default_font_face);
	obs_data_set_default_string(settings, settings::font_style_name, settings::default_font_style);
	obs_data_set_default_string(settings, settings::font_display_name, font_display.c_str());
	obs_data_set_default_int(settings, settings::size_name, clock_style::default_size);
	obs_data_set_default_int(settings, settings::colon_offset_percent_name, colon_offset_percent);
	obs_data_set_default_int(settings, settings::tracking_percent_name, 0);
	obs_data_set_default_int(settings, settings::color_name, 0xFFFFFFFF);
	obs_data_set_default_bool(settings, settings::shadow_name, false);
}

obs_properties_t *clock_source_get_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_t *format_props = obs_properties_create();

	obs_property_t *date_list = obs_properties_add_list(format_props, settings::date_format_name,
							    obs_module_text("ClockSource.DateFormat"),
							    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const date_format_option &option : date_format_options) {
		const std::string name = option.value == date_format::none
						 ? obs_module_text("ClockSource.DateFormat.None")
						 : format_date(option.value, sample_month, sample_day, sample_weekday);
		obs_property_list_add_string(date_list, name.c_str(), option.id);
	}

	obs_property_t *time_list = obs_properties_add_list(format_props, settings::twelve_hour_name,
							    obs_module_text("ClockSource.TimeFormat"),
							    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_BOOL);
	const std::string sample_24h = format_time(sample_hour, sample_minute, false);
	const std::string sample_12h =
		format_time(sample_hour, sample_minute, true) + " " + format_meridiem(sample_hour, true);
	obs_property_list_add_bool(time_list, sample_24h.c_str(), false);
	obs_property_list_add_bool(time_list, sample_12h.c_str(), true);

	obs_properties_add_group(props, "format_group", obs_module_text("ClockSource.FormatGroup"), OBS_GROUP_NORMAL,
				 format_props);

	obs_properties_t *layout_props = obs_properties_create();
	obs_properties_add_text(layout_props, settings::font_display_name, obs_module_text("ClockSource.Font"),
				OBS_TEXT_INFO);
	obs_properties_add_button2(layout_props, settings::select_font_name, obs_module_text("ClockSource.SelectFont"),
				   clock_source_select_font, data);
	obs_properties_add_int_slider(layout_props, settings::size_name, obs_module_text("ClockSource.Size"), 20, 200,
				      1);
	obs_properties_add_int_slider(layout_props, settings::colon_offset_percent_name,
				      obs_module_text("ClockSource.ColonOffsetPercent"),
				      settings::colon_offset_percent_min, settings::colon_offset_percent_max, 1);
	obs_properties_add_int_slider(layout_props, settings::tracking_percent_name,
				      obs_module_text("ClockSource.TrackingPercent"), -20, 10, 1);
	obs_properties_add_group(props, "layout_group", obs_module_text("ClockSource.TextLayoutGroup"),
				 OBS_GROUP_NORMAL, layout_props);

	obs_properties_t *appearance_props = obs_properties_create();
	obs_properties_add_color(appearance_props, settings::color_name, obs_module_text("ClockSource.Color"));
	obs_properties_add_bool(appearance_props, settings::shadow_name, obs_module_text("ClockSource.Shadow"));
	obs_properties_add_group(props, "color_and_shadow_group", obs_module_text("ClockSource.TextAppearanceGroup"),
				 OBS_GROUP_NORMAL, appearance_props);

	return props;
}

void clock_source_update(void *data, obs_data_t *settings)
{
	auto *context = static_cast<clock_source *>(data);

	const date_format date_format = [&settings] {
		const char *stored = obs_data_get_string(settings, settings::date_format_name);
		for (const date_format_option &option : date_format_options) {
			if (std::strcmp(option.id, stored) == 0) {
				return option.value;
			}
		}
		return date_format_options[0].value;
	}();

	auto twelve_hour = static_cast<bool>(obs_data_get_bool(settings, settings::twelve_hour_name));
	auto font_face = static_cast<std::string>(obs_data_get_string(settings, settings::font_face_name));
	auto font_style = static_cast<std::string>(obs_data_get_string(settings, settings::font_style_name));
	auto size = static_cast<double>(obs_data_get_int(settings, settings::size_name));
	auto colon_offset_percent =
		static_cast<double>(obs_data_get_int(settings, settings::colon_offset_percent_name));
	auto tracking_percent = static_cast<double>(obs_data_get_int(settings, settings::tracking_percent_name));
	auto color = static_cast<std::uint32_t>(obs_data_get_int(settings, settings::color_name));
	auto shadow = static_cast<bool>(obs_data_get_bool(settings, settings::shadow_name));

	context->clock_style = {
		.date_format = date_format,
		.twelve_hour = twelve_hour,
		.font_face = font_face,
		.font_style = font_style,
		.size = size,
		.colon_offset_ratio = colon_offset_percent / 100,
		.tracking_em = tracking_percent / 100,
		.color = color,
		.shadow = shadow,
	};
	context->prepared_clock = prepare_clock(context->clock_style);
	context->last_read_time = 0;
	refresh_content(context);
	clock_source_rebuild_texture(context);
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
	if (!context->clock_texture.texture) {
		return;
	}

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_PREMULTIPLIED_ALPHA);
	while (gs_effect_loop(effect, "Draw")) {
		obs_source_draw(context->clock_texture.texture, 0, 0, 0, 0, false);
	}
}

std::string font_display_text(const std::string &font_face, const std::string &font_style)
{
	return font_style.empty() ? font_face : font_face + " " + font_style;
}

bool clock_source_select_font(obs_properties_t *, obs_property_t *, void *data)
{
	auto *context = static_cast<clock_source *>(data);
	obs_data_t *settings = obs_source_get_settings(context->source);

	std::string font_face = obs_data_get_string(settings, settings::font_face_name);
	std::string font_style = obs_data_get_string(settings, settings::font_style_name);
	if (!select_font(font_face, font_style, context->clock_style.date_format, context->clock_style.twelve_hour)) {
		obs_data_release(settings);
		return false;
	}

	const int colon_offset_percent = suggested_colon_offset_percent(font_face, font_style);

	obs_data_set_string(settings, settings::font_face_name, font_face.c_str());
	obs_data_set_string(settings, settings::font_style_name, font_style.c_str());
	obs_data_set_string(settings, settings::font_display_name, font_display_text(font_face, font_style).c_str());
	obs_data_set_int(settings, settings::colon_offset_percent_name, colon_offset_percent);
	obs_data_set_int(settings, settings::tracking_percent_name, 0);
	obs_source_update(context->source, settings);
	obs_data_release(settings);

	return true;
}

int suggested_colon_offset_percent(const std::string &font_face, const std::string &font_style)
{
	const clock_style clock_style = {.font_face = font_face, .font_style = font_style};
	const double suggested_colon_offset_ratio = suggest_colon_offset_ratio(clock_style);
	return std::clamp(static_cast<int>(std::lround(suggested_colon_offset_ratio * 100)),
			  settings::colon_offset_percent_min, settings::colon_offset_percent_max);
}

static void clock_source_rebuild_texture(clock_source *context)
{
	const rendered_text bitmap = context->prepared_clock ? context->prepared_clock->render(context->clock_strings)
							     : rendered_text{};
	if (!bitmap.valid()) {
		context->clock_texture.clear();
		return;
	}
	const std::uint8_t *rows = bitmap.pixels.data();

	obs_enter_graphics();
	clock_texture &clock_texture = context->clock_texture;
	if (clock_texture.texture && clock_texture.texture_width == bitmap.width &&
	    clock_texture.texture_height == bitmap.height) {
		gs_texture_set_image(clock_texture.texture, rows, bitmap.width * 4, false);
	} else {
		if (clock_texture.texture) {
			gs_texture_destroy(clock_texture.texture);
		}
		clock_texture.texture = gs_texture_create(bitmap.width, bitmap.height, GS_RGBA, 1, &rows, GS_DYNAMIC);
		clock_texture.texture_width = bitmap.width;
		clock_texture.texture_height = bitmap.height;
	}
	obs_leave_graphics();
}

bool refresh_content(clock_source *context)
{
	const std::time_t now = std::time(nullptr);
	if (now == context->last_read_time) {
		return false;
	}
	context->last_read_time = now;

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

	std::string date = format_date(context->clock_style.date_format, month, day, weekday);
	std::string time = format_time(hour, minute, context->clock_style.twelve_hour);
	const char *meridiem = format_meridiem(hour, context->clock_style.twelve_hour);

	const clock_strings clock_strings = context->clock_strings;
	if (date == clock_strings.date && time == clock_strings.time && meridiem == clock_strings.meridiem) {
		return false;
	}
	context->clock_strings = {.date = date, .time = time, .meridiem = meridiem};
	return true;
}

void register_clock_source()
{
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
	obs_register_source(&info);
}
