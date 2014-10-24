#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include <mopher.h>

#undef BINNAME
#define BINNAME "mopherctl"

#define BUFLEN 4096

static int   moctl_debug = LOG_ERR;
static char *moctl_config;
static char *moctl_server;
static int   moctl_socket;

typedef struct moctl_command {
	char	*mc_cmdline;
	char	*mc_srvcmd;
	int	 mc_args;
	int	 mc_data;
} moctl_command_t;

static moctl_command_t moctl_commands[] = {
	{"dump", "table_dump", 2, 1},
	{"greylist dump", "greylist_dump", 2, 1},
	{"greylist pass", "greylist_pass", 5, 0},
	{NULL, NULL, 0}
};

static void
moctl_usage(void)
{
	log_error("Usage: mopherctl [-h] [-d level] [-c file] [-s host] command");
	log_error("");
	log_error("mopherctl controls the mopher daemon.");
	log_error("");
	log_error("Available options are:");
	log_error("");
	log_error("-h      Show usage message.");
	log_error("");
	log_error("-d level");
	log_error("        Set debug severity level to level");
	log_error("");
	log_error("-c file");
	log_error("        Read configuration from file.");
	log_error("");
	log_error("-s host");
	log_error("        Connect to host.");
	log_error("");
	log_error("Available commands are:");
	log_error("");
	log_error("dump <table>");
	log_error("        Print raw content of table.");
	log_error("");
	log_error("greylist dump");
	log_error("        Print formatted content of the greylist-table.");
	log_error("");
	log_error("greylist pass <origin> <from> <rcpt>");
	log_error("        Temporarily whitelist triplet.");
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

int
main(int argc, char **argv)
{
	int opt;
	int i;
	int len;
	char cmdline[BUFLEN];
	char srvcmd[BUFLEN];
	moctl_command_t *cmd;
	int index;
	int args;
	int r;
	char *dump = NULL;

	memset(cmdline, 0, sizeof cmdline);
	memset(srvcmd, 0, sizeof srvcmd);

	// Open log
	log_init(BINNAME, moctl_debug, 0, 1);

	// No command supplied
	if (argc < 2)
	{
		moctl_usage();
	}

	while ((opt = getopt(argc, argv, "?hd:c:s:")) != -1) {
		switch(opt) {
		case '?':
		case 'h':
			moctl_usage();
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

		default:
			moctl_usage();
			break;
		}
	}

	// Create command string
	for (i = optind; i < argc; ++i)
	{
		len = strlen(cmdline);
		if (len)
		{
			strncat(cmdline, " ", sizeof cmdline - len - 1);
		}

		len = strlen(cmdline);
		strncat(cmdline, argv[i], sizeof cmdline - len - 1);
	}
	args = i - 1;

	// Translate argv into server command
	for (cmd = moctl_commands; cmd->mc_cmdline != NULL; ++cmd)
	{
		index = strlen(cmd->mc_cmdline);
		if(strncmp(cmdline, cmd->mc_cmdline, index))
		{
			continue;
		}

		if (args != cmd->mc_args)
		{
			moctl_usage();
		}

		snprintf(srvcmd, sizeof srvcmd, "%s%s", cmd->mc_srvcmd,
			cmdline + index);
		break;
	}

	if (strlen(srvcmd) == 0)
	{
		moctl_usage();
	}

	moctl_init();

	if (cmd->mc_data)
	{
		r = server_data_cmd(moctl_socket, srvcmd, &dump);
	}
	else
	{
		r = server_cmd(moctl_socket, srvcmd);
	}

	if (r)
	{
		log_die(EX_SOFTWARE, "moctl_table_dump: server command failed");
	}

	if (dump)
	{
		printf(dump);
		free(dump);
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
