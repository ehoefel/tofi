#include <glib.h>
#include <stdbool.h>
#include "desktop_vec.h"
#include "fuzzy_match.h"
#include "log.h"
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
	}
	free(vec->buf);
}

void desktop_vec_add(
		struct desktop_vec *restrict vec,
		const char *restrict id,
		const char *restrict name,
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
	vec->buf[vec->count].path = xstrdup(path);
	vec->buf[vec->count].keywords = xstrdup(keywords);
	vec->buf[vec->count].search_score = 0;
	vec->buf[vec->count].history_score = 0;
	vec->count++;
}

void desktop_vec_add_file(struct desktop_vec *vec, const char *id, const char *path)
{
	GKeyFile *file = g_key_file_new();
	if (!g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL)) {
		log_error("Failed to open %s.\n", path);
		return;
	}

	const char *group = "Desktop Entry";

	if (g_key_file_get_boolean(file, group, "Hidden", NULL)
			|| g_key_file_get_boolean(file, group, "NoDisplay", NULL)) {
		goto cleanup_file;
	}

	char *name = g_key_file_get_locale_string(file, group, "Name", NULL, NULL);
	if (name == NULL) {
		log_error("%s: No name found.\n", path);
		goto cleanup_file;
	}

	char *icon = g_key_file_get_locale_string(file, group, "Icon", NULL, NULL);
	if (icon == NULL) {
    icon="󱀶";
	}

  char *displayname;
  size_t sz;
  sz = snprintf(NULL, 0, "%s %s", icon, name);
  displayname = (char *)malloc(sz + 1);
  snprintf(displayname, sz+1, "%s %s", icon, name);

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

	desktop_vec_add(vec, id, displayname, path, keywords);

cleanup_all:
	free(keywords);
	free(name);
  free(displayname);
cleanup_file:
	g_key_file_unref(file);
}

static int cmpdesktopp(const void *restrict a, const void *restrict b)
{
	struct desktop_entry *restrict d1 = (struct desktop_entry *)a;
	struct desktop_entry *restrict d2 = (struct desktop_entry *)b;
	return strcmp(d1->name, d2->name);
}

static int cmpscorep(const void *restrict a, const void *restrict b)
{
	struct scored_string *restrict str1 = (struct scored_string *)a;
	struct scored_string *restrict str2 = (struct scored_string *)b;

	int hist_diff = str2->history_score - str1->history_score;
	int search_diff = str2->search_score - str1->search_score;
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

struct string_ref_vec desktop_vec_filter(
		const struct desktop_vec *restrict vec,
		const char *restrict substr,
		bool fuzzy)
{
	struct string_ref_vec filt = string_ref_vec_create();
	for (size_t i = 0; i < vec->count; i++) {
		int32_t search_score;
		if (fuzzy) {
			search_score = fuzzy_match_words(substr, vec->buf[i].name);
		} else {
			search_score = fuzzy_match_simple_words(substr, vec->buf[i].name);
		}
		if (search_score != INT32_MIN) {
			string_ref_vec_add(&filt, vec->buf[i].name);
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
				string_ref_vec_add(&filt, vec->buf[i].name);
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
	 * Sort the results by this search_score. This moves matches at the beginnings
	 * of words to the front of the result list.
	 */
	qsort(filt.buf, filt.count, sizeof(filt.buf[0]), cmpscorep);
	return filt;
}

struct desktop_vec desktop_vec_load(FILE *file)
{
	struct desktop_vec vec = desktop_vec_create();
	if (file == NULL) {
		return vec;
	}

	ssize_t bytes_read;
	char *line = NULL;
	size_t len;
	while ((bytes_read = getline(&line, &len, file)) != -1) {
		if (line[bytes_read - 1] == '\n') {
			line[bytes_read - 1] = '\0';
		}
		char *id = line;
		size_t sublen = strlen(line);
		char *name = &line[sublen + 1];
		sublen = strlen(name);
		char *path = &name[sublen + 1];
		sublen = strlen(path);
		char *keywords = &path[sublen + 1];
		desktop_vec_add(&vec, id, name, path, keywords);
	}
	free(line);

	return vec;
}

void desktop_vec_save(struct desktop_vec *restrict vec, FILE *restrict file)
{
	/*
	 * Using null bytes for field separators is a bit odd, but it makes
	 * parsing very quick and easy.
	 */
	for (size_t i = 0; i < vec->count; i++) {
		fputs(vec->buf[i].id, file);
		fputc('\0', file);
		fputs(vec->buf[i].name, file);
		fputc('\0', file);
		fputs(vec->buf[i].path, file);
		fputc('\0', file);
		fputs(vec->buf[i].keywords, file);
		fputc('\n', file);
	}
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
