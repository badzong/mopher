#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>

#include <mopher.h>

#define BINNAME "mopher-table"

int
main(int argc, char **argv)
{
	int append = 0;
	int dump = 0;

	while ((opt = getopt(argc, argv, "da")) != -1) {
		switch(opt) {
		case 'a':
			append = 1;
			break;

		case 'd':
			dump = 1;
			break;

		default:
			fprintf(stderr, "Usage: %s [-a | -d] TABLE\n", BINNAME);
			fprintf(stderr, "Dump or modify mopher database table.\n\n");
			fprintf(stderr, "  -a	      Append record\n");
			fprintf(stderr, "  -d         Dump table contents\n");
			fprintf(stderr, "  -h         Show this message\n");
			fprintf(stderr, "\nTry man %s (1) for more information.\n", BINNAME);

			exit(EX_USAGE);
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

	dbt_init();

	if (append)
	{
		log_error("NOT IMPLEMENTED YET");
	}

	if (dump)
	{
		log_error("DUMP");
	}

	return 0;
}
