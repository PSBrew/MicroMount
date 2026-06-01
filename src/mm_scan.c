#include "mm_scan.h"

#include "mm_log.h"
#include "mm_paths.h"
#include "mm_runtime.h"
#include "mm_sha256.h"
#include "mm_util.h"

static int mm_compare_candidates(const void *left, const void *right) {
  const mm_image_candidate_t *a = left;
  const mm_image_candidate_t *b = right;
  return strcmp(a->source_path, b->source_path);
}

static bool mm_candidate_list_contains_source(const mm_candidate_list_t *list,
                                              const char *source_path) {
  size_t index;

  for (index = 0; index < list->count; ++index) {
    if (strcmp(list->items[index].source_path, source_path) == 0)
      return true;
  }

  return false;
}

static bool mm_candidate_list_append(mm_candidate_list_t *list,
                                     const mm_image_candidate_t *candidate) {
  mm_image_candidate_t *resized;
  size_t new_capacity;

  if (list->count == list->capacity) {
    new_capacity = (list->capacity == 0) ? 16u : list->capacity * 2u;
    resized =
        realloc(list->items, new_capacity * sizeof(mm_image_candidate_t));
    if (!resized)
      return false;
    list->items = resized;
    list->capacity = new_capacity;
  }

  list->items[list->count++] = *candidate;
  return true;
}

static void mm_extract_title_id(const char *file_name, char out[16]) {
  char stem[NAME_MAX];
  size_t index;
  size_t write_index = 0;

  mm_remove_extension(file_name, stem, sizeof(stem));

  for (index = 0; stem[index] != '\0'; ++index) {
    size_t offset;
    bool letters_ok = true;

    for (offset = 0; offset < 4; ++offset) {
      if (stem[index + offset] == '\0' ||
          !isalpha((unsigned char)stem[index + offset])) {
        letters_ok = false;
        break;
      }
    }
    if (!letters_ok)
      continue;

    if (isdigit((unsigned char)stem[index + 4]) &&
        isdigit((unsigned char)stem[index + 5]) &&
        isdigit((unsigned char)stem[index + 6]) &&
        isdigit((unsigned char)stem[index + 7])) {
      size_t length = 8u;
      if (isdigit((unsigned char)stem[index + 8]))
        length = 9u;
      for (offset = 0; offset < length; ++offset)
        out[offset] = (char)toupper((unsigned char)stem[index + offset]);
      out[length] = '\0';
      return;
    }
  }

  for (index = 0; stem[index] != '\0' && write_index < 10u; ++index) {
    if (!isalnum((unsigned char)stem[index]))
      continue;
    out[write_index++] = (char)toupper((unsigned char)stem[index]);
  }

  if (write_index == 0)
    (void)mm_copy_string(out, 16, "UNKNOWN");
  else
    out[write_index] = '\0';
}

static bool mm_build_candidate(const mm_config_t *config, const char *source_path,
                               mm_image_candidate_t *candidate) {
  const char *file_name;

  memset(candidate, 0, sizeof(*candidate));
  if (!mm_copy_string(candidate->source_path, sizeof(candidate->source_path),
                      source_path)) {
    mm_log_warn("SCAN", "source path too long: %s", source_path);
    return false;
  }

  file_name = mm_basename_ptr(source_path);
  if (!mm_copy_string(candidate->file_name, sizeof(candidate->file_name),
                      file_name)) {
    mm_log_warn("SCAN", "file name too long: %s", file_name);
    return false;
  }

  mm_extract_title_id(file_name, candidate->title_id);
  mm_sha256_first8_hex_string(source_path, candidate->source_hash);
  if (snprintf(candidate->mount_name, sizeof(candidate->mount_name), "%s%s-%s",
               MM_MANAGED_PREFIX, candidate->title_id,
               candidate->source_hash) < 0) {
    return false;
  }

  if (!mm_path_join(candidate->mount_path, sizeof(candidate->mount_path),
                    config->target_directory, candidate->mount_name)) {
    mm_log_warn("SCAN", "mount path too long for %s", source_path);
    return false;
  }

  return true;
}

static bool mm_scan_directory(const mm_config_t *config,
                              const char *current_path, unsigned int depth,
                              mm_candidate_list_t *list) {
  DIR *dir;
  struct dirent *entry;

  if (mm_should_stop())
    return false;

  dir = opendir(current_path);
  if (!dir) {
    if (errno != ENOENT)
      mm_log_warn("SCAN", "failed to open %s: %s", current_path,
                  strerror(errno));
    return errno == ENOENT;
  }

  while ((entry = readdir(dir)) != NULL) {
    char full_path[PATH_MAX];
    struct stat st;

    if (mm_should_stop()) {
      closedir(dir);
      return false;
    }

    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    if (strncmp(entry->d_name, MM_MANAGED_PREFIX,
                strlen(MM_MANAGED_PREFIX)) == 0) {
      continue;
    }

    if (!mm_path_join(full_path, sizeof(full_path), current_path,
                      entry->d_name)) {
      mm_log_warn("SCAN", "skipping overlong path under %s", current_path);
      continue;
    }

    if (lstat(full_path, &st) != 0) {
      mm_log_warn("SCAN", "failed to stat %s: %s", full_path, strerror(errno));
      continue;
    }

    if (S_ISDIR(st.st_mode)) {
      if (depth < config->scan_depth &&
          !mm_scan_directory(config, full_path, depth + 1u, list)) {
        closedir(dir);
        return false;
      }
      continue;
    }

    if (!S_ISREG(st.st_mode))
      continue;
    if (!mm_string_ends_with_ignore_case(entry->d_name, ".ffpfsc"))
      continue;
    if (mm_candidate_list_contains_source(list, full_path))
      continue;

    {
      mm_image_candidate_t candidate;

      if (!mm_build_candidate(config, full_path, &candidate)) {
        closedir(dir);
        return false;
      }

      if (!mm_candidate_list_append(list, &candidate)) {
        mm_log_error("SCAN", "out of memory while tracking %s", full_path);
        closedir(dir);
        return false;
      }

      mm_log_debug("SCAN", "discovered %s -> %s", candidate.source_path,
                   candidate.mount_path);
    }
  }

  closedir(dir);
  return true;
}

void mm_candidate_list_init(mm_candidate_list_t *list) {
  if (!list)
    return;
  memset(list, 0, sizeof(*list));
}

void mm_candidate_list_free(mm_candidate_list_t *list) {
  if (!list)
    return;
  free(list->items);
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
}

bool mm_scan_for_images(const mm_config_t *config, mm_candidate_list_t *list) {
  size_t index;
  bool success = true;

  if (!config || !list)
    return false;

  mm_candidate_list_free(list);
  mm_candidate_list_init(list);

  for (index = 0; index < config->scan_path_count; ++index) {
    if (mm_should_stop())
      return false;
    mm_log_debug("SCAN", "walking root=%s depth=%u", config->scan_paths[index],
                 config->scan_depth);
    if (!mm_scan_directory(config, config->scan_paths[index], 0u, list)) {
      if (mm_should_stop())
        return false;
      success = false;
    }
  }

  if (list->count > 1)
    qsort(list->items, list->count, sizeof(list->items[0]),
          mm_compare_candidates);

  mm_log_info("SCAN", "found %zu candidate image(s)", list->count);
  return success;
}
