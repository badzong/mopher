#ifndef _SERVER_H_
#define _SERVER_H_

typedef struct {
	char * sf_name;
	char * sf_help;
	int (*sf_callback)(int sock, int argc, char **argv);
} server_function_t;

/*
 * Prototypes
 */

int server_init();
void server_clear(void);
int server_dummy(int sock, int argc, char **argv);
int server_help(int sock, int argc, char **argv);
int server_quit(int sock, int argc, char **argv);
int server_greylist_dump(int sock, int argc, char **argv);
int server_greylist_pass(int sock, int argc, char **argv);

#endif /* _SERVER_H_ */
