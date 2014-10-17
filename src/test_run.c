#include <config.h>
#include <test.h>
#include <mopher.h>
#include <string.h>

static int test_start;

test_handler_t test_handlers[] = {
	{"sht.c", sht_test},
	{"var.c", var_test},
	{"exp.c", exp_test},
	{"dbt.c", dbt_test},
	{"regdom.c", regdom_test},
	{ NULL, NULL }
};

int
main (int argc, char **argv)
{
	int seconds, stat;
	test_handler_t *handler;
	int i;

	log_init("mopher test", LOG_ERR, 0, 1);

	log_error("Start tests..\n");
	test_start = time(NULL);

	for(handler = test_handlers; handler->th_name; ++handler)
	{
		stat = test_tests;

		// Test only modules given on argv
		if (argc > 1)
		{
			for (i = 1; i < argc; ++i)
			{
				if (!strcmp(argv[i], handler->th_name))
				{
					break;
				}
			}
			if (i == argc)
			{
				continue;
			}
		}

		handler->th_callback(); // Dies on error
		log_error("%-12s: %4d OK", handler->th_name, test_tests-stat);
	}

	seconds = time(NULL) - test_start;
	log_error("\n%d tests in %d seconds.", test_tests, seconds);

	return 0;
}
