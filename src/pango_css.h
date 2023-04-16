#ifndef ENTRY_PANGO_H
#define ENTRY_PANGO_H

#include <pango/pangocairo.h>
#include "css.h"

struct engine;

struct pango {
	PangoContext *context;
	PangoLayout *layout;
};

void pango_init(struct engine *engine, uint32_t *width, uint32_t *height);
void pango_destroy(struct engine *engine);
void pango_update(struct engine *engine);

#endif /* ENTRY_PANGO_H */
