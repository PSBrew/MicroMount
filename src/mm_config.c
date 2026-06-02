#include "mm_config.h"

#include "mm_log.h"
#include "mm_paths.h"
#include "mm_util.h"

static const char *const k_default_scan_paths[] =
    MM_DEFAULT_SCAN_PATHS_INITIALIZER;

extern unsigned char config_ini_example[];
extern unsigned int config_ini_example_len;

static bool mm_config_add_scan_path(mm_config_t *config, const char *path) {
  size_t index;

  if (!config || !path)
    return false;
  if (path[0] == '\0')
    return false;

  for (index = 0; index < config->scan_path_count; ++index) {
    if (strcmp(config->scan_paths[index], path) == 0)
      return true;
  }

  if (config->scan_path_count >= MM_MAX_SCAN_PATHS)
    return false;

  if (!mm_copy_string(config->scan_paths[config->scan_path_count],
                      sizeof(config->scan_paths[config->scan_path_count]),
                      path)) {
    return false;
  }

  mm_normalize_directory(config->scan_paths[config->scan_path_count]);
  config->scan_path_count++;
  return true;
}

static void mm_config_clear_scan_paths(mm_config_t *config) {
  if (!config)
    return;
  config->scan_path_count = 0;
}

static void mm_config_set_default_scan_paths(mm_config_t *config) {
  int index;

  mm_config_clear_scan_paths(config);
  for (index = 0; k_default_scan_paths[index] != NULL; ++index)
    (void)mm_config_add_scan_path(config, k_default_scan_paths[index]);
}

static void mm_config_apply_defaults(mm_config_t *config) {
  memset(config, 0, sizeof(*config));
  (void)mm_copy_string(config->target_directory,
                       sizeof(config->target_directory), "/mnt/shadowmnt");
  config->scan_depth = 1u;
  config->scan_interval_seconds = 15u;
  config->debug_enabled = true;

  config->mount_profile.lvd_image_type = 0u;
  config->mount_profile.lvd_sector_size = 65536u;
  config->mount_profile.lvd_secondary_unit = 65536u;
  config->mount_profile.lvd_raw_flags = 0x9u;
  (void)mm_copy_string(config->mount_profile.pfs_fstype,
                       sizeof(config->mount_profile.pfs_fstype), "pfs");
  (void)mm_copy_string(config->mount_profile.pfs_mkeymode,
                       sizeof(config->mount_profile.pfs_mkeymode), "AC");
  (void)mm_copy_string(config->mount_profile.pfs_budgetid,
                       sizeof(config->mount_profile.pfs_budgetid), "system");
  config->mount_profile.pfs_sigverify = false;
  config->mount_profile.pfs_playgo = false;
  config->mount_profile.pfs_disc = false;
  config->mount_profile.pfs_use_ekpfs = true;
  config->mount_profile.pfs_read_only = true;
  config->mount_profile.pfs_force = false;

  mm_config_set_default_scan_paths(config);
}

static void mm_config_parse_scan_paths_value(mm_config_t *config,
                                             const char *value) {
  char buffer[2048];
  char *token = NULL;
  char *cursor = NULL;

  if (!mm_copy_string(buffer, sizeof(buffer), value))
    return;

  for (token = strtok_r(buffer, ",;", &cursor); token != NULL;
       token = strtok_r(NULL, ",;", &cursor)) {
    char *trimmed = mm_trim(token);
    if (trimmed[0] != '\0' && !mm_config_add_scan_path(config, trimmed)) {
      mm_log_warn("CFG", "ignoring scan path entry: %s", trimmed);
    }
  }
}

