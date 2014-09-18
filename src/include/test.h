#ifndef _TEST_H_
#define _TEST_H_

extern int test_tests;

struct test_handler
{
	char *th_name;
	int (*th_callback)(void);
};
typedef struct test_handler test_handler_t;

#endif /* _TEST_H_ */
