#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "log.h"
#include "acl.h"
#include "modules.h"
#include "milter.h"
#include "cf.h"

int
main(int argc, char **argv)
{
	int r, opt, foreground, loglevel;
	char *config;

	foreground = 0;
	loglevel = LOG_WARNING;
	config = "/etc/mopher.conf";

	while ((opt = getopt(argc, argv, "fd:c:")) != -1) {
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
		}
	}

	log_init("mopher", loglevel, foreground);

	cf_init(config);

	modules_init();

	r = milter();

	modules_clear();

	cf_clear();

	log_close();

	return r;
}
