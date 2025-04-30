// MODIFY THIS FILE

#include "utils/log.h"

#define thpool_log_error(fmt, ...)  log_error(fmt __VA_OPT__(,) __VA_ARGS__)
#define thpool_log_warn(fmt, ...)   log_warn(fmt __VA_OPT__(,) __VA_ARGS__)
#define thpool_log_info(fmt, ...)   log_info(fmt __VA_OPT__(,) __VA_ARGS__)
#define thpool_log_debug(fmt, ...)  log_debug(fmt __VA_OPT__(,) __VA_ARGS__)