#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include "css.h"
#include "color.h"
#include "log.h"
#include "xmalloc.h"
#include "wlr-layer-shell-unstable-v1.h"

#define ANCHOR_TOP_LEFT (\
  ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP \
  | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT \
  )
#define ANCHOR_TOP (\
  ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP \
  | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT \
  | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT \
  )
#define ANCHOR_TOP_RIGHT (\
  ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP \
  | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT \
  )
#define ANCHOR_RIGHT (\
  ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT \
  | ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP \
  | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM \
  )
#define ANCHOR_BOTTOM_RIGHT (\
  ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM \
  | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT \
  )
#define ANCHOR_BOTTOM (\
  ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM \
  | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT \
  | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT \
  )
#define ANCHOR_BOTTOM_LEFT (\
  ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM \
  | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT \
  )
#define ANCHOR_LEFT (\
  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT \
  | ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP \
  | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM \
  )
#define ANCHOR_CENTER (\
  ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP \
  | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM \
  | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT \
  | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT \
  )

bool str_endswith(char *str, char *end)
{
  return strcmp(str + (strlen(str) - strlen(end)), end) == 0;
}

bool str_startswith(char *str, char *start)
{
  return strncmp(str, start, strlen(start)) == 0;
}

bool str_equals(char *str, char *str2)
{
  return strcmp(str, str2) == 0;
}

char *trim(char *str)
{
    size_t len = 0;
    char *frontp = str;
    char *endp = NULL;

    if( str == NULL ) { return NULL; }
    if( str[0] == '\0' ) { return str; }

    len = strlen(str);
    endp = str + len;

    /* Move the front and back pointers to address the first non-whitespace
     * characters from each end.
     */
    while( isspace((unsigned char) *frontp) ) { ++frontp; }
    if( endp != frontp )
    {
        while( isspace((unsigned char) *(--endp)) && endp != frontp ) {}
    }

    if( frontp != str && endp == frontp )
            *str = '\0';
    else if( str + len - 1 != endp )
            *(endp + 1) = '\0';

    /* Shift the string so that it starts at str so that if it's dynamically
     * allocated, we can still free it on the returned pointer.  Note the reuse
     * of endp to mean the front of the string buffer now.
     */
    endp = str;
    if( frontp != str )
    {
            while( *frontp ) { *endp++ = *frontp++; }
            *endp = '\0';
    }

    return str;
}

struct css_attr *find_attr(struct css_rule *rule, char *attr_name)
{
  for (int i = 0; i < rule->count; i++) {
    struct css_attr *attr = &rule->attrs[i];
    if (strcmp(attr->name, attr_name) == 0) {
      return attr;
    }
  }
  return NULL;
}

struct color css_get_attr_color(struct css_rule *rule, char *attr_name)
{
  struct css_attr *attr = find_attr(rule, attr_name);
  if (attr == NULL) {
    log_debug("rule does not have attr '%s'\n", attr_name);
    exit(-1);
  }

  if (attr->unit != HEX_COLOR) {
    log_debug("cannot convert type '%d' to color\n", attr->unit);
    exit(-1);
  }
  struct color color = hex_to_color(attr->value);
  return color;
}

char *css_get_attr_str(struct css_rule *rule, char *attr_name)
{
  struct css_attr *attr = find_attr(rule, attr_name);
  if (attr == NULL) {
    log_debug("rule does not have attr '%s'\n", attr_name);
    exit(-1);
  }

  return attr->value;

}

int css_get_attr_int(struct css_rule *rule, char *attr_name)
{
  struct css_attr *attr = find_attr(rule, attr_name);
  if (attr == NULL) {
    log_debug("rule does not have attr '%s'\n", attr_name);
    exit(-1);
  }

  if (attr->unit == LITERAL && str_equals(attr->name, "anchor")) {
    if (str_equals(attr->value, "center")) {
      return ANCHOR_CENTER;
    } else if (str_equals(attr->value, "top")) {
      return ANCHOR_TOP;
    } else if (str_equals(attr->value, "left")) {
      return ANCHOR_LEFT;
    } else if (str_equals(attr->value, "top-left")) {
      return ANCHOR_TOP_LEFT;
    } else if (str_equals(attr->value, "right")) {
      return ANCHOR_RIGHT;
    } else if (str_equals(attr->value, "top-right")) {
      return ANCHOR_TOP_RIGHT;
    } else if (str_equals(attr->value, "bottom")) {
      return ANCHOR_BOTTOM;
    } else if (str_equals(attr->value, "bottom-left")) {
      return ANCHOR_BOTTOM_LEFT;
    } else if (str_equals(attr->value, "bottom-right")) {
      return ANCHOR_BOTTOM_RIGHT;
    } else {
      log_debug("unknown value for anchor '%s'\n", attr->value);
      exit(-1);
    }
  }
  int value = atoi(attr->value);
  return value;

}

bool selector_match(struct css_selector *selector, char *query)
{
  return strcmp(selector->type, query) == 0;
}

void css_rule_apply_attr(struct css_rule *rule, struct css_attr *attr)
{
  for (int i = 0; i < rule->count; i++) {
    struct css_attr *attr_o = &rule->attrs[i];
    if (strcmp(attr_o->name, attr->name) == 0) {
      attr_o->value = attr->value;
      attr_o->unit = attr->unit;
      return;
    }
  }
  struct css_attr *attr_o = &rule->attrs[rule->count];
  attr_o->name = attr->name;
  attr_o->value = attr->value;
  attr_o->unit = attr->unit;
  rule->count++;
}

