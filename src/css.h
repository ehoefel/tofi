#ifndef CSS_H
#define CSS_H

#include <stdio.h>
#include "color.h"

struct css_line {
  uint32_t width;
  struct color color;
};

struct css_selector {
  char *type;
  char **classes;
};

struct css_attr {
  char *name;
  char *value;
};

struct css_rule {
  struct css_selector selector;
  struct css_attr *attrs;
  size_t count;
  size_t size;
};

struct css {
  size_t count;
  size_t size;
  struct css_rule *rules;
};

struct css_line css_get_attr_line(struct css_rule *css_rule, char *attr_name);
struct color css_get_attr_color(struct css_rule *css_rule, char *attr_name);
char *css_get_attr_str(struct css_rule *css_rule, char *attr_name);
int css_get_attr_int(struct css_rule *css_rule, char *attr_name);
struct css_rule css_select(struct css *css, char *selector);
struct css css_parse(char *data);

#endif /* CSS_H */
