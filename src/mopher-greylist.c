#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>

#include <mopher.h>

#undef BINNAME
#define BINNAME "mopher-greylist"

void
usage(void)
{
	fprintf(stderr, "Usage: %s OPTIONS\n", BINNAME);
	fprintf(stderr, "Dump mopher greylist table or let selected tuples pass greylisting.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "OPTIONS:\n\n");
	fprintf(stderr, "  -d         Dump mopher greylist database\n");
	fprintf(stderr, "  -p         Pass greylisting (requires -s, -f and -r )\n");
	fprintf(stderr, "  -s         Source address or domain\n");
	fprintf(stderr, "  -f         Envelope sender address (from)\n");
	fprintf(stderr, "  -r         Envelope recipient address\n");
	fprintf(stderr, "  -h         Show this message\n");
	fprintf(stderr, "\nEXAMPLES:\n\n");
	fprintf(stderr, "Dump greylist\n");
	fprintf(stderr, " %s -d\n\n", BINNAME);
	fprintf(stderr, "Pass greylisting\n");
	fprintf(stderr, " %s -p -s example.com -f bob@example.com -r me@mydomain.org\n\n", BINNAME);
	exit(EX_USAGE);
}


int
main(int argc, char **argv)
{
	int opt;
	int pass = 0;
	int dump = 0;
	char *source = NULL;
	char *from = NULL;
	char *rcpt = NULL;
	char *workdir;
	int n;

	while ((opt = getopt(argc, argv, "dps:f:r:")) != -1) {
		switch(opt) {
		case 'p':
			pass = 1;
			break;

		case 's':
			source = optarg;
			break;

		case 'f':
			from = optarg;
			break;

		case 'r':
			rcpt = optarg;
			break;

		case 'd':
			dump = 1;
			break;

		default:
			usage();
		}
	}

	/*
	 * Initialize log (foreground, stderr)
	 */
	log_init(BINNAME, LOG_ERR, 0, 1);

	cf_init();

	/*
	 * Need to change working directory for relative paths
	 */

	workdir = cf_workdir_path ? cf_workdir_path : defs_mopherd_dir;
	if (chdir(workdir))
	{
		log_sys_die(EX_OSERR, "chdir to \"%s\"", cf_workdir_path);
	}

	/*
	 * Initialize database.
	 */

	dbt_init();
	milter_init();

	if (dump)
	{
		greylist_dump();
	}
	else if (pass)
	{
		if (source == NULL || from == NULL || rcpt == NULL)
		{
			usage();
		}

		n = greylist_pass(source, from, rcpt);

		switch (n)
		{
		case 0:
			log_error("Record does not exist or has already passed greylisting");
			break;
		case 1:
			log_error("Record updated");
			break;
		default:
			log_sys_die(EX_SOFTWARE, "Update failed");
				
		}
	}

	milter_clear();

	return 0;
}
