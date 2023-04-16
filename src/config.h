#ifndef TOFI_CONFIG_H
#define TOFI_CONFIG_H

#include "tofi.h"

#define CSS(...) #__VA_ARGS__

static char *css = CSS(
  window {
    width: 1280px;
    height: 720px;
    scale: 1;
    font-family: "jetbrains mono";
    font-size: 24px;
    anchor: center;
    background-color: #303030;
  }

  body {
    padding: 8px;
    border: 12px #767676;
    outline: 4px #262626;
    background-color: #303030;
  }

  input::before {
    content: "run: ";
    color: #FFFFFF;
    font-weight: bold;
  }

  input::placeholder {
    color: #767676;
    content: "";
    font-style: italic;
    caret: #FFFFFF block;
  }

  entry::before {
    content: "";
    color: #767676;
    margin-right: 1em;
  }

  entry.firefox::before {
    content: "";
    color: #E66000;
    padding-top: 2;
    padding-right: 3;
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

static bool use_history = true;
static bool require_match = true;
static bool fuzzy_match = true;
static bool multiple_instance = false;
static int32_t exclusive_zone = -1;

#endif /* TOFI_CONFIG_H */
