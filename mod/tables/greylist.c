#include <stdio.h>

#include "log.h"
#include "var.h"


static int
greylist_update(void)
{
	printf("greylist_update\n");

	return 0;
}

static int
greylist_cleanup(void)
{
	printf("greylist_cleanup\n");

	return 0;
}

int
init(void)
{
	var_t *schema;

	schema = var_schema_create(
		"hostaddr",	VT_ADDR,	VF_KEEPNAME | VF_KEY, 
		"envfrom",	VT_STRING,	VF_KEEPNAME | VF_KEY,
		"envrcpt",	VT_STRING,	VF_KEEPNAME | VF_KEY,
		"created",	VT_INT,		VF_KEEPNAME,
		"delay",	VT_INT,		VF_KEEPNAME,
		"retries",	VT_INT,		VF_KEEPNAME,
		"visa",		VT_INT,		VF_KEEPNAME,
		"delivered",	VT_INT,		VF_KEEPNAME,
		"valid",	VT_INT,		VF_KEEPNAME,
		NULL);

	if (schema == NULL) {
		log_warning("greylist: init: var_schema_create failed");
		return -1;
	}

	table_register("greylist", schema, greylist_update, greylist_cleanup);

	return 0;
}
