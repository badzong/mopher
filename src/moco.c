#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include <mopher.h>

#undef BINNAME
#define BINNAME "moco"

#define ACTION_BUCKETS 16
#define BUFLEN 2048

static int   moco_debug = LOG_ERR;
static char *moco_config;
static char *moco_server;
static char  moco_options[64] = "?hd:c:s:";
static int   moco_socket;

static void
moco_usage(void)
{
	log_error("Usage: %s <scope> options", BINNAME);
	log_error("Mopher control client.");
	log_error("");
	log_error("SCOPES:");
	log_error("  table      Dump database tables");
	log_error("  greylist   Manipulate greylist database");
	log_error("");
	log_error("TABLE OPTIONS:");
	log_error("  -D table   Dump database table");
	log_error("");
	log_error("GREYLIST OPTIONS:");
	log_error("  -D         Dump mopher greylist database");
	log_error("  -p         Pass greylisting (requires -m, -f and -r )");
	log_error("  -m source  MTA source address or domain");
	log_error("  -f from    Envelope sender address");
	log_error("  -r rcpt    Envelope recipient address");
	log_error("");
	log_error("GENERAL OPTIONS:");
	log_error("  -d level   Debug level 0-7");
	log_error("  -c file    Use configuration file");
	log_error("  -s server  Connect to server");
	log_error("  -?         Show this message");
	log_error("");
	log_error("EXAMPLES:");
	log_error("");
	log_error("  Dump greylist:");
	log_error("  %s table -D counter_penpal", BINNAME);
	log_error("");
	log_error("  Dump greylist:");
	log_error("  %s greylist -D", BINNAME);
	log_error("");
	log_error("  Pass greylisting:");
	log_error("  %s greylist -p -m example.com -f bob@example.com -r me@mydomain.org", BINNAME);
	log_error("");
	exit(EX_USAGE);
}


static int
moco_greylist_dump(void)
{
	char *dump;
	
	if (server_data_cmd(moco_socket, "greylist_dump", &dump))
	{
		log_die(EX_SOFTWARE, "moco_greylist_dump: server_data_cmd failed");
	}

	printf(dump);
	free(dump);

	return 0;
}


static int
moco_greylist_pass(char *source, char *from, char *rcpt)
{
	char cmd[BUFLEN];
	int n;
	
	n = snprintf(cmd, sizeof cmd, "greylist_pass %s %s %s", source, from,
	    rcpt);
	if (n >= sizeof cmd)
	{
		log_die(EX_SOFTWARE, "moco_greylist_dump: buffer exhausted");
	}

	if(server_cmd(moco_socket, cmd))
	{
		log_die(EX_SOFTWARE, "moco_greylist_dump: server_cmd failed");
	}

	return 0;
}

static int
moco_general(char opt, char *optarg)
{
	switch(opt) {
	case 'd':
		moco_debug = atoi(optarg);
		return 0;

	case 'c':
		moco_config = optarg;
		return 0;

	case 's':
		moco_server = optarg;
		return 0;

	default:
		moco_usage();
	}

	return -1;
}


static void
moco_init(void)
{
	/*
	 * Initialize log (foreground, stderr)
	 */
	log_init(BINNAME, moco_debug, 0, 1);

	/*
	 * Get server socket string
	 */
	if (moco_server == NULL)
	{
		if (moco_config)
		{
			cf_path(moco_config);
		}
		cf_init();
		moco_server = cf_control_socket;
	}

	if (moco_server == NULL)
	{
		log_die(EX_CONFIG, "moco_init: server_socket not configured");
	}

	/*
	 * Connect Server
	 */
	moco_socket = sock_connect(moco_server);
	if (moco_socket == -1)
	{
		log_die(EX_SOFTWARE, "moco_init: connection to %s failed", moco_server);
	}

	log_debug("moco_init: connected to %s", moco_server);

	return;
}

static int
moco_table(int argc, char **argv)
{
	char *dump;
	char cmd[BUFLEN];
	int n;
	int opt;
	char *tablename = NULL;

	// Append greylisting specific options
	strcat(moco_options, "D:");
	
	while ((opt = getopt(argc, argv, moco_options)) != -1) {
		switch(opt) {
		case 'D':
			tablename = optarg;
			break;

		default:
			moco_general(opt, optarg);
			break;
		}
	}

	if (!tablename)
	{
		return -1;
	}

	moco_init();

	n = snprintf(cmd, sizeof cmd, "table_dump %s", tablename);
	if (n >= sizeof cmd)
	{
		log_die(EX_SOFTWARE, "moco_table_dump: buffer exhausted");
	}
	
	if (server_data_cmd(moco_socket, cmd, &dump))
	{
		log_die(EX_SOFTWARE, "moco_table_dump: server_data_cmd failed");
	}

	printf(dump);
	free(dump);

	return 0;
}


static int
moco_greylist(int argc, char **argv)
{
	int opt;
	int pass = 0;
	int dump = 0;
	char *source = NULL;
	char *from = NULL;
	char *rcpt = NULL;

	// Append greylisting specific options
	strcat(moco_options, "Dpm:f:r:");
	
	while ((opt = getopt(argc, argv, moco_options)) != -1) {
		switch(opt) {
		case 'D':
			dump = 1;
			break;

		case 'p':
			pass = 1;
			break;

		case 'm':
			source = optarg;
			break;

		case 'f':
			from = optarg;
			break;

		case 'r':
			rcpt = optarg;
			break;

		default:
			moco_general(opt, optarg);
			break;
		}
	}

	moco_init();

	if (dump)
	{
		return moco_greylist_dump();
	}

	if (pass && source && from && rcpt)
	{
		return moco_greylist_pass(source, from, rcpt);
	}

	return -1;
}

int
main(int argc, char **argv)
{
	char *scope;
	int (*callback)(int, char **);

	// Open log
	log_init(BINNAME, moco_debug, 0, 1);

	if (argc < 2)
	{
		moco_usage();
		return EX_USAGE;
	}

	scope = argv[1];
	argv += 1;
	argc -= 1; 

	// Get scope
	if (!strcmp(scope, "greylist"))
	{
		callback = moco_greylist;
	}
	else if (!strcmp(scope, "table"))
	{
		callback = moco_table;
	}
	else
	{
		moco_usage();
		return EX_USAGE;
	}

	// Execute scope callback
	if (callback(argc, argv))
	{
		moco_usage();
		return EX_USAGE;
	}

	// Cleanup
	if (moco_socket)
	{
		close(moco_socket);
	}

	if (moco_config)
	{
		cf_clear();
	}

	return 0;
}

