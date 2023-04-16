#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <unistd.h>
#include "desktop_vec.h"
#include "input.h"
#include "log.h"
#include "nelem.h"
#include "entry.h"
#include "tofi.h"
#include "unicode.h"


static void add_character(struct tofi *tofi, xkb_keycode_t keycode);
static void delete_character(struct tofi *tofi);
static void delete_word(struct tofi *tofi);
static void clear_input(struct tofi *tofi);
static void paste(struct tofi *tofi);
static void select_previous_result(struct tofi *tofi);
static void select_next_result(struct tofi *tofi);
static void select_previous_page(struct tofi *tofi);
static void select_next_page(struct tofi *tofi);
static void next_cursor_or_result(struct tofi *tofi);
static void previous_cursor_or_result(struct tofi *tofi);
static void reset_selection(struct tofi *tofi);

void input_handle_keypress(struct tofi *tofi, xkb_keycode_t keycode)
{
	if (tofi->xkb_state == NULL) {
		return;
	}

	/*
	 * Use physical key code for shortcuts, ignoring layout changes.
	 * Linux keycodes are 8 less than XKB keycodes.
	 */
	const uint32_t key = keycode - 8;

	xkb_keysym_t sym = xkb_state_key_get_one_sym(tofi->xkb_state, keycode);

	uint32_t ch = xkb_state_key_get_utf32(
			tofi->xkb_state,
			keycode);
	if (utf32_isprint(ch)) {
		add_character(tofi, keycode);
	} else if ((sym == XKB_KEY_BackSpace || key == KEY_W)
			&& xkb_state_mod_name_is_active(
				tofi->xkb_state,
				XKB_MOD_NAME_CTRL,
				XKB_STATE_MODS_EFFECTIVE))
	{
		delete_word(tofi);
	} else if (sym == XKB_KEY_BackSpace) {
		delete_character(tofi);
	} else if (key == KEY_U
			&& xkb_state_mod_name_is_active(
				tofi->xkb_state,
				XKB_MOD_NAME_CTRL,
				XKB_STATE_MODS_EFFECTIVE)
		   )
	{
		clear_input(tofi);
	} else if (key == KEY_V
			&& xkb_state_mod_name_is_active(
				tofi->xkb_state,
				XKB_MOD_NAME_CTRL,
				XKB_STATE_MODS_EFFECTIVE)
		   )
	{
		paste(tofi);
	} else if (sym == XKB_KEY_Left) {
		previous_cursor_or_result(tofi);
	} else if (sym == XKB_KEY_Right) {
		next_cursor_or_result(tofi);
	} else if (sym == XKB_KEY_Up || sym == XKB_KEY_Left || sym == XKB_KEY_ISO_Left_Tab
			|| ((key == KEY_K || key == KEY_P)
				&& xkb_state_mod_name_is_active(
					tofi->xkb_state,
					XKB_MOD_NAME_CTRL,
					XKB_STATE_MODS_EFFECTIVE)
			   )
	   ) {
		select_previous_result(tofi);
	} else if (sym == XKB_KEY_Down || sym == XKB_KEY_Right || sym == XKB_KEY_Tab
			|| ((key == KEY_J || key == KEY_N)
				&& xkb_state_mod_name_is_active(
					tofi->xkb_state,
					XKB_MOD_NAME_CTRL,
					XKB_STATE_MODS_EFFECTIVE)
			   )
		  ) {
		select_next_result(tofi);
	} else if (sym == XKB_KEY_Home) {
		reset_selection(tofi);
	} else if (sym == XKB_KEY_Page_Up) {
		select_previous_page(tofi);
	} else if (sym == XKB_KEY_Page_Down) {
		select_next_page(tofi);
	} else if (sym == XKB_KEY_Escape
			|| (key == KEY_C
				&& xkb_state_mod_name_is_active(
					tofi->xkb_state,
					XKB_MOD_NAME_CTRL,
					XKB_STATE_MODS_EFFECTIVE)
			   )
		  )
	{
		tofi->closed = true;
		return;
	} else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		tofi->submit = true;
		return;
	}

	tofi->window.surface.redraw = true;
}