struct css_rule css_select(struct css *css, char *query)
{
  struct css_rule rule = {
    .count = 0,
    .size = 20,
    .attrs = malloc(rule.size * sizeof(struct css_attr*))
  };

  for (int i = 0; i < css->count; i++) {
    struct css_rule *rule_o = &css->rules[i];
    if (selector_match(&rule_o->selector, query)) {
      for (int j = 0; j < rule_o->count; j++) {
        struct css_attr *attr = &rule_o->attrs[j];
        css_rule_apply_attr(&rule, attr);
      }
    }
  }

  return rule;

}

int index_of(char *str, char search)
{
  char *ptr;
  ptr = strchr(str, search);
  if (ptr == NULL) {
    return -1;
  }

  return ptr - str;
}

char *css_substring(char *data, int *pos, char delimiter)
{
  int start = *pos;
  char *str = data + start;
  int index = index_of(str, delimiter);
  if (index < 0) {
    return NULL;
  }

  if (index == 0)
  {
    return "";
  }

  *pos += index + 1;

  char *sub = (char *)malloc(sizeof(char) * index);
  strncpy(sub, str, index);
  sub[index] = '\0';
  trim(sub);
  return sub;
}

void parse_selector(char *data, struct css_selector *selector){
  selector->type = data;
}

void rule_add_attr_v(struct css_rule *rule, char *name, char *value)
{
  struct css_attr *attr = &rule->attrs[rule->count];

  if (str_endswith(value, "px")) {
    attr->unit = PX;
    int len = strlen(value);
    value[len-1] = '\0';
    value[len-2] = '\0';
  } else if (str_startswith(value, "#")) {
    attr->unit = HEX_COLOR;
  } else if (str_startswith(value, "\"") && str_endswith(value, "\"")) {
    attr->unit = TEXT;
    value[strlen(value)-1] = '\0';
    value++;
  } else if (strspn(value, "-+0123456789")) {
    attr->unit = INT;
  } else if (strspn(value, "-+0123456789%")) {
    attr->unit = PERCENT;
  } else if (strspn(value, "abcdefghijklmnopqrstuvwxyz")) {
    attr->unit = LITERAL;
  } else {
    log_debug("Could not recognize type of value '%s'\n", value);
    exit(-1);
  }

  size_t size_name = strlen(name) + 1;
  attr->name = malloc(sizeof(char) * size_name);
  strncpy(attr->name, name, size_name);

  size_t size_value = strlen(value) + 1;
  attr->value = malloc(sizeof(char) * size_value);
  strncpy(attr->value, value, size_value);

  rule->count++;
}

void rule_add_attr(struct css_rule *rule, struct css_attr *attr)
{
  char *name = attr->name;
  char *value = attr->value;
  if (str_equals(name, "padding")) {
    rule_add_attr_v(rule, "padding-left", value);
    rule_add_attr_v(rule, "padding-bottom", value);
    rule_add_attr_v(rule, "padding-top", value);
    rule_add_attr_v(rule, "padding-right", value);
  } else if (str_equals(name, "border") || str_equals(name, "outline")) {
    int index_space = index_of(value, ' ');
    char *color = value + index_space + 1;
    value[index_space] = '\0';
    char *width = value;

    size_t color_name_size = strlen(name) + strlen("-color") + 1;
    char color_name [color_name_size];
    sprintf(color_name, "%s-color", name);

    size_t width_name_size = strlen(name) + strlen("-width") + 1;
    char width_name [width_name_size];
    sprintf(width_name, "%s-width", name);

    rule_add_attr_v(rule, color_name, color);
    rule_add_attr_v(rule, width_name, width);
  } else {
    rule_add_attr_v(rule, name, value);
  }

}

void parse_attr(char *data, struct css_attr *attr)
{
  int pos = 0;
  attr->name = css_substring(data, &pos, ':');
  attr->value = css_substring(data, &pos, '\0');
}

bool parse_rule(char *data, int *pos, struct css_rule *rule)
{
  int start = *pos;
  char *str = data + start;
  int rule_len = index_of(str, '}');
  if (rule_len <= 0) {
    return false;
  }

  int curr_pos = 0;
  char *selector_str = css_substring(str, &curr_pos, '{');
  parse_selector(selector_str, &rule->selector);
  rule->count = 0;
  rule->size = 128;
  rule->attrs = malloc(rule->size * sizeof(struct css_rule*));


  while (true) {
    int next_pos = curr_pos;
    char *attr = css_substring(str, &next_pos, ';');
    if (attr == NULL || next_pos >= rule_len) {
      break;
    }

    struct css_attr parsed_attr;
    parse_attr(attr, &parsed_attr);
    rule_add_attr(rule, &parsed_attr);

    curr_pos = next_pos;
  }

  *pos += rule_len + 1;
  return true;
}

struct css css_parse(char *data)
{

  struct css css = {
    .count = 0,
    .size = 128,
    .rules = malloc(css.size * sizeof(struct css_rule*))
  };

  int curr_char = 0;
  while (true) {
    bool res = parse_rule(data, &curr_char, &css.rules[css.count]);
    if (!res) {
      break;
    }

    css.count++;
  }

  return css;

}
