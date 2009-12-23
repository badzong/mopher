#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/time.h>
#include <pwd.h>
#include <grp.h>

#include "log.h"

#define ADDR6_LEN 16
#define ADDR6_STRLEN 40
#define BUFLEN 1024

char *
util_strdupenc(const char *src, const char *encaps)
{
	char *dup;
	int len;

	len = strlen(src);

	/*
	 * No encapsulation found.
	 */
	if (!(src[0] == encaps[0] && src[len - 1] == encaps[1])) {
		return strdup(src);
	}

	if ((dup = strdup(src + 1)) == NULL) {
		log_warning("util_strdupenc: strdup");
		return NULL;
	}

	dup[len - 2] = 0;

	return dup;
}


int
util_strmail(char *buffer, int size, const char *src)
{
	char *start, *end;
	int len;

	start = strchr(src, '<');
	if (start == NULL)
	{
		return -1;
	}

	end = strchr(start, '>');
	if (end == NULL)
	{
		return -1;
	}

	/*
	 * end - start - '<' - '>' + '\0'
	 */ 
	len = end - start - 1;

	if (len >= size)
	{
		return -1;
	}

	strncpy(buffer, start + 1, len);

	buffer[len] = 0;

	return len; 
}


struct sockaddr_storage*
util_strtoaddr(const char *str)
{
	struct sockaddr_storage *ss;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;

	if((ss = (struct sockaddr_storage *)
		malloc(sizeof(struct sockaddr_storage))) == NULL) {
		return NULL;
	}

	sin = (struct sockaddr_in *) ss;
	sin6 = (struct sockaddr_in6 *) ss;

	if (inet_pton(AF_INET, str, &sin->sin_addr) == 1) {
		ss->ss_family = AF_INET;
	}
	else if (inet_pton(AF_INET6, str, &sin6->sin6_addr) == 1) {
		ss->ss_family = AF_INET6;
	}
	else {
		return NULL;
	}

	return ss;
}

char *
util_addrtostr(struct sockaddr_storage *ss)
{
	char addr[ADDR6_STRLEN], *paddr;
	struct sockaddr_in *sin = (struct sockaddr_in *) ss;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) ss;
	const char *p;

	/*
	if((ss = (struct sockaddr_storage *)
		malloc(sizeof(struct sockaddr_storage))) == NULL) {
		return NULL;
	}
	*/

	sin = (struct sockaddr_in *) ss;
	sin6 = (struct sockaddr_in6 *) ss;

	if (ss->ss_family == AF_INET6) {
		p = inet_ntop(AF_INET6, &sin6->sin6_addr, addr, sizeof(addr));
	}
	else if (ss->ss_family == AF_INET) {
		p = inet_ntop(AF_INET, &sin->sin_addr, addr, sizeof(addr));
	}
	else {
		log_error("util_addrtostr: bad address family");
		return NULL;
	}

	if (p == NULL) {
		log_error("util_addrtostr: inet_ntop");
		return NULL;
	}

	paddr = strdup(addr);
	if (paddr == NULL) {
		log_error("util_addrtostr: strdup");
	}

	return paddr;
}


int
util_file_exists(char *path)
{
	struct stat fs;

	if (stat(path, &fs) == 0) {
		return 1;
	}

	if(errno == ENOENT) {
		log_notice("util_file_exists: stat \"%s\"", path);
		return 0;
	}

	log_warning("util_file_exists: stat");

	return -1;
}

int
util_file(char *path, char **buffer)
{
	struct stat fs;
	int fd, n;

	if (stat(path, &fs) == -1) {
		log_warning("util_file: stat '%s'", path);
		return -1;
	}

	if (fs.st_size == 0) {
		log_notice("util_file: '%s' is empty", path);
		return 0;
	}

	if((*buffer = (char *) malloc(fs.st_size)) == NULL) {
		log_warning("util_file: malloc");
		return -1;

	}

	if ((fd = open(path, O_RDONLY)) == -1) {
		log_warning("util_file: open '%s'", path);
		return -1;
	}

	if ((n = read(fd, *buffer, fs.st_size)) == -1) {
		log_warning("util_file: read '%s'", path);
		return -1;
	}

	close(fd);

	return n;
}

