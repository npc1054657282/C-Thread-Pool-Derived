#ifndef THREADPOOL_LOG_CONFIG_H
#define THREADPOOL_LOG_CONFIG_H

// MODIFY THIS FILE

#include "utils/log.h"

#define PP_THIRD_ARG(a, b, c, ...) c
#define VA_OPT_SUPPORTED_I(...) PP_THIRD_ARG(__VA_OPT__(,), true, false, )
#define VA_OPT_SUPPORTED VA_OPT_SUPPORTED_I(x)

#if VA_OPT_SUPPORTED
#define thpool_log_fatal(fmt, ...)  log_fatal(fmt __VA_OPT__(,) __VA_ARGS__)
#define thpool_log_error(fmt, ...)  log_error(fmt __VA_OPT__(,) __VA_ARGS__)
#define thpool_log_warn(fmt, ...)   log_warn(fmt __VA_OPT__(,) __VA_ARGS__)
#define thpool_log_info(fmt, ...)   log_info(fmt __VA_OPT__(,) __VA_ARGS__)
#define thpool_log_debug(fmt, ...)  log_debug(fmt __VA_OPT__(,) __VA_ARGS__)
#elif defined(__GNUC__) || defined(__clang__)
#define thpool_log_fatal(fmt, ...)  log_fatal(fmt, ##__VA_ARGS__)
#define thpool_log_error(fmt, ...)  log_error(fmt, ##__VA_ARGS__)
#define thpool_log_warn(fmt, ...)   log_warn(fmt, ##__VA_ARGS__)
#define thpool_log_info(fmt, ...)   log_info(fmt, ##__VA_ARGS__)
#define thpool_log_debug(fmt, ...)  log_debug(fmt, ##__VA_ARGS__)
#else
#define thpool_log_fatal log_fatal
#define thpool_log_error log_error
#define thpool_log_warn log_warn
#define thpool_log_info log_info
#define thpool_log_debug log_debug
#endif

#endif