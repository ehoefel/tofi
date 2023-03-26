#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <pango/pango.h>
#include "../entry.h"
#include "../icon.h"
#include "../log.h"
#include "../nelem.h"
#include "../theme.h"
#include "../unicode.h"
#include "../xmalloc.h"

#undef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#undef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))

const int32_t char_width = 24;
const int32_t char_height = 42;

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

static void render_text_themed(
		cairo_t *cr,
		struct entry *entry,
		const char *text,
		const struct text_theme *theme,
		PangoRectangle *ink_rect,
		PangoRectangle *logical_rect)
{
	PangoLayout *layout = entry->pango.layout;
	struct color color = theme->foreground_color;
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
		const struct text_theme *theme,
		uint32_t cursor_position,
		const struct cursor_theme *cursor_theme,
		PangoRectangle *ink_rect,
		PangoRectangle *logical_rect)
{
	struct directional padding = theme->padding;
	struct color color = theme->foreground_color;
	cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);

	pango_layout_set_text(layout, text, -1);
	pango_cairo_update_layout(cr, layout);
	pango_cairo_show_layout(cr, layout);

	pango_layout_get_pixel_extents(layout, ink_rect, logical_rect);

	double extra_cursor_advance = 0;
	if (cursor_position == text_length && cursor_theme->show) {
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
		extra_cursor_advance += logical_rect->width
			- logical_rect->x
			- ink_rect->width;
	}

	if (theme->background_color.a != 0) {
		cairo_save(cr);
		color = theme->background_color;
		cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
		cairo_translate(
				cr,
				floor(-padding.left + ink_rect->x),
				-padding.top);
		rounded_rectangle(
				cr,
				ceil(ink_rect->width + extra_cursor_advance + padding.left + padding.right),
				ceil(logical_rect->height + padding.top + padding.bottom),
				theme->background_corner_radius
				);
		cairo_fill(cr);
		cairo_restore(cr);

		color = theme->foreground_color;
		cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);

		pango_cairo_show_layout(cr, layout);
	}

	if (!cursor_theme->show) {
		/* No cursor to draw, we're done. */
		return;
	}

	double cursor_x;
	double cursor_width;

	if (cursor_position == text_length) {
		cursor_x = logical_rect->width + logical_rect->x;
		cursor_width = cursor_theme->em_width;
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
	color = cursor_theme->color;
	cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
	cairo_translate(cr, cursor_x, 0);
	switch (cursor_theme->style) {
		case CURSOR_STYLE_BAR:
			rounded_rectangle(cr, cursor_theme->thickness, logical_rect->height, cursor_theme->corner_radius);
			cairo_fill(cr);
			break;
		case CURSOR_STYLE_BLOCK:
			rounded_rectangle(cr, cursor_width, logical_rect->height, cursor_theme->corner_radius);
			cairo_fill_preserve(cr);
			cairo_clip(cr);
			cairo_translate(cr, -cursor_x, 0);
			color = cursor_theme->text_color;
			cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
			pango_cairo_show_layout(cr, layout);
			break;
		case CURSOR_STYLE_UNDERSCORE:
			cairo_translate(cr, 0, cursor_theme->underline_depth);
			rounded_rectangle(cr, cursor_width, cursor_theme->thickness, cursor_theme->corner_radius);
			cairo_fill(cr);
			break;
	}

	logical_rect->width += extra_cursor_advance;
	cairo_restore(cr);
}

