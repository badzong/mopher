#include <config.h>
#include <test.h>
#include <mopher.h>

int test_tests;
static int test_start;

test_handler_t test_handlers[] = {
	{"regdom.c", regdom_test},
	{ NULL, NULL }
};

int
main (int argc, char **argv)
{
	int seconds, stat;
	test_handler_t *handler;
	int r = 0;

	log_init("mopher test", LOG_ERR, 0, 1);

	log_error("Start tests..\n");
	test_start = time(NULL);

	for(handler = test_handlers; handler->th_name; ++handler)
	{
		stat = test_tests;

		if(handler->th_callback())
		{
			log_error("%s: FAILED!", handler->th_name);
			r = 255;
			break;
		}

		log_error("%s: \t%3d tests OK", handler->th_name, test_tests-stat);
	}


	seconds = time(NULL) - test_start;
	log_error("\n%d tests in %d seconds.", test_tests, seconds);

	return r;
}