void reset_selection(struct tofi *tofi) {
	struct engine *engine = &tofi->window.engine;
	engine->selection = 0;
	engine->first_result = 0;
}

void add_character(struct tofi *tofi, xkb_keycode_t keycode)
{
	struct engine *engine = &tofi->window.engine;

	if (engine->input_utf32_length >= N_ELEM(engine->input_utf32) - 1) {
		/* No more room for input */
		return;
	}

	char buf[5]; /* 4 UTF-8 bytes plus null terminator. */
	int len = xkb_state_key_get_utf8(
			tofi->xkb_state,
			keycode,
			buf,
			sizeof(buf));
	if (engine->cursor_position == engine->input_utf32_length) {
		engine->input_utf32[engine->input_utf32_length] = utf8_to_utf32(buf);
		engine->input_utf32_length++;
		engine->input_utf32[engine->input_utf32_length] = U'\0';
		memcpy(&engine->input_utf8[engine->input_utf8_length],
				buf,
				N_ELEM(buf));
		engine->input_utf8_length += len;

		if (engine->drun) {
			struct entry_ref_vec results = desktop_vec_filter(&engine->apps, engine->input_utf8, tofi->fuzzy_match);
			entry_ref_vec_destroy(&engine->results);
			engine->results = results;
		} else {
//			struct string_ref_vec tmp = engine->results;
//			engine->results = string_ref_vec_filter(&engine->results, engine->input_utf8, tofi->fuzzy_match);
//			string_ref_vec_destroy(&tmp);
		}

		reset_selection(tofi);
	} else {
		for (size_t i = engine->input_utf32_length; i > engine->cursor_position; i--) {
			engine->input_utf32[i] = engine->input_utf32[i - 1];
		}
		engine->input_utf32[engine->cursor_position] = utf8_to_utf32(buf);
		engine->input_utf32_length++;
		engine->input_utf32[engine->input_utf32_length] = U'\0';

		input_refresh_results(tofi);
	}

	engine->cursor_position++;
}

void input_refresh_results(struct tofi *tofi)
{
	struct engine *engine = &tofi->window.engine;

	size_t bytes_written = 0;
	for (size_t i = 0; i < engine->input_utf32_length; i++) {
		bytes_written += utf32_to_utf8(
				engine->input_utf32[i],
				&engine->input_utf8[bytes_written]);
	}
	engine->input_utf8[bytes_written] = '\0';
	engine->input_utf8_length = bytes_written;
	entry_ref_vec_destroy(&engine->results);
	if (engine->drun) {
		engine->results = desktop_vec_filter(&engine->apps, engine->input_utf8, tofi->fuzzy_match);
	} else {
		//engine->results = string_ref_vec_filter(&engine->commands, engine->input_utf8, tofi->fuzzy_match);
	}

	reset_selection(tofi);
}

void delete_character(struct tofi *tofi)
{
	struct engine *engine = &tofi->window.engine;

	if (engine->input_utf32_length == 0) {
		/* No input to delete. */
		return;
	}

	if (engine->cursor_position == 0) {
		return;
	} else if (engine->cursor_position == engine->input_utf32_length) {
		engine->cursor_position--;
		engine->input_utf32_length--;
		engine->input_utf32[engine->input_utf32_length] = U'\0';
	} else {
		for (size_t i = engine->cursor_position - 1; i < engine->input_utf32_length - 1; i++) {
			engine->input_utf32[i] = engine->input_utf32[i + 1];
		}
		engine->cursor_position--;
		engine->input_utf32_length--;
		engine->input_utf32[engine->input_utf32_length] = U'\0';
	}

	input_refresh_results(tofi);
}

