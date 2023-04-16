#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <pango/pango.h>
#include "pango_css.h"
#include "css.h"
#include "engine.h"
#include "icon.h"
#include "log.h"
#include "nelem.h"
#include "unicode.h"
#include "xmalloc.h"

#undef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#undef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))

const int32_t char_width = 24;
const int32_t char_height = 42;

static double cursor_underline_depth;
static double cursor_underline_thickness;

static void rounded_rectangle(cairo_t *cr, uint32_t width, uint32_t height, uint32_t r)
{
  cairo_new_path(cr);

  /* Top-left */
  cairo_arc(cr, r, r, r, -M_PI, -M_PI_2);

  /* Top-right */
  cairo_arc(cr, width - r, r, r, -M_PI_2, 0);

  /* Bottom-right */
  cairo_arc(cr, width - r, height - r, r, 0, M_PI_2);

  /* Bottom-left */
  cairo_arc(cr, r, height - r, r, M_PI_2, M_PI);

  cairo_close_path(cr);
}

static void render_text(
    cairo_t *cr,
    struct engine *engine,
    const char *text,
    const struct css_rule *css,
    PangoRectangle *ink_rect,
    PangoRectangle *logical_rect)
{
  PangoLayout *layout = engine->pango.layout;
  struct color color = css_get_attr_color(css, "color");
  cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);

  pango_layout_set_text(layout, text, -1);
  pango_cairo_update_layout(cr, layout);
  pango_cairo_show_layout(cr, layout);

  pango_layout_get_pixel_extents(layout, ink_rect, logical_rect);
}

static void render_input(
    cairo_t *cr,
    PangoLayout *layout,
    const char *text,
    uint32_t text_length,
    const struct css_rule *css,
    uint32_t cursor_position,
    PangoRectangle *ink_rect,
    PangoRectangle *logical_rect)
{
  struct directional padding = css_get_attr_dir(css, "padding");
  struct color color = css_get_attr_color(css, "color");
  cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);

  pango_layout_set_text(layout, text, -1);
  pango_cairo_update_layout(cr, layout);
  pango_cairo_show_layout(cr, layout);

  pango_layout_get_pixel_extents(layout, ink_rect, logical_rect);

  double extra_cursor_advance = 0;
  if (cursor_position == text_length) {
    /*
    switch (cursor_theme->style) {
      case CURSOR_STYLE_BAR:
        extra_cursor_advance = cursor_theme->thickness;
        break;
      case CURSOR_STYLE_BLOCK:
        extra_cursor_advance = cursor_theme->em_width;
        break;
      case CURSOR_STYLE_UNDERSCORE:
        extra_cursor_advance = cursor_theme->em_width;
        break;
    }
    */
    extra_cursor_advance += logical_rect->width
      - logical_rect->x
      - ink_rect->width;
  }

  struct color bg_color = css_get_attr_color(css, "background-color");
  if (bg_color.a != 0) {
    cairo_save(cr);
    cairo_set_source_rgba(cr, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
    cairo_translate(
        cr,
        floor(-padding.left + ink_rect->x),
        -padding.top);
    rounded_rectangle(
        cr,
        ceil(ink_rect->width + extra_cursor_advance + padding.left + padding.right),
        ceil(logical_rect->height + padding.top + padding.bottom),
        0
        );
    cairo_fill(cr);
    cairo_restore(cr);

    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);

    pango_cairo_show_layout(cr, layout);
  }

  double cursor_x;
  double cursor_width;

  if (cursor_position == text_length) {
    cursor_x = logical_rect->width + logical_rect->x;
    cursor_width = logical_rect->width;
  } else {
    /*
     * Pango wants a byte index rather than a character index for
     * the cursor position, so we have to calculate that here.
     */
    const char *tmp = text;
    for (size_t i = 0; i < cursor_position; i++) {
      tmp = utf8_next_char(tmp);
    }
    uint32_t start_byte_index = tmp - text;
    uint32_t end_byte_index = utf8_next_char(tmp) - text;
    PangoRectangle start_pos;
    PangoRectangle end_pos;
    pango_layout_get_cursor_pos(layout, start_byte_index, &start_pos, NULL);
    pango_layout_get_cursor_pos(layout, end_byte_index, &end_pos, NULL);
    cursor_x = (double)start_pos.x / PANGO_SCALE;
    cursor_width = (double)(end_pos.x - start_pos.x) / PANGO_SCALE;;
  }

  cairo_save(cr);
  struct color cursor = css_get_attr_color(css, "cursor-color");
  cairo_set_source_rgba(cr, cursor.r, cursor.g, cursor.b, cursor.a);
  cairo_translate(cr, cursor_x, 0);
  switch (css_get_attr_shape(css, "curshor-shape")) {
    case BAR:
      rounded_rectangle(cr, 2, logical_rect->height, 0);
      cairo_fill(cr);
      break;
    case BLOCK:
      rounded_rectangle(cr, cursor_width, logical_rect->height, 0);
      cairo_fill_preserve(cr);
      cairo_clip(cr);
      cairo_translate(cr, -cursor_x, 0);
      cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
      pango_cairo_show_layout(cr, layout);
      break;
    case UNDERSCORE:
      cairo_translate(cr, cursor_underline_depth, cursor_underline_thickness);
      rounded_rectangle(cr, cursor_width, cursor_underline_thickness, 0);
      cairo_fill(cr);
      break;
  }

  logical_rect->width += extra_cursor_advance;
  cairo_restore(cr);
}

