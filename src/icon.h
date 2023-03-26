#ifndef ICON_H
#define ICON_H

#include "color.h"

struct icon {
  char *text;
  struct color *color;
  int adjust_x;
  int adjust_y;
};

[[nodiscard("memory leaked")]]
void icon_init(struct icon *restrict icon, char *text);
void icon_adjust(struct icon *restrict icon);
void icon_destroy(struct icon *restrict icon);
#endif /* ICON_H */
