#ifndef RESULT_H
#define RESULT_H

#include "color.h"
#include "desktop_vec.h"
#include "history.h"
#include "icon.h"

struct result {
  struct icon *icon;
  char *name;
  char *comment;
};

struct scored_result {
  struct result *result;
	int32_t search_score;
	int32_t history_score;
};

struct result_ref_vec {
	size_t count;
	size_t size;
	struct scored_result *buf;
};

[[nodiscard("memory leaked")]]
struct result_ref_vec result_ref_vec_create(void);
struct result_ref_vec result_ref_vec_copy(
    const struct result_ref_vec *restrict vec);
void result_ref_vec_destroy(struct result_ref_vec *restrict vec);
void result_ref_vec_history_sort(struct result_ref_vec *restrict vec, struct history *history);
struct scored_string_ref *result_ref_vec_find_sorted(struct result_ref_vec *restrict vec, const char *str);

[[nodiscard("memory leaked")]]
struct result_ref_vec result_ref_vec_filter(
		const struct result_ref_vec *restrict vec,
		const char *restrict substr,
		bool fuzzy);

[[nodiscard("memory leaked")]]
struct result_ref_vec result_ref_vec_from_buffer(char *buffer);

void result_ref_vec_add(struct result_ref_vec *restrict vec,
    struct result *restrict str);

void result_ref_vec_add_desktop(struct result_ref_vec *restrict vec,
    struct desktop_entry *restrict des);

#endif /* RESULT_H */
