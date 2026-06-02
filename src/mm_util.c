#include "mm_util.h"

bool mm_copy_string(char *dst, size_t dst_size, const char *src) {
  size_t src_len;

  if (!dst || dst_size == 0)
    return false;

  if (!src) {
    dst[0] = '\0';
    return true;
  }

  src_len = strlen(src);
  if (src_len >= dst_size)
    src_len = dst_size - 1;

  memcpy(dst, src, src_len);
  dst[src_len] = '\0';
  return src[src_len] == '\0';
}

char *mm_trim(char *text) {
  char *end;

  if (!text)
    return NULL;

  while (*text != '\0' && isspace((unsigned char)*text))
    text++;

  if (*text == '\0')
    return text;

  end = text + strlen(text) - 1;
  while (end > text && isspace((unsigned char)*end)) {
    *end = '\0';
    end--;
  }

  return text;
}

bool mm_parse_bool_string(const char *text, bool *out_value) {
  if (!text || !out_value)
    return false;

  if (strcasecmp(text, "1") == 0 || strcasecmp(text, "true") == 0 ||
      strcasecmp(text, "yes") == 0 || strcasecmp(text, "on") == 0 ||
      strcasecmp(text, "rw") == 0) {
    *out_value = true;
    return true;
  }

  if (strcasecmp(text, "0") == 0 || strcasecmp(text, "false") == 0 ||
      strcasecmp(text, "no") == 0 || strcasecmp(text, "off") == 0 ||
      strcasecmp(text, "ro") == 0) {
    *out_value = false;
    return true;
  }

  return false;
}

bool mm_parse_u32_string(const char *text, uint32_t min_value,
                         uint32_t max_value, uint32_t *out_value) {
  char *end = NULL;
  unsigned long parsed;

  if (!text || !out_value)
    return false;

  errno = 0;
  parsed = strtoul(text, &end, 0);
  if (errno != 0 || end == text || *end != '\0')
    return false;
  if (parsed < min_value || parsed > max_value)
    return false;

  *out_value = (uint32_t)parsed;
  return true;
}

bool mm_parse_u16_string(const char *text, uint16_t *out_value) {
  uint32_t parsed = 0;

  if (!mm_parse_u32_string(text, 0u, UINT16_MAX, &parsed))
    return false;

  *out_value = (uint16_t)parsed;
  return true;
}

static bool mm_make_directory_0777(const char *path) {
  struct stat st;

  if (mkdir(path, 0777) != 0) {
    if (errno != EEXIST)
      return false;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
      return false;
  }

  if (chmod(path, 0777) != 0)
    return false;

  return true;
}

bool mm_path_join(char *out, size_t out_size, const char *left,
                  const char *right) {
  int written;

  if (!out || out_size == 0 || !left || !right)
    return false;

  if (left[0] == '\0')
    written = snprintf(out, out_size, "%s", right);
  else if (right[0] == '\0')
    written = snprintf(out, out_size, "%s", left);
  else if (left[strlen(left) - 1] == '/')
    written = snprintf(out, out_size, "%s%s", left,
                       right[0] == '/' ? right + 1 : right);
  else
    written = snprintf(out, out_size, "%s/%s", left,
                       right[0] == '/' ? right + 1 : right);

  return written >= 0 && (size_t)written < out_size;
}

const char *mm_basename_ptr(const char *path) {
  const char *slash;

  if (!path)
    return "";

  slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

void mm_remove_extension(const char *file_name, char *out, size_t out_size) {
  char *dot;

  if (!out || out_size == 0) {
    return;
  }

  if (!mm_copy_string(out, out_size, file_name)) {
    out[out_size - 1] = '\0';
  }

  dot = strrchr(out, '.');
  if (dot)
    *dot = '\0';
}

bool mm_ensure_dir_recursive(const char *path) {
  char temp[PATH_MAX];
  size_t len;
  char *cursor;

  if (!path || path[0] == '\0')
    return false;

  if (!mm_copy_string(temp, sizeof(temp), path))
    return false;

  mm_normalize_directory(temp);
  len = strlen(temp);
  if (len == 0)
    return false;
  if (len == 1 && temp[0] == '/')
    return true;

  for (cursor = temp + 1; *cursor != '\0'; ++cursor) {
    if (*cursor != '/')
      continue;
    *cursor = '\0';
    if (!mm_make_directory_0777(temp))
      return false;
    *cursor = '/';
  }

  if (!mm_make_directory_0777(temp))
    return false;

  return true;
}

bool mm_dir_is_empty(const char *path, bool *out_empty) {
  DIR *dir;
  struct dirent *entry;

  if (!path || !out_empty)
    return false;

  dir = opendir(path);
  if (!dir)
    return false;

  *out_empty = true;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    *out_empty = false;
    break;
  }

  closedir(dir);
  return true;
}

bool mm_string_ends_with_ignore_case(const char *text, const char *suffix) {
  size_t text_len;
  size_t suffix_len;

  if (!text || !suffix)
    return false;

  text_len = strlen(text);
  suffix_len = strlen(suffix);
  if (suffix_len > text_len)
    return false;

  return strcasecmp(text + text_len - suffix_len, suffix) == 0;
}

bool mm_file_exists(const char *path) {
  struct stat st;
  return path && stat(path, &st) == 0;
}

void mm_normalize_directory(char *path) {
  size_t len;

  if (!path)
    return;

  len = strlen(path);
  while (len > 1 && path[len - 1] == '/') {
    path[len - 1] = '\0';
    len--;
  }
}
