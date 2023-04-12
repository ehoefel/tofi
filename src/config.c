#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "tofi.h"
#include "color.h"
#include "config.h"
#include "css.h"
#include "log.h"
#include "nelem.h"
#include "scale.h"
#include "unicode.h"
#include "xmalloc.h"

/* Convenience macros for anchor combinations */
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

void config_apply(struct tofi *tofi)
{
  struct css parsed_css = css_parse(css);
  log_debug("parsed all css\n");
  struct css_rule window = css_select(&parsed_css, "window");
  log_debug("selected css window with %d attributes\n", window.count);
  int width = css_get_attr_int(&window, "width");
  int height = css_get_attr_int(&window, "height");
  log_debug("found res %d x %d\n", width, height);
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
