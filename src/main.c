#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <locale.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <threads.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>
#include "tofi.h"
#include "config.h"
#include "drun.h"
#include "setup.h"
#include "engine.h"
#include "input.h"
#include "log.h"
#include "nelem.h"
#include "lock.h"
#include "entry.h"
#include "scale.h"
#include "shm.h"
#include "string_vec.h"
#include "unicode.h"
#include "viewporter.h"
#include "xmalloc.h"
#include "color.h"

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

#undef MAX
#undef MIN
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static const char *mime_type_text_plain = "text/plain";
static const char *mime_type_text_plain_utf8 = "text/plain;charset=utf-8";

static uint32_t gettime_ms() {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);

	uint32_t ms = t.tv_sec * 1000;
	ms += t.tv_nsec / 1000000;
	return ms;
}


/* Read all of stdin into a buffer. */
static char *read_stdin(bool normalize) {
	const size_t block_size = BUFSIZ;
	size_t num_blocks = 1;
	size_t buf_size = block_size;

	char *buf = xmalloc(buf_size);
	for (size_t block = 0; ; block++) {
		if (block == num_blocks) {
			num_blocks *= 2;
			buf = xrealloc(buf, num_blocks * block_size);
		}
		size_t bytes_read = fread(
				&buf[block * block_size],
				1,
				block_size,
				stdin);
		if (bytes_read != block_size) {
			if (!feof(stdin) && ferror(stdin)) {
				log_error("Error reading stdin.\n");
			}
			buf[block * block_size + bytes_read] = '\0';
			break;
		}
	}
	if (normalize) {
		if (utf8_validate(buf)) {
			char *tmp = utf8_normalize(buf);
			free(buf);
			buf = tmp;
		} else {
			log_error("Invalid UTF-8 in stdin.\n");
		}
	}
	return buf;
}

static void zwlr_layer_surface_configure(
		void *data,
		struct zwlr_layer_surface_v1 *zwlr_layer_surface,
		uint32_t serial,
		uint32_t width,
		uint32_t height)
{
	struct tofi *tofi = data;
	if (width == 0 || height == 0) {
		/* Compositor is deferring to us, so don't do anything. */
		log_debug("Layer surface configure with no width or height.\n");
		return;
	}
	log_debug("Layer surface configure, %u x %u.\n", width, height);

	/*
	 * Resize the main window.
	 * We want actual pixel width / height, so we have to scale the
	 * values provided by Wayland.
	 */
	if (tofi->window.fractional_scale != 0) {
		tofi->window.surface.width = scale_apply(width, tofi->window.fractional_scale);
		tofi->window.surface.height = scale_apply(height, tofi->window.fractional_scale);
	} else {
		tofi->window.surface.width = width * tofi->window.scale;
		tofi->window.surface.height = height * tofi->window.scale;
	}

	zwlr_layer_surface_v1_ack_configure(
			tofi->window.zwlr_layer_surface,
			serial);
}

static void zwlr_layer_surface_close(
		void *data,
		struct zwlr_layer_surface_v1 *zwlr_layer_surface)
{
	struct tofi *tofi = data;
	tofi->closed = true;
	log_debug("Layer surface close.\n");
}

static const struct zwlr_layer_surface_v1_listener zwlr_layer_surface_listener = {
	.configure = zwlr_layer_surface_configure,
	.closed = zwlr_layer_surface_close
};

static void wl_keyboard_keymap(
		void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t format,
		int32_t fd,
		uint32_t size)
{
	struct tofi *tofi = data;
	assert(format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1);

	char *map_shm = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	assert(map_shm != MAP_FAILED);

	log_debug("Configuring keyboard.\n");
	struct xkb_keymap *xkb_keymap = xkb_keymap_new_from_string(
			tofi->xkb_context,
			map_shm,
			XKB_KEYMAP_FORMAT_TEXT_V1,
			XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map_shm, size);
	close(fd);

	struct xkb_state *xkb_state = xkb_state_new(xkb_keymap);
	xkb_keymap_unref(tofi->xkb_keymap);
	xkb_state_unref(tofi->xkb_state);
	tofi->xkb_keymap = xkb_keymap;
	tofi->xkb_state = xkb_state;
	log_debug("Keyboard configured.\n");
	munmap(map_shm, size);
	close(fd);
}

static void wl_keyboard_enter(
		void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		struct wl_surface *surface,
		struct wl_array *keys)
{
	/* Deliberately left blank */
}

static void wl_keyboard_leave(
		void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		struct wl_surface *surface)
{
	/* Deliberately left blank */
}

static void wl_keyboard_key(
		void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		uint32_t time,
		uint32_t key,
		uint32_t state)
{
	struct tofi *tofi = data;

	/*
	 * If this wasn't a keypress (i.e. was a key release), just update key
	 * repeat info and return.
	 */
	uint32_t keycode = key + 8;
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (keycode == tofi->repeat.keycode) {
			tofi->repeat.active = false;
		} else {
			tofi->repeat.next = gettime_ms() + tofi->repeat.delay;
		}
		return;
	}

	/* A rate of 0 disables key repeat */
	if (xkb_keymap_key_repeats(tofi->xkb_keymap, keycode) && tofi->repeat.rate != 0) {
		tofi->repeat.active = true;
		tofi->repeat.keycode = keycode;
		tofi->repeat.next = gettime_ms() + tofi->repeat.delay;
	}
	input_handle_keypress(tofi, keycode);
}

static void wl_keyboard_modifiers(
		void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		uint32_t mods_depressed,
		uint32_t mods_latched,
		uint32_t mods_locked,
		uint32_t group)
{
	struct tofi *tofi = data;
	if (tofi->xkb_state == NULL) {
		return;
	}
	xkb_state_update_mask(
			tofi->xkb_state,
			mods_depressed,
			mods_latched,
			mods_locked,
			0,
			0,
			group);
}

