#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "mopher.h"

#define BUFLEN 1024

static int parser_linenumber;
static char *parser_filename;


void
parser_line(void)
{
	++parser_linenumber;

	return;
}

void
parser_error(const char *fmt, ...)
{
	va_list ap;
	char buffer[BUFLEN];

	va_start(ap, fmt);
	vsnprintf(buffer, sizeof buffer, fmt, ap);
	va_end(ap);

	log_die(EX_CONFIG, "%s in \"%s\" on line %d\n", buffer,
	    parser_filename, parser_linenumber);

	return;
}

int
parser_tok_int(int r, long *i, char *token)
{
	*i = atol(token);

	return r;
}

int
parser_tok_float(int r, double *d, char *token)
{
	*d = atof(token);

	return r;
}

int
parser_tok_addr(int r, struct sockaddr_storage **ss, char *token)
{
	*ss = util_strtoaddr(token);
	if (*ss == NULL)
	{
		log_die(EX_SOFTWARE, "parser_tok_addr: util_strtoaddr failed");
	}

	return r;
}

int
parser_tok_encstr(int r, char **str, char *token)
{
	*str = util_strdupenc(token, "\"\"");
	if (*str == NULL)
	{
		log_die(EX_SOFTWARE,
		    "parser_tok_encstr: util_strdupenc failed");
	}

	return r;
}

int
parser_tok_str(int r, char **str, char *token)
{
	*str = strdup(token);
	if (*str == NULL)
	{
		log_die(EX_OSERR, "parser_tok_str: strdup");
	}

	return r;
}

int
parser(char *path, FILE ** input, int (*parser_callback) (void))
{
	struct stat fs;

	if (stat(path, &fs) == -1) {
		log_error("parser: stat '%s'", path);
		return -1;
	}

	if (fs.st_size == 0) {
		log_notice("parser: '%s' is empty", path);
		return -1;
	}

	if ((*input = fopen(path, "r")) == NULL) {
		log_error("parser: fopen '%s'", path);
		return -1;
	}

	parser_linenumber = 1;
	parser_filename = path;

	printf("INIT: %d\n", parser_linenumber);

	if (parser_callback()) {
		log_error("parser: supplied parser callback failed");
		return -1;
	}

	fclose(*input);

	return 0;
}
