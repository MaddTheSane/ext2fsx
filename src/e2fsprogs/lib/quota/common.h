/*
 *
 *	Various things common for all utilities
 *
 */

#ifndef __QUOTA_COMMON_H__
#define __QUOTA_COMMON_H__

#ifndef __attribute__
# if !defined __GNUC__ || __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 8) || __STRICT_ANSI__
#  define __attribute__(x)
# endif
#endif

#define log_err(format, ...)	fprintf(stderr, \
				"[ERROR] %s:%d:%s:: " format "\n", \
				__FILE__, __LINE__, __func__, __VA_ARGS__)

#ifdef DEBUG_QUOTA
# define log_debug(format, ...)	fprintf(stderr, \
				"[DEBUG] %s:%d:%s:: " format "\n", \
				__FILE__, __LINE__, __func__, __VA_ARGS__)
#else
# define log_debug(format, ...)
#endif

#endif /* __QUOTA_COMMON_H__ */