static void wl_keyboard_repeat_info(
		void *data,
		struct wl_keyboard *wl_keyboard,
		int32_t rate,
		int32_t delay)
{
	struct tofi *tofi = data;
	tofi->repeat.rate = rate;
	tofi->repeat.delay = delay;
	if (rate > 0) {
		log_debug("Key repeat every %u ms after %u ms.\n",
				1000 / rate,
				delay);
	} else {
		log_debug("Key repeat disabled.\n");
	}
}

static const struct wl_keyboard_listener wl_keyboard_listener = {
	.keymap = wl_keyboard_keymap,
	.enter = wl_keyboard_enter,
	.leave = wl_keyboard_leave,
	.key = wl_keyboard_key,
	.modifiers = wl_keyboard_modifiers,
	.repeat_info = wl_keyboard_repeat_info,
};

static void wl_pointer_enter(
		void *data,
		struct wl_pointer *pointer,
		uint32_t serial,
		struct wl_surface *surface,
		wl_fixed_t surface_x,
		wl_fixed_t surface_y)
{
	struct tofi *tofi = data;
	if (tofi->hide_cursor) {
		/* Hide the cursor by setting its surface to NULL. */
		wl_pointer_set_cursor(tofi->wl_pointer, serial, NULL, 0, 0);
	}
}

static void wl_pointer_leave(
		void *data,
		struct wl_pointer *pointer,
		uint32_t serial,
		struct wl_surface *surface)
{
	/* Deliberately left blank */
}

static void wl_pointer_motion(
		void *data,
		struct wl_pointer *pointer,
		uint32_t time,
		wl_fixed_t surface_x,
		wl_fixed_t surface_y)
{
	/* Deliberately left blank */
}

static void wl_pointer_button(
		void *data,
		struct wl_pointer *pointer,
		uint32_t serial,
		uint32_t time,
		uint32_t button,
		enum wl_pointer_button_state state)
{
	/* Deliberately left blank */
}

static void wl_pointer_axis(
		void *data,
		struct wl_pointer *pointer,
		uint32_t time,
		enum wl_pointer_axis axis,
		wl_fixed_t value)
{
	/* Deliberately left blank */
}

static void wl_pointer_frame(void *data, struct wl_pointer *pointer)
{
	/* Deliberately left blank */
}

static void wl_pointer_axis_source(
		void *data,
		struct wl_pointer *pointer,
		enum wl_pointer_axis_source axis_source)
{
	/* Deliberately left blank */
}

static void wl_pointer_axis_stop(
		void *data,
		struct wl_pointer *pointer,
		uint32_t time,
		enum wl_pointer_axis axis)
{
	/* Deliberately left blank */
}

static void wl_pointer_axis_discrete(
		void *data,
		struct wl_pointer *pointer,
		enum wl_pointer_axis axis,
		int32_t discrete)
{
	/* Deliberately left blank */
}

static const struct wl_pointer_listener wl_pointer_listener = {
	.enter = wl_pointer_enter,
	.leave = wl_pointer_leave,
	.motion = wl_pointer_motion,
	.button = wl_pointer_button,
	.axis = wl_pointer_axis,
	.frame = wl_pointer_frame,
	.axis_source = wl_pointer_axis_source,
	.axis_stop = wl_pointer_axis_stop,
	.axis_discrete = wl_pointer_axis_discrete
};

static void wl_seat_capabilities(
		void *data,
		struct wl_seat *wl_seat,
		uint32_t capabilities)
{
	struct tofi *tofi = data;

	bool have_keyboard = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;
	bool have_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;

	if (have_keyboard && tofi->wl_keyboard == NULL) {
		tofi->wl_keyboard = wl_seat_get_keyboard(tofi->wl_seat);
		wl_keyboard_add_listener(
				tofi->wl_keyboard,
				&wl_keyboard_listener,
				tofi);
		log_debug("Got keyboard from seat.\n");
	} else if (!have_keyboard && tofi->wl_keyboard != NULL) {
		wl_keyboard_release(tofi->wl_keyboard);
		tofi->wl_keyboard = NULL;
		log_debug("Released keyboard.\n");
	}

	if (have_pointer && tofi->wl_pointer == NULL) {
		tofi->wl_pointer = wl_seat_get_pointer(tofi->wl_seat);
		wl_pointer_add_listener(
				tofi->wl_pointer,
				&wl_pointer_listener,
				tofi);
		log_debug("Got pointer from seat.\n");
	} else if (!have_pointer && tofi->wl_pointer != NULL) {
		wl_pointer_release(tofi->wl_pointer);
		tofi->wl_pointer = NULL;
		log_debug("Released pointer.\n");
	}
}

static void wl_seat_name(void *data, struct wl_seat *wl_seat, const char *name)
{
	/* Deliberately left blank */
}

static const struct wl_seat_listener wl_seat_listener = {
	.capabilities = wl_seat_capabilities,
	.name = wl_seat_name,
};

static void wl_data_offer_offer(
		void *data,
		struct wl_data_offer *wl_data_offer,
		const char *mime_type)
{
	struct clipboard *clipboard = data;

	/* Only accept plain text, and prefer utf-8. */
	if (!strcmp(mime_type, mime_type_text_plain)) {
		if (clipboard->mime_type != NULL) {
			clipboard->mime_type = mime_type_text_plain;
		}
	} else if (!strcmp(mime_type, mime_type_text_plain_utf8)) {
		clipboard->mime_type = mime_type_text_plain_utf8;
	}
}

static void wl_data_offer_source_actions(
		void *data,
		struct wl_data_offer *wl_data_offer,
		uint32_t source_actions)
{
	/* Deliberately left blank */
}

static void wl_data_offer_action(
		void *data,
		struct wl_data_offer *wl_data_offer,
		uint32_t action)
{
	/* Deliberately left blank */
}

static const struct wl_data_offer_listener wl_data_offer_listener = {
	.offer = wl_data_offer_offer,
	.source_actions = wl_data_offer_source_actions,
	.action = wl_data_offer_action
};

