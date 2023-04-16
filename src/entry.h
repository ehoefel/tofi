#ifndef ENTRY_H
#define ENTRY_H

#include "color.h"
#include "css.h"
#include "desktop_vec.h"
#include "history.h"
#include "icon.h"

struct entry {
  struct icon *icon;
  char *name;
  char *comment;
  struct css_classes classes;
};

struct scored_entry {
  struct entry *entry;
  int32_t search_score;
  int32_t history_score;
};

struct entry_ref_vec {
  size_t count;
  size_t size;
  struct scored_entry *buf;
};

[[nodiscard("memory leaked")]]
struct entry_ref_vec entry_ref_vec_create(void);
struct entry_ref_vec entry_ref_vec_copy(
    const struct entry_ref_vec *restrict vec);
void entry_ref_vec_destroy(struct entry_ref_vec *restrict vec);
void entry_ref_vec_history_sort(struct entry_ref_vec *restrict vec, struct history *history);
struct scored_string_ref *entry_ref_vec_find_sorted(struct entry_ref_vec *restrict vec, const char *str);

[[nodiscard("memory leaked")]]
struct entry_ref_vec entry_ref_vec_filter(
		const struct entry_ref_vec *restrict vec,
		const char *restrict substr,
		bool fuzzy);

[[nodiscard("memory leaked")]]
struct entry_ref_vec entry_ref_vec_from_buffer(char *buffer);

void entry_ref_vec_add(struct entry_ref_vec *restrict vec,
    struct entry *restrict str);

void entry_ref_vec_add_desktop(struct entry_ref_vec *restrict vec,
    struct desktop_entry *restrict des);

#endif /* ENTRY_H */
