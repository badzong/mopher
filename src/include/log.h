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
void log_logv(int type, int syserr, char *f, va_list ap);
void log_log(int type, int syserr, char *f, ...);
void log_exit(int r, int syserr, char *f, ...);
void log_message(int type, var_t *mailspec, char *f, ...);

#define log_debug(...) log_log(LOG_DEBUG, 0, __VA_ARGS__)
#define log_info(...) log_log(LOG_INFO, 0, __VA_ARGS__)
#define log_notice(...) log_log(LOG_NOTICE, 0, __VA_ARGS__)
#define log_error(...) log_log(LOG_ERR, 0, __VA_ARGS__)
#define log_warning(...) log_log(LOG_WARNING, 0, __VA_ARGS__)
#define log_crit(...) log_log(LOG_CRIT, 0, __VA_ARGS__)
#define log_die(RETURN, ...) log_exit(RETURN, 0, __VA_ARGS__)

#define log_sys_debug(...) log_log(LOG_DEBUG, 1, __VA_ARGS__)
#define log_sys_info(...) log_log(LOG_INFO, 1, __VA_ARGS__)
#define log_sys_notice(...) log_log(LOG_NOTICE, 1, __VA_ARGS__)
#define log_sys_error(...) log_log(LOG_ERR, 1, __VA_ARGS__)
#define log_sys_warning(...) log_log(LOG_WARNING, 1, __VA_ARGS__)
#define log_sys_die(RETURN, ...) log_exit(RETURN, 1, __VA_ARGS__)

#endif /* _LOG_H_ */
