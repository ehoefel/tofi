#ifndef THEME_H
#define THEME_H

#include <uchar.h>
#include "color.h"

enum cursor_style {
	CURSOR_STYLE_BAR,
	CURSOR_STYLE_BLOCK,
	CURSOR_STYLE_UNDERSCORE
};

struct directional {
	int32_t top;
	int32_t right;
	int32_t bottom;
	int32_t left;
};

struct text_theme {
	struct color foreground_color;
	struct color background_color;
	struct directional padding;
	uint32_t background_corner_radius;

	bool foreground_specified;
	bool background_specified;
	bool padding_specified;
	bool radius_specified;
};

struct cursor_theme {
	struct color color;
	struct color text_color;
	enum cursor_style style;
	uint32_t corner_radius;
	uint32_t thickness;

	double underline_depth;
	double em_width;

	bool color_specified;
	bool text_color_specified;
	bool thickness_specified;

	bool show;
};

struct text_theme text_theme_copy(const struct text_theme *restrict src);

#endif /* THEME_H */
