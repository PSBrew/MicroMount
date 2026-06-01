#ifndef MM_TYPES_H
#define MM_TYPES_H

#include "mm_platform.h"

#define MM_MAX_SCAN_PATHS 64

typedef struct {
  uint16_t lvd_image_type;
  uint32_t lvd_sector_size;
  uint32_t lvd_secondary_unit;
  uint16_t lvd_raw_flags;
  char pfs_fstype[32];
  char pfs_mkeymode[16];
  char pfs_budgetid[16];
  bool pfs_sigverify;
  bool pfs_playgo;
  bool pfs_disc;
  bool pfs_use_ekpfs;
  bool pfs_read_only;
  bool pfs_force;
} mm_mount_profile_t;

typedef struct {
  char target_directory[PATH_MAX];
  char scan_paths[MM_MAX_SCAN_PATHS][PATH_MAX];
  size_t scan_path_count;
  unsigned int scan_depth;
  unsigned int scan_interval_seconds;
  bool debug_enabled;
  bool config_file_present;
  time_t config_mtime;
  mm_mount_profile_t mount_profile;
} mm_config_t;

typedef struct {
  char source_path[PATH_MAX];
  char file_name[NAME_MAX];
  char title_id[16];
  char source_hash[9];
  char mount_name[NAME_MAX];
  char mount_path[PATH_MAX];
} mm_image_candidate_t;

typedef struct {
  mm_image_candidate_t *items;
  size_t count;
  size_t capacity;
} mm_candidate_list_t;

typedef struct {
  size_t discovered;
  size_t mounted;
  size_t skipped;
  size_t cleaned;
  size_t errors;
  size_t total_mounted;
} mm_cycle_summary_t;

#endif
