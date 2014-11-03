#ifndef _UTIL_H_
#define _UTIL_H_

#include <sys/signal.h>

#define UTIL_MIN(a,b) (a < b ? a : b)
#define UTIL_MAX(a,b) (a > b ? a : b)

struct util_thread
{
	void *(*ut_callback)(void *);
	void   *ut_arg;
};
typedef struct util_thread util_thread_t;

/*
 * Prototypes
 */

char * util_strdupenc(const char *src, const char *encaps);
int util_strmail(char *buffer, int size, const char *src);
struct sockaddr_storage* util_strtoaddr(const char *str);
char * util_addrtostr(struct sockaddr_storage *ss);
int util_addrtoint(struct sockaddr_storage *ss);
int util_file_exists(char *path);
int util_file(char *path, char **buffer);
struct sockaddr_storage * util_hostaddr(struct sockaddr_storage *ss);
int util_addrcmp(struct sockaddr_storage *ss1, struct sockaddr_storage *ss2);
int util_block_signals(int sig, ...);
int util_unblock_signals(int sig, ...);
int util_signal(int signum, void (*handler)(int));
int util_thread_create(pthread_t *thread, void *callback, void *arg);
void util_thread_join(pthread_t thread);
int util_now(struct timespec *ts);
int util_concat(char *buffer, int size, ...);
void util_setgid(char *name);
void util_setuid(char *name);
void util_daemonize(void);
void util_pidfile(char *path);
int util_chmod(char *path, int mode_decimal);
int util_dirname(char *buffer, int size, char *path);
void util_tolower(char *p);
void util_test(void);
#endif /* _UTIL_H_ */
