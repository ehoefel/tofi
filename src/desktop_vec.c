#include <glib.h>
#include <stdbool.h>
#include "desktop_vec.h"
#include "fuzzy_match.h"
#include "icon.h"
#include "log.h"
#include "entry.h"
#include "string_vec.h"
#include "unicode.h"
#include "xmalloc.h"

static bool match_current_desktop(char * const *desktop_list, gsize length);

[[nodiscard("memory leaked")]]
struct desktop_vec desktop_vec_create(void)
{
	struct desktop_vec vec = {
		.count = 0,
		.size = 128,
		.buf = xcalloc(128, sizeof(*vec.buf)),
	};
	return vec;
}

void desktop_vec_destroy(struct desktop_vec *restrict vec)
{
	for (size_t i = 0; i < vec->count; i++) {
		free(vec->buf[i].id);
		free(vec->buf[i].name);
		free(vec->buf[i].path);
		free(vec->buf[i].keywords);
    icon_destroy(&vec->buf[i].icon);
	}
	free(vec->buf);
}

struct desktop_entry *desktop_vec_add(
		struct desktop_vec *restrict vec,
		const char *restrict id,
		const char *restrict name,
		const char *restrict icon,
		const char *restrict path,
		const char *restrict keywords)
{
	if (vec->count == vec->size) {
		vec->size *= 2;
		vec->buf = xrealloc(vec->buf, vec->size * sizeof(vec->buf[0]));
	}
	vec->buf[vec->count].id = xstrdup(id);
	vec->buf[vec->count].name = utf8_normalize(name);
	if (vec->buf[vec->count].name == NULL) {
		vec->buf[vec->count].name = xstrdup(name);
	}
	icon_init(&vec->buf[vec->count].icon, icon);
	vec->buf[vec->count].path = xstrdup(path);
	vec->buf[vec->count].keywords = xstrdup(keywords);
	vec->buf[vec->count].search_score = 0;
	vec->buf[vec->count].history_score = 0;
	vec->count++;

  return &vec->buf[vec->count];
}

void desktop_vec_add_file(
    struct desktop_vec *vec,
    const char *id,
    const char *path)
{
  log_debug("parse_desktop_file %s\n", path);
	GKeyFile *file = g_key_file_new();
  log_debug("have gkeyfile\n");
	if (!g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL)) {
		log_error("Failed to open %s.\n", path);
		return;
	}
  log_debug("loaded file\n");

	const char *group = "Desktop Entry";

	if (g_key_file_get_boolean(file, group, "Hidden", NULL)
			|| g_key_file_get_boolean(file, group, "NoDisplay", NULL)) {
		goto cleanup_file;
	}

  log_debug("loading name\n");
	char *name = g_key_file_get_locale_string(file, group, "Name", NULL, NULL);
	if (name == NULL) {
		log_error("%s: No name found.\n", path);
		goto cleanup_file;
	}
  log_debug("parse_desktop_file %s\n", name);

	char *icon = g_key_file_get_locale_string(file, group, "Icon", NULL, NULL);

	/*
	 * This is really a list rather than a string, but for the purposes of
	 * matching against user input it's easier to just keep it as a string.
	 */
	char *keywords = g_key_file_get_locale_string(file, group, "Keywords", NULL, NULL);
	if (keywords == NULL) {
		keywords = xmalloc(1);
		*keywords = '\0';
	}

	gsize length;
	gchar **list = g_key_file_get_string_list(file, group, "OnlyShowIn", &length, NULL);
	if (list) {
		bool match = match_current_desktop(list, length);
		g_strfreev(list);
		list = NULL;
		if (!match) {
			goto cleanup_all;
		}
	}

	list = g_key_file_get_string_list(file, group, "NotShowIn", &length, NULL);
	if (list) {
		bool match = match_current_desktop(list, length);
		g_strfreev(list);
		list = NULL;
		if (match) {
			goto cleanup_all;
		}
	}

	struct desktop_entry *d = desktop_vec_add(vec, id, name, icon, path,
      keywords);