struct sockaddr_storage *
util_hostaddr(struct sockaddr_storage *ss)
{
	struct sockaddr_storage *cleancopy;
	struct sockaddr_in *sin, *sin_copy;
	struct sockaddr_in6 *sin6, *sin6_copy;

	cleancopy = (struct sockaddr_storage *)
		malloc(sizeof(struct sockaddr_storage));

	if (cleancopy == NULL) {
		log_warning("util_hostaddr: malloc");
		return NULL;
	}

	memset(cleancopy, 0, sizeof(struct sockaddr_storage));

	cleancopy->ss_family = ss->ss_family;

	if (ss->ss_family == AF_INET) {
		sin = (struct sockaddr_in *) ss;
		sin_copy = (struct sockaddr_in *) cleancopy;

		sin_copy->sin_addr = sin->sin_addr;

		return cleancopy;
	}

	sin6 = (struct sockaddr_in6 *) ss;
	sin6_copy = (struct sockaddr_in6 *) cleancopy;

	memcpy(&sin6_copy->sin6_addr, &sin6->sin6_addr,
		sizeof(sin6->sin6_addr));

	return cleancopy;
}

int
util_addrcmp(struct sockaddr_storage *ss1, struct sockaddr_storage *ss2)
{
	unsigned long inaddr1, inaddr2;

	if (ss1->ss_family < ss2->ss_family) {
		return -1;
	}

	if (ss1->ss_family > ss2->ss_family) {
		return 1;
	}

	switch (ss1->ss_family) {

	case AF_INET:
		inaddr1 = ntohl(((struct sockaddr_in *) ss1)->sin_addr.s_addr);
		inaddr2 = ntohl(((struct sockaddr_in *) ss2)->sin_addr.s_addr);

		if (inaddr1 < inaddr2) {
			return -1;
		}

		if (inaddr1 > inaddr2) {
			return 1;
		}

		return 0;

	case AF_INET6:
		/*
		 * XXX: Simple implementation.
		 */
		return memcmp(&((struct sockaddr_in6 *) ss1)->sin6_addr.s6_addr,
			      &((struct sockaddr_in6 *) ss2)->sin6_addr.s6_addr,
			      ADDR6_LEN);

	default:
		log_warning("util_addrcmp: bad address family");
	}

	return 0;
}


int
util_block_signals(int sig, ...)
{
	sigset_t	sigset;
	int		signal;
	va_list		ap;

	if (sigemptyset(&sigset))
	{
		log_error("util_block_signals: sigemptyset");
		return -1;
	}

	va_start(ap, sig);

	while ((signal = va_arg(ap, int)))
	{
		if (sigaddset(&sigset, signal))
		{
			log_error("util_block_signals: sigaddset");
			return -1;
		}
	}
	
	if (pthread_sigmask(SIG_BLOCK, &sigset, NULL))
	{
		log_error("util_block_signals: pthread_sigmask");
		return -1;
	}

	va_end(ap);

	return 0;
}

int
util_unblock_signals(int sig, ...)
{
	sigset_t	sigset;
	int		signal;
	va_list		ap;

	if (sigemptyset(&sigset))
	{
		log_error("util_unblock_signals: sigemptyset");
		return -1;
	}

	va_start(ap, sig);

	while ((signal = va_arg(ap, int)))
	{
		if (sigaddset(&sigset, signal))
		{
			log_error("util_unblock_signals: sigaddset");
			return -1;
		}
	}
	
	if (pthread_sigmask(SIG_UNBLOCK, &sigset, NULL))
	{
		log_error("util_unblock_signals: pthread_sigmask");
		return -1;
	}

	va_end(ap);

	return 0;
}

