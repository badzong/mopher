#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
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
#include <ctype.h>

#include <log.h>

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
		log_sys_warning("util_strdupenc: strdup");
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

	if (src == NULL)
	{
		return -1;
	}

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

	/*
	 * Check for <>
	 */
	if (len > 0)
	{
		strncpy(buffer, start + 1, len);
	}

	buffer[len] = 0;

	return len; 
}


struct sockaddr_storage*
util_strtoaddr(const char *str)
{
	struct sockaddr_storage *ss;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;

	if (str == NULL)
	{
		return NULL;
	}

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
	char addr[ADDR6_STRLEN];
	char *paddr;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	const char *p;

	if (ss == NULL)
	{
		// CAVEAT: Need to strdup here. String is freed later.
		return strdup("(null)");
	}

	sin = (struct sockaddr_in *) ss;
	sin6 = (struct sockaddr_in6 *) ss;

	switch(ss->ss_family)
	{
	case AF_INET6:
		p = inet_ntop(AF_INET6, &sin6->sin6_addr, addr, sizeof(addr));
		break;

	case AF_INET:
		p = inet_ntop(AF_INET, &sin->sin_addr, addr, sizeof(addr));
		break;

	case AF_UNIX:
		p = "unix domain socket";
		break;

	default:
		log_error("util_addrtostr: bad address family: %d",
			ss->ss_family);
		return NULL;
	}

	if (p == NULL) {
		log_error("util_addrtostr: inet_ntop");
		return NULL;
	}

	paddr = strdup(p);
	if (paddr == NULL) {
		log_sys_error("util_addrtostr: strdup");
	}

	return paddr;
}

int
util_addrtoint(struct sockaddr_storage *ss)
{
	struct sockaddr_in *sin;

	if (ss == NULL)
	{
		return -1;
	}

	sin = (struct sockaddr_in *) ss;

	/*
	 * FIXME: return -1 as address
	 */
	if (ss->ss_family != AF_INET) {
		return -1;
	}

	return ntohl(sin->sin_addr.s_addr);
}


int
util_file_exists(char *path)
{
	struct stat fs;

	if (path == NULL)
	{
		return -1;
	}

	if (stat(path, &fs) == 0) {
		return 1;
	}

	if(errno == ENOENT) {
		log_sys_notice("util_file_exists: stat \"%s\"", path);
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
		log_sys_warning("util_file: stat '%s'", path);
		return -1;
	}

	if (fs.st_size == 0) {
		log_notice("util_file: '%s' is empty", path);
		return 0;
	}

	if((*buffer = (char *) malloc(fs.st_size + 1)) == NULL) {
		log_sys_warning("util_file: malloc");
		return -1;

	}

	if ((fd = open(path, O_RDONLY)) == -1) {
		log_sys_warning("util_file: open '%s'", path);
		return -1;
	}

	if ((n = read(fd, *buffer, fs.st_size)) == -1) {
		log_sys_warning("util_file: read '%s'", path);
		return -1;
	}

	close(fd);

	// Terminate file
	(*buffer)[n] = 0;

	return n;
}

struct sockaddr_storage *
util_hostaddr(struct sockaddr_storage *ss)
{
	struct sockaddr_storage *cleancopy;
	struct sockaddr_in *sin, *sin_copy;
	struct sockaddr_in6 *sin6, *sin6_copy;

	if (ss == NULL)
	{
		log_info("util_hostaddr: supplied sockaddr_storage is NULL");
		return NULL;
	}

	cleancopy = (struct sockaddr_storage *)
		malloc(sizeof(struct sockaddr_storage));

	if (cleancopy == NULL)
	{
		log_sys_warning("util_hostaddr: malloc");
		return NULL;
	}

	memset(cleancopy, 0, sizeof(struct sockaddr_storage));

	cleancopy->ss_family = ss->ss_family;

	if (ss->ss_family == AF_INET)
	{
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
	int cmp;

	if (ss1 == NULL && ss2 == NULL)
	{
		return 0;
	}

	if (ss1 == NULL)
	{
		return -1;
	}

	if (ss2 == NULL)
	{
		return 1;
	}

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
		cmp = memcmp(&((struct sockaddr_in6 *) ss1)->sin6_addr.s6_addr,
			      &((struct sockaddr_in6 *) ss2)->sin6_addr.s6_addr,
			      ADDR6_LEN);

		if (cmp < 0)
		{
			return -1;
		}
		if (cmp > 0)
		{
			return 1;
		}
		return 0;

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
		log_sys_error("util_block_signals: sigemptyset");
		return -1;
	}

	va_start(ap, sig);

	while ((signal = va_arg(ap, int)))
	{
		if (sigaddset(&sigset, signal))
		{
			log_sys_error("util_block_signals: sigaddset");
			return -1;
		}
	}
	
	if (pthread_sigmask(SIG_BLOCK, &sigset, NULL))
	{
		log_sys_error("util_block_signals: pthread_sigmask");
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
		log_sys_error("util_unblock_signals: sigemptyset");
		return -1;
	}

	va_start(ap, sig);

	while ((signal = va_arg(ap, int)))
	{
		if (sigaddset(&sigset, signal))
		{
			log_sys_error("util_unblock_signals: sigaddset");
			return -1;
		}
	}
	
	if (pthread_sigmask(SIG_UNBLOCK, &sigset, NULL))
	{
		log_sys_error("util_unblock_signals: pthread_sigmask");
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
		log_sys_error("util_signal: sigaction");
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
		log_sys_error("util_signal: sigaction");
		return -1;
	}

	return 0;
}


