#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <sysexits.h>
#define _GNU_SOURCE
#include <getopt.h>

#include "ll.h"

/*
 * Macros
 */
#define err(...) fprintf(stderr, __VA_ARGS__)
#define die(...) fprintf(stderr, __VA_ARGS__); _exit(EX_OSERR)

/*
 * Global configuration variables
 */

char *cf_file = "/etc/listkeeper";
int cf_log_level = 15;
int cf_foreground = 1;

ll_t *cf_master_processes;

char *cf_judge_socket = "unix:/tmp/lk/judge.sock";
int cf_judge_dump_interval = 600;

char *cf_acl_path = "default.acl";

int cf_greylist_valid = 8 * 60 * 60;

char *cf_milter_socket = "unix:/var/spool/postfix/milter.sock";
ll_t *cf_milter_dnsrbl;
char *cf_milter_spamd_socket = "inet:127.0.0.1:783";
int cf_milter_socket_timeout = 60;

int cf_greylist_default_delay = 3600;
int cf_greylist_default_visa = 86400;
int cf_greylist_default_valid = 0;

int cf_greylist_dynamic_factor = 1800;
int cf_greylist_dynamic_divisor = 1;

char *cf_acl_mod_path = "mod/acl";

/*
 * cf.c globals
 */

static struct option cf_options[] = {
	{"log-level", 1, NULL, 'l'},
	{"config", 1, NULL, 'c'},
	{"foreground", 0, NULL, 'f'}
};

static void
cf_defaults(void)
{
	if ((cf_master_processes = ll_create()) == NULL) {
		die("cf_defaults: ll_create failed\n");
	}
	/*
	 * ll_insert has to be called with strdup as ll_delete will be called with
	 * free. See cf_clear below.
	 */
	LL_INSERT(cf_master_processes, strdup("judge/judge"));
	LL_INSERT(cf_master_processes, strdup("milter/milter"));

	if ((cf_milter_dnsrbl = ll_create()) == NULL) {
		die("cf_defaults: ll_create failed\n");
	}
	LL_INSERT(cf_milter_dnsrbl, strdup("zen.spamhaus.org"));
	LL_INSERT(cf_milter_dnsrbl, strdup("bl.spamcop.net"));

	return;
}

static void
cf_load(void)
{
	err("cf_load: not implemented yet.\n");

	return;
}

void
cf_clear(void)
{
	ll_delete(cf_master_processes, free);
	ll_delete(cf_milter_dnsrbl, free);

	return;
}

void
cf_init(int argc, char **argv)
{
	int foreground = 0;
	int log_level = 0;
	int index = 0;
	int c;

	for (;;) {

		if ((c =
		     getopt_long(argc, argv, "c:fl:", cf_options,
				 &index)) == -1) {
			break;
		}

		switch (c) {

		case 'c':
			cf_file = optarg;
			break;

		case 'f':
			foreground = 1;
			break;

		case 'l':
			log_level = atoi(optarg);
			break;

		default:
			err("Usage: %s [options]\n", basename(argv[0]));
			err("  -c, --config FILE\tLoad configuration from FILE\n");
			err("  -f, --foreground\tDon't fork into background\n");
			err("  -l, --log-level LEVEL\tSet log-level to LEVEL\n");
			_exit(EX_CONFIG);
		}
	}

	cf_defaults();
	cf_load();

	if (foreground) {
		cf_foreground = foreground;
	}

	if (log_level) {
		cf_log_level = log_level;
	}

	return;
}
