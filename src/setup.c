#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "setup.h"
#include "tofi.h"
#include "color.h"
#include "config.h"
#include "css.h"
#include "log.h"
#include "nelem.h"
#include "scale.h"
#include "unicode.h"
#include "xmalloc.h"

void apply_window_css(struct tofi *tofi, struct css_rule *rule) {
  tofi->window.width = css_get_attr_int(rule, "width");
  tofi->window.height = css_get_attr_int(rule, "height");
  tofi->window.scale = css_get_attr_int(rule, "scale");
  char *font_family = css_get_attr_str(rule, "font-family");
  strncpy(tofi->window.engine.font_name, font_family, strlen(font_family));
  tofi->window.engine.font_size = css_get_attr_int(rule, "font-size");
  tofi->window.engine.font_size = css_get_attr_int(rule, "font-size");
  log_debug("applying anchor\n");
  tofi->anchor = css_get_attr_int(rule, "anchor");
}

void apply_body_css(struct tofi *tofi, struct css_rule *rule) {
  tofi->window.engine.padding_top = css_get_attr_int(rule, "padding-top");
  tofi->window.engine.padding_bottom = css_get_attr_int(rule, "padding-bottom");
  tofi->window.engine.padding_left = css_get_attr_int(rule, "padding-left");
  tofi->window.engine.padding_right = css_get_attr_int(rule, "padding-right");

  tofi->window.engine.border_width = css_get_attr_int(rule, "border-width");
  struct color border_color = css_get_attr_color(rule, "border-color");
  color_copy(&border_color, &tofi->window.engine.border_color);

  tofi->window.engine.outline_width = css_get_attr_int(rule, "outline-width");
  struct color outline_color = css_get_attr_color(rule, "outline-color");
  color_copy(&outline_color, &tofi->window.engine.outline_color);
}

void apply_prompt_css(struct tofi *tofi, struct css_rule *rule) {
  char *text = css_get_attr_str(rule, "text");
  strncpy(tofi->window.engine.prompt_text, text, strlen(text));

  struct color color = css_get_attr_color(rule, "color");
  color_copy(&color, &tofi->window.engine.prompt_theme.foreground_color);
  tofi->window.engine.prompt_theme.foreground_specified = true;
}

void apply_placeholder_css(struct tofi *tofi, struct css_rule *rule) {
  char *text = css_get_attr_str(rule, "text");
  strncpy(tofi->window.engine.placeholder_text, text, strlen(text));

  struct color color = css_get_attr_color(rule, "color");
  color_copy(&color, &tofi->window.engine.placeholder_theme.foreground_color);
  tofi->window.engine.placeholder_theme.foreground_specified = true;
}

void setup_apply_config(struct tofi *tofi)
{
  struct css *parsed_css = tofi->window.engine.css;
  struct css_rule window = css_select(parsed_css, "window");
  apply_window_css(tofi, &window);

  struct css_rule body = css_select(parsed_css, "body");
  apply_body_css(tofi, &body);

  struct css_rule prompt = css_select(parsed_css, "prompt");
  apply_prompt_css(tofi, &prompt);

  struct css_rule placeholder = css_select(parsed_css, "input::placeholder");
  apply_placeholder_css(tofi, &placeholder);

  tofi->use_history = use_history;
  tofi->require_match = require_match;
  tofi->fuzzy_match = fuzzy_match;
  tofi->multiple_instance = multiple_instance;
  tofi->window.exclusive_zone = exclusive_zone;

  config_fixup_values(tofi);
}

void config_fixup_values(struct tofi *tofi)
{
  uint32_t base_width = tofi->output_width;
  uint32_t base_height = tofi->output_height;
  uint32_t scale;
  if (tofi->window.fractional_scale != 0) {
    scale = tofi->window.fractional_scale;
  } else {
    scale = tofi->window.scale * 120;
  }

  /*
   * If we're going to be scaling these values in Cairo,
   * we need to apply the inverse scale here.
   */
  if (tofi->use_scale) {
    base_width = scale_apply_inverse(base_width, scale);
    base_height = scale_apply_inverse(base_height, scale);
  }

  /*
   * Window width and height are a little special. We're only going to be
   * using them to specify sizes to Wayland, which always wants scaled
   * pixels, so always scale them here (unless we've directly specified a
   * scaled size).
   */
  if (tofi->window.width_is_percent || !tofi->use_scale) {
    tofi->window.width = scale_apply_inverse(tofi->window.width, scale);
  }
  if (tofi->window.height_is_percent || !tofi->use_scale) {
    tofi->window.height = scale_apply_inverse(tofi->window.height, scale);
  }
}
