#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>

#include <mopher.h>

int
main(int argc, char **argv)
{
	int r, opt, foreground, loglevel, check_config;
	char *pidfile = NULL;

	check_config = 0;
	foreground = 0;
	loglevel = LOG_WARNING;

	while ((opt = getopt(argc, argv, "fhCd:c:p:")) != -1) {
		switch(opt) {
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

		default:
			fprintf(stderr, "Usage: %s [-c file] [-d N] [-f] [-h]\n", 
				BINNAME);
			fprintf(stderr, "Start the %s mail filter system.\n\n", 
				BINNAME);
			fprintf(stderr, "  -c file    Read configuration from file\n");
			fprintf(stderr, "  -C         Check configuration file syntax\n");
			fprintf(stderr, "  -d N       Set log verbosity level\n");
			fprintf(stderr, "  -f         Don't detach into background\n");
			fprintf(stderr, "  -h         Show this message\n");
			fprintf(stderr, "  -p file    Write PID to file\n");
			fprintf(stderr, "\nTry man %s (1) for more information.\n", BINNAME);

			exit(EX_USAGE);
		}
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
	 * Write PID file.
	 */
	if (pidfile)
	{
		util_pidfile(pidfile);
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

	log_error("started");

	r = milter();

	/*
	 * Remove PID file
	 */
	if (pidfile)
	{
		if (unlink(pidfile))
		{
			log_sys_error("mopherd: unlink");
		}
	}

	/*
	 * Cleanup
	 */
	milter_clear();
	log_close();

	log_error("terminated");

	return r;
}
