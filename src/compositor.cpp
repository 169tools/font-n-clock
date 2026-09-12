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

#include "compositor.hpp"

#include "layout.hpp"
#include "text-renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
struct rgba;
rgba decode(const std::uint32_t abgr);
std::uint8_t to_byte(const float value);
void blend_over(float *base, const rgba &color, const float alpha);
std::vector<float> distance_to_ink(const std::vector<float> &coverage, const long width, const long height,
				   const float max_dist);
void box_horizontal_blur(const std::vector<float> &source, std::vector<float> &target, const long width,
			 const long height, const long radius);
void box_vertical_blur(const std::vector<float> &source, std::vector<float> &target, const long width,
		       const long height, const long radius);
std::vector<float> shadow_alpha(const std::vector<float> &shape, const long width, const long height,
				const shadow_style &shadow);

struct rgba {
	float r = 0;
	float g = 0;
	float b = 0;
	float a = 0;
};

struct horizontal_nearest_ink {
	float dx = 0;
	float edge_offset = 0;

	float dist() const noexcept { return dx + edge_offset; }
};

rgba decode(const std::uint32_t abgr)
{
	return {
		.r = static_cast<float>(abgr & 0xff) / 255.0f,
		.g = static_cast<float>((abgr >> 8) & 0xff) / 255.0f,
		.b = static_cast<float>((abgr >> 16) & 0xff) / 255.0f,
		.a = static_cast<float>((abgr >> 24) & 0xff) / 255.0f,
	};
}

std::uint8_t to_byte(const float value)
{
	return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

void blend_over(float *base, const rgba &color, const float alpha)
{
	base[0] = color.r * alpha + base[0] * (1.0f - alpha);
	base[1] = color.g * alpha + base[1] * (1.0f - alpha);
	base[2] = color.b * alpha + base[2] * (1.0f - alpha);
	base[3] = alpha + base[3] * (1.0f - alpha);
}

std::vector<float> distance_to_ink(const std::vector<float> &coverage, const long width, const long height,
				   const float max_dist)
{
	long min_x = width;
	long max_x = -1;
	long min_y = height;
	long max_y = -1;
	for (long y = 0; y < height; ++y) {
		for (long x = 0; x < width; ++x) {
			if (coverage[static_cast<std::size_t>(y) * width + x] > 0.0f) {
				min_x = std::min(min_x, x);
				max_x = std::max(max_x, x);
				min_y = std::min(min_y, y);
				max_y = std::max(max_y, y);
			}
		}
	}
	std::vector<float> dist(coverage.size(), max_dist);
	if (max_x < 0) {
		return dist;
	}
	const long reach = static_cast<long>(std::ceil(max_dist));
	const long x0 = std::max(0L, min_x - reach);
	const long x1 = std::min(width - 1, max_x + reach);
	const long y0 = std::max(0L, min_y - reach);
	const long y1 = std::min(height - 1, max_y + reach);

	std::vector<horizontal_nearest_ink> horizontal(coverage.size(), {max_dist, 0.0f});
	for (long y = y0; y <= y1; ++y) {
		const std::size_t row = static_cast<std::size_t>(y) * width;

		for (const bool leftward : {false, true}) {
			horizontal_nearest_ink running = {max_dist, 0.0f};
			for (long i = x0; i <= x1; ++i) {
				running.dx = std::min(max_dist, running.dx + 1);

				const long x = leftward ? x0 + x1 - i : i;
				const float coverage_value = coverage[row + x];
				if (coverage_value > 0.0f) {
					const horizontal_nearest_ink here = {0.0f, 0.5f - coverage_value};
					if (here.dist() < running.dist()) {
						running = here;
					}
				}
				if (running.dist() < horizontal[row + x].dist()) {
					horizontal[row + x] = running;
				}
			}
		}
	}

	for (long y = y0; y <= y1; ++y) {
		for (long x = x0; x <= x1; ++x) {
			float best = max_dist;
			for (long dy = -reach; dy <= reach; ++dy) {
				const long yy = y + dy;
				if (yy < 0 || yy >= height) {
					continue;
				}
				const horizontal_nearest_ink &ink =
					horizontal[static_cast<std::size_t>(yy) * width + x];
				const float squared = ink.dx * ink.dx + static_cast<float>(dy * dy);
				if (squared >= (best + 0.5f) * (best + 0.5f)) {
					continue;
				}
				best = std::min(best, std::sqrt(squared) + ink.edge_offset);
			}
			dist[static_cast<std::size_t>(y) * width + x] = best;
		}
	}
	return dist;
}

void box_horizontal_blur(const std::vector<float> &source, std::vector<float> &target, const long width,
			 const long height, const long radius)
{
	const float scale = 1.0f / (radius * 2 + 1);
	for (long y = 0; y < height; ++y) {
		const std::size_t row = static_cast<std::size_t>(y) * width;
		float sum = 0;
		for (long x = 0; x <= radius && x < width; ++x) {
			sum += source[row + x];
		}
		for (long x = 0; x < width; ++x) {
			target[row + x] = sum * scale;
			if (x + radius + 1 < width) {
				sum += source[row + x + radius + 1];
			}
			if (x - radius >= 0) {
				sum -= source[row + x - radius];
			}
		}
	}
}

void box_vertical_blur(const std::vector<float> &source, std::vector<float> &target, const long width,
		       const long height, const long radius)
{
	const float scale = 1.0f / (radius * 2 + 1);
	for (long x = 0; x < width; ++x) {
		float sum = 0;
		for (long y = 0; y <= radius && y < height; ++y) {
			sum += source[static_cast<std::size_t>(y) * width + x];
		}
		for (long y = 0; y < height; ++y) {
			target[static_cast<std::size_t>(y) * width + x] = sum * scale;
			if (y + radius + 1 < height) {
				sum += source[static_cast<std::size_t>(y + radius + 1) * width + x];
			}
			if (y - radius >= 0) {
				sum -= source[static_cast<std::size_t>(y - radius) * width + x];
			}
		}
	}
}

std::vector<float> shadow_alpha(const std::vector<float> &shape, const long width, const long height,
				const shadow_style &shadow)
{
	constexpr int passes = 3;

	// SVG の feGaussianBlur と同じ近似。幅 d = floor(sigma * 3 * sqrt(2 * pi) / 4 + 0.5) のボックスを 3 回重ねると
	// ガウシアンに収束する。3 * sqrt(2 * pi) / 4 = 1.88。
	const double sigma = shadow.blur / 2;
	const long radius = std::max(1L, static_cast<long>(std::floor(sigma * 1.88 + 0.5)) / 2);

	std::vector<float> blur = shape;
	std::vector<float> horizontal_blur(shape.size());
	for (int pass = 0; pass < passes; ++pass) {
		box_horizontal_blur(blur, horizontal_blur, width, height, radius);
		box_vertical_blur(horizontal_blur, blur, width, height, radius); // ここの改変は問題ない？
	}

	std::vector<float> result(shape.size(), 0.0f);
	const long offset_y = std::lround(shadow.offset);
	for (long y = 0; y < height; ++y) {
		const long source_y = y - offset_y;
		if (source_y < 0 || source_y >= height) {
			continue;
		}
		for (long x = 0; x < width; ++x) {
			result[static_cast<std::size_t>(y) * width + x] =
				blur[static_cast<std::size_t>(source_y) * width + x] *
				static_cast<float>(shadow_style::opacity);
		}
	}
	return result;
}
} // namespace