static void wl_data_device_data_offer(
		void *data,
		struct wl_data_device *wl_data_device,
		struct wl_data_offer *wl_data_offer)
{
	struct clipboard *clipboard = data;
	clipboard_reset(clipboard);
	clipboard->wl_data_offer = wl_data_offer;
	wl_data_offer_add_listener(
			wl_data_offer,
			&wl_data_offer_listener,
			clipboard);
}

static void wl_data_device_enter(
		void *data,
		struct wl_data_device *wl_data_device,
		uint32_t serial,
		struct wl_surface *wl_surface,
		int32_t x,
		int32_t y,
		struct wl_data_offer *wl_data_offer)
{
	/* Drag-and-drop is just ignored for now. */
	wl_data_offer_accept(
			wl_data_offer,
			serial,
			NULL);
	wl_data_offer_set_actions(
			wl_data_offer,
			WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE,
			WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE);
}

static void wl_data_device_leave(
		void *data,
		struct wl_data_device *wl_data_device)
{
	/* Deliberately left blank */
}

static void wl_data_device_motion(
		void *data,
		struct wl_data_device *wl_data_device,
		uint32_t time,
		int32_t x,
		int32_t y)
{
	/* Deliberately left blank */
}

static void wl_data_device_drop(
		void *data,
		struct wl_data_device *wl_data_device)
{
	/* Deliberately left blank */
}

static void wl_data_device_selection(
		void *data,
		struct wl_data_device *wl_data_device,
		struct wl_data_offer *wl_data_offer)
{
	struct clipboard *clipboard = data;
	if (wl_data_offer == NULL) {
		clipboard_reset(clipboard);
	}
}

static const struct wl_data_device_listener wl_data_device_listener = {
	.data_offer = wl_data_device_data_offer,
	.enter = wl_data_device_enter,
	.leave = wl_data_device_leave,
	.motion = wl_data_device_motion,
	.drop = wl_data_device_drop,
	.selection = wl_data_device_selection
};

static void output_geometry(
		void *data,
		struct wl_output *wl_output,
		int32_t x,
		int32_t y,
		int32_t physical_width,
		int32_t physical_height,
		int32_t subpixel,
		const char *make,
		const char *model,
		int32_t transform)
{
	struct tofi *tofi = data;
	struct output_list_element *el;
	wl_list_for_each(el, &tofi->output_list, link) {
		if (el->wl_output == wl_output) {
			el->transform = transform;
		}
	}
}

static void output_mode(
		void *data,
		struct wl_output *wl_output,
		uint32_t flags,
		int32_t width,
		int32_t height,
		int32_t refresh)
{
	struct tofi *tofi = data;
	struct output_list_element *el;
	wl_list_for_each(el, &tofi->output_list, link) {
		if (el->wl_output == wl_output) {
			if (flags & WL_OUTPUT_MODE_CURRENT) {
				el->width = width;
				el->height = height;
			}
		}
	}
}

static void output_scale(
		void *data,
		struct wl_output *wl_output,
		int32_t factor)
{
	struct tofi *tofi = data;
	struct output_list_element *el;
	wl_list_for_each(el, &tofi->output_list, link) {
		if (el->wl_output == wl_output) {
			el->scale = factor;
		}
	}
}

static void output_name(
		void *data,
		struct wl_output *wl_output,
		const char *name)
{
	struct tofi *tofi = data;
	struct output_list_element *el;
	wl_list_for_each(el, &tofi->output_list, link) {
		if (el->wl_output == wl_output) {
			el->name = xstrdup(name);
		}
	}
}

static void output_description(
		void *data,
		struct wl_output *wl_output,
		const char *description)
{
	/* Deliberately left blank */
}

static void output_done(void *data, struct wl_output *wl_output)
{
	log_debug("Output configuration done.\n");
}

static const struct wl_output_listener wl_output_listener = {
	.geometry = output_geometry,
	.mode = output_mode,
	.done = output_done,
	.scale = output_scale,
#ifndef NO_WL_OUTPUT_NAME
	.name = output_name,
	.description = output_description,
#endif
};

static void registry_global(
		void *data,
		struct wl_registry *wl_registry,
		uint32_t name,
		const char *interface,
		uint32_t version)
{
	struct tofi *tofi = data;
	//log_debug("Registry %u: %s v%u.\n", name, interface, version);
	if (!strcmp(interface, wl_compositor_interface.name)) {
		tofi->wl_compositor = wl_registry_bind(
				wl_registry,
				name,
				&wl_compositor_interface,
				4);
		log_debug("Bound to compositor %u.\n", name);
	} else if (!strcmp(interface, wl_seat_interface.name)) {
		tofi->wl_seat = wl_registry_bind(
				wl_registry,
				name,
				&wl_seat_interface,
				7);
		wl_seat_add_listener(
				tofi->wl_seat,
				&wl_seat_listener,
				tofi);
		log_debug("Bound to seat %u.\n", name);
	} else if (!strcmp(interface, wl_output_interface.name)) {
		struct output_list_element *el = xmalloc(sizeof(*el));
		if (version < 4) {
			el->name = xstrdup("");
			log_warning("Using an outdated compositor, "
					"output selection will not work.\n");
		} else {
			version = 4;
		}
		el->wl_output = wl_registry_bind(
				wl_registry,
				name,
				&wl_output_interface,
				version);
		wl_output_add_listener(
				el->wl_output,
				&wl_output_listener,
				tofi);
		wl_list_insert(&tofi->output_list, &el->link);
		log_debug("Bound to output %u.\n", name);
	} else if (!strcmp(interface, wl_shm_interface.name)) {
		tofi->wl_shm = wl_registry_bind(
				wl_registry,
				name,
				&wl_shm_interface,
				1);
		log_debug("Bound to shm %u.\n", name);
	} else if (!strcmp(interface, wl_data_device_manager_interface.name)) {
		tofi->wl_data_device_manager = wl_registry_bind(
				wl_registry,
				name,
				&wl_data_device_manager_interface,
				3);
		log_debug("Bound to data device manager  %u.\n", name);
	} else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name)) {
		if (version < 3) {
			log_warning("Using an outdated compositor, "
					"screen anchoring may not work.\n");
		} else {
			version = 3;
		}
		tofi->zwlr_layer_shell = wl_registry_bind(
				wl_registry,
				name,
				&zwlr_layer_shell_v1_interface,
				version);
		log_debug("Bound to zwlr_layer_shell_v1 %u.\n", name);
	} else if (!strcmp(interface, wp_viewporter_interface.name)) {
		tofi->wp_viewporter = wl_registry_bind(
				wl_registry,
				name,
				&wp_viewporter_interface,
				1);
		log_debug("Bound to wp_viewporter %u.\n", name);
	} else if (!strcmp(interface, wp_fractional_scale_manager_v1_interface.name)) {
		tofi->wp_fractional_scale_manager = wl_registry_bind(
				wl_registry,
				name,
				&wp_fractional_scale_manager_v1_interface,
				1);
		log_debug("Bound to wp_fractional_scale_manager_v1 %u.\n", name);
	}
}

