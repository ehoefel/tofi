#ifndef COLOR_H
#define COLOR_H

#include <stdbool.h>
#include <stdint.h>

struct color {
	float r;
	float g;
	float b;
	float a;
};

struct color hex_to_color(const char *hex);
void color_copy(const struct color *a, struct color *b);
void color_set_from_hex(struct color *color, const char *hex);
struct color color_mix(struct color *a, struct color *b, float perc);

#endif /* COLOR_H */