void pango_init(struct engine *engine, uint32_t *width, uint32_t *height)
{
  cairo_t *cr = engine->cairo[0].cr;

  /* Setup Pango. */
  log_debug("Creating Pango context.\n");
  PangoContext *context = pango_cairo_create_context(cr);

  log_debug("Creating Pango font description.\n");
  PangoFontDescription *font_description =
    pango_font_description_from_string(engine->font_name);
  pango_font_description_set_size(
      font_description,
      engine->font_size * PANGO_SCALE);
  if (engine->font_variations[0] != 0) {
    pango_font_description_set_variations(
        font_description,
        engine->font_variations);
  }
  //pango_font_description_set_style(font_description, PANGO_STYLE_ITALIC);
  pango_context_set_font_description(context, font_description);

  engine->pango.layout = pango_layout_new(context);

  if (engine->font_features[0] != 0) {
    log_debug("Setting font features.\n");
    PangoAttribute *attr = pango_attr_font_features_new(engine->font_features);
    PangoAttrList *attr_list = pango_attr_list_new();
    pango_attr_list_insert(attr_list, attr);
    pango_layout_set_attributes(engine->pango.layout, attr_list);
  }

  log_debug("Loading Pango font.\n");
  PangoFontMap *map = pango_cairo_font_map_get_default();
  PangoFont *font = pango_font_map_load_font(map, context, font_description);
  PangoFontMetrics *metrics = pango_font_get_metrics(font, NULL);
  hb_font_t *hb_font = pango_font_get_hb_font(font);

  uint32_t m_codepoint;
  if (hb_font_get_glyph_from_name(hb_font, "m", -1, &m_codepoint)) {
    engine->cursor_theme.em_width = (double)hb_font_get_glyph_h_advance(hb_font, m_codepoint) / PANGO_SCALE;
  } else {
    engine->cursor_theme.em_width = (double)pango_font_metrics_get_approximate_char_width(metrics) / PANGO_SCALE;
  }

  cursor_underline_depth = (double)
    (
     pango_font_metrics_get_ascent(metrics)
     - pango_font_metrics_get_underline_position(metrics)
    ) / PANGO_SCALE;

  cursor_underline_thickness = pango_font_metrics_get_underline_thickness(metrics) / PANGO_SCALE;

  pango_font_metrics_unref(metrics);
  g_object_unref(font);
  log_debug("Loaded.\n");

  pango_font_description_free(font_description);

  engine->pango.context = context;
}

void pango_destroy(struct engine *engine)
{
  g_object_unref(engine->pango.layout);
  g_object_unref(engine->pango.context);
}