static void registry_global_remove(
		void *data,
		struct wl_registry *wl_registry,
		uint32_t name)
{
	/* Deliberately left blank */
}

static const struct wl_registry_listener wl_registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static void surface_enter(
		void *data,
		struct wl_surface *wl_surface,
		struct wl_output *wl_output)
{
	log_debug("Surface entered output.\n");
}

static void surface_leave(
		void *data,
		struct wl_surface *wl_surface,
		struct wl_output *wl_output)
{
	/* Deliberately left blank */
}

static const struct wl_surface_listener wl_surface_listener = {
	.enter = surface_enter,
	.leave = surface_leave
};

/*
 * These "dummy_*" functions are callbacks just for the dummy surface used to
 * select the default output if there's more than one.
 */
static void dummy_layer_surface_configure(
		void *data,
		struct zwlr_layer_surface_v1 *zwlr_layer_surface,
		uint32_t serial,
		uint32_t width,
		uint32_t height)
{
	zwlr_layer_surface_v1_ack_configure(
			zwlr_layer_surface,
			serial);
}

static void dummy_layer_surface_close(
		void *data,
		struct zwlr_layer_surface_v1 *zwlr_layer_surface)
{
}

static const struct zwlr_layer_surface_v1_listener dummy_layer_surface_listener = {
	.configure = dummy_layer_surface_configure,
	.closed = dummy_layer_surface_close
};

static void dummy_fractional_scale_preferred_scale(
		void *data,
		struct wp_fractional_scale_v1 *wp_fractional_scale,
		uint32_t scale)
{
	struct tofi *tofi = data;
	tofi->window.fractional_scale = scale;
}

static const struct wp_fractional_scale_v1_listener dummy_fractional_scale_listener = {
	.preferred_scale = dummy_fractional_scale_preferred_scale
};

static void dummy_surface_enter(
		void *data,
		struct wl_surface *wl_surface,
		struct wl_output *wl_output)
{
	struct tofi *tofi = data;
	struct output_list_element *el;
	wl_list_for_each(el, &tofi->output_list, link) {
		if (el->wl_output == wl_output) {
			tofi->default_output = el;
			break;
		}
	}
}

static void dummy_surface_leave(
		void *data,
		struct wl_surface *wl_surface,
		struct wl_output *wl_output)
{
	/* Deliberately left blank */
}

static const struct wl_surface_listener dummy_surface_listener = {
	.enter = dummy_surface_enter,
	.leave = dummy_surface_leave
};


static bool do_submit(struct tofi *tofi)
{
	struct engine *engine = &tofi->window.engine;
	uint32_t selection = engine->selection + engine->first_result;
	char *res = engine->results.buf[selection].entry->name;

	if (tofi->window.engine.results.count == 0) {
		/* Always require a match in drun mode. */
		if (tofi->require_match) {
			return false;
		} else {
			printf("%s\n", engine->input_utf8);
			return true;
		}
	}

	/*
	 * At this point, the list of apps is history sorted rather
	 * than alphabetically sorted, so we can't use
	 * desktop_vec_find_sorted().
	 */
	struct desktop_entry *app = NULL;
	for (size_t i = 0; i < engine->apps.count; i++) {
		if (!strcmp(res, engine->apps.buf[i].name)) {
			app = &engine->apps.buf[i];
			break;
		}
	}
	if (app == NULL) {
		log_error("Couldn't find application file! This shouldn't happen.\n");
		return false;
	}
	char *path = app->path;
	drun_print(path, tofi->default_terminal);
	if (tofi->use_history) {
		history_add(
				&engine->history,
				engine->results.buf[selection].entry->name);
		if (tofi->history_file[0] == 0) {
			history_save_default_file(&engine->history, engine->drun);
		} else {
			history_save(&engine->history, tofi->history_file);
		}
	}
	return true;
}