cleanup_all:
	free(keywords);
	free(name);
	free(icon);
cleanup_file:
	g_key_file_unref(file);
}

static int cmpdesktopp(const void *restrict a, const void *restrict b)
{
	struct desktop_entry *restrict d1 = (struct desktop_entry *)a;
	struct desktop_entry *restrict d2 = (struct desktop_entry *)b;
	return strcmp(d1->name, d2->name);
}

static int cmpresultscorep(const void *restrict a, const void *restrict b)
{
	struct scored_entry *restrict res1 = (struct scored_entry *)a;
	struct scored_entry *restrict res2 = (struct scored_entry *)b;

	int hist_diff = res2->history_score - res1->history_score;
	int search_diff = res2->search_score - res1->search_score;
	return hist_diff + search_diff;
}

void desktop_vec_sort(struct desktop_vec *restrict vec)
{
	qsort(vec->buf, vec->count, sizeof(vec->buf[0]), cmpdesktopp);
}

struct desktop_entry *desktop_vec_find_sorted(struct desktop_vec *restrict vec, const char *name)
{
	/*
	 * Explicitly cast away const-ness, as even though we won't modify the
	 * name, the compiler rightly complains that we might.
	 */
	struct desktop_entry tmp = { .name = (char *)name };
	return bsearch(&tmp, vec->buf, vec->count, sizeof(vec->buf[0]), cmpdesktopp);
}

struct entry_ref_vec desktop_vec_filter(
		const struct desktop_vec *restrict vec,
		const char *restrict substr,
		bool fuzzy)
{
	struct entry_ref_vec filt = entry_ref_vec_create();
	for (size_t i = 0; i < vec->count; i++) {
		int32_t search_score;
		if (fuzzy) {
			search_score = fuzzy_match_words(substr, vec->buf[i].name);
		} else {
			search_score = fuzzy_match_simple_words(substr, vec->buf[i].name);
		}
		if (search_score != INT32_MIN) {
			entry_ref_vec_add_desktop(&filt, &vec->buf[i]);
			/*
			 * Store the position of the match in the string as
			 * its search_score, for later sorting.
			 */
			filt.buf[filt.count - 1].search_score = search_score;
			filt.buf[filt.count - 1].history_score = vec->buf[i].history_score;
		} else {
			/* If we didn't match the name, check the keywords. */
			if (fuzzy) {
				search_score = fuzzy_match_words(substr, vec->buf[i].keywords);
			} else {
				search_score = fuzzy_match_simple_words(substr, vec->buf[i].keywords);
			}
			if (search_score != INT32_MIN) {
				entry_ref_vec_add_desktop(&filt, &vec->buf[i]);
				/*
				 * Arbitrary score addition to make name
				 * matches preferred over keyword matches.
				 */
				filt.buf[filt.count - 1].search_score = search_score - 20;
				filt.buf[filt.count - 1].history_score = vec->buf[i].history_score;
			}
		}
	}
	/*
	 * Sort the entrys by this search_score. This moves matches at the beginnings
	 * of words to the front of the entry list.
	 */
	qsort(filt.buf, filt.count, sizeof(filt.buf[0]), cmpresultscorep);
	return filt;
}

bool match_current_desktop(char * const *desktop_list, gsize length)
{
	const char *xdg_current_desktop = getenv("XDG_CURRENT_DESKTOP");
	if (xdg_current_desktop == NULL) {
		return false;
	}

	struct string_vec desktops = string_vec_create();

	char *saveptr = NULL;
	char *tmp = xstrdup(xdg_current_desktop);
 	char *desktop = strtok_r(tmp, ":", &saveptr);
 	while (desktop != NULL) {
		string_vec_add(&desktops, desktop);
 		desktop = strtok_r(NULL, ":", &saveptr);
 	}

	string_vec_sort(&desktops);
	for (gsize i = 0; i < length; i++) {
		if (string_vec_find_sorted(&desktops, desktop_list[i])) {
			return true;
		}
 	}

	string_vec_destroy(&desktops);
	free(tmp);
	return false;
}