static void *
util_thread_init(void *p)
{
	util_thread_t *ut = p;
	void *(*callback)(void *) = ut->ut_callback;
	void *arg = ut->ut_arg;

	free(ut);

	if (util_block_signals(SIGHUP, SIGTERM, SIGINT, 0))
	{
		log_die(EX_SOFTWARE,
		    "util_thread_init: util_block_signal failed");
	}

	return callback(arg);
}


int
util_thread_create(pthread_t *thread, void *callback, void *arg)
{
	pthread_attr_t attr;
	util_thread_t *ut;

	ut = (util_thread_t *) malloc(sizeof (util_thread_t));
	if (ut == NULL)
	{
		log_sys_error("util_thread_create: malloc");
		return -1;
	}
	
	ut->ut_callback = callback;
	ut->ut_arg = arg;

	if (pthread_attr_init(&attr))
	{
		log_sys_error("util_thread_create: pthread_attr_init");
		return -1;
	}

	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE))
	{
		log_sys_error("util_thread_create: pthread_attr_setdetachstate");
		return -1;
	}

	if (pthread_create(thread, NULL, util_thread_init, ut))
	{
		log_sys_error("util_thread_create: pthread_create");
		return -1;
	}

	if (pthread_attr_destroy(&attr))
	{
		log_sys_error("util_thread_create: pthread_attr_destroy");
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
		log_sys_error("dbt_clear: pthread_mutex_join: %s", e);
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
		log_sys_error("util_now: gettimeofday");
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
		log_die(EX_SOFTWARE, "unknown group: %s", name);
	}

	if (setgid(gr->gr_gid))
	{
		log_sys_die(EX_OSERR, "util_setgid: setgid");
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
		log_die(EX_SOFTWARE, "unknown user: %s", name);
	}

	if (setuid(pw->pw_uid))
	{
		log_sys_die(EX_OSERR, "util_setuid: setuid");
	}

	return;
}


void
util_daemonize(void)
{
	pid_t pid;
	int r;

	/*
	 * Fork into background.
	 */
	pid = fork();
	if(pid == -1)
	{
		/*
		 * log is probably not initialized yet.
		 */
		perror("util_daemonize: fork");
		_exit(EX_OSERR);
	}

	if(pid)
	{
		_exit(EX_OK);
	}

	if(setsid() == -1)
	{
		perror("util_daemonize: setsid");
		_exit(EX_OSERR);
	}

	/*
	 * Redirect stdin, stdout, stderr to /dev/null
	 */
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	r  = (open("/dev/null", O_RDONLY) == -1);
	r |= (open("/dev/null", O_WRONLY) == -1);
	r |= (open("/dev/null", O_WRONLY) == -1);

	if (r)
	{
		perror("util_daemonize: open");
		_exit(EX_OSERR);
	}

	return;
}


void
util_pidfile(char *path)
{
	pid_t pid;
	char string[6];
	int fd, len;

	if (util_file_exists(path))
	{
		log_die(EX_SOFTWARE, "util_pidfile: %s exists", path);
	}

	pid = getpid();

	len = snprintf(string, sizeof string, "%hu", pid);

	fd = open(path, O_WRONLY | O_CREAT | O_EXCL,
	    S_IRUSR | S_IRGRP | S_IROTH);
	if (fd == -1)
	{
		log_sys_die(EX_SOFTWARE, "util_pidfile: open");
		return;
	}

	if (write(fd, string, len) == -1)
	{
		log_sys_die(EX_SOFTWARE, "util_pidfile: write");
	}

	close(fd);

	return;
}