static void read_clipboard(struct tofi *tofi)
{
	struct engine *engine = &tofi->window.engine;

	/* Make a copy of any text after the cursor. */
	uint32_t *end_text = NULL;
	size_t end_text_length = engine->input_utf32_length - engine->cursor_position;
	if (end_text_length > 0) {
		end_text = xcalloc(end_text_length, sizeof(*engine->input_utf32));
		memcpy(end_text,
				&engine->input_utf32[engine->cursor_position],
				end_text_length * sizeof(*engine->input_utf32));
	}
	/* Buffer for 4 UTF-8 bytes plus a null terminator. */
	char buffer[5];
	memset(buffer, 0, N_ELEM(buffer));
	errno = 0;
	bool eof = false;
	while (engine->cursor_position < N_ELEM(engine->input_utf32)) {
		for (size_t i = 0; i < 4; i++) {
			/*
			 * Read input 1 byte at a time. This is slow, but easy,
			 * and speed of pasting shouldn't really matter.
			 */
			int res = read(tofi->clipboard.fd, &buffer[i], 1);
			if (res == 0) {
				eof = true;
				break;
			} else if (res == -1) {
				if (errno == EAGAIN) {
					/*
					 * There was no more data to be read,
					 * but EOF hasn't been reached yet.
					 *
					 * This could mean more than a pipe's
					 * capacity (64k) of data was sent, in
					 * which case we'd potentially skip
					 * a character, but we should hit the
					 * input length limit long before that.
					 */
					input_refresh_results(tofi);
					tofi->window.surface.redraw = true;
					return;
				}
				log_error("Failed to read clipboard: %s\n", strerror(errno));
				clipboard_finish_paste(&tofi->clipboard);
				return;
			}
			uint32_t unichar = utf8_to_utf32_validate(buffer);
			if (unichar == (uint32_t)-2) {
				/* The current character isn't complete yet. */
				continue;
			} else if (unichar == (uint32_t)-1) {
				log_error("Invalid UTF-8 character in clipboard: %s\n", buffer);
				break;
			} else {
				engine->input_utf32[engine->cursor_position] = unichar;
				engine->cursor_position++;
				break;
			}
		}
		memset(buffer, 0, N_ELEM(buffer));
		if (eof) {
			break;
		}
	}
	engine->input_utf32_length = engine->cursor_position;

	/* If there was any text after the cursor, re-insert it now. */
	if (end_text != NULL) {
		for (size_t i = 0; i < end_text_length; i++) {
			if (engine->input_utf32_length == N_ELEM(engine->input_utf32)) {
				break;
			}
			engine->input_utf32[engine->input_utf32_length] = end_text[i];
			engine->input_utf32_length++;
		}
		free(end_text);
	}
	engine->input_utf32[MIN(engine->input_utf32_length, N_ELEM(engine->input_utf32) - 1)] = U'\0';

	clipboard_finish_paste(&tofi->clipboard);

	input_refresh_results(tofi);
	tofi->window.surface.redraw = true;
}

