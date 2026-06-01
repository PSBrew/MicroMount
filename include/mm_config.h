#ifndef MM_CONFIG_H
#define MM_CONFIG_H

#include "mm_types.h"

void mm_config_init_defaults(mm_config_t *config);
bool mm_config_load(mm_config_t *config);
bool mm_config_reload_if_changed(mm_config_t *config, bool *reloaded_out);

#endif
