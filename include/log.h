#ifndef _LOG_H_
#define _LOG_H_

#include <syslog.h>
#include <sysexits.h>

void log_init(char *name);
void log_close(void);
void log_log(int type, char *f, ...);
void log_die(int r, char *f, ...);

#define log_debug(...) log_log(LOG_DEBUG, __VA_ARGS__)
#define log_info(...) log_log(LOG_INFO, __VA_ARGS__)
#define log_notice(...) log_log(LOG_NOTICE, __VA_ARGS__)
#define log_error(...) log_log(LOG_ERR, __VA_ARGS__)
#define log_warning(...) log_log(LOG_WARNING, __VA_ARGS__)

#endif /* _LOG_H_ */
