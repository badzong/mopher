#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include <mopher.h>

static int
run_tests(int optind, int argc, char **argv)
{
#ifndef DEBUG
	fprintf(stderr, "Test not available. Rebuild mopher with -DDEBUG.\n");
	exit(EX_SOFTWARE);
#else
	int test_start, seconds, stat;
	test_handler_t *handler;
	int i;

	test_handler_t test_handlers[] = {
		{"ll.c", ll_test},
		{"sht.c", sht_test},
		{"vp.c", vp_test},
		{"exp.c", exp_test},
		{"dbt.c", dbt_test},
		{"regdom.c", regdom_test},
		{ NULL, NULL }
	};

	log_init("mopher test", LOG_ERR, 0, 1);
	log_error("Start tests..\n");
	test_start = time(NULL);

	for(handler = test_handlers; handler->th_name; ++handler)
	{
		stat = test_tests;

		// Test only modules given on argv
		if (argc > optind)
		{
			for (i = optind; i < argc; ++i)
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
#endif
}

static void
mopherd_unlink_pidfile(char *pidfile)
{
	if (pidfile == NULL)
	{
		return;
	}

	if (!util_file_exists(pidfile))
	{
		return;
	}

	if (unlink(pidfile))
	{
			log_sys_error("mopherd_unlink_pidfile: unlink");
	}

	return;
}

int
main(int argc, char **argv)
{
	int r, opt, foreground, loglevel, check_config, test;
	char *pidfile = NULL;

	check_config = 0;
	foreground = 0;
	test = 0;
	loglevel = LOG_WARNING;

	while ((opt = getopt(argc, argv, "?hvfCTd:c:p:")) != -1) {
		switch(opt) {
		case 'v':
			printf("%s-%s\n", BINNAME, PACKAGE_VERSION);
			exit(EX_OK);
			break;
		case 'f':
			foreground = 1;
			break;

		case 'd':
			loglevel = atoi(optarg);
			break;

		case 'c':
			cf_path(optarg);
			break;

		case 'C':
			check_config = 1;
			loglevel = 0;
			break;

		case 'p':
			pidfile = optarg;
			break;

		case 'T':
			test = 1;
			break;
			

		default:
			fprintf(stderr, "Usage: %s [-c file] [-d N] [-f] [-h]\n", 
				BINNAME);
			fprintf(stderr, "Start the mail filter daemon %s.\n\n", 
				BINNAME);
			fprintf(stderr, "  -c file    Read configuration from file\n");
			fprintf(stderr, "  -C         Check configuration file syntax\n");
			fprintf(stderr, "  -d N       Set log verbosity level\n");
			fprintf(stderr, "  -f         Don't detach into background\n");
			fprintf(stderr, "  -h         Show this message\n");
			fprintf(stderr, "  -p file    Write PID to file\n");
			fprintf(stderr, "  -T         Run mopher unit tests\n");
			fprintf(stderr, "  -v         Show version information\n");
			fprintf(stderr, "\nTry man %s (8) for more information.\n", BINNAME);

			exit(EX_USAGE);
		}
	}

	/*
	 * Run tests
	 */
	if (test)
	{
		return run_tests(optind, argc, argv);
	}

	/*
	 * Daemonize
	 */
	if (foreground == 0 && check_config == 0)
	{
		util_daemonize();
	}

	/*
	 * Open log (syslog == 1)
	 */
	log_init(BINNAME, loglevel, 1, foreground);

	/*
	 * Initialize milter
	 */
	milter_init();

	/*
	 * Check configuration and exit
	 */
	if (check_config)
	{
		milter_clear();
		printf("%s: configuration ok.\n", BINNAME);

		return 0;
	}

	/*
	 * Write PID file.
	 */
	if (pidfile)
	{
		util_pidfile(pidfile);
	}

	log_error("mopherd-%s started", PACKAGE_VERSION);

	r = milter();

	/*
	 * Remove PID file
	 */
	mopherd_unlink_pidfile(pidfile);

	/*
	 * Cleanup
	 */
	milter_clear();
	log_close();

	log_error("terminated");

	return r;
}
