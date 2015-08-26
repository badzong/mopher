#ifndef _PARSER_H_
#define _PARSER_H_

#include <sys/types.h>
#include <stdio.h>

extern int parser_stack_ptr;

/*
 * Prototypes
 */

void parser_stack(char *filename);
int parser_pop(void);
void parser_line(void);
int parser_get_line(void);
char *parser_get_filename(void);
void parser_error(const char *fmt, ...);
int parser_tok_int(int r, long *i, char *token);
int parser_tok_float(int r, double *d, char *token);
int parser_tok_addr(int r, struct sockaddr_storage **ss, char *token);
int parser_tok_encstr(int r, char **str, char *token);
int parser_tok_str(int r, char **str, char *token);
int parser(char *path, FILE ** input, int (*parser_callback) (void));
void parser_clear(void);
#endif /* _PARSER_H_ */
