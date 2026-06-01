#include "mm_mount.h"

#include "mm_log.h"
#include "mm_paths.h"
#include "mm_runtime.h"
#include "mm_util.h"

#define MM_LVD_CTRL_PATH "/dev/lvdctl"
#define MM_SCE_LVD_IOC_ATTACH_V0 0xC0286D00
#define MM_SCE_LVD_IOC_DETACH 0xC0286D01
#define MM_LVD_ENTRY_TYPE_FILE 1
#define MM_LVD_ENTRY_FLAG_NO_BITMAP 0x1
#define MM_LVD_NODE_WAIT_US 100000u
#define MM_LVD_NODE_WAIT_RETRIES 60
#define MM_EKPFS_ZERO                                                         \
  "0000000000000000000000000000000000000000000000000000000000000000"

typedef struct {
  uint16_t source_type;
  uint16_t flags;
  uint32_t reserved0;
  const char *path;
  uint64_t offset;
  uint64_t size;
  const char *bitmap_path;
  uint64_t bitmap_offset;
  uint64_t bitmap_size;
} mm_lvd_layer_t;

typedef struct {
  uint32_t io_version;
  int32_t device_id;
  uint32_t sector_size;
  uint32_t secondary_unit;
  uint16_t flags;
  uint16_t image_type;
  uint32_t layer_count;
  uint64_t device_size;
  mm_lvd_layer_t *layers_ptr;
} mm_lvd_attach_t;

typedef struct {
  uint32_t reserved0;
  int32_t device_id;
  uint8_t reserved[0x20];
} mm_lvd_detach_t;

typedef struct {
  char path[PATH_MAX];
  bool mounted;
  bool empty;
  int lvd_unit_id;
} mm_managed_mount_t;

typedef struct {
  mm_managed_mount_t *items;
  size_t count;
  size_t capacity;
} mm_managed_mount_list_t;

static void mm_lvd_detach(int unit_id);

static uint16_t mm_normalize_raw_flags(uint16_t raw_flags) {
  switch (raw_flags) {
  case 0x8:
    return 0x14;
  case 0x9:
    return 0x1C;
  case 0xC:
    return 0x16;
  case 0xD:
    return 0x1E;
  case 0x0:
    return 0x04;
  case 0x1:
    return 0x0C;
  default:
    mm_log_warn("MOUNT", "raw LVD flags 0x%X not recognized; using as-is",
                (unsigned)raw_flags);
    return raw_flags;
  }
}

static uint32_t mm_effective_secondary_unit(const mm_mount_profile_t *profile) {
  if (profile->lvd_secondary_unit != 0u)
    return profile->lvd_secondary_unit;
  return profile->lvd_sector_size;
}

static bool mm_wait_for_dev_node(const char *devname, bool should_exist) {
  int retry;

  for (retry = 0; retry < MM_LVD_NODE_WAIT_RETRIES; ++retry) {
    struct stat st;
    bool exists = (stat(devname, &st) == 0);
    if (mm_should_stop())
      return false;
    if (exists == should_exist)
      return true;
    (void)sceKernelUsleep(MM_LVD_NODE_WAIT_US);
  }

  return false;
}