static bool size_overflows(struct engine *engine, uint32_t width, uint32_t height)
{
  cairo_t *cr = engine->cairo[engine->index].cr;
  cairo_matrix_t mat;
  cairo_get_matrix(cr, &mat);
  if (engine->horizontal) {
    if (mat.x0 + width > engine->clip_x + engine->clip_width) {
      return true;
    }
  } else {
    if (mat.y0 + height > engine->clip_y + engine->clip_height) {
      return true;
    }
  }
  return false;
}

/*
 * This is pretty much a direct translation of the corresponding function in
 * the harfbuzz backend. As that's the one that I care about most, there are
 * more explanatory comments than there are here, so go look at that if you
 * want to understand how tofi's text rendering works.
 */
void pango_update(struct engine *engine)
{
  cairo_t *cr = engine->cairo[engine->index].cr;
  PangoLayout *layout = engine->pango.layout;

  cairo_save(cr);

  /* Render the prompt */
  PangoRectangle ink_rect;
  PangoRectangle logical_rect;
  struct css_rule prompt_css = css_select(engine->css, "input::before")
  render_text(cr, engine, engine->prompt_text, &prompt_css, &ink_rect, &logical_rect);

  cairo_translate(cr, logical_rect.width + logical_rect.x, 0);
  int prompt_padding_right = css_get_attr_int(&prompt_css, "padding-right");

  /* Render the engine text */
  struct css_rule input_css = css_select(engine->css, "input")
  struct css_rule input_placeholder_css = css_select(engine->css, "input::placeholder")
  if (engine->input_utf8_length == 0) {
    render_input(
        cr,
        layout,
        engine->placeholder_text,
        utf8_strlen(engine->placeholder_text),
        &input_placeholder_css,
        0,
        &ink_rect,
        &logical_rect);
  } else {
    render_input(
        cr,
        layout,
        engine->input_utf8,
        engine->input_utf32_length,
        &input_css,
        engine->cursor_position,
        &ink_rect,
        &logical_rect);
  }
  logical_rect.width = MAX(logical_rect.width, (int)engine->input_width);

  uint32_t num_results;
  if (engine->num_results == 0) {
    num_results = engine->results.count;
  } else {
    num_results = MIN(engine->num_results, engine->results.count);
  }
  /* Render our results */
  size_t i;
  for (i = 0; i < num_results; i++) {

    cairo_translate(cr, 0, char_height + engine->result_spacing);
    if (engine->num_results == 0) {
      if (size_overflows(engine, 0, 0)) {
        break;
      }
    } else if (i >= engine->num_results) {
      break;
    }
    size_t index = i + engine->first_result;
    /*
     * We may be on the last page, which could have fewer results
     * than expected, so check and break if necessary.
     */
    if (index >= engine->results.count) {
      break;
    }

    const char *name, *comment;
    const struct icon *icon;
    if (i < engine->results.count) {
      struct entry_ref *result = engine->results.buf[index].entry;
      icon = result->icon;
      name = result->name;
      comment = result->comment;
    } else {
      name = "";
      comment = "";
    }

    struct css_rule icon_css = css_select(engine->css, "entry::before");

    struct css_rule result_css = css_select(engine->css, "entry");
    struct color color = css_get_attr_color(&result_css, "color");
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);

    if (engine->num_results > 0) {
      render_text(cr, engine, name, &result_css, &ink_rect, &logical_rect);
    } else {

      if (size_overflows(engine, 0, logical_rect.height)) {
        engine->num_results_drawn = i;
        break;
      }

      //int32_t padding = css_get_attr_int(icon_css, "padding-right")

      /*
      if (icon) {
        cairo_translate(cr, icon->adjust_x, icon->adjust_y);
        render_text_themed(cr, engine, icon->text, &theme_icon, &ink_rect, &logical_rect);
        cairo_translate(cr, -icon->adjust_x, -icon->adjust_y);
      }
      */

      render_text_themed(cr, engine, name, theme, &ink_rect, &logical_rect);
    }
  }
  engine->num_results_drawn = i;

  cairo_restore(cr);
}
