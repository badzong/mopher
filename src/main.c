#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>

#include "mopher.h"

int
main(int argc, char **argv)
{
	int r, opt, foreground, loglevel, check_config;

	check_config = 0;
	foreground = 0;
	loglevel = LOG_WARNING;

	while ((opt = getopt(argc, argv, "fhCd:c:")) != -1) {
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
			fprintf(stderr, "\nTry man %s (1) for more information.\n", BINNAME);

			exit(EX_USAGE);
		}
	}

	/*
	 * Open syslog
	 */
	log_init(BINNAME, loglevel, foreground);

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

	r = milter();

	/*
	 * Cleanup
	 */
	milter_clear();
	log_close();

	return r;
}
