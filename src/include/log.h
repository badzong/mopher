#ifndef _LOG_H_
#define _LOG_H_

#include <syslog.h>
#include <sysexits.h>

#include <mopher.h>

/*
 * Prototypes
 */

void log_init(char *name, int level, int syslog, int foreground);
void log_close(void);
void log_logv(int type, char *f, va_list ap);
void log_log(int type, char *f, ...);
void log_die(int r, char *f, ...);
void log_message(int type, var_t *mailspec, char *f, ...);

#define log_debug(...) log_log(LOG_DEBUG, __VA_ARGS__)
#define log_info(...) log_log(LOG_INFO, __VA_ARGS__)
#define log_notice(...) log_log(LOG_NOTICE, __VA_ARGS__)
#define log_error(...) log_log(LOG_ERR, __VA_ARGS__)
#define log_warning(...) log_log(LOG_WARNING, __VA_ARGS__)

#endif /* _LOG_H_ */
