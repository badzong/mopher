#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include <mopher.h>

#undef BINNAME
#define BINNAME "mopherctl"

#define ACTION_BUCKETS 16
#define BUFLEN 2048

static int   moctl_debug = LOG_ERR;
static char *moctl_config;
static char *moctl_server;
static int   moctl_socket;

static void
moctl_usage(void)
{
	log_error("Usage: %s [-hG] [-d level] [-c file] [-s host] [-D table] [-P <origin> <from> <rcpt>]", BINNAME);
	log_error("");
	log_error("mopherctl controls the mopher daemon.");
	log_error("");
	log_error("Available options are:");
	log_error("");
	log_error("  -h      Show usage message.");
	log_error("");
	log_error("  -d level");
	log_error("    Set logging severity level to level");
	log_error("");
	log_error("  -c file");
	log_error("    Read configuration from file.");
	log_error("");
	log_error("  -s host");
	log_error("    Connect to host.");
	log_error("");
	log_error("  -D <table>");
	log_error("    Print raw content of table.");
	log_error("");
	log_error("  -G");
	log_error("    Print formatted content of the greylist-table.");
	log_error("");
	log_error("  -P <origin> <from> <rcpt>");
	log_error("    Temporarily whitelist triplet.");
	log_error("");

	exit(EX_USAGE);
}

static void
moctl_init(void)
{
	/*
	 * Initialize log (foreground, stderr)
	 */
	log_init(BINNAME, moctl_debug, 0, 1);

	/*
	 * Get server socket string
	 */
	if (moctl_server == NULL)
	{
		if (moctl_config)
		{
			cf_path(moctl_config);
		}
		cf_init();
		moctl_server = cf_control_socket;
	}

	if (moctl_server == NULL)
	{
		log_die(EX_CONFIG, "moctl_init: server_socket not configured");
	}

	/*
	 * Connect Server
	 */
	moctl_socket = sock_connect(moctl_server);
	if (moctl_socket == -1)
	{
		log_die(EX_SOFTWARE, "moctl_init: connection to %s failed", moctl_server);
	}

	log_debug("moctl_init: connected to %s", moctl_server);

	return;
}

static int
moctl_greylist(void)
{
	char *dump;
	
	if (server_data_cmd(moctl_socket, "greylist_dump", &dump))
	{
		log_die(EX_SOFTWARE, "moctl_greylist_dump: server_data_cmd failed");
	}

	printf(dump);
	free(dump);

	return 0;
}

static int
moctl_pass(char *source, char *from, char *rcpt)
{
	char cmd[BUFLEN];
	int n;
	
	n = snprintf(cmd, sizeof cmd, "greylist_pass %s %s %s", source, from,
	    rcpt);
	if (n >= sizeof cmd)
	{
		log_die(EX_SOFTWARE, "moctl_greylist_dump: buffer exhausted");
	}

	if(server_cmd(moctl_socket, cmd))
	{
		log_die(EX_SOFTWARE, "moctl_greylist_dump: server_cmd failed");
	}

	return 0;
}

static int
moctl_dump(char *tablename)
{
	char *dump;
	char cmd[BUFLEN];
	int n;

	n = snprintf(cmd, sizeof cmd, "table_dump %s", tablename);
	if (n >= sizeof cmd)
	{
		log_die(EX_SOFTWARE, "moctl_table_dump: buffer exhausted");
	}
	
	if (server_data_cmd(moctl_socket, cmd, &dump))
	{
		log_die(EX_SOFTWARE, "moctl_table_dump: server_data_cmd failed");
	}

	printf(dump);
	free(dump);

	return 0;
}

int
main(int argc, char **argv)
{
	int opt;
	int dump = 0;
	int greylist = 0;
	int pass = 0;
	int commands = 0;
	char *table;
	char *source;
	char *from;
	char *rcpt;

	// Open log
	log_init(BINNAME, moctl_debug, 0, 1);

	// No command supplied
	if (argc < 2)
	{
		moctl_usage();
	}

	while ((opt = getopt(argc, argv, "?hGPd:c:s:D:")) != -1) {
		switch(opt) {
		case '?':
		case 'h':
			moctl_usage();
			break;

		case 'G':
			++commands;
			greylist = 1;
			break;

		case 'P':
			++commands;
			pass = 1;
			break;

		case 'd':
			moctl_debug = atoi(optarg);
			break;

		case 'c':
			moctl_config = optarg;
			break;

		case 's':
			moctl_server = optarg;
			break;

		case 'D':
			++commands;
			dump = 1;
			table = optarg;
			break;

		default:
			moctl_usage();
			break;
		}
	}

	// mopherctl takes exactly 1 command -G | -D | -P
	if (commands != 1)
	{
		moctl_usage();
	}

	moctl_init();

	if (dump)
	{
		return moctl_dump(table);
	}
	else if (greylist)
	{
		return moctl_greylist();
	}
	else if (pass)
	{
		if (argc - optind != 3)
		{
			moctl_usage();
			return EX_USAGE;
		}

		source = argv[optind];
		from = argv[optind + 1];
		rcpt = argv[optind + 2];

		return moctl_pass(source, from, rcpt);
	}

	// Impossible
	moctl_usage();
	return EX_USAGE;

	// Cleanup
	if (moctl_socket)
	{
		close(moctl_socket);
	}

	if (moctl_config)
	{
		cf_clear();
	}

	return 0;
}
