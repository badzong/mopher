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
static char  moctl_options[64] = "?hd:c:s:";
static int   moctl_socket;

static void
moctl_usage(void)
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
moctl_greylist_dump(void)
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
moctl_greylist_pass(char *source, char *from, char *rcpt)
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
moctl_general(char opt, char *optarg)
{
	switch(opt) {
	case 'd':
		moctl_debug = atoi(optarg);
		return 0;

	case 'c':
		moctl_config = optarg;
		return 0;

	case 's':
		moctl_server = optarg;
		return 0;

	default:
		moctl_usage();
	}

	return -1;
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
moctl_table(int argc, char **argv)
{
	char *dump;
	char cmd[BUFLEN];
	int n;
	int opt;
	char *tablename = NULL;

	// Append greylisting specific options
	strcat(moctl_options, "D:");
	
	while ((opt = getopt(argc, argv, moctl_options)) != -1) {
		switch(opt) {
		case 'D':
			tablename = optarg;
			break;

		default:
			moctl_general(opt, optarg);
			break;
		}
	}

	if (!tablename)
	{
		return -1;
	}

	moctl_init();

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


static int
moctl_greylist(int argc, char **argv)
{
	int opt;
	int pass = 0;
	int dump = 0;
	char *source = NULL;
	char *from = NULL;
	char *rcpt = NULL;

	// Append greylisting specific options
	strcat(moctl_options, "Dpm:f:r:");
	
	while ((opt = getopt(argc, argv, moctl_options)) != -1) {
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
			moctl_general(opt, optarg);
			break;
		}
	}

	moctl_init();

	if (dump)
	{
		return moctl_greylist_dump();
	}

	if (pass && source && from && rcpt)
	{
		return moctl_greylist_pass(source, from, rcpt);
	}

	return -1;
}

int
main(int argc, char **argv)
{
	char *scope;
	int (*callback)(int, char **);

	// Open log
	log_init(BINNAME, moctl_debug, 0, 1);

	if (argc < 2)
	{
		moctl_usage();
		return EX_USAGE;
	}

	scope = argv[1];
	argv += 1;
	argc -= 1; 

	// Get scope
	if (!strcmp(scope, "greylist"))
	{
		callback = moctl_greylist;
	}
	else if (!strcmp(scope, "table"))
	{
		callback = moctl_table;
	}
	else
	{
		moctl_usage();
		return EX_USAGE;
	}

	// Execute scope callback
	if (callback(argc, argv))
	{
		moctl_usage();
		return EX_USAGE;
	}

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

