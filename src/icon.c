#include "icon.h"
#include "color.h"
#include "unicode.h"


void icon_init(struct icon *restrict icon, char *text)
{
  icon->text = utf8_normalize(text);
  icon_adjust(icon);
}

void icon_color_set(struct icon *restrict icon, char *color_hex)
{
  if (icon->color == NULL) {
    icon->color = malloc(sizeof(struct color));
  }
  color_set_from_hex(icon->color, color_hex);

}

void icon_adjust(struct icon *restrict icon)
{
  if (strcmp(icon->text, "") == 0) {
    icon->adjust_x = 2;
    icon->adjust_y = 0;
    icon_color_set(icon, "#E66000");
  } else if (strcmp(icon->text, "") == 0) {
    icon->adjust_x = 2;
    icon->adjust_y = -3;
    icon_color_set(icon, "#8000d7");
  } else if (strcmp(icon->text, "󱋧") == 0) {
    icon->adjust_x = 4;
    icon->adjust_y = 5;
    icon_color_set(icon, "#7d5bed");
  } else if (strcmp(icon->text, "󱉟") == 0) {
    icon->adjust_x = 0;
    icon->adjust_y = 5;
    icon_color_set(icon, "#5fff5f");
  } else if (strcmp(icon->text, "󰇧") == 0) {
    icon->adjust_x = -1;
    icon->adjust_y = 5;
    icon_color_set(icon, "#1e89c6");
  } else if (strcmp(icon->text, "") == 0) {
    icon->adjust_x = 3;
    icon->adjust_y = -2;
    icon_color_set(icon, "#c0c81f");
  } else if (strcmp(icon->text, "󱇤") == 0) {
    icon->adjust_x = 0;
    icon->adjust_y = 5;
    icon_color_set(icon, "#e2a464");
  } else if (strcmp(icon->text, "󰴸") == 0) {
    icon->adjust_x = 0;
    icon->adjust_y = 4;
    icon_color_set(icon, "#729fcf");
  } else if (strcmp(icon->text, "󰌨") == 0) {
    icon->adjust_x = 0;
    icon->adjust_y = 6;
    icon_color_set(icon, "#a4aad2");
  } else if (strcmp(icon->text, "󱙿") == 0) {
    icon->adjust_x = 2;
    icon->adjust_y = 5;
    icon_color_set(icon, "#deada7");
  } else if (strcmp(icon->text, "󰞇") == 0) {
    icon->adjust_x = -2;
    icon->adjust_y = 5;
    icon_color_set(icon, "#fe1607");
  }

}

void icon_destroy(struct icon *restrict icon)
{
  free(icon->text);
  free(icon->color);
}