static int mm_lvd_attach(const char *image_path, off_t image_size,
                         const mm_mount_profile_t *profile,
                         char *devname_out, size_t devname_out_size) {
  int fd;
  int saved_errno;
  int result;
  int unit_id;
  mm_lvd_layer_t layer;
  mm_lvd_attach_t request;

  fd = open(MM_LVD_CTRL_PATH, O_RDWR);
  if (fd < 0) {
    mm_log_error("MOUNT", "open %s failed: %s", MM_LVD_CTRL_PATH,
                 strerror(errno));
    return -1;
  }

  memset(&layer, 0, sizeof(layer));
  layer.source_type = MM_LVD_ENTRY_TYPE_FILE;
  layer.flags = MM_LVD_ENTRY_FLAG_NO_BITMAP;
  layer.path = image_path;
  layer.size = (uint64_t)image_size;

  memset(&request, 0, sizeof(request));
  request.io_version = 0;
  request.device_id = -1;
  request.sector_size = profile->lvd_sector_size;
  request.secondary_unit = mm_effective_secondary_unit(profile);
  request.flags = mm_normalize_raw_flags(profile->lvd_raw_flags);
  request.image_type = profile->lvd_image_type;
  request.layer_count = 1;
  request.device_size = (uint64_t)image_size;
  request.layers_ptr = &layer;

  result = ioctl(fd, MM_SCE_LVD_IOC_ATTACH_V0, &request);
  saved_errno = errno;
  close(fd);
  if (result != 0) {
    errno = saved_errno;
    return -1;
  }

  unit_id = request.device_id;
  if (unit_id < 0) {
    mm_log_error("MOUNT", "LVD attach returned invalid unit id %d", unit_id);
    return -1;
  }

  if (snprintf(devname_out, devname_out_size, "/dev/lvd%d", unit_id) < 0) {
    mm_log_error("MOUNT", "failed to format device path for LVD %d", unit_id);
    return -1;
  }

  if (!mm_wait_for_dev_node(devname_out, true)) {
    mm_lvd_detach(unit_id);
    mm_log_error("MOUNT", "device node did not appear: %s", devname_out);
    return -1;
  }

  return unit_id;
}

static void mm_lvd_detach(int unit_id) {
  int fd;
  mm_lvd_detach_t request;

  if (unit_id < 0)
    return;

  fd = open(MM_LVD_CTRL_PATH, O_RDWR);
  if (fd < 0) {
    mm_log_warn("MOUNT", "open %s for detach failed: %s", MM_LVD_CTRL_PATH,
                strerror(errno));
    return;
  }

  memset(&request, 0, sizeof(request));
  request.device_id = unit_id;
  if (ioctl(fd, MM_SCE_LVD_IOC_DETACH, &request) != 0) {
    mm_log_warn("MOUNT", "detach LVD %d failed: %s", unit_id, strerror(errno));
  }
  close(fd);
}

static bool mm_try_pfs_nmount(const char *devname, const char *mount_path,
                              const mm_mount_profile_t *profile,
                              char *error_message, size_t error_message_size) {
  char errmsg[256];
  const char *sv = profile->pfs_sigverify ? "1" : "0";
  const char *pg = profile->pfs_playgo ? "1" : "0";
  const char *dc = profile->pfs_disc ? "1" : "0";
  struct iovec iov[32];
  unsigned int mount_flags = profile->pfs_read_only ? MNT_RDONLY : 0u;
  int count = 0;

  memset(errmsg, 0, sizeof(errmsg));
  if (error_message && error_message_size > 0)
    error_message[0] = '\0';

#define MM_IOV_STR(key, value)                                                \
  do {                                                                        \
    iov[count].iov_base = (void *)(uintptr_t)(key);                           \
    iov[count].iov_len = strlen(key) + 1u;                                    \
    count++;                                                                  \
    iov[count].iov_base = (void *)(uintptr_t)(value);                         \
    iov[count].iov_len = strlen(value) + 1u;                                  \
    count++;                                                                  \
  } while (0)

#define MM_IOV_NULL(key)                                                      \
  do {                                                                        \
    iov[count].iov_base = (void *)(uintptr_t)(key);                           \
    iov[count].iov_len = strlen(key) + 1u;                                    \
    count++;                                                                  \
    iov[count].iov_base = NULL;                                               \
    iov[count].iov_len = 0;                                                   \
    count++;                                                                  \
  } while (0)

#define MM_IOV_BUF(key, value, value_size)                                    \
  do {                                                                        \
    iov[count].iov_base = (void *)(uintptr_t)(key);                           \
    iov[count].iov_len = strlen(key) + 1u;                                    \
    count++;                                                                  \
    iov[count].iov_base = (void *)(value);                                    \
    iov[count].iov_len = (value_size);                                        \
    count++;                                                                  \
  } while (0)

  MM_IOV_STR("from", devname);
  MM_IOV_STR("fspath", mount_path);
  MM_IOV_STR("fstype", profile->pfs_fstype);
  MM_IOV_STR("sigverify", sv);
  MM_IOV_STR("mkeymode", profile->pfs_mkeymode);
  MM_IOV_STR("budgetid", profile->pfs_budgetid);
  MM_IOV_STR("playgo", pg);
  MM_IOV_STR("disc", dc);
  if (profile->pfs_use_ekpfs)
    MM_IOV_STR("ekpfs", MM_EKPFS_ZERO);
  MM_IOV_NULL("async");
  MM_IOV_NULL("noatime");
  MM_IOV_NULL("automounted");
  MM_IOV_BUF("errmsg", errmsg, sizeof(errmsg));
  if (profile->pfs_force)
    MM_IOV_NULL("force");

#undef MM_IOV_STR
#undef MM_IOV_NULL
#undef MM_IOV_BUF

  if (nmount(iov, (unsigned int)count, (int)mount_flags) == 0)
    return true;

  if (error_message && error_message_size > 0 && errmsg[0] != '\0')
    (void)mm_copy_string(error_message, error_message_size, errmsg);
  return false;
}

