#include <stdio.h>

#include "log.h"
#include "acl.h"
#include "modules.h"
#include "milter.h"

int
main(int argc, char **argv)
{
	int r;

	log_init("mopher");
	modules_init();

	r = milter();

	modules_clear();
	log_close();

	return r;
}