void entry_backend_pango_init(struct entry *entry, uint32_t *width, uint32_t *height)
{
	cairo_t *cr = entry->cairo[0].cr;

	/* Setup Pango. */
	log_debug("Creating Pango context.\n");
	PangoContext *context = pango_cairo_create_context(cr);

	log_debug("Creating Pango font description.\n");
	PangoFontDescription *font_description =
		pango_font_description_from_string(entry->font_name);
	pango_font_description_set_size(
			font_description,
			entry->font_size * PANGO_SCALE);
	if (entry->font_variations[0] != 0) {
		pango_font_description_set_variations(
				font_description,
				entry->font_variations);
	}
  //pango_font_description_set_style(font_description, PANGO_STYLE_ITALIC);
	pango_context_set_font_description(context, font_description);

	entry->pango.layout = pango_layout_new(context);

	if (entry->font_features[0] != 0) {
		log_debug("Setting font features.\n");
		PangoAttribute *attr = pango_attr_font_features_new(entry->font_features);
		PangoAttrList *attr_list = pango_attr_list_new();
		pango_attr_list_insert(attr_list, attr);
		pango_layout_set_attributes(entry->pango.layout, attr_list);
	}

	log_debug("Loading Pango font.\n");
	PangoFontMap *map = pango_cairo_font_map_get_default();
	PangoFont *font = pango_font_map_load_font(map, context, font_description);
	PangoFontMetrics *metrics = pango_font_get_metrics(font, NULL);
	hb_font_t *hb_font = pango_font_get_hb_font(font);

	uint32_t m_codepoint;
	if (hb_font_get_glyph_from_name(hb_font, "m", -1, &m_codepoint)) {
		entry->cursor_theme.em_width = (double)hb_font_get_glyph_h_advance(hb_font, m_codepoint) / PANGO_SCALE;
	} else {
		entry->cursor_theme.em_width = (double)pango_font_metrics_get_approximate_char_width(metrics) / PANGO_SCALE;
	}

	entry->cursor_theme.underline_depth = (double)
		(
		 pango_font_metrics_get_ascent(metrics)
		 - pango_font_metrics_get_underline_position(metrics)
		) / PANGO_SCALE;
	if (entry->cursor_theme.style == CURSOR_STYLE_UNDERSCORE && !entry->cursor_theme.thickness_specified) {
		entry->cursor_theme.thickness = pango_font_metrics_get_underline_thickness(metrics) / PANGO_SCALE;
	}

	pango_font_metrics_unref(metrics);
	g_object_unref(font);
	log_debug("Loaded.\n");

	pango_font_description_free(font_description);

	entry->pango.context = context;
}

void entry_backend_pango_destroy(struct entry *entry)
{
	g_object_unref(entry->pango.layout);
	g_object_unref(entry->pango.context);
}