static bool mm_lookup_mount_info(const char *mount_path, bool *mounted_out,
                                 int *lvd_unit_id_out) {
  struct statfs *mounts = NULL;
  int mount_count;
  int index;

  if (mounted_out)
    *mounted_out = false;
  if (lvd_unit_id_out)
    *lvd_unit_id_out = -1;

  mount_count = getmntinfo(&mounts, MNT_NOWAIT);
  if (mount_count <= 0)
    return true;

  for (index = 0; index < mount_count; ++index) {
    if (strcmp(mounts[index].f_mntonname, mount_path) != 0)
      continue;

    if (mounted_out)
      *mounted_out = true;

    if (lvd_unit_id_out) {
      const char *from = mounts[index].f_mntfromname;
      if (strncmp(from, "/dev/lvd", 8) == 0)
        *lvd_unit_id_out = atoi(from + 8);
    }
    return true;
  }

  return true;
}

static bool mm_managed_mount_list_append(mm_managed_mount_list_t *list,
                                         const mm_managed_mount_t *item) {
  mm_managed_mount_t *resized;
  size_t new_capacity;

  if (list->count == list->capacity) {
    new_capacity = (list->capacity == 0) ? 16u : list->capacity * 2u;
    resized = realloc(list->items, new_capacity * sizeof(*resized));
    if (!resized)
      return false;
    list->items = resized;
    list->capacity = new_capacity;
  }

  list->items[list->count++] = *item;
  return true;
}

static void mm_managed_mount_list_free(mm_managed_mount_list_t *list) {
  free(list->items);
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
}

static bool mm_collect_managed_mounts(const mm_config_t *config,
                                      mm_managed_mount_list_t *list) {
  DIR *dir;
  struct dirent *entry;

  memset(list, 0, sizeof(*list));
  dir = opendir(config->target_directory);
  if (!dir) {
    if (errno == ENOENT)
      return true;
    mm_log_warn("CLEANUP", "failed to open %s: %s", config->target_directory,
                strerror(errno));
    return false;
  }

  while ((entry = readdir(dir)) != NULL) {
    char full_path[PATH_MAX];
    struct stat st;
    mm_managed_mount_t item;

    if (mm_should_stop()) {
      closedir(dir);
      mm_managed_mount_list_free(list);
      return false;
    }

    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    if (strncmp(entry->d_name, MM_MANAGED_PREFIX,
                strlen(MM_MANAGED_PREFIX)) != 0) {
      continue;
    }

    if (!mm_path_join(full_path, sizeof(full_path), config->target_directory,
                      entry->d_name)) {
      mm_log_warn("CLEANUP", "skipping overlong managed path for %s",
                  entry->d_name);
      continue;
    }

    if (lstat(full_path, &st) != 0 || !S_ISDIR(st.st_mode))
      continue;

    memset(&item, 0, sizeof(item));
    (void)mm_copy_string(item.path, sizeof(item.path), full_path);
    item.lvd_unit_id = -1;
    (void)mm_lookup_mount_info(item.path, &item.mounted, &item.lvd_unit_id);
    if (!mm_dir_is_empty(item.path, &item.empty))
      item.empty = false;

    if (!mm_managed_mount_list_append(list, &item)) {
      closedir(dir);
      mm_managed_mount_list_free(list);
      return false;
    }
  }

  closedir(dir);
  return true;
}