int main(int argc, char *argv[])
{
	/* Call log_debug to initialise the timers we use for perf checking. */
	log_debug("This is tofi.\n");

	/*
	 * Set the locale to the user's default, so we can deal with non-ASCII
	 * characters.
	 */
	setlocale(LC_ALL, "");

	/* Default options. */
	struct tofi tofi = {
		.window = {
			.engine = {
				.hidden_character_utf8 = u8"*",
				.clip_to_padding = true,
				.foreground_color = hex_to_color("#767676"),
				.selection_theme.foreground_color = hex_to_color("#ffffff"),
				.selection_theme.foreground_specified = true,
				.cursor_theme.thickness = 2
			}
		},
		.use_scale = true,
	};
	wl_list_init(&tofi.output_list);
	if (getenv("TERMINAL") != NULL) {
		snprintf(
				tofi.default_terminal,
				N_ELEM(tofi.default_terminal),
				"%s",
				getenv("TERMINAL"));
	}

	log_debug("Config done\n");

	if (!tofi.multiple_instance && lock_check()) {
		log_error("Another instance of tofi is already running.\n");
		exit(EXIT_FAILURE);
	}

	/*
	 * Initial Wayland & XKB setup.
	 * The first thing to do is connect a listener to the global registry,
	 * so that we can bind to the various global objects and start talking
	 * to Wayland.
	 */

	log_debug("Connecting to Wayland display.\n");
	tofi.wl_display = wl_display_connect(NULL);
	if (tofi.wl_display == NULL) {
		log_error("Couldn't connect to Wayland display.\n");
		exit(EXIT_FAILURE);
	}
	tofi.wl_registry = wl_display_get_registry(tofi.wl_display);
	log_debug("Creating xkb context.\n");
	tofi.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (tofi.xkb_context == NULL) {
		log_error("Couldn't create an XKB context.\n");
		exit(EXIT_FAILURE);
	}
	wl_registry_add_listener(
			tofi.wl_registry,
			&wl_registry_listener,
			&tofi);

	/*
	 * After this first roundtrip, the only thing that should have happened
	 * is our registry_global() function being called and setting up the
	 * various global object bindings.
	 */
	log_debug("First roundtrip start.\n");
	log_indent();
	wl_display_roundtrip(tofi.wl_display);
	log_unindent();
	log_debug("First roundtrip done.\n");

	/*
	 * The next roundtrip causes the listeners we set up in
	 * registry_global() to be called. Notably, the output should be
	 * configured, telling us the scale factor and size.
	 */
	log_debug("Second roundtrip start.\n");
	log_indent();
	wl_display_roundtrip(tofi.wl_display);
	log_unindent();
	log_debug("Second roundtrip done.\n");

	{
		/*
		 * Determine the output we're going to appear on, and get its
		 * fractional scale if supported.
		 *
		 * This seems like an ugly solution, but as far as I know
		 * there's no way to determine the default output other than to
		 * call get_layer_surface with NULL as the output and see which
		 * output our surface turns up on.
		 *
		 * Additionally, determining fractional scale factors can
		 * currently only be done by attaching a wp_fractional_scale to
		 * a surface and displaying it.
		 *
		 * Here we set up a single pixel surface, perform the required
		 * two roundtrips, then tear it down. tofi.default_output
		 * should then contain the output our surface was assigned to,
		 * and tofi.window.fractional_scale should have the scale
		 * factor.
		 */
		log_debug("Determining output.\n");
		log_indent();
		struct surface surface = {
			.width = 1,
			.height = 1
		};
		surface.wl_surface =
			wl_compositor_create_surface(tofi.wl_compositor);
		wl_surface_add_listener(
				surface.wl_surface,
				&dummy_surface_listener,
				&tofi);

		struct wp_fractional_scale_v1 *wp_fractional_scale = NULL;
		if (tofi.wp_fractional_scale_manager != NULL) {
			wp_fractional_scale =
				wp_fractional_scale_manager_v1_get_fractional_scale(
						tofi.wp_fractional_scale_manager,
						surface.wl_surface);
			wp_fractional_scale_v1_add_listener(
					wp_fractional_scale,
					&dummy_fractional_scale_listener,
					&tofi);
		}

		/*
		 * If we have a desired output, make sure we appear on it so we
		 * can determine the correct fractional scale.
		 */
		struct wl_output *wl_output = NULL;
		if (tofi.target_output_name[0] != '\0') {
			struct output_list_element *el;
			wl_list_for_each(el, &tofi.output_list, link) {
				if (!strcmp(tofi.target_output_name, el->name)) {
					wl_output = el->wl_output;
					break;
				}
			}
		}

		struct zwlr_layer_surface_v1 *zwlr_layer_surface =
			zwlr_layer_shell_v1_get_layer_surface(
					tofi.zwlr_layer_shell,
					surface.wl_surface,
					wl_output,
					ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
					"dummy");
		/*
		 * Workaround for Hyprland, where if this is not set the dummy
		 * surface never enters an output for some reason.
		 */
		zwlr_layer_surface_v1_set_keyboard_interactivity(
				zwlr_layer_surface,
				ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
				);
		zwlr_layer_surface_v1_add_listener(
				zwlr_layer_surface,
				&dummy_layer_surface_listener,
				&tofi);
		zwlr_layer_surface_v1_set_size(
				zwlr_layer_surface,
				1,
				1);
		wl_surface_commit(surface.wl_surface);
		log_debug("First dummy roundtrip start.\n");
		log_indent();
		wl_display_roundtrip(tofi.wl_display);
		log_unindent();
		log_debug("First dummy roundtrip done.\n");
		log_debug("Initialising dummy surface.\n");
		log_indent();
		surface_init(&surface, tofi.wl_shm);
		surface_draw(&surface);
		log_unindent();
		log_debug("Dummy surface initialised.\n");
		log_debug("Second dummy roundtrip start.\n");
		log_indent();
		wl_display_roundtrip(tofi.wl_display);
		log_unindent();
		log_debug("Second dummy roundtrip done.\n");
		surface_destroy(&surface);
		zwlr_layer_surface_v1_destroy(zwlr_layer_surface);
		if (wp_fractional_scale != NULL) {
			wp_fractional_scale_v1_destroy(wp_fractional_scale);
		}
		wl_surface_destroy(surface.wl_surface);

		/*
		 * Walk through our output list and select the one we want if
		 * the user's asked for a specific one, otherwise just get the
		 * default one.
		 */
		bool found_target = false;
		struct output_list_element *head;
		head = wl_container_of(tofi.output_list.next, head, link);

		struct output_list_element *el;
		struct output_list_element *tmp;
		if (tofi.target_output_name[0] != 0) {
			log_debug("Looking for output %s.\n", tofi.target_output_name);
		} else if (tofi.default_output != NULL) {
			snprintf(
					tofi.target_output_name,
					N_ELEM(tofi.target_output_name),
					"%s",
					tofi.default_output->name);
			/* We don't need this anymore. */
			tofi.default_output = NULL;
		}
		wl_list_for_each_reverse_safe(el, tmp, &tofi.output_list, link) {
			if (!strcmp(tofi.target_output_name, el->name)) {
				found_target = true;
				continue;
			}
			/*
			 * If we've already found the output we're looking for
			 * or this isn't the first output in the list, remove
			 * it.
			 */
			if (found_target || el != head) {
				wl_list_remove(&el->link);
				wl_output_release(el->wl_output);
				free(el->name);
				free(el);
			}
		}

		/*
		 * The only output left should either be the one we want, or
		 * the first that was advertised.
		 */
		el = wl_container_of(tofi.output_list.next, el, link);

		/*
		 * If we're rotated 90 degrees, we need to swap width and
		 * height to calculate percentages.
		 */
		switch (el->transform) {
			case WL_OUTPUT_TRANSFORM_90:
			case WL_OUTPUT_TRANSFORM_270:
			case WL_OUTPUT_TRANSFORM_FLIPPED_90:
			case WL_OUTPUT_TRANSFORM_FLIPPED_270:
				tofi.output_width = el->height;
				tofi.output_height = el->width;
				break;
			default:
				tofi.output_width = el->width;
				tofi.output_height = el->height;
		}
		tofi.window.scale = el->scale;
		tofi.window.transform = el->transform;
		log_unindent();
		log_debug("Selected output %s.\n", el->name);
	}

  struct css parsed_css = css_parse(css);
  tofi.window.engine.css = &parsed_css;
  setup_apply_config(&tofi);

	/*
	 * If we were invoked as tofi-run, generate the command list.
	 * If we were invoked as tofi-drun, generate the desktop app list.
	 * Otherwise, just read standard input.
	 */
	log_debug("Generating desktop app list.\n");
	log_indent();
	tofi.window.engine.drun = true;
	//struct desktop_vec apps = drun_generate_cached();
	struct desktop_vec apps = drun_generate();
	if (tofi.use_history) {
		if (tofi.history_file[0] == 0) {
			tofi.window.engine.history = history_load_default_file(tofi.window.engine.drun);
		} else {
			tofi.window.engine.history = history_load(tofi.history_file);
		}
		if (tofi.use_history) {
			drun_history_sort(&apps, &tofi.window.engine.history);
		}
	}
	struct entry_ref_vec commands = entry_ref_vec_create();
	for (size_t i = 0; i < apps.count; i++) {
		entry_ref_vec_add_desktop(&commands, &apps.buf[i]);
	}
	tofi.window.engine.commands = commands;
	tofi.window.engine.apps = apps;
	log_unindent();
	log_debug("App list generated.\n");
	tofi.window.engine.results = entry_ref_vec_copy(&tofi.window.engine.commands);

	/*
	 * Next, we create the Wayland surface, which takes on the
	 * layer shell role.
	 */
	log_debug("Creating main window surface.\n");
	tofi.window.surface.wl_surface =
		wl_compositor_create_surface(tofi.wl_compositor);
	wl_surface_add_listener(
			tofi.window.surface.wl_surface,
			&wl_surface_listener,
			&tofi);

	/* Grab the first (and only remaining) output from our list. */
	struct wl_output *wl_output;
	{
		struct output_list_element *el;
		el = wl_container_of(tofi.output_list.next, el, link);
		wl_output = el->wl_output;
	}

	tofi.window.zwlr_layer_surface = zwlr_layer_shell_v1_get_layer_surface(
			tofi.zwlr_layer_shell,
			tofi.window.surface.wl_surface,
			wl_output,
			ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
			"launcher");
	zwlr_layer_surface_v1_set_keyboard_interactivity(
			tofi.window.zwlr_layer_surface,
			ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
	zwlr_layer_surface_v1_add_listener(
			tofi.window.zwlr_layer_surface,
			&zwlr_layer_surface_listener,
			&tofi);
	zwlr_layer_surface_v1_set_anchor(
			tofi.window.zwlr_layer_surface,
			tofi.anchor);
	zwlr_layer_surface_v1_set_exclusive_zone(
			tofi.window.zwlr_layer_surface,
			tofi.window.exclusive_zone);
	zwlr_layer_surface_v1_set_margin(
			tofi.window.zwlr_layer_surface,
			tofi.window.margin_top,
			tofi.window.margin_right,
			tofi.window.margin_bottom,
			tofi.window.margin_left);
	/*
	 * No matter whether we're scaling via Cairo or not, we're presenting a
	 * scaled buffer to Wayland, so scale the window size here if we
	 * haven't already done so.
	 */
	zwlr_layer_surface_v1_set_size(
			tofi.window.zwlr_layer_surface,
			tofi.window.width,
			tofi.window.height);

	/*
	 * Set up a viewport for our surface, necessary for fractional scaling.
	 */
	tofi.window.wp_viewport = wp_viewporter_get_viewport(
			tofi.wp_viewporter,
			tofi.window.surface.wl_surface);
	wp_viewport_set_destination(
			tofi.window.wp_viewport,
			tofi.window.width,
			tofi.window.height);

	/* Commit the surface to finalise setup. */
	wl_surface_commit(tofi.window.surface.wl_surface);

	/*
	 * Create a data device and setup a listener for data offers. This is
	 * required for clipboard support.
	 */
	tofi.wl_data_device = wl_data_device_manager_get_data_device(
			tofi.wl_data_device_manager,
			tofi.wl_seat);
	wl_data_device_add_listener(
			tofi.wl_data_device,
			&wl_data_device_listener,
			&tofi.clipboard);

	/*
	 * Now that we've done all our Wayland-related setup, we do another
	 * roundtrip. This should cause the layer surface window to be
	 * configured, after which we're ready to start drawing to the screen.
	 */
	log_debug("Third roundtrip start.\n");
	log_indent();
	wl_display_roundtrip(tofi.wl_display);
	log_unindent();
	log_debug("Third roundtrip done.\n");


	/*
	 * Create the various structures for our window surface. This needs to
	 * be done before calling engine_init as that performs some initial
	 * drawing, and surface_init allocates the buffers we'll be drawing to.
	 */
	log_debug("Initialising window surface.\n");
	log_indent();
	surface_init(&tofi.window.surface, tofi.wl_shm);
	log_unindent();
	log_debug("Window surface initialised.\n");

	/*
	 * Initialise the structures for rendering the engine.
	 * Cairo needs to know the size of the surface it's creating, and
	 * there's no way to resize it aside from tearing everything down and
	 * starting again, so we make sure to do this after we've determined
	 * our output's scale factor. This stops us being able to change the
	 * scale factor after startup, but this is just a launcher, which
	 * shouldn't be moving between outputs while running.
	 */
	log_debug("Initialising renderer.\n");
	log_indent();
	{
		/*
		 * No matter how we're scaling (with fractions, integers or not
		 * at all), we pass a fractional scale factor (the numerator of
		 * a fraction with denominator 120) to our setup function for
		 * ease.
		 */
		uint32_t scale = 120;
		if (tofi.use_scale) {
			if (tofi.window.fractional_scale != 0) {
				scale = tofi.window.fractional_scale;
			} else {
				scale = tofi.window.scale * 120;
			}
		}
		engine_init(
				&tofi.window.engine,
				tofi.window.surface.shm_pool_data,
				tofi.window.surface.width,
				tofi.window.surface.height,
				scale);
	}
	log_unindent();
	log_debug("Renderer initialised.\n");

	/* Perform an initial render. */
	surface_draw(&tofi.window.surface);

	/*
	 * engine_init() left the second of the two buffers we use for
	 * double-buffering unpainted to lower startup time, as described
	 * there. Here, we flush our first, finished buffer to the screen, then
	 * copy over the image to the second buffer before we need to use it in
	 * the main loop. This ensures we paint to the screen as quickly as
	 * possible after startup.
	 */
	wl_display_roundtrip(tofi.wl_display);
	log_debug("Initialising second buffer.\n");
	memcpy(
		cairo_image_surface_get_data(tofi.window.engine.cairo[1].surface),
		cairo_image_surface_get_data(tofi.window.engine.cairo[0].surface),
		tofi.window.surface.width * tofi.window.surface.height * sizeof(uint32_t)
	);
	log_debug("Second buffer initialised.\n");

	/* We've just rendered, so we don't need to do it again right now. */
	tofi.window.surface.redraw = false;

	/*
	 * Main event loop.
	 * See the wl_display(3) man page for an explanation of the
	 * order of the various functions called here.
	 */
	while (!tofi.closed) {
		struct pollfd pollfds[2] = {{0}, {0}};
		pollfds[0].fd = wl_display_get_fd(tofi.wl_display);

		/* Make sure we're ready to receive events on the main queue. */
		while (wl_display_prepare_read(tofi.wl_display) != 0) {
			wl_display_dispatch_pending(tofi.wl_display);
		}

		/* Make sure all our requests have been sent to the server. */
		while (wl_display_flush(tofi.wl_display) != 0) {
			pollfds[0].events = POLLOUT;
			poll(&pollfds[0], 1, -1);
		}

		/*
		 * Set time to wait for poll() to -1 (unlimited), unless
		 * there's some key repeating going on.
		 */
		int timeout = -1;
		if (tofi.repeat.active) {
			int64_t wait = (int64_t)tofi.repeat.next - (int64_t)gettime_ms();
			if (wait >= 0) {
				timeout = wait;
			} else {
				timeout = 0;
			}
		}

		pollfds[0].events = POLLIN | POLLPRI;
		int res;
		if (tofi.clipboard.fd == 0) {
			res = poll(&pollfds[0], 1, timeout);
		} else {
			/*
			 * We're trying to paste from the clipboard, which is
			 * done by reading from a pipe, so poll that file
			 * descriptor as well.
			 */
			pollfds[1].fd = tofi.clipboard.fd;
			pollfds[1].events = POLLIN | POLLPRI;
			res = poll(pollfds, 2, timeout);
		}
		if (res == 0) {
			/*
			 * No events to process and no error - we presumably
			 * have a key repeat to handle.
			 */
			wl_display_cancel_read(tofi.wl_display);
			if (tofi.repeat.active) {
				int64_t wait = (int64_t)tofi.repeat.next - (int64_t)gettime_ms();
				if (wait <= 0) {
					input_handle_keypress(&tofi, tofi.repeat.keycode);
					tofi.repeat.next += 1000 / tofi.repeat.rate;
				}
			}
		} else if (res < 0) {
			/* There was an error polling the display. */
			wl_display_cancel_read(tofi.wl_display);
		} else {
			if (pollfds[0].revents & (POLLIN | POLLPRI)) {
				/* Events to read, so put them on the queue. */
				wl_display_read_events(tofi.wl_display);
			} else {
				/*
				 * No events to read - we were woken up to
				 * handle clipboard data.
				 */
				wl_display_cancel_read(tofi.wl_display);
			}
			if (pollfds[1].revents & (POLLIN | POLLPRI)) {
				/* Read clipboard data. */
				if (tofi.clipboard.fd > 0) {
					read_clipboard(&tofi);
				}
			}
			if (pollfds[1].revents & POLLHUP) {
				/*
				 * The other end of the clipboard pipe has
				 * closed, cleanup.
				 */
				clipboard_finish_paste(&tofi.clipboard);
			}
		}

		/* Handle any events we read. */
		wl_display_dispatch_pending(tofi.wl_display);

		if (tofi.window.surface.redraw) {
			engine_update(&tofi.window.engine);
			surface_draw(&tofi.window.surface);
			tofi.window.surface.redraw = false;
		}
		if (tofi.submit) {
			tofi.submit = false;
			if (do_submit(&tofi)) {
				break;
			}
		}

	}

	log_debug("Window closed, performing cleanup.\n");
#ifdef DEBUG
	/*
	 * For debug builds, try to cleanup as much as possible, to make using
	 * e.g. Valgrind easier. There's still a few unavoidable leaks though,
	 * mostly from Pango, and Cairo holds onto quite a bit of cached data
	 * (without leaking it)
	 */
	surface_destroy(&tofi.window.surface);
	engine_destroy(&tofi.window.engine);
	if (tofi.window.wp_viewport != NULL) {
		wp_viewport_destroy(tofi.window.wp_viewport);
	}
	zwlr_layer_surface_v1_destroy(tofi.window.zwlr_layer_surface);
	wl_surface_destroy(tofi.window.surface.wl_surface);
	if (tofi.wl_keyboard != NULL) {
		wl_keyboard_release(tofi.wl_keyboard);
	}
	if (tofi.wl_pointer != NULL) {
		wl_pointer_release(tofi.wl_pointer);
	}
	wl_compositor_destroy(tofi.wl_compositor);
	if (tofi.clipboard.wl_data_offer != NULL) {
		wl_data_offer_destroy(tofi.clipboard.wl_data_offer);
	}
	wl_data_device_release(tofi.wl_data_device);
	wl_data_device_manager_destroy(tofi.wl_data_device_manager);
	wl_seat_release(tofi.wl_seat);
	{
		struct output_list_element *el;
		struct output_list_element *tmp;
		wl_list_for_each_safe(el, tmp, &tofi.output_list, link) {
			wl_list_remove(&el->link);
			wl_output_release(el->wl_output);
			free(el->name);
			free(el);
		}
	}
	wl_shm_destroy(tofi.wl_shm);
	if (tofi.wp_fractional_scale_manager != NULL) {
		wp_fractional_scale_manager_v1_destroy(tofi.wp_fractional_scale_manager);
	}
	if (tofi.wp_viewporter != NULL) {
		wp_viewporter_destroy(tofi.wp_viewporter);
	}
	zwlr_layer_shell_v1_destroy(tofi.zwlr_layer_shell);
	xkb_state_unref(tofi.xkb_state);
	xkb_keymap_unref(tofi.xkb_keymap);
	xkb_context_unref(tofi.xkb_context);
	wl_registry_destroy(tofi.wl_registry);
	desktop_vec_destroy(&tofi.window.engine.apps);
	if (tofi.window.engine.command_buffer != NULL) {
		free(tofi.window.engine.command_buffer);
	}
	entry_ref_vec_destroy(&tofi.window.engine.commands);
	entry_ref_vec_destroy(&tofi.window.engine.results);
	if (tofi.use_history) {
		history_destroy(&tofi.window.engine.history);
	}
#endif
	/*
	 * For release builds, skip straight to display disconnection and quit.
	 */
	wl_display_roundtrip(tofi.wl_display);
	wl_display_disconnect(tofi.wl_display);

	log_debug("Finished, exiting.\n");
	return EXIT_SUCCESS;
}
