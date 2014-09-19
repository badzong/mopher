#include <config.h>
#include <test.h>
#include <mopher.h>
#include <string.h>

#define BUFLEN 1024

int test_tests;

void
test_assert(char *file, int line, int cond, char *m, ...)
{
	va_list ap;
	char f[BUFLEN];

	++test_tests;
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
