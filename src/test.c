#ifdef DEBUG

#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <config.h>
#include <mopher.h>

#define BUFLEN 1024
#define TEST_THREADS 1

static pthread_mutex_t test_mutex = PTHREAD_MUTEX_INITIALIZER;
int test_tests;

static void
test_count(void)
{
	if (pthread_mutex_lock(&test_mutex))
	{
		log_die(EX_SOFTWARE, "test_count: pthread_mutex_lock");
	}

	++test_tests;

	if (pthread_mutex_unlock(&test_mutex))
	{
		log_die(EX_SOFTWARE, "test_count: pthread_mutex_unlock");
	}
}

void
test_assert(char *file, int line, int cond, char *m, ...)
{
	va_list ap;
	char f[BUFLEN];

	test_count();

	if (cond)
	{
		return;
	}

	if (snprintf(f, sizeof f, "%s:%d *** %s", file, line, m) >= sizeof f)
	{
		log_die(EX_SOFTWARE, "test_assert: buffer exhausted");
	}

	va_start(ap, m);
	log_logv(LOG_ERR, 0, f, ap);
	va_end(ap);

	log_die(EX_SOFTWARE, "\nTEST FAILED!");

	return;
}

int
test_in_argv(test_handler_t *handler, int optind, int argc, char *argv[])
{
	int i;

	// No tests specified in argv. Run all tests
	if (argc <= optind)
	{
		return 1;
	}
	
	// Tests were specified.
	for (i = optind; i < argc; ++i)
	{
		if (strcmp(argv[i], handler->th_name) == 0)
		{
			return 1;
		}
	}

	return 0;
}

static void
test_thread(test_data_t *data)
{
	data->td_handler->th_test(data->td_number);
	return;
}

static void
test_thread_delete(test_data_t *data)
{
	util_thread_join(*data->td_thread);
	free(data->td_thread);
	free(data);	

	return;
}

void
test_threads(int threads, test_handler_t *handler)
{
	int i;
	int stat, tests_run, tests_per_thread;
	pthread_t *thread;
	ll_t thread_list;
	test_data_t *data;
	char buffer[BUFLEN];

	stat = test_tests;
	ll_init(&thread_list);

	// Start threads
	for (i = 0; i < threads; ++i)
	{
		thread = (pthread_t *) malloc(sizeof (pthread_t));
		if (thread == NULL)
		{
			log_sys_die(EX_OSERR, "test_threads: malloc");
		}
		memset(thread, 0, sizeof (pthread_t));

		data = (test_data_t *) malloc(sizeof (test_data_t));
		if (data == NULL)
		{
			log_sys_die(EX_OSERR, "test_threads: malloc");
		}

		data->td_number = i;
		data->td_thread = thread;
		data->td_handler = handler;

		util_thread_create(thread, test_thread, data);

		LL_INSERT(&thread_list, data);
	}

	// Join and free threads
	ll_clear(&thread_list, (void *) test_thread_delete);

	tests_run = test_tests - stat;
	tests_per_thread = tests_run / threads;


	if (handler->th_descr)
	{
		snprintf(buffer, sizeof buffer, "%s/%s", handler->th_name,
			handler->th_descr);
	}
	
	log_error("%-23s: %6d OK (%d/thread)",
		handler->th_descr? buffer: handler->th_name, tests_run,
		tests_per_thread);
}

void
test_run(int optind, int argc, char **argv)
{
	int test_start, seconds;
	test_handler_t *handler;

	test_handler_t multi_threaded_tests[] = {
		{"ll.c", NULL, NULL, ll_test, NULL},
		{"sht.c", NULL, NULL, sht_test, NULL},
		{"util.c", NULL, NULL, util_test, NULL},
		{"vp.c", NULL, NULL, vp_test, NULL},
		{"msgmod.c", NULL, NULL, msgmod_test, NULL},
		{"regdom.c", NULL, regdom_test_init, regdom_test, regdom_clear},
		{"exp.c", NULL, exp_test_init, exp_test, exp_clear},
		{"sql.c", NULL, NULL, sql_test, NULL},
                {"dbt.c", "memdb.c (stage 1)", dbt_test_memdb_init, dbt_test_stage1, dbt_test_clear },
                {"dbt.c", "bdb.c (stage 1)", dbt_test_bdb_init, dbt_test_stage1, dbt_test_clear },
                {"dbt.c", "bdb.c (stage 2)", dbt_test_bdb_init, dbt_test_stage2, dbt_test_clear },
                {"dbt.c", "pgsql.c (stage 1)", dbt_test_pgsql_init, dbt_test_stage1, dbt_test_clear },
                {"dbt.c", "pgsql.c (stage 2)", dbt_test_pgsql_init, dbt_test_stage2, dbt_test_clear },
                {"dbt.c", "mysql.c (stage 1)", dbt_test_mysql_init, dbt_test_stage1, dbt_test_clear },
                {"dbt.c", "mysql.c (stage 2)", dbt_test_mysql_init, dbt_test_stage2, dbt_test_clear },
		{ NULL, NULL, NULL, NULL, NULL }
	};

	log_init("mopher test", LOG_ERR, 0, 1);
	test_start = time(NULL);

	// Multi-threaded tests
	for(handler = multi_threaded_tests; handler->th_name; ++handler)
	{
		if (!test_in_argv(handler, optind, argc, argv))
		{
			continue;
		}

		// Initialize test
		if (handler->th_init != NULL)
		{
			if(handler->th_init())
			{
				continue;
			}
		}

		// Start test
		test_threads(TEST_THREADS, handler);

		// Finalize test
		if (handler->th_clear != NULL)
		{
			handler->th_clear();
		}
	}

	seconds = time(NULL) - test_start;
	log_error("\n%d tests in %d seconds.", test_tests, seconds);

	return;
}

#endif
