#include <string.h>
#include <pthread.h>
#include <config.h>
#include <mopher.h>

#define BUFLEN 1024

static pthread_mutex_t test_mutex = PTHREAD_MUTEX_INITIALIZER;
int test_tests;

static void
test_count(void)
{
	if (pthread_mutex_lock(&test_mutex))
	{
		log_die(EX_SOFTWARE, "test_count: pthread_mutex_lock");
	}

	++test_tests;

	if (pthread_mutex_unlock(&test_mutex))
	{
		log_die(EX_SOFTWARE, "test_count: pthread_mutex_unlock");
	}
}

void
test_assert(char *file, int line, int cond, char *m, ...)
{
	va_list ap;
	char f[BUFLEN];

	test_count();

	if (cond)
	{
		return;
	}

	if (snprintf(f, sizeof f, "%s:%d *** %s", file, line, m) >= sizeof f)
	{
		log_die(EX_SOFTWARE, "test_assert: buffer exhausted");
	}

	va_start(ap, m);
	log_logv(LOG_ERR, 0, f, ap);
	va_end(ap);

	log_die(EX_SOFTWARE, "\nTEST FAILED!");

	return;
}
