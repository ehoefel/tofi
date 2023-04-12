#ifndef TOFI_CONFIG_H
#define TOFI_CONFIG_H

#include "tofi.h"

#define CSS(...) #__VA_ARGS__
/*
#include <stdint.h>
#include <stdbool.h>
#include "color.h"
#include "theme.h"
/*
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

static uint32_t width = 1280;
static uint32_t height = 720;
static uint32_t scale = 1;
static char *font_name = "jetbrains mono";
static uint32_t font_size = 24;
static char *prompt_text = "run: ";
static uint32_t border_width = 12;
static uint32_t outline_width = 4;

static struct directional padding = {
  .top = 8,
  .bottom = 8,
  .left = 8,
  .right = 8
};

static char *background_color = "#303030";
static char *foreground_color = "#767676";
static char *border_color = "#767676";
static char *outline_color = "#262626";

static char *placeholder_foreground_color = "#FFFFFF";
static char *selection_foreground_color = "#FFFFFF";

static struct cursor_theme cursor = {
  .thickness = 2
};

static uint32_t anchor = ANCHOR_CENTER;
static bool use_history = true;
static bool require_match = true;
static bool use_scale = true;
*/

static char *css = CSS(
  window {
    width: 1280px;
    height: 720px;
    scale: 1;
    font-family: "jetbrains mono";
    font-size: 24px;
    anchor: center;
  }

  body {
    padding: 8px;
    border: 12px #767676;
    outline: 4px #262626;
    background-color: #303030;
  }

  prompt {
    text: "run: ";
    color: #FFFFFF;
    font-weight: bold;
  }

  input::placeholder {
    color: #767676;
    text: "";
    font-style: italic;
  }

  entry {
    color: #767676;
  }

  entry.selected {
    color: #FFFFFF;
  }

  entry.disabled {
    color: #767676;
    font-style: italic;
  }

  entry.selected.disabled {
    color: #303030;
    background-color: #767676;
  }
);

void config_apply(struct tofi *tofi);
void config_fixup_values(struct tofi *tofi);

#endif /* TOFI_CONFIG_H */
