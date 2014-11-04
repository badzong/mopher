#ifndef _TEST_H_
#define _TEST_H_

#include <pthread.h>

extern int test_tests;

struct test_handler
{
	char *th_name;
	char *th_descr;
	void (*th_init)(void);
	void (*th_test)(int n);
	void (*th_clear)(void);
};
typedef struct test_handler test_handler_t;

struct test_data
{
	int             td_number;
	pthread_t      *td_thread;
	test_handler_t *td_handler;
};
typedef struct test_data test_data_t;

void test_assert(char *file, int line, int cond, char *m, ...);
int test_in_argv(test_handler_t *handler, int optind, int argc, char *argv[]);
void test_threads(int threads, test_handler_t *handler);
void test_run(int optind, int argc, char **argv);

#define TEST_ASSERT(...) test_assert(__FILE__, __LINE__, __VA_ARGS__)

#endif /* _TEST_H_ */