int
util_chmod(char *path, int mode_decimal)
{
	char user;
	char group;
	char others;
	mode_t mode = 0;

	user   = (mode_decimal / 100) & 7;
	group  = ((mode_decimal / 10) % 10) & 7;
	others = (mode_decimal % 10) & 7;

	/*
	 * FIXME: Is this translation neccessary?
	 */
	if (user   & 1<<2)	mode |= S_IRUSR;
	if (user   & 1<<1)	mode |= S_IWUSR;
	if (user   & 1<<0)	mode |= S_IXUSR;
	if (group  & 1<<2)	mode |= S_IRGRP;
	if (group  & 1<<1)	mode |= S_IWGRP;
	if (group  & 1<<0)	mode |= S_IXGRP;
	if (others & 1<<2)	mode |= S_IROTH;
	if (others & 1<<1)	mode |= S_IWOTH;
	if (others & 1<<0)	mode |= S_IXOTH;

	if (chmod(path, mode))
	{
		log_sys_error("util_chmod: chmod: %s", path);
		return -1;
	}

	return 0;
}

int
util_dirname(char *buffer, int size, char *path)
{
	struct stat s;
	char *p;

	if (size < strlen(path) + 1)
	{
		log_error("util_dirname: buffer exhausted");
		return -1;
	}

	strcpy(buffer, path);

	if (stat(path, &s))
	{
		log_sys_error("util_dirname: stat");
		return -1;
	}

	/*
	 * Path already is a directory
	 */
	if (s.st_mode & S_IFDIR)
	{
		return 0;
	}

	/*
	 * Missing slash
	 */
	p = strrchr(buffer, '/');
	if (p == NULL)
	{
		return -1;
	}

	*p = 0;

	return 0;
}

void
util_tolower(char *p)
{
	for (; *p; ++p)
	{
		// Convert to lowercase
		if (isupper(*p))
		{
			*p = tolower(*p);
		}
	}

	return;
}


#ifdef DEBUG

void
util_test(int n)
{
	char *p;
	char buffer[BUFLEN];
	struct sockaddr_storage *ss;
	unsigned long ul;

	/*
	 * util_strdupenc
	 */
	p = util_strdupenc("<test>", "<>");
	TEST_ASSERT(p != NULL, "util_strdupenc failed");
	TEST_ASSERT(strcmp(p, "test") == 0, "util_strdupenc returned wrong value");
	free(p);

	p = util_strdupenc("-test-", "--");
	TEST_ASSERT(p != NULL, "util_strdupenc failed");
	TEST_ASSERT(strcmp(p, "test") == 0, "util_strdupenc returned wrong value");
	free(p);

	p = util_strdupenc("test", "<>");
	TEST_ASSERT(p != NULL, "util_strdupenc failed");
	TEST_ASSERT(strcmp(p, "test") == 0, "util_strdupenc returned wrong value");
	free(p);

	/*
	 * util_strmail
	 */
	TEST_ASSERT(util_strmail(buffer, sizeof buffer, "Test <test@test.com>") > 0, "util_strmail failed");
	TEST_ASSERT(strcmp(buffer, "test@test.com") == 0, "util_strmail returnd wrong result: %s", buffer);
	TEST_ASSERT(util_strmail(buffer, sizeof buffer, "Test >test@test.com<") == -1, "util_strmail should fail here");
	TEST_ASSERT(util_strmail(buffer, sizeof buffer, NULL) == -1, "util_strmail should fail here");

	/*
         * util_strtoaddr, util_addrtostr
	 */
	ss = util_strtoaddr("127.0.0.1");
	TEST_ASSERT(ss != NULL, "util_strtoaddr failed");

	p = util_addrtostr(ss);
	TEST_ASSERT(p != NULL, "util_addrtostr failed");
	TEST_ASSERT(strcmp(p, "127.0.0.1") == 0, "util_addrtostr or util_strtoaddr failed");
	free(p);
	free(ss);

	ss = util_strtoaddr("1234::5678");
	TEST_ASSERT(ss != NULL, "util_strtoaddr failed");

	p = util_addrtostr(ss);
	TEST_ASSERT(p != NULL, "util_addrtostr failed");
	TEST_ASSERT(strcmp(p, "1234::5678") == 0, "util_addrtostr or util_strtoaddr failed");
	free(p);
	free(ss);

	TEST_ASSERT(util_strtoaddr(NULL) == NULL, "util_strtoaddr should return NULL");

	/*
	 * util_addrtoint
	 */
	ss = util_strtoaddr("1.1.1.1");
	TEST_ASSERT(ss != NULL, "util_strtoaddr failed");
	ul = util_addrtoint(ss);
	TEST_ASSERT(ul == 0x01010101, "util_addrtoint failed. Expected %lx got %lx", 0x01010101, ul);
	free(ss);

	TEST_ASSERT(util_addrtoint(NULL) == -1, "util_addrtoint should fail here");

	/*
	 * util_file_exists
	 */
	TEST_ASSERT(util_file_exists("/") == 1, "Root should exist");
	TEST_ASSERT(util_file_exists("/nonexistent") == 0, "/nonexistent should not exist");
	TEST_ASSERT(util_file_exists(NULL) == -1, "This should fail");
}

#endif
