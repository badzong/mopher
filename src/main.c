#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>

#include "mopher.h"

int
main(int argc, char **argv)
{
	int r, opt, foreground, loglevel;
	char *config;

	foreground = 0;
	loglevel = LOG_WARNING;
	config = "/etc/mopher.conf";

	while ((opt = getopt(argc, argv, "fhd:c:")) != -1) {
		switch(opt) {
		case 'f':
			foreground = 1;
			break;

		case 'd':
			loglevel = atoi(optarg);
			break;

		case 'c':
			config = optarg;
			break;
		default:
			fprintf(stderr, "Usage: %s [-c file] [-d N] [-f] [-h]\n", 
				BINNAME);
			fprintf(stderr, "Start the %s mail filter system.\n\n", 
				BINNAME);
			fprintf(stderr, "  -c file  Read configuration from file\n");
			fprintf(stderr, "  -d N     Set log verbosity level\n");
			fprintf(stderr, "  -f       Don't detach into background\n");
			fprintf(stderr, "  -h       Show this message\n");
			fprintf(stderr, "\nTry man %s (1) for more information.\n", BINNAME);

			exit(EX_USAGE);
		}
	}

	log_init(BINNAME, loglevel, foreground);
	cf_init(config);
	modules_init();
	dbt_init();
	greylist_init();

	r = milter();

	dbt_clear();
	modules_clear();
	cf_clear();
	log_close();

	return r;
}
