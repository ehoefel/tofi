#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include "css.h"
#include "log.h"
#include "xmalloc.h"

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

struct css_line css_get_attr_line(struct css_rule *rule, char *attr_name)
{

}

struct color css_get_attr_color(struct css_rule *rule, char *attr_name)
{

}

char *css_get_attr_str(struct css_rule *rule, char *attr_name)
{

}

int css_get_attr_int(struct css_rule *rule, char *attr_name)
{
  struct css_attr *attr = find_attr(rule, attr_name);
  if (attr == NULL) {
    log_debug("rule does not have attr '%s'\n", attr_name);
    exit(-1);
  }
  char *value_copy = xstrdup(attr->value);
  int len = strlen(value_copy);
  if (value_copy[len-1] == 'x') {
    value_copy[len-1] = '\0';
    len = strlen(value_copy);
  }
  if (value_copy[len-1] == 'p') {
    value_copy[len-1] = '\0';
    len = strlen(value_copy);
  }
  int value = atoi(value_copy);
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
      return;
    }
  }
  struct css_attr *attr_o = &rule->attrs[rule->count];
  attr_o->name = attr->name;
  attr_o->value = attr->value;
  rule->count++;
}

struct css_rule css_select(struct css *css, char *query)
{
  struct css_rule rule = {
    .count = 0,
    .size = 128,
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

char *css_substring(char *data, uint32_t *pos, char delimiter)
{
  uint32_t start = *pos;
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

void parse_attr(char *data, struct css_attr *attr)
{
  uint32_t pos = 0;
  attr->name = css_substring(data, &pos, ':');
  attr->value = css_substring(data, &pos, '\0');
}

bool parse_rule(char *data, uint32_t *pos, struct css_rule *rule)
{
  uint32_t start = *pos;
  char *str = data + start;
  int rule_len = index_of(str, '}');
  if (rule_len <= 0) {
    return false;
  }

  uint32_t curr_pos = 0;
  char *selector_str = css_substring(str, &curr_pos, '{');
  parse_selector(selector_str, &rule->selector);
  rule->count = 0;
  rule->size = 128;
  rule->attrs = malloc(rule->size * sizeof(struct css_rule*));


  while (true) {
    uint32_t next_pos = curr_pos;
    char *attr = css_substring(str, &next_pos, ';');
    if (attr == NULL || next_pos >= rule_len) {
      break;
    }

    parse_attr(attr, &rule->attrs[rule->count]);
    rule->count++;

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

  struct css_rule *rule;
  uint32_t curr_char = 0;
  while (true) {
    bool res = parse_rule(data, &curr_char, &css.rules[css.count]);
    if (!res) {
      break;
    }

    css.count++;
  }

  return css;

}
