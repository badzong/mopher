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
log_append_syserr(char *buffer, int buflen)
{
	strncat(buffer + strlen(buffer), ": ", buflen - strlen(buffer));
	strncat(buffer + strlen(buffer), strerror(errno),
		buflen - strlen(buffer));

	return;
}

void
log_logv(int type, int syserr, char *f, va_list ap)
{
	char buffer[BUFLEN];

	if (type > log_level)
	{
		errno = 0;
		return;
	}

	vsnprintf(buffer, BUFLEN, f, ap);

	if (syserr)
	{
		log_append_syserr(buffer, BUFLEN);
	}

	if (log_syslog)
	{
		//CAVEAT: buffer might contain %
		syslog(type | cf_syslog_facility, "%s", buffer);
	}
	else
	{
		fputs(buffer, stderr);
		fputc('\n', stderr);
	}

	return;
}


void
log_log(int type, int syserr, char *f, ...)
{
	va_list ap;

	va_start(ap, f);
	log_logv(type, syserr, f, ap);
	va_end(ap);

	return;
}


void
log_exit(int r, int syserr, char *f, ...)
{
	va_list ap;

	va_start(ap, f);
	log_logv(LOG_ERR, syserr, f, ap);
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


	if (vtable_dereference(mailspec, "id", &id, "stagename",
	    &stage, NULL) != 2)
	{
		log_log(LOG_ERR, 0, "log_message: vtable_dereference failed");
		return;
	}

	if (strlen(f) == 0)
	{
		log_log(type, 0, "%lu: %s", *id, stage);
		return;
	}

	va_start(ap, f);

	if (vsnprintf(message, sizeof message, f, ap) >= sizeof message)
	{
		log_log(LOG_ERR, 0, "log_message: buffer exhausted");
		return;
	}

	va_end(ap);

	log_log(type, 0, "%lu: %s: %s", *id, stage, message);

	return;
}
