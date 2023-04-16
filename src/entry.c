
#include <glib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "entry.h"
#include "desktop_vec.h"
#include "fuzzy_match.h"
#include "history.h"
#include "icon.h"
#include "unicode.h"
#include "xmalloc.h"

static int cmpresultp(const void *restrict a, const void *restrict b)
{
	struct scored_entry *restrict res1 = (struct scored_entry *)a;
	struct scored_entry *restrict res2 = (struct scored_entry *)b;

	/*
	 * Ensure any NULL strings are shoved to the end.
	 */
	if (res1->entry->name == NULL) {
		return 1;
	}
	if (res2->entry->name == NULL) {
		return -1;
	}
	return strcmp(res1->entry->name, res2->entry->name);
}

static int cmpresultscorep(const void *restrict a, const void *restrict b)
{
	struct scored_entry *restrict res1 = (struct scored_entry *)a;
	struct scored_entry *restrict res2 = (struct scored_entry *)b;

	int hist_diff = res2->history_score - res1->history_score;
	int search_diff = res2->search_score - res1->search_score;
	return hist_diff + search_diff;
}

static int cmpresulthistoryp(const void *restrict a, const void *restrict b)
{
	struct scored_entry *restrict res1 = (struct scored_entry *)a;
	struct scored_entry *restrict res2 = (struct scored_entry *)b;

	return res2->history_score - res1->history_score;
}

struct entry_ref_vec entry_ref_vec_create(void)
{
	struct entry_ref_vec vec = {
		.count = 0,
		.size = 128,
		.buf = xcalloc(128, sizeof(*vec.buf)),
	};
	return vec;
}

void entry_ref_vec_destroy(struct entry_ref_vec *restrict vec)
{
	free(vec->buf);
}

void entry_ref_vec_add_desktop(struct entry_ref_vec *restrict vec,
    struct desktop_entry *restrict des)
{
	if (vec->count == vec->size) {
		vec->size *= 2;
		vec->buf = xrealloc(vec->buf, vec->size * sizeof(vec->buf[0]));
	}

  struct entry *res = (struct entry*) malloc(sizeof(struct entry));

	vec->buf[vec->count].entry = res;
	vec->buf[vec->count].entry->name = des->name;
	vec->buf[vec->count].entry->icon = &des->icon;
	vec->buf[vec->count].entry->comment = des->comment;
	vec->buf[vec->count].search_score = 0;
	vec->buf[vec->count].history_score = 0;
	vec->count++;

  return res;
}

void entry_ref_vec_add(struct entry_ref_vec *restrict vec,
    struct entry *restrict res)
{
	if (vec->count == vec->size) {
		vec->size *= 2;
		vec->buf = xrealloc(vec->buf, vec->size * sizeof(vec->buf[0]));
	}
	vec->buf[vec->count].entry = res;
	vec->buf[vec->count].search_score = 0;
	vec->buf[vec->count].history_score = 0;
	vec->count++;
}

void scored_entry_history_sort(struct entry_ref_vec *restrict vec,
    struct history *history)
{
	/*
	 * To find elements without assuming the vector is pre-sorted, we use a
	 * hash table, which results in O(N+M) work (rather than O(N*M) for
	 * linear search.
	 */
	GHashTable *hash = g_hash_table_new(g_str_hash, g_str_equal);
	for (size_t i = 0; i < vec->count; i++) {
		g_hash_table_insert(hash, vec->buf[i].entry->name, &vec->buf[i]);
	}
	for (size_t i = 0; i < history->count; i++) {
		struct scored_entry *res = g_hash_table_lookup(hash, history->buf[i].name);
		if (res == NULL) {
			continue;
		}
		res->history_score = history->buf[i].run_count;
	}
	g_hash_table_unref(hash);

	qsort(vec->buf, vec->count, sizeof(vec->buf[0]), cmpresulthistoryp);
}

struct scored_result *entry_vec_find_sorted(
    struct entry_ref_vec *restrict vec, const char * str)
{
  struct entry base = { .name = str };
	return bsearch(&base, vec->buf, vec->count, sizeof(vec->buf[0]), cmpresultp);
}

struct entry_ref_vec entry_ref_vec_copy(
    const struct entry_ref_vec *restrict vec)
{
	struct entry_ref_vec copy = {
		.count = vec->count,
		.size = vec->size,
		.buf = xcalloc(vec->size, sizeof(*copy.buf)),
	};

	for (size_t i = 0; i < vec->count; i++) {
		copy.buf[i].entry = vec->buf[i].entry;
		copy.buf[i].search_score = vec->buf[i].search_score;
		copy.buf[i].history_score = vec->buf[i].history_score;
	}

	return copy;
}

struct entry_ref_vec entry_ref_vec_filter(
		const struct entry_ref_vec *restrict vec,
		const char *restrict substr,
		bool fuzzy)
{
	if (substr[0] == '\0') {
		return entry_ref_vec_copy(vec);
	}
	struct entry_ref_vec filt = entry_ref_vec_create();
	for (size_t i = 0; i < vec->count; i++) {
		int32_t search_score;
		if (fuzzy) {
			search_score = fuzzy_match_words(substr, vec->buf[i].entry->name);
		} else {
			search_score = fuzzy_match_simple_words(substr, vec->buf[i].entry->name);
		}
		if (search_score != INT32_MIN) {
			entry_ref_vec_add(&filt, vec->buf[i].entry);
			filt.buf[filt.count - 1].search_score = search_score;
			filt.buf[filt.count - 1].history_score = vec->buf[i].history_score;
		}
	}
	/* Sort the results by their search score. */
	qsort(filt.buf, filt.count, sizeof(filt.buf[0]), cmpresultscorep);
	return filt;
}