static bool mm_candidate_mount_path_exists(const mm_candidate_list_t *candidates,
                                           const char *mount_path) {
  size_t index;

  for (index = 0; index < candidates->count; ++index) {
    if (strcmp(candidates->items[index].mount_path, mount_path) == 0)
      return true;
  }

  return false;
}

static bool mm_cleanup_mount_path(const mm_managed_mount_t *item) {
  bool empty_after = false;

  if (item->mounted) {
    if (unmount(item->path, MNT_FORCE) != 0) {
      mm_log_error("CLEANUP", "failed to unmount %s: %s", item->path,
                   strerror(errno));
      return false;
    }
    mm_log_info("CLEANUP", "unmounted %s", item->path);
    if (item->lvd_unit_id >= 0)
      mm_lvd_detach(item->lvd_unit_id);
  }

  if (mm_dir_is_empty(item->path, &empty_after) && empty_after) {
    if (rmdir(item->path) != 0 && errno != ENOENT) {
      mm_log_warn("CLEANUP", "failed to remove %s: %s", item->path,
                  strerror(errno));
      return false;
    }
    mm_log_info("CLEANUP", "removed empty path %s", item->path);
  }

  return true;
}

static bool mm_mount_candidate(const mm_config_t *config,
                               const mm_image_candidate_t *candidate) {
  struct stat st;
  char devname[32];
  char errmsg[256];
  int unit_id;

  if (mm_should_stop())
    return false;

  if (stat(candidate->source_path, &st) != 0) {
    mm_log_error("MOUNT", "source missing before mount %s: %s",
                 candidate->source_path, strerror(errno));
    return false;
  }
  if (st.st_size <= 0) {
    mm_log_error("MOUNT", "source has invalid size %lld: %s",
                 (long long)st.st_size, candidate->source_path);
    return false;
  }

  if (!mm_ensure_dir_recursive(candidate->mount_path)) {
    mm_log_error("MOUNT", "failed to create %s: %s", candidate->mount_path,
                 strerror(errno));
    return false;
  }

  mm_notify_debug("Detected %s\n%s", candidate->title_id, candidate->file_name);
  unit_id = mm_lvd_attach(candidate->source_path, st.st_size,
                          &config->mount_profile, devname, sizeof(devname));
  if (unit_id < 0) {
    mm_log_error("MOUNT", "LVD attach failed for %s", candidate->source_path);
    return false;
  }

  if (mm_should_stop()) {
    mm_lvd_detach(unit_id);
    return false;
  }

  errmsg[0] = '\0';
  if (!mm_try_pfs_nmount(devname, candidate->mount_path,
                         &config->mount_profile, errmsg, sizeof(errmsg))) {
    mm_log_error("MOUNT",
                 "nmount failed source=%s target=%s errno=%d %s%s%s",
                 candidate->source_path, candidate->mount_path, errno,
                 strerror(errno), errmsg[0] != '\0' ? " errmsg=" : "",
                 errmsg[0] != '\0' ? errmsg : "");
    mm_lvd_detach(unit_id);
    return false;
  }

  mm_log_info(
      "MOUNT",
      "mounted title=%s source=%s target=%s profile={img=%u sec=%u sec2=%u "
      "raw=0x%X fstype=%s mkey=%s budget=%s ro=%d force=%d}",
      candidate->title_id, candidate->source_path, candidate->mount_path,
      (unsigned)config->mount_profile.lvd_image_type,
      config->mount_profile.lvd_sector_size,
      mm_effective_secondary_unit(&config->mount_profile),
      (unsigned)config->mount_profile.lvd_raw_flags,
      config->mount_profile.pfs_fstype, config->mount_profile.pfs_mkeymode,
      config->mount_profile.pfs_budgetid,
      config->mount_profile.pfs_read_only ? 1 : 0,
      config->mount_profile.pfs_force ? 1 : 0);
  mm_notify_debug("Mounted %s\n%s", candidate->title_id, candidate->mount_path);
  return true;
}

