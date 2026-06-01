#ifndef MM_UTIL_H
#define MM_UTIL_H

#include "mm_platform.h"

bool mm_copy_string(char *dst, size_t dst_size, const char *src);
char *mm_trim(char *text);
bool mm_parse_bool_string(const char *text, bool *out_value);
bool mm_parse_u32_string(const char *text, uint32_t min_value,
                         uint32_t max_value, uint32_t *out_value);
bool mm_parse_u16_string(const char *text, uint16_t *out_value);
bool mm_path_join(char *out, size_t out_size, const char *left,
                  const char *right);
const char *mm_basename_ptr(const char *path);
void mm_remove_extension(const char *file_name, char *out, size_t out_size);
bool mm_ensure_dir_recursive(const char *path);
bool mm_dir_is_empty(const char *path, bool *out_empty);
bool mm_string_ends_with_ignore_case(const char *text, const char *suffix);
bool mm_file_exists(const char *path);
void mm_normalize_directory(char *path);

#endif
