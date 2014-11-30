#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include <mopher.h>

static char *mopherd_pidfile;

static int
mopherd_test(int optind, int argc, char **argv)
{
#ifndef DEBUG
	fprintf(stderr, "Test not available. Rebuild mopher with -DDEBUG.\n");
	exit(EX_SOFTWARE);
#else
	test_run(optind, argc, argv);
	return 0;
#endif
}

void
mopherd_cleanup(void)
{
	milter_clear();
	log_close();

	/*
	 * Remove PID file
	 */
	if (mopherd_pidfile == NULL)
	{
		return;
	}

	if (!util_file_exists(mopherd_pidfile))
	{
		return;
	}

	if (unlink(mopherd_pidfile))
	{
	 	log_sys_error("mopherd_cleanup: unlink");
	}

	return;
}

int
main(int argc, char **argv)
{
	int r, opt, foreground, loglevel, check_config, test;

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
			break;

		case 'p':
			mopherd_pidfile = optarg;
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
		return mopherd_test(optind, argc, argv);
	}

	/*
	 * Daemonize
	 */
	if (foreground == 0 && check_config == 0)
	{
		util_daemonize();
	}

	/*
	 * Register mopherd_cleanup
	 */
	if (atexit(mopherd_cleanup))
	{
		log_sys_die(EX_OSERR, "mopherd: atexit failed");
	}

	/*
	 * Open log (syslog == 1)
	 */
	log_init(BINNAME, loglevel, 1, foreground);

	/*
	 * Write PID file.
	 */
	if (mopherd_pidfile)
	{
		util_pidfile(mopherd_pidfile);
	}

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

	log_error("mopherd-%s started", PACKAGE_VERSION);

	r = milter();

	log_error("terminated");

	return r;
}