static void mm_config_parse_key_value(mm_config_t *config, const char *key,
                                      const char *value,
                                      bool *custom_scan_paths_seen,
                                      int line_number) {
  uint32_t u32_value;
  uint16_t u16_value;
  bool bool_value;

  if (strcasecmp(key, "target_directory") == 0) {
    if (!mm_copy_string(config->target_directory,
                        sizeof(config->target_directory), value)) {
      mm_log_warn("CFG", "line %d target_directory too long", line_number);
      return;
    }
    mm_normalize_directory(config->target_directory);
    return;
  }

  if (strcasecmp(key, "scanpath") == 0 || strcasecmp(key, "scan_paths") == 0) {
    if (!*custom_scan_paths_seen) {
      mm_config_clear_scan_paths(config);
      *custom_scan_paths_seen = true;
    }
    if (strcasecmp(key, "scanpath") == 0) {
      if (!mm_config_add_scan_path(config, value))
        mm_log_warn("CFG", "line %d invalid scanpath: %s", line_number, value);
    } else {
      mm_config_parse_scan_paths_value(config, value);
    }
    return;
  }

  if (strcasecmp(key, "scan_depth") == 0) {
    if (mm_parse_u32_string(value, 0u, 32u, &u32_value))
      config->scan_depth = u32_value;
    else
      mm_log_warn("CFG", "line %d invalid scan_depth: %s", line_number, value);
    return;
  }

  if (strcasecmp(key, "scan_interval_seconds") == 0) {
    if (mm_parse_u32_string(value, 1u, 3600u, &u32_value))
      config->scan_interval_seconds = u32_value;
    else
      mm_log_warn("CFG", "line %d invalid scan_interval_seconds: %s",
                  line_number, value);
    return;
  }

  if (strcasecmp(key, "debug") == 0) {
    if (mm_parse_bool_string(value, &bool_value))
      config->debug_enabled = bool_value;
    else
      mm_log_warn("CFG", "line %d invalid debug value: %s", line_number, value);
    return;
  }

  if (strcasecmp(key, "lvd_image_type") == 0) {
    if (mm_parse_u16_string(value, &u16_value))
      config->mount_profile.lvd_image_type = u16_value;
    else
      mm_log_warn("CFG", "line %d invalid lvd_image_type: %s", line_number,
                  value);
    return;
  }

  if (strcasecmp(key, "lvd_sector_size") == 0) {
    if (mm_parse_u32_string(value, 512u, UINT32_MAX, &u32_value))
      config->mount_profile.lvd_sector_size = u32_value;
    else
      mm_log_warn("CFG", "line %d invalid lvd_sector_size: %s", line_number,
                  value);
    return;
  }

  if (strcasecmp(key, "lvd_secondary_unit") == 0) {
    if (mm_parse_u32_string(value, 0u, UINT32_MAX, &u32_value))
      config->mount_profile.lvd_secondary_unit = u32_value;
    else
      mm_log_warn("CFG", "line %d invalid lvd_secondary_unit: %s", line_number,
                  value);
    return;
  }

  if (strcasecmp(key, "lvd_raw_flags") == 0) {
    if (mm_parse_u16_string(value, &u16_value))
      config->mount_profile.lvd_raw_flags = u16_value;
    else
      mm_log_warn("CFG", "line %d invalid lvd_raw_flags: %s", line_number,
                  value);
    return;
  }

  if (strcasecmp(key, "pfs_fstype") == 0) {
    if (!mm_copy_string(config->mount_profile.pfs_fstype,
                        sizeof(config->mount_profile.pfs_fstype), value)) {
      mm_log_warn("CFG", "line %d pfs_fstype too long", line_number);
    }
    return;
  }

  if (strcasecmp(key, "pfs_mkeymode") == 0) {
    if (!mm_copy_string(config->mount_profile.pfs_mkeymode,
                        sizeof(config->mount_profile.pfs_mkeymode), value)) {
      mm_log_warn("CFG", "line %d pfs_mkeymode too long", line_number);
    }
    return;
  }

  if (strcasecmp(key, "pfs_budgetid") == 0) {
    if (!mm_copy_string(config->mount_profile.pfs_budgetid,
                        sizeof(config->mount_profile.pfs_budgetid), value)) {
      mm_log_warn("CFG", "line %d pfs_budgetid too long", line_number);
    }
    return;
  }

  if (strcasecmp(key, "pfs_sigverify") == 0) {
    if (mm_parse_bool_string(value, &bool_value))
      config->mount_profile.pfs_sigverify = bool_value;
    else
      mm_log_warn("CFG", "line %d invalid pfs_sigverify: %s", line_number,
                  value);
    return;
  }

  if (strcasecmp(key, "pfs_playgo") == 0) {
    if (mm_parse_bool_string(value, &bool_value))
      config->mount_profile.pfs_playgo = bool_value;
    else
      mm_log_warn("CFG", "line %d invalid pfs_playgo: %s", line_number, value);
    return;
  }

  if (strcasecmp(key, "pfs_disc") == 0) {
    if (mm_parse_bool_string(value, &bool_value))
      config->mount_profile.pfs_disc = bool_value;
    else
      mm_log_warn("CFG", "line %d invalid pfs_disc: %s", line_number, value);
    return;
  }

  if (strcasecmp(key, "pfs_use_ekpfs") == 0) {
    if (mm_parse_bool_string(value, &bool_value))
      config->mount_profile.pfs_use_ekpfs = bool_value;
    else
      mm_log_warn("CFG", "line %d invalid pfs_use_ekpfs: %s", line_number,
                  value);
    return;
  }

  if (strcasecmp(key, "pfs_read_only") == 0) {
    if (mm_parse_bool_string(value, &bool_value))
      config->mount_profile.pfs_read_only = bool_value;
    else
      mm_log_warn("CFG", "line %d invalid pfs_read_only: %s", line_number,
                  value);
    return;
  }

  if (strcasecmp(key, "pfs_force") == 0) {
    if (mm_parse_bool_string(value, &bool_value))
      config->mount_profile.pfs_force = bool_value;
    else
      mm_log_warn("CFG", "line %d invalid pfs_force: %s", line_number, value);
    return;
  }

  mm_log_debug("CFG", "line %d ignored unknown key: %s", line_number, key);
}

