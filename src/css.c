#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include "css.h"
#include "color.h"
#include "log.h"
#include "theme.h"
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

int index_of(char *str, char search)
{
  char *ptr;
  ptr = strchr(str, search);
  if (ptr == NULL) {
    return -1;
  }

  return ptr - str;
}


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
  return (str == NULL && str2 == NULL) ||
    (str == NULL && strlen(str2) == 0) ||
    (str2 == NULL && strlen(str) == 0) ||
    (str != NULL && str2 != NULL && strcmp(str, str2) == 0);
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
    if (str_equals(attr->name, attr_name)) {
      return attr;
    }
  }
  return NULL;
}

enum shape css_get_attr_shape(struct css_rule *rule, char *attr_name)
{
  struct css_attr *attr = find_attr(rule, attr_name);
  if (attr == NULL) {
    log_debug("rule does not have attr '%s'\n", attr_name);
    log_debug("attrs available: %d\n", rule->count);
    for (int i = 0; i < rule->count; i++) {
      log_debug("%d: %s\n", i, rule->attrs[i].name);
    }
    exit(-1);
  }

  if (attr->unit != SHAPE) {
    log_debug("cannot convert type '%d' to shape (%s: %s)\n", attr->unit, attr->name, attr->value);
    exit(-1);
  }

  enum shape shape = atoi(attr->value);
  return shape;
}

struct directional css_get_attr_dir(struct css_rule *rule, char *attr_name)
{
  if (str_equals(attr_name, "padding")) {
    struct directional dir = {
      .left = css_get_attr_int(rule, "padding-left"),
      .bottom = css_get_attr_int(rule, "padding-bottom"),
      .top = css_get_attr_int(rule, "padding-top"),
      .right = css_get_attr_int(rule, "padding-right")
    };
    return dir;
  }
}

struct color css_get_attr_color(struct css_rule *rule, char *attr_name)
{
  struct css_attr *attr = find_attr(rule, attr_name);
  if (attr == NULL) {
    log_debug("rule '%s' does not have attr '%s'\n", rule->selector.str_repr, attr_name);
    log_debug("available attrs: %d\n", rule->count);
    exit(-1);
  }

  if (attr->unit != HEX_COLOR) {
    log_debug("cannot convert type '%d' to color (%s: %s)\n", attr->unit, attr->name, attr->value);
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
    return 0;
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

  if (attr->unit == EM) {
    value *= 24;
  }

  return value;

}


bool selector_match(struct css_selector *rule_selector,
    struct css_selector *query_selector)
{

  if (!str_equals(rule_selector->element, query_selector->element)) {
    return false;
  }

  if (rule_selector->pseudo_element != NULL && strlen(rule_selector->pseudo_element) > 0) {
    if (!str_equals(rule_selector->pseudo_element, query_selector->pseudo_element)) {
      return false;
    }
  }

  return true;
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

char *css_extract_element(char *query) {
  int end;
  int len = strlen(query);

  for (end = 1; end < len; end++) {
    if (query[end] == '.' || query[end] == ':')
      break;
  }

  char *element = (char *)xmalloc(sizeof(char) * (end+ 1));
  strncpy(element, query, end);
  element[end] = '\0';

  log_debug("css_extract_element(\"%s\") -> %s\n", query, element);
  return element;
}

char *css_extract_pseudo_element(char *query) {
  int start = index_of(query, ':');

  if (start < 0) {
    return NULL;
  }

  start++;
  char *str = query + start;

  if (str[0] != ':') {
    return NULL;
  }

  start++;
  str = query + start;
  int len = strlen(str);
  int end;

  for (end = 1; end < len; end++) {
    if (str[end] == '.' || str[end] == ':')
      break;
  }

  char *element = (char *)xmalloc(sizeof(char) * (end + 1));
  strncpy(element, str, end);
  element[end] = '\0';
  log_debug("css_extract_pseudo_element(\"%s\") -> %s\n", query, element);
  return element;
}

char *css_extract_class(char *query, int *pos) {
  int start = *pos;
  char *str = query + start;
  start = index_of(str, '.');

  if (start < 0) {
    return NULL;
  }

  start += *pos;
  start++;
  str = str + start;

  if (str[0] == ':') {
    return NULL;
  }

  int end;

  for (end = 1; end < strlen(str); end++) {
    if (str[end] == '.' || str[end] == ':')
      break;
  }

  char *element = (char *)xmalloc(sizeof(char) * (end + 1));
  strncpy(element, str, end);
  element[end] = '\0';
  *pos = start + end;
  return element;
}

struct css_classes css_extract_classes(char *query) {
  struct css_classes classes;

