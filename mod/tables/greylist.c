#include <stdio.h>

#include "log.h"
#include "var.h"
#include "table.h"


static int
greylist_validate(var_t *record)
{
	VAR_INT_T *created;
	VAR_INT_T *valid;

	if (var_list_dereference(record, NULL, NULL, NULL, &created, &valid,
		NULL, NULL, NULL, NULL)) {
		log_warning("greylist_valid: var_list_unpack failed");
		return -1;
	}

	if (table_cleanup_cycle > *created + *valid) {
		return 0;
	}

	return 1;
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
		"valid",	VT_INT,		VF_KEEPNAME,
		"delay",	VT_INT,		VF_KEEPNAME,
		"retries",	VT_INT,		VF_KEEPNAME,
		"visa",		VT_INT,		VF_KEEPNAME,
		"passed",	VT_INT,		VF_KEEPNAME,
		NULL);

	if (schema == NULL) {
		log_warning("greylist: init: var_schema_create failed");
		return -1;
	}

	/*
	 * greylist_valid need to be registered at table
	 * sql bulk del where
	 */

	if (table_register("greylist", schema, NULL, greylist_validate)) {
		log_warning("greylist: init: table_register failed");
		return -1;
	}

	return 0;
}
