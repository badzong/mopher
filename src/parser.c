#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>

#include <mopher.h>

#define BUFLEN 1024

static parser_file_t *
parser_file_create(char *path)
{
	parser_file_t *pf;

	pf = (parser_file_t *) malloc(sizeof(parser_file_t));
	if (pf == NULL)
	{
		log_sys_die(EX_OSERR, "parser_file: malloc");
	}
	
	memset(pf, 0, sizeof(parser_file_t));
	strncpy(pf->pf_path, path, PARSER_PATHLEN);
	pf->pf_path[PARSER_PATHLEN - 1] = 0;

	pf->pf_filename = strrchr(pf->pf_path, '/');
	if (pf->pf_filename == NULL)
	{
		pf->pf_filename = pf->pf_path;
	}
	// Remove leading /
	else
	{
		++pf->pf_filename;
	}

	return pf;
}

static void
parser_init(parser_t *p)
{
	ll_init(&p->p_stack);
	ll_init(&p->p_files);

	return;
}

void
parser_clear(parser_t *p)
{
	ll_clear(&p->p_files, free);

	return;
}


void
parser_stack(parser_t *p, void *yy_buffer, char *path)
{
	parser_file_t *pf_current;
	parser_file_t *pf_new;

	// Save buffer to current file
	pf_current = parser_current_file(p);
	if (pf_current)
	{
		pf_current->pf_yy_buffer = yy_buffer;
	}

	if (p->p_stack.ll_size > PARSER_MAXSTACK)
	{
		log_die(EX_SOFTWARE, "parser_stack: %s: exceeded maximum include stack size %d",
			path, PARSER_MAXSTACK);
	}

	pf_new = parser_file_create(path);
	if (pf_new == NULL)
	{
		log_die(EX_SOFTWARE, "parser_stack: parser_file create failed");
	}

	if (LL_PUSH(&p->p_files, pf_new) == -1 || LL_PUSH(&p->p_stack, pf_new) == -1)
	{
		log_die(EX_SOFTWARE, "parser_stack: LL_PUSH failed");
	}

	return;
}

int
parser_pop(parser_t *p)
{
	LL_POP(&p->p_stack);

	return p->p_stack.ll_size;
}

parser_file_t *
parser_current_file(parser_t *p)
{
	// The stack is not empty
	if (p->p_stack.ll_size > 0)
	{
		return LL_HEAD(&p->p_stack);
	}

	// Last file has been popped so the stack ist empty.
	if (p->p_files.ll_size > 0)
	{
		return LL_TAIL(&p->p_files);
	}

	// Initializing stack and files are empty
	return NULL;
}

void
parser_next_line(parser_t *p)
{
	parser_file_t *pf = parser_current_file(p);
	++pf->pf_line;

	return;
}

int
parser_current_line(parser_t *p)
{
	parser_file_t *pf = parser_current_file(p);
	return pf->pf_line;
}

char *
parser_current_filename(parser_t *p)
{
	parser_file_t *pf = parser_current_file(p);
	return pf->pf_filename;
}

char *
parser_current_path(parser_t *p)
{
	parser_file_t *pf = parser_current_file(p);
	return pf->pf_path;
}


void *
parser_current_yy_buffer(parser_t *p)
{
	parser_file_t *pf = parser_current_file(p);
	return pf->pf_yy_buffer;
}

void
parser_error(parser_t *p, const char *fmt, ...)
{
	va_list ap;
	char buffer[BUFLEN];

	va_start(ap, fmt);
	vsnprintf(buffer, sizeof buffer, fmt, ap);
	va_end(ap);

	log_die(EX_CONFIG, "%s on line %d: %s\n", parser_current_path(p),
		parser_current_line(p), buffer);

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

void
parser(parser_t *p, char *path, int open_file, FILE **input, int (*parser_callback) (void))
{
	struct stat fs;

	if (open_file)
	{
		if (stat(path, &fs) == -1)
		{
			log_sys_die(EX_CONFIG, "%s", path);
		}

		if (fs.st_size == 0)
		{
			log_die(EX_CONFIG, "parser: '%s' is empty", path);
		}

		if ((*input = fopen(path, "r")) == NULL)
		{
			log_sys_die(EX_CONFIG, "parser: fopen '%s'", path);
		}
	}

	parser_init(p);
	parser_stack(p, NULL, path);

	if (parser_callback())
	{
		log_die(EX_SOFTWARE, "parser: supplied parser callback failed");
	}

	if (open_file)
	{
		fclose(*input);
	}

	return;
}
