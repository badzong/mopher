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
	char *mopherd_conf;
	char *mail_acl;

	check_config = 0;
	foreground = 0;
	loglevel = LOG_WARNING;
	mopherd_conf = MOPHERD_CONF;

	while ((opt = getopt(argc, argv, "fhCd:c:")) != -1) {
		switch(opt) {
		case 'f':
			foreground = 1;
			break;

		case 'd':
			loglevel = atoi(optarg);
			break;

		case 'c':
			mopherd_conf = optarg;
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
			fprintf(stderr, "  -c file  Read configuration from file\n");
			fprintf(stderr, "  -d N     Set log verbosity level\n");
			fprintf(stderr, "  -f       Don't detach into background\n");
			fprintf(stderr, "  -h       Show this message\n");
			fprintf(stderr, "\nTry man %s (1) for more information.\n", BINNAME);

			exit(EX_USAGE);
		}
	}

	log_init(BINNAME, loglevel, foreground);
	
	cf_init(mopherd_conf);

	/*
	 * Load default ACL path if not configured
	 */
	mail_acl = (char *) cf_get(VT_STRING, "acl_path", NULL);
	if(mail_acl == NULL)
	{
		mail_acl = MAIL_ACL;
		log_warning("acl_path not set: using \"%s\"", mail_acl);
	}

	/*
	 * Check configuration and exit
	 */
	if (check_config)
	{
		dbt_init();
		acl_init();
		greylist_init();
		milter_init();
		module_init();
		acl_read(mail_acl);

		printf("%s: configuration ok.\n", BINNAME);

		return 0;
	}

	dbt_init();
	acl_init();
	greylist_init();
	milter_init();

	/*
	 * Load modules
	 */
	module_init();

	dbt_open_databases();
	acl_read(mail_acl);

	r = milter();

	acl_clear();
	dbt_clear();
	module_clear();
	cf_clear();
	log_close();

	return r;
}