void mm_cleanup_managed_mounts(const mm_config_t *config,
                               const mm_candidate_list_t *candidates,
                               size_t *cleaned_out, size_t *errors_out) {
  mm_managed_mount_list_t list;
  size_t index;

  if (cleaned_out)
    *cleaned_out = 0;
  if (errors_out)
    *errors_out = 0;

  if (!mm_collect_managed_mounts(config, &list)) {
    if (mm_should_stop())
      return;
    if (errors_out)
      (*errors_out)++;
    return;
  }

  for (index = 0; index < list.count; ++index) {
    const mm_managed_mount_t *item = &list.items[index];
    bool desired = mm_candidate_mount_path_exists(candidates, item->path);
    bool should_cleanup =
        (!desired) || item->empty || (desired && item->mounted && item->empty);

    if (mm_should_stop())
      break;

    if (!should_cleanup)
      continue;

    mm_log_info("CLEANUP", "cleaning path=%s desired=%d mounted=%d empty=%d",
                item->path, desired ? 1 : 0, item->mounted ? 1 : 0,
                item->empty ? 1 : 0);
    if (mm_cleanup_mount_path(item)) {
      if (cleaned_out)
        (*cleaned_out)++;
    } else if (errors_out) {
      (*errors_out)++;
    }
  }

  mm_managed_mount_list_free(&list);
}

void mm_reconcile_mounts(const mm_config_t *config,
                         const mm_candidate_list_t *candidates,
                         size_t *mounted_out, size_t *skipped_out,
                         size_t *errors_out) {
  size_t index;

  if (mounted_out)
    *mounted_out = 0;
  if (skipped_out)
    *skipped_out = 0;

  for (index = 0; index < candidates->count; ++index) {
    const mm_image_candidate_t *candidate = &candidates->items[index];
    bool mounted = false;
    bool empty = true;
    struct stat st;

    if (mm_should_stop())
      break;

    (void)mm_lookup_mount_info(candidate->mount_path, &mounted, NULL);
    if (mounted) {
      if (mm_dir_is_empty(candidate->mount_path, &empty) && empty) {
        mm_managed_mount_t stale_mount;
        memset(&stale_mount, 0, sizeof(stale_mount));
        (void)mm_copy_string(stale_mount.path, sizeof(stale_mount.path),
                             candidate->mount_path);
        (void)mm_lookup_mount_info(stale_mount.path, &stale_mount.mounted,
                                   &stale_mount.lvd_unit_id);
        stale_mount.empty = true;
        if (!mm_cleanup_mount_path(&stale_mount) && errors_out)
          (*errors_out)++;
      } else {
        mm_log_info("MOUNT", "skip already-mounted source=%s target=%s",
                    candidate->source_path, candidate->mount_path);
        if (skipped_out)
          (*skipped_out)++;
        continue;
      }
    }

    if (lstat(candidate->mount_path, &st) == 0) {
      if (!S_ISDIR(st.st_mode)) {
        mm_log_error("MOUNT", "mount path exists but is not a directory: %s",
                     candidate->mount_path);
        if (errors_out)
          (*errors_out)++;
        continue;
      }
      if (!mm_dir_is_empty(candidate->mount_path, &empty))
        empty = false;
      if (!empty) {
        mm_log_warn("MOUNT", "mount path exists and is not empty, skipping %s",
                    candidate->mount_path);
        if (errors_out)
          (*errors_out)++;
        continue;
      }
    }

    if (mm_mount_candidate(config, candidate)) {
      if (mounted_out)
        (*mounted_out)++;
    } else if (!mm_should_stop() && errors_out) {
      (*errors_out)++;
    }
  }
}

size_t mm_count_managed_mounts(const mm_config_t *config) {
  struct statfs *mounts = NULL;
  int mount_count;
  int index;
  size_t total = 0;
  size_t target_len;

  target_len = strlen(config->target_directory);
  mount_count = getmntinfo(&mounts, MNT_NOWAIT);
  if (mount_count <= 0)
    return 0;

  for (index = 0; index < mount_count; ++index) {
    const char *target = mounts[index].f_mntonname;
    const char *suffix;

    if (strncmp(target, config->target_directory, target_len) != 0)
      continue;
    if (target[target_len] != '/')
      continue;

    suffix = target + target_len + 1;
    if (strncmp(suffix, MM_MANAGED_PREFIX, strlen(MM_MANAGED_PREFIX)) == 0)
      total++;
  }

  return total;
}
