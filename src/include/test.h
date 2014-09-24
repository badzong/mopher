#ifndef _TEST_H_
#define _TEST_H_

extern int test_tests;

struct test_handler
{
	char *th_name;
	void (*th_callback)(void);
};
typedef struct test_handler test_handler_t;

void test_assert(char *file, int line, int cond, char *m, ...);

#define TEST_ASSERT(...) test_assert(__FILE__, __LINE__, __VA_ARGS__)

#endif /* _TEST_H_ */
