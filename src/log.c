#include <config.h>

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>

#include <mopher.h>

#define BUFLEN 1024

static int log_level;
static int log_syslog;

void
log_init(char *name, int level, int syslog, int foreground)
{
	int option;

	log_syslog = syslog;
	log_level = level;

	if (!syslog)
	{
		return;
	}

	option = foreground ? LOG_PID | LOG_PERROR : LOG_PID;

	openlog(name, option, LOG_MAIL);

	return;
}

void
log_close(void)
{
	if (log_syslog)
	{
		closelog();
	}

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
log_logv(int type, char *f, va_list ap)
{
	char buffer[BUFLEN];

	if (type > log_level) {
		return;
	}

	log_assemble(buffer, sizeof(buffer), f, ap);

	if (log_syslog)
	{
		syslog(type, buffer);
	}
	else
	{
		fprintf(stderr, buffer);
		fputc('\n', stderr);
	}

	return;
}


void
log_log(int type, char *f, ...)
{
	va_list ap;

	va_start(ap, f);
	log_logv(type, f, ap);
	va_end(ap);

	return;
}


void
log_die(int r, char *f, ...)
{
	va_list ap;

	va_start(ap, f);
	log_logv(LOG_ERR, f, ap);
	va_end(ap);

	log_close();
	exit(r);

	return;
}


void
log_message(int type, var_t *mailspec, char *f, ...)
{
	va_list ap;
	VAR_INT_T *id;
	char *stage;
	char message[BUFLEN];


	if (vtable_dereference(mailspec, "milter_id", &id, "milter_stagename",
	    &stage, NULL) != 2)
	{
		log_log(LOG_ERR, "log_message: vtable_dereference failed");
		return;
	}

	if (strlen(f) == 0)
	{
		log_log(type, "%lu: %s", *id, stage);

		return;
	}

	va_start(ap, f);

	if (vsnprintf(message, sizeof message, f, ap) >= sizeof message)
	{
		log_log(LOG_ERR, "log_message: buffer exhausted");
		return;
	}

	va_end(ap);

	log_log(type, "%lu: %s: %s", *id, stage, message);

	return;
}