  for (int i = 0; i < 10; i++) {
    classes.classes[i] = NULL;
  }

  int count = 0;
  int pos = 0;
  while (true) {
    classes.classes[count] = css_extract_class(query, &pos);
    if (classes.classes[count] == NULL) {
      break;
    }
    count++;
    if (count >= 9)
      exit(0);
  }

  return classes;
}

char *css_extract_pseudo_class(char *query, int *pos) {
  int start = *pos;
  char *str = query + start;
  start = index_of(str, ':');

  if (start < 0) {
    return NULL;
  }

  start++;
  str = str + start;

  if (str[0] == ':') {
    return NULL;
  }

  int end;

  for (end = 1; end < strlen(str); end++) {
    if (str[end] == '.' || str[end] == ':')
      break;
  }

  char *element = (char *)xmalloc(sizeof(char) * (end + 1));
  strncpy(element, str, end);
  element[end] = '\0';
  *pos = start + end;
  return element;
}

struct css_classes css_extract_pseudo_classes(char *query) {
  struct css_classes classes;

  for (int i = 0; i < 10; i++) {
    classes.classes[i] = NULL;
  }

  int count = 0;
  int pos = 0;
  while (true) {
    classes.classes[count] = css_extract_pseudo_class(query, &pos);
    if (classes.classes[count] == NULL) {
      break;
    }
    count++;
  }

  return classes;
}

struct css_rule css_select(struct css *css, char *query)
{
  struct css_rule rule = {
    .selector = {
      .element = css_extract_element(query),
      .pseudo_element = css_extract_pseudo_element(query),
      .classes = css_extract_classes(query),
      .pseudo_classes = css_extract_pseudo_classes(query),
      .str_repr = query
    },
    .count = 0,
    .size = 128,
    .attrs = xcalloc(128, sizeof(struct css_attr*))
  };

  for (int i = 0; i < css->count; i++) {
    struct css_rule *rule_o = &css->rules[i];
    if (selector_match(&rule_o->selector, &rule.selector)) {
      for (int j = 0; j < rule_o->count; j++) {
        struct css_attr *attr = &rule_o->attrs[j];
        css_rule_apply_attr(&rule, attr);
      }
    }
  }

  return rule;

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

  char *sub = (char *)xmalloc(sizeof(char) * index);
  strncpy(sub, str, index);
  sub[index] = '\0';
  trim(sub);
  return sub;
}

void parse_selector(char *data, struct css_selector *selector){
  log_debug("parsing selector '%s'\n", data);
  selector->element = css_extract_element(data);
  selector->pseudo_element = css_extract_pseudo_element(data);
  selector->classes = css_extract_classes(data);
  selector->pseudo_classes = css_extract_pseudo_classes(data);
  selector->str_repr = data;
}

void rule_add_attr_v(struct css_rule *rule, char *name, char *value)
{
  struct css_attr *attr = &rule->attrs[rule->count];

  if (str_endswith(value, "px")) {
    attr->unit = PX;
    int len = strlen(value);
    value[len-1] = '\0';
    value[len-2] = '\0';
  } else if (str_endswith(value, "em")) {
    attr->unit = EM;
    int len = strlen(value);
    value[len-1] = '\0';
    value[len-2] = '\0';
  } else if (str_endswith(name, "-shape")) {
    attr->unit = SHAPE;
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
  attr->name = xmalloc(sizeof(char) * size_name);
  strncpy(attr->name, name, size_name);
  attr->name[size_name] = '\0';

  size_t size_value = strlen(value) + 1;
  attr->value = xmalloc(sizeof(char) * size_value);
  strncpy(attr->value, value, size_value);
  attr->value[size_value] = '\0';

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
  } else if (str_equals(name, "caret")) {
    int index_space = index_of(value, ' ');
    char *shape = value + index_space + 1;
    value[index_space] = '\0';
    char *color = value;

    size_t color_name_size = strlen(name) + strlen("-color") + 1;
    char color_name [color_name_size];
    sprintf(color_name, "%s-color", name);

    size_t shape_name_size = strlen(name) + strlen("-shape") + 1;
    char shape_name [shape_name_size];
    sprintf(shape_name, "%s-shape", name);

    rule_add_attr_v(rule, color_name, color);
    rule_add_attr_v(rule, shape_name, shape);
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
  rule->attrs = xmalloc(rule->size * sizeof(struct css_rule*));


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
    free(attr);
  }

  *pos += rule_len + 1;
  return true;
}

struct css css_parse(char *data)
{

  struct css css = {
    .count = 0,
    .size = 128,
    .rules = xmalloc(css.size * sizeof(struct css_rule))
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
