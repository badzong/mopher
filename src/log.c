#include "config.h"

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>

#include "cf.h"

#define BUFLEN 1024

static int log_level;

void
log_init(char *name, int level, int foreground)
{
	int option;

	log_level = level;

	option = foreground ? LOG_PID | LOG_PERROR : LOG_PID;

	openlog(name, option, LOG_MAIL);

	return;
}

void
log_close(void)
{
	closelog();

	return;
}

static void
log_assemble(char *buffer, int32_t buflen, char *f, va_list ap)
{
	vsnprintf(buffer, buflen, f, ap);

	if (errno && errno != EINTR) {
		strncat(buffer + strlen(buffer), ": ", buflen - strlen(buffer));
		strncat(buffer + strlen(buffer), strerror(errno),
			buflen - strlen(buffer));
	}

	errno = 0;

	return;
}

void
log_log(int type, char *f, ...)
{
	char buffer[BUFLEN];
	va_list ap;

	if (type > log_level) {
		return;
	}

	va_start(ap, f);
	log_assemble(buffer, sizeof(buffer), f, ap);
	va_end(ap);

	syslog(type, buffer);

	return;
}

void
log_die(int r, char *f, ...)
{
	char buffer[BUFLEN];
	va_list ap;

	va_start(ap, f);
	log_assemble(buffer, BUFLEN, f, ap);
	va_end(ap);

	syslog(LOG_ERR, buffer);
	closelog();

	exit(r);

	return;
}
