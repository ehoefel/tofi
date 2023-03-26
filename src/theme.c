#include <string.h>
#include <stdlib.h>
#include "theme.h"
#include "color.h"

struct text_theme text_theme_copy(const struct text_theme *restrict src) {

  struct text_theme copy = {
    .foreground_color = {
      .r = src->foreground_color.r,
      .g = src->foreground_color.g,
      .b = src->foreground_color.b,
      .a = src->foreground_color.a,
    },
    .foreground_specified = src->foreground_specified,
    .background_color = {
      .r = src->background_color.r,
      .g = src->background_color.g,
      .b = src->background_color.b,
      .a = src->background_color.a,
    },
    .background_specified = src->background_specified,
    .padding = {
      .top = src->padding.top,
      .right = src->padding.right,
      .bottom = src->padding.bottom,
      .left = src->padding.left,
    },
    .padding_specified = src->padding_specified,
    .background_corner_radius = src->background_corner_radius,
    .radius_specified = src->radius_specified
  };

  return copy;
}
