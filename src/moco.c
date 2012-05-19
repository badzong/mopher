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


static void
moco_usage(void)
{
	log_error("Usage: %s ACTION OPTIONS", BINNAME);
	log_error("Mopher control client.");
	log_error("");
	log_error("ACTIONS:");
	log_error("  greylist   Manipulate Greylist Database");
	log_error("");
	log_error("GREYLIST OPTIONS:");
	log_error("  -d         Dump mopher greylist database");
	log_error("  -p         Pass greylisting (requires -s, -f and -r )");
	log_error("  -m source  MTA source address or domain");
	log_error("  -f from    Envelope sender address");
	log_error("  -r rcpt    Envelope recipient address");
	log_error("");
	log_error("GENERAL OPTIONS:");
	log_error("  -c file    Use configuration file");
	log_error("  -s server  Connect to server");
	log_error("  -?         Show this message");
	log_error("");
	log_error("EXAMPLES:");
	log_error("");
	log_error("  Dump greylist");
	log_error("  %s greylist -d", BINNAME);
	log_error("");
	log_error("  Pass greylisting");
	log_error("  %s -p -s example.com -f bob@example.com -r me@mydomain.org", BINNAME);
	log_error("");
	exit(EX_USAGE);
}


static int
moco_greylist_dump(int sock)
{
	char *dump;
	int n;
	
	if(server_data_cmd(sock, "greylist_dump", &dump))
	{
		log_die(EX_SOFTWARE, "moco_greylist_dump: server_data_cmd "
		    "failed");
	}

	printf(dump);
	free(dump);

	return 0;
}


static int
moco_greylist_pass(int sock, char *source, char *from, char *rcpt)
{
	char cmd[BUFLEN];
	int n;
	
	n = snprintf(cmd, sizeof cmd, "greylist_pass %s %s %s", source, from,
	    rcpt);
	if (n >= sizeof cmd)
	{
		log_die(EX_SOFTWARE, "moco_greylist_dump: buffer exhausted");
	}

	if(server_cmd(sock, cmd))
	{
		log_die(EX_SOFTWARE, "moco_greylist_dump: server_cmd failed");
	}

	return 0;
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
	char *config = NULL;
	char *server = NULL;
	char *action;
	int sock;
	int r;

	/*
	 * Initialize log (foreground, stderr)
	 */
	log_init(BINNAME, LOG_ERR, 0, 1);

	while ((opt = getopt(argc, argv, "c:s:dpm:f:r:")) != -1) {
		switch(opt) {
		case 'c':
			config = optarg;
			break;

		case 's':
			server = optarg;
			break;

		case 'd':
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
			moco_usage();
		}
	}

	if (optind >= argc) {
		moco_usage();
	}

	action = argv[optind];

	/*
	 * Get server socket string
	 */
	if (server == NULL)
	{
		if (config)
		{
			cf_path(config);
		}
		cf_init();
		server = cf_server_socket;
	}
	if (server == NULL)
	{
		log_die(EX_CONFIG, "moco: server_socket not configured");
	}

	/*
	 * Connect Server
	 */
	sock = sock_connect(server);
	if (sock == -1)
	{
		log_die(EX_SOFTWARE, "moco: connection to %s failed", server);
	}

	if (!strcmp(action, "greylist"))
	{
		if ((!dump && !pass) || (pass && (!source || !from || !rcpt)))
		{
			moco_usage();
		}

		if (dump)
		{
			r = moco_greylist_dump(sock);
		}
		else if (pass)
		{
			r = moco_greylist_pass(sock, source, from, rcpt);
		}

		if (r == -1)
		{
			log_die(EX_SOFTWARE, "moco: greylist failed");
		}
	}

	close(sock);

	if (config)
	{
		cf_clear();
	}

	return 0;
}