int
util_signal(int signum, void (*handler)(int))
{
	struct sigaction new, old;

	new.sa_handler = handler;
	sigemptyset(&new.sa_mask);
	new.sa_flags = 0;

	if(sigaction(signum, NULL, &old) == -1)
	{
		log_error("util_signal: sigaction");
		return -1;
	}

	/*
	 * Check if a signal handler was already installed
	 */
	if (new.sa_handler != SIG_DFL &&
	    new.sa_handler != SIG_IGN &&
	    old.sa_handler != SIG_DFL &&
	    old.sa_handler != SIG_IGN)
	{
		log_error("util_signal: handler for signal %d already "
		    "installed", signum);
	}

	if(sigaction(signum, &new, NULL) == -1)
	{
		log_error("util_signal: sigaction");
		return -1;
	}

	return 0;
}


static void *
util_thread_init(void *arg)
{
	void *(*callback)(void *) = arg;

	if (util_block_signals(SIGHUP, SIGTERM, SIGINT, 0))
	{
		log_die(EX_SOFTWARE,
		    "util_thread_init: util_block_signal failed");
	}

	return callback(NULL);
}


int
util_thread_create(pthread_t *thread, void *callback)
{
	pthread_attr_t attr;

	if (pthread_attr_init(&attr))
	{
		log_error("util_thread_create: pthread_attr_init");
		return -1;
	}

	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE))
	{
		log_error("util_thread_create: pthread_attr_setdetachstate");
		return -1;
	}

	if (pthread_create(thread, NULL, util_thread_init, callback))
	{
		log_error("util_thread_create: pthread_create");
		return -1;
	}

	if (pthread_attr_destroy(&attr))
	{
		log_error("util_thread_create: pthread_attr_destroy");
		return -1;
	}

	return 0;
}


void
util_thread_join(pthread_t thread)
{
	static char *edeadlk = "Deadlock";
	static char *einval  = "Thread not joinable";
	static char *esrch   = "No such thread";
	static char *enknwn  = "Unknown error";

	char *e = NULL;
	int r;

	r = pthread_join(thread, NULL);
	switch (r)
	{
	case 0:				break;
	case EDEADLK:	e = edeadlk;	break;
	case EINVAL:	e = einval;	break;
	case ESRCH:	e = esrch;	break;
	default:	e = enknwn;
	}

	if (e)
	{
		log_error("dbt_clear: pthread_mutex_join: %s", e);
	}

	return;
}


int
util_now(struct timespec *ts)
{
	struct timeval	tv;

	/*
	 * Get current time
	 */
	if (gettimeofday(&tv, NULL))
	{
		log_error("util_now: gettimeofday");
		return -1;
	}

	/*
	 * Convert into timespec
	 */
	ts->tv_sec = tv.tv_sec;
	ts->tv_nsec = tv.tv_usec * 1000;

	return 0;
}


int
util_concat(char *buffer, int size, ...)
{
	va_list ap;
	char *part;
	int len, n;

	va_start(ap, size);

	for (len = 0;; len += n)
	{
		part = va_arg(ap, char *);
		if (part == NULL)
		{
			break;
		}

		n = strlen(part);

		if (len + n > size - 1)
		{
			return -1;
		}

		strcpy(buffer + len, part);
	}

	va_end(ap);

	return len;
}


void
util_setgid(char *name)
{
	struct group *gr;

	/*
	 * util_setgid is not thread safe. It's used only once at startup by
	 * the initial program thread.
	 */
	gr = getgrnam(name);
	if (gr == NULL)
	{
		log_die(EX_SOFTWARE, "util_setgid: getgrnam");
	}

	if (setgid(gr->gr_gid))
	{
		log_die(EX_OSERR, "util_setgid: setgid");
	}

	return;
}


void
util_setuid(char *name)
{
	struct passwd *pw;

	/*
	 * util_setuid is not thread safe. It's used only once at startup by
	 * the initial program thread.
	 */
	pw = getpwnam(name);
	if (pw == NULL)
	{
		log_die(EX_SOFTWARE, "util_setuid: getpwnam");
	}

	if (setuid(pw->pw_uid))
	{
		log_die(EX_OSERR, "util_setuid: setuid");
	}

	return;
}
