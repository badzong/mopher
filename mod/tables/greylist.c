#include <stdio.h>

#include "mopher.h"

static dbt_t greylist_dbt;

static int
greylist_validate(dbt_t *dbt, var_t *record)
{
	VAR_INT_T *created;
	VAR_INT_T *valid;

	if (var_list_dereference(record, NULL, NULL, NULL, &created, &valid,
		NULL, NULL, NULL, NULL)) {
		log_warning("greylist_valid: var_list_unpack failed");
		return -1;
	}

	/*
	 * dbt->dbt_cleanup_schedule == time(NULL)
	 */
	if (dbt->dbt_cleanup_schedule > *created + *valid) {
		return 0;
	}

	return 1;
}


int
init(void)
{
	var_t *scheme;

	scheme = var_scheme_create(
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

	if (scheme == NULL) {
		log_warning("greylist: init: var_scheme_create failed");
		return -1;
	}

	greylist_dbt.dbt_name = "greylist";
	greylist_dbt.dbt_scheme = scheme;
	greylist_dbt.dbt_validate = (dbt_validate_t) greylist_validate;
	greylist_dbt.dbt_sql_invalid_where =
		"`valid` + `created` < unix_timestamp()";

	/*
	 * greylist_valid need to be registered at table
	 * sql bulk del where
	 */

	dbt_register(&greylist_dbt);

	return 0;
}


void
fini(void)
{
	var_delete(greylist_dbt.dbt_scheme);

	return;
}
