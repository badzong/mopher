#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>

#include <mopher.h>

#define BUFLEN 1024
#define MAX_INCLUDE 256

static int parser_linenumber[ACL_INCLUDE_DEPTH];
static char *parser_filename[ACL_INCLUDE_DEPTH];
static char *parser_filename_buffer[MAX_INCLUDE];
static int parser_filename_index;

// Increased to 0 after first call to parser_stack() in parser()
int parser_stack_ptr = -1;


void
parser_line(void)
{
	++parser_linenumber[parser_stack_ptr];

	return;
}

void
parser_stack(char *path)
{
	char *copy;
	char *filename;

	if (parser_stack_ptr >= ACL_INCLUDE_DEPTH - 1)
	{
		log_die(EX_CONFIG, "Too many include levels in %s on line %d\n",
			parser_filename[parser_stack_ptr], parser_linenumber[parser_stack_ptr]);
	}

	// Need a copy here, because basename may modify path.
	copy = strdup(path);
	if (copy == NULL)
	{
		log_die(EX_OSERR, "strdup failed");
	}

	// Copy again, because filename points somewehre into copy. Appended to
	// parser_filename_buffer and free'd in parser_clear().
	filename = strdup(basename(copy));
	if (filename == NULL)
	{
		log_die(EX_OSERR, "strdup failed");
	}

	parser_filename_buffer[parser_filename_index++] = filename;
	if (parser_filename_index >= MAX_INCLUDE)
	{
		log_die(EX_CONFIG, "Too many includes\n");
	}

	free(copy);

	++parser_stack_ptr;
	parser_filename[parser_stack_ptr] = filename;
	parser_linenumber[parser_stack_ptr] = 1;

	return;
}

int
parser_pop(void)
{
	parser_filename[parser_stack_ptr] = NULL;
	parser_linenumber[parser_stack_ptr] = 0;

	return --parser_stack_ptr;
}

int
parser_get_line()
{
	return parser_linenumber[parser_stack_ptr];
}

char *
parser_get_filename()
{
	// Happens after Flex has terminated and Bison is evaluating the last rule.
	if (parser_stack_ptr < 0)
	{
		return parser_filename_buffer[0];
	}

	return parser_filename[parser_stack_ptr];
}

void
parser_error(const char *fmt, ...)
{
	va_list ap;
	char buffer[BUFLEN];

	va_start(ap, fmt);
	vsnprintf(buffer, sizeof buffer, fmt, ap);
	va_end(ap);

	log_die(EX_CONFIG, "%s in \"%s\" on line %d\n", buffer, parser_get_filename(),
		parser_get_line());

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
		log_sys_die(EX_OSERR, "parser_tok_str: strdup");
	}

	return r;
}

int
parser(char *path, FILE ** input, int (*parser_callback) (void))
{
	struct stat fs;

	if (stat(path, &fs) == -1) {
		log_sys_error("%s", path);
		return -1;
	}

	if (fs.st_size == 0) {
		log_notice("parser: '%s' is empty", path);
		return -1;
	}

	if ((*input = fopen(path, "r")) == NULL) {
		log_sys_error("parser: fopen '%s'", path);
		return -1;
	}

	parser_stack(path);

	if (parser_callback()) {
		log_error("parser: supplied parser callback failed");
		return -1;
	}

	fclose(*input);

	return 0;
}

void
parser_clear(void)
{
	for(;parser_filename_index >=0; --parser_filename_index)
	{
		free(parser_filename_buffer[parser_filename_index]);
	}

	return;
}