static bool size_overflows(struct entry *entry, uint32_t width, uint32_t height)
{
	cairo_t *cr = entry->cairo[entry->index].cr;
	cairo_matrix_t mat;
	cairo_get_matrix(cr, &mat);
	if (entry->horizontal) {
		if (mat.x0 + width > entry->clip_x + entry->clip_width) {
			return true;
		}
	} else {
		if (mat.y0 + height > entry->clip_y + entry->clip_height) {
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
void entry_backend_pango_update(struct entry *entry)
{
	cairo_t *cr = entry->cairo[entry->index].cr;
	PangoLayout *layout = entry->pango.layout;

	cairo_save(cr);

	/* Render the prompt */
	PangoRectangle ink_rect;
	PangoRectangle logical_rect;
	render_text_themed(cr, entry, entry->prompt_text, &entry->prompt_theme, &ink_rect, &logical_rect);

	cairo_translate(cr, logical_rect.width + logical_rect.x, 0);
	cairo_translate(cr, entry->prompt_padding, 0);

	/* Render the entry text */
	if (entry->input_utf8_length == 0) {
		render_input(
				cr,
				layout,
				entry->placeholder_text,
				utf8_strlen(entry->placeholder_text),
				&entry->placeholder_theme,
				0,
				&entry->cursor_theme,
				&ink_rect,
				&logical_rect);
	} else if (entry->hide_input) {
		size_t nchars = entry->input_utf32_length;
		size_t char_size = entry->hidden_character_utf8_length;
		char *buf = xmalloc(1 + nchars * char_size);
		for (size_t i = 0; i < nchars; i++) {
			for (size_t j = 0; j < char_size; j++) {
				buf[i * char_size + j] = entry->hidden_character_utf8[j];
			}
		}
		buf[char_size * nchars] = '\0';

		render_input(
				cr,
				layout,
				buf,
				entry->input_utf32_length,
				&entry->input_theme,
				entry->cursor_position,
				&entry->cursor_theme,
				&ink_rect,
				&logical_rect);
		free(buf);
	} else {
		render_input(
				cr,
				layout,
				entry->input_utf8,
				entry->input_utf32_length,
				&entry->input_theme,
				entry->cursor_position,
				&entry->cursor_theme,
				&ink_rect,
				&logical_rect);
	}
	logical_rect.width = MAX(logical_rect.width, (int)entry->input_width);

	struct color color = entry->foreground_color;
	cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);

	uint32_t num_results;
	if (entry->num_results == 0) {
		num_results = entry->results.count;
	} else {
		num_results = MIN(entry->num_results, entry->results.count);
	}
	/* Render our results */
	size_t i;
	for (i = 0; i < num_results; i++) {
		if (entry->horizontal) {
			cairo_translate(cr, logical_rect.x + logical_rect.width + entry->result_spacing, 0);
		} else {
			cairo_translate(cr, 0, char_height + entry->result_spacing);
		}
		if (entry->num_results == 0) {
			if (size_overflows(entry, 0, 0)) {
				break;
			}
		} else if (i >= entry->num_results) {
			break;
		}
		size_t index = i + entry->first_result;
		/*
		 * We may be on the last page, which could have fewer results
		 * than expected, so check and break if necessary.
		 */
		if (index >= entry->results.count) {
			break;
		}

		const char *name, *comment;
    const struct icon *icon;
		if (i < entry->results.count) {
			icon = entry->results.buf[index].result->icon;
			name = entry->results.buf[index].result->name;
			comment = entry->results.buf[index].result->comment;
		} else {
			name = "";
			comment = "";
		}

		const struct text_theme *theme;
		if (i == entry->selection) {
			theme = &entry->selection_theme;
		} else if (index % 2) {
			theme = &entry->alternate_result_theme;;
		} else {
			theme = &entry->default_result_theme;;
		}


    struct text_theme theme_icon = {
      .foreground_specified = true,
    };

    color_copy(&theme->foreground_color, &theme_icon.foreground_color);

    if (icon->color != NULL) {
      color_copy(icon->color, &theme_icon.foreground_color);
    }

		if (i != entry->selection) {
      struct color mix = color_mix(&theme_icon.foreground_color, &entry->default_result_theme, 0.5);
      color_copy(&mix, &theme_icon.foreground_color);
    }

		if (entry->num_results > 0) {
			render_text_themed(cr, entry, name, theme, &ink_rect, &logical_rect);
		} else if (!entry->horizontal) {

			if (size_overflows(entry, 0, logical_rect.height)) {
				entry->num_results_drawn = i;
				break;
			}

      int32_t padding = 2 * char_width;
      int32_t dist_x;
      int32_t dist_y;

      switch (i) {
        case 0:
          dist_x = 0;
          dist_y = 0;
          break;
        case 1:
          dist_x = 0;
          dist_y = -3;
          break;
        case 2:
          dist_x = 0;
          dist_y = 5;
          break;
        case 3:
          dist_x = 0;
          dist_y = 5;
          break;
        case 4:
          dist_x = 0;
          dist_y = 5;
          break;
        case 5:
          dist_x = 0;
          dist_y = -2;
          break;
        case 6:
          dist_x = 0;
          dist_y = 5;
          break;
        case 7:
          dist_x = 0;
          dist_y = 4;
          break;
        case 8:
          dist_x = 0;
          dist_y = 5;
          break;
        case 9:
          dist_x = 0;
          dist_y = 5;
          break;
        case 10:
          dist_x = 0;
          dist_y = 6;
          break;
        default:
          dist_x = 0;
          dist_y = 0;
          break;
      }

      if (icon) {
        cairo_translate(cr, icon->adjust_x, icon->adjust_y);
        render_text_themed(cr, entry, icon->text, &theme_icon, &ink_rect, &logical_rect);
        cairo_translate(cr, -icon->adjust_x, -icon->adjust_y);
      }

      dist_x = logical_rect.x + entry->result_spacing + padding;
			cairo_translate(cr, dist_x, 0);
			render_text_themed(cr, entry, name, theme, &ink_rect, &logical_rect);
			cairo_translate(cr, -dist_x, 0);
		} else {
      log_debug("Enter C\n");
			cairo_push_group(cr);
			render_text_themed(cr, entry, name, theme, &ink_rect, &logical_rect);

			cairo_pattern_t *group = cairo_pop_group(cr);
			if (size_overflows(entry, logical_rect.width, 0)) {
				entry->num_results_drawn = i;
				cairo_pattern_destroy(group);
				break;
			} else {
				cairo_save(cr);
				cairo_set_source(cr, group);
				cairo_paint(cr);
				cairo_restore(cr);
				cairo_pattern_destroy(group);
			}
		}
	}
	entry->num_results_drawn = i;
	log_debug("Drew %zu results.\n", i);

	cairo_restore(cr);
}