rendered_text composite_text(const std::vector<text_layer> &layers, const composite_style &style)
{
	if (layers.empty() || !layers.front().coverage.valid()) {
		return {};
	}
	const text_coverage &first = layers.front().coverage;
	const long width = first.width;
	const long height = first.height;
	const std::size_t size = first.pixels.size();

	for (const text_layer &layer : layers) {
		if (layer.coverage.width != first.width || layer.coverage.height != first.height ||
		    layer.coverage.pixels.size() != size) {
			return {};
		}
	}

	std::vector<float> fill(size, 0.0f);
	std::vector<float> ring(size, 0.0f);
	for (const text_layer &layer : layers) {
		for (std::size_t i = 0; i < size; ++i) {
			fill[i] = std::max(fill[i], layer.coverage.pixels[i]);
		}
		if (layer.outline_width_px <= 0) {
			continue;
		}
		const auto w = static_cast<float>(layer.outline_width_px);
		const std::vector<float> dist = distance_to_ink(layer.coverage.pixels, width, height, w + 1);
		for (std::size_t i = 0; i < size; ++i) {
			ring[i] = std::max(ring[i], std::clamp(w + 0.5f - dist[i], 0.0f, 1.0f));
		}
	}

	const rgba fill_color = decode(style.color);
	const rgba outline_color = decode(style.outline_color);
	std::vector<float> pixels(size * 4, 0.0f);
	for (std::size_t i = 0; i < size; ++i) {
		float *pixel = &pixels[i * 4];
		blend_over(pixel, outline_color, ring[i] * outline_color.a);
		blend_over(pixel, fill_color, fill[i] * fill_color.a);
	}

	if (style.shadow) {
		std::vector<float> shape(size);
		for (std::size_t i = 0; i < shape.size(); ++i) {
			shape[i] = pixels[i * 4 + 3];
		}
		const std::vector<float> shadow = shadow_alpha(shape, width, height, *style.shadow);

		for (std::size_t i = 0; i < shape.size(); ++i) {
			float &alpha = pixels[i * 4 + 3];
			alpha += shadow[i] * (1.0f - alpha);
		}
	}

	rendered_text result = {
		.width = static_cast<std::uint32_t>(width),
		.height = static_cast<std::uint32_t>(height),
	};
	result.pixels.resize(pixels.size());
	for (std::size_t i = 0; i < pixels.size(); ++i) {
		result.pixels[i] = to_byte(pixels[i]);
	}
	return result;
}