static bool mm_config_parse_file(mm_config_t *config) {
  FILE *fp;
  char line[2048];
  bool custom_scan_paths_seen = false;
  int line_number = 0;

  fp = fopen(MM_CONFIG_FILE, "r");
  if (!fp) {
    if (errno != ENOENT)
      mm_log_warn("CFG", "failed to open %s: %s", MM_CONFIG_FILE,
                  strerror(errno));
    return errno == ENOENT;
  }

  while (fgets(line, sizeof(line), fp) != NULL) {
    char *trimmed;
    char *separator;
    char *key;
    char *value;

    line_number++;
    trimmed = mm_trim(line);
    if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';')
      continue;

    separator = strchr(trimmed, '=');
    if (!separator) {
      mm_log_warn("CFG", "line %d ignored malformed entry", line_number);
      continue;
    }

    *separator = '\0';
    key = mm_trim(trimmed);
    value = mm_trim(separator + 1);
    mm_config_parse_key_value(config, key, value, &custom_scan_paths_seen,
                              line_number);
  }

  fclose(fp);

  if (custom_scan_paths_seen && config->scan_path_count == 0) {
    mm_log_warn("CFG", "no valid custom scan paths; using defaults");
    mm_config_set_default_scan_paths(config);
  }

  return true;
}

static bool mm_config_write_example_file(void) {
  FILE *fp;
  size_t written;

  fp = fopen(MM_CONFIG_FILE, "wb");
  if (!fp) {
    mm_log_warn("CFG", "failed to create %s: %s", MM_CONFIG_FILE,
                strerror(errno));
    return false;
  }

  written = fwrite(config_ini_example, 1, config_ini_example_len, fp);
  if (written != (size_t)config_ini_example_len) {
    mm_log_warn("CFG", "failed to write %s", MM_CONFIG_FILE);
    fclose(fp);
    (void)unlink(MM_CONFIG_FILE);
    return false;
  }

  if (fclose(fp) != 0) {
    mm_log_warn("CFG", "failed to finalize %s: %s", MM_CONFIG_FILE,
                strerror(errno));
    (void)unlink(MM_CONFIG_FILE);
    return false;
  }

  mm_log_info("CFG", "created default config from embedded template: %s",
              MM_CONFIG_FILE);
  return true;
}

static void mm_config_create_if_missing(void) {
  struct stat st;

  if (stat(MM_CONFIG_FILE, &st) == 0)
    return;

  if (errno != ENOENT) {
    mm_log_warn("CFG", "failed to stat %s: %s", MM_CONFIG_FILE,
                strerror(errno));
    return;
  }

  (void)mm_config_write_example_file();
}

void mm_config_init_defaults(mm_config_t *config) {
  if (!config)
    return;
  mm_config_apply_defaults(config);
}

bool mm_config_load(mm_config_t *config) {
  struct stat st;

  if (!config)
    return false;

  mm_config_apply_defaults(config);
  if (!mm_ensure_dir_recursive(MM_ROOT_DIR)) {
    mm_log_warn("CFG", "failed to create %s: %s", MM_ROOT_DIR, strerror(errno));
  }

  mm_config_create_if_missing();

  config->config_file_present = (stat(MM_CONFIG_FILE, &st) == 0);
  if (config->config_file_present)
    config->config_mtime = st.st_mtime;
  else
    config->config_mtime = 0;

  if (!mm_config_parse_file(config))
    return false;

  mm_normalize_directory(config->target_directory);
  if (!mm_ensure_dir_recursive(config->target_directory)) {
    mm_log_warn("CFG", "failed to create %s: %s", config->target_directory,
                strerror(errno));
  }
  mm_log_set_debug_enabled(config->debug_enabled);
  mm_log_info("CFG",
              "loaded config target=%s scan_paths=%zu scan_depth=%u "
              "scan_interval=%u debug=%d mount={img=%u sec=%u sec2=%u "
              "raw=0x%X fstype=%s mkey=%s budget=%s ro=%d force=%d}",
              config->target_directory, config->scan_path_count,
              config->scan_depth, config->scan_interval_seconds,
              config->debug_enabled ? 1 : 0,
              (unsigned)config->mount_profile.lvd_image_type,
              config->mount_profile.lvd_sector_size,
              config->mount_profile.lvd_secondary_unit,
              (unsigned)config->mount_profile.lvd_raw_flags,
              config->mount_profile.pfs_fstype,
              config->mount_profile.pfs_mkeymode,
              config->mount_profile.pfs_budgetid,
              config->mount_profile.pfs_read_only ? 1 : 0,
              config->mount_profile.pfs_force ? 1 : 0);
  return true;
}

bool mm_config_reload_if_changed(mm_config_t *config, bool *reloaded_out) {
  struct stat st;
  bool file_present;
  time_t mtime;

  if (!config)
    return false;

  if (reloaded_out)
    *reloaded_out = false;

  file_present = (stat(MM_CONFIG_FILE, &st) == 0);
  mtime = file_present ? st.st_mtime : 0;

  if (file_present == config->config_file_present && mtime == config->config_mtime)
    return true;

  mm_log_info("CFG", "detected config change, reloading %s", MM_CONFIG_FILE);
  if (!mm_config_load(config))
    return false;

  if (reloaded_out)
    *reloaded_out = true;
  return true;
}