void delete_word(struct tofi *tofi)
{
	struct engine *engine = &tofi->window.engine;

	if (engine->cursor_position == 0) {
		/* No input to delete. */
		return;
	}

	uint32_t new_cursor_pos = engine->cursor_position;
	while (new_cursor_pos > 0 && utf32_isspace(engine->input_utf32[new_cursor_pos - 1])) {
		new_cursor_pos--;
	}
	while (new_cursor_pos > 0 && !utf32_isspace(engine->input_utf32[new_cursor_pos - 1])) {
		new_cursor_pos--;
	}
	uint32_t new_length = engine->input_utf32_length - (engine->cursor_position - new_cursor_pos);
	for (size_t i = 0; i < new_length; i++) {
		engine->input_utf32[new_cursor_pos + i] = engine->input_utf32[engine->cursor_position + i];
	}
	engine->input_utf32_length = new_length;
	engine->input_utf32[engine->input_utf32_length] = U'\0';

	engine->cursor_position = new_cursor_pos;
	input_refresh_results(tofi);
}

void clear_input(struct tofi *tofi)
{
	struct engine *engine = &tofi->window.engine;

	engine->cursor_position = 0;
	engine->input_utf32_length = 0;
	engine->input_utf32[0] = U'\0';

	input_refresh_results(tofi);
}

void paste(struct tofi *tofi)
{
	if (tofi->clipboard.wl_data_offer == NULL || tofi->clipboard.mime_type == NULL) {
		return;
	}

	/*
	 * Create a pipe, and give the write end to the compositor to give to
	 * the clipboard manager.
	 */
	errno = 0;
	int fildes[2];
	if (pipe2(fildes, O_CLOEXEC | O_NONBLOCK) == -1) {
		log_error("Failed to open pipe for clipboard: %s\n", strerror(errno));
		return;
	}
	wl_data_offer_receive(tofi->clipboard.wl_data_offer, tofi->clipboard.mime_type, fildes[1]);
	close(fildes[1]);

	/* Keep the read end for reading in the main loop. */
	tofi->clipboard.fd = fildes[0];
}

void select_previous_result(struct tofi *tofi)
{
	struct engine *engine = &tofi->window.engine;

	if (engine->selection > 0) {
		engine->selection--;
		return;
	}

	uint32_t nsel = MAX(MIN(engine->num_results_drawn, engine->results.count), 1);

	if (engine->first_result > nsel) {
		engine->first_result -= engine->last_num_results_drawn;
		engine->selection = engine->last_num_results_drawn - 1;
	} else if (engine->first_result > 0) {
		engine->selection = engine->first_result - 1;
		engine->first_result = 0;
	} else {
		engine->selection = engine->num_results_drawn - 1;
  }
}

void select_next_result(struct tofi *tofi)
{
	struct engine *engine = &tofi->window.engine;

	uint32_t nsel = MAX(MIN(engine->num_results_drawn, engine->results.count), 1);

	engine->selection++;
	if (engine->selection >= nsel) {
		engine->selection -= nsel;
		if (engine->results.count > 0) {
			engine->first_result += nsel;
			engine->first_result %= engine->results.count;
		} else {
			engine->first_result = 0;
		}
		engine->last_num_results_drawn = engine->num_results_drawn;
	}
}

void previous_cursor_or_result(struct tofi *tofi)
{
	struct engine *engine = &tofi->window.engine;

	if (engine->cursor_theme.show
			&& engine->selection == 0
			&& engine->cursor_position > 0) {
		engine->cursor_position--;
	} else {
		select_previous_result(tofi);
	}
}

void next_cursor_or_result(struct tofi *tofi)
{
	struct engine *engine = &tofi->window.engine;

	if (engine->cursor_theme.show
			&& engine->cursor_position < engine->input_utf32_length) {
		engine->cursor_position++;
	} else {
		select_next_result(tofi);
	}
}

void select_previous_page(struct tofi *tofi)
{
	struct engine *engine = &tofi->window.engine;

	if (engine->first_result >= engine->last_num_results_drawn) {
		engine->first_result -= engine->last_num_results_drawn;
	} else {
		engine->first_result = 0;
	}
	engine->selection = 0;
	engine->last_num_results_drawn = engine->num_results_drawn;
}

void select_next_page(struct tofi *tofi)
{
	struct engine *engine = &tofi->window.engine;

	engine->first_result += engine->num_results_drawn;
	if (engine->first_result >= engine->results.count) {
		engine->first_result = 0;
	}
	engine->selection = 0;
	engine->last_num_results_drawn = engine->num_results_drawn;
}
