#ifndef _PARSER_H_
#define _PARSER_H_

#include <sys/types.h>
#include <stdio.h>

#define PARSER_PATHLEN 256
#define PARSER_MAXSTACK 10

typedef struct parser {
	ll_t	p_stack;
	ll_t	p_files;
} parser_t;

typedef struct parser_file {
	int	 pf_line;
	char	 pf_path[PARSER_PATHLEN];
	char	*pf_filename;
	void	*pf_yy_buffer;
} parser_file_t;


/*
 * Prototypes
 */

void parser_clear(parser_t *p);
void parser_stack(parser_t *p, void *yy_buffer, char *path);
int parser_pop(parser_t *p);
parser_file_t * parser_current_file(parser_t *p);
void parser_next_line(parser_t *p);
int parser_current_line(parser_t *p);
char * parser_current_filename(parser_t *p);
char * parser_current_path(parser_t *p);
void * parser_current_yy_buffer(parser_t *p);
void parser_error(parser_t *p, const char *fmt, ...);
int parser_tok_int(int r, long *i, char *token);
int parser_tok_float(int r, double *d, char *token);
int parser_tok_addr(int r, struct sockaddr_storage **ss, char *token);
int parser_tok_encstr(int r, char **str, char *token);
int parser_tok_str(int r, char **str, char *token);
void parser(parser_t *p, char *path, FILE ** input, int (*parser_callback) (void));

#endif /* _PARSER_H_ */
