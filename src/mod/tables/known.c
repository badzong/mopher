#include "config.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#include "mopher.h"

static dbt_t known_relay_dbt;
static dbt_t known_sender_dbt;
static dbt_t known_penpal_dbt;

static int
known_update(milter_stage_t stage, acl_action_type_t at, var_t *mailspec)
{
	if (stage != MS_EOM)
	{
		return 0;
	}

	switch (at)
	{
	case ACL_ACCEPT:
	case ACL_CONTINUE:
		break;
	
	default:
		return 0;
	}

	printf("KNOWN UPDATE");
	// HERE!!!
	
	return 0;
}


int
known_init(void)
{
	var_t *relay_scheme;
	var_t *sender_scheme;
	var_t *penpal_scheme;

	relay_scheme = vlist_scheme("known_relay",
		"hostaddr",	VT_ADDR,	VF_KEEPNAME | VF_KEY,
		"created",	VT_INT,		VF_KEEPNAME,
		"updated",	VT_INT,		VF_KEEPNAME,
		"valid",	VT_INT,		VF_KEEPNAME,
		"count",	VT_INT,		VF_KEEPNAME,
		NULL);

	sender_scheme = vlist_scheme("known_sender",
		"envfrom",	VT_STRING,	VF_KEEPNAME | VF_KEY,
		"created",	VT_INT,		VF_KEEPNAME,
		"updated",	VT_INT,		VF_KEEPNAME,
		"valid",	VT_INT,		VF_KEEPNAME,
		"count",	VT_INT,		VF_KEEPNAME,
		NULL);

	penpal_scheme = vlist_scheme("known_penpal",
		"envfrom",	VT_STRING,	VF_KEEPNAME | VF_KEY,
		"envrcpt",	VT_STRING,	VF_KEEPNAME | VF_KEY,
		"created",	VT_INT,		VF_KEEPNAME,
		"updated",	VT_INT,		VF_KEEPNAME,
		"valid",	VT_INT,		VF_KEEPNAME,
		"count",	VT_INT,		VF_KEEPNAME,
		NULL);

	if (relay_scheme == NULL ||
	    sender_scheme == NULL ||
	    penpal_scheme == NULL)
	{
		log_die(EX_SOFTWARE, "penpal_init: vlist_scheme failed");
	}

	known_relay_dbt.dbt_scheme		= relay_scheme;
	known_relay_dbt.dbt_validate		= dbt_common_validate;
	known_relay_dbt.dbt_sql_invalid_where	= DBT_COMMON_INVALID_SQL;

	known_sender_dbt.dbt_scheme		= sender_scheme;
	known_sender_dbt.dbt_validate		= dbt_common_validate;
	known_sender_dbt.dbt_sql_invalid_where	= DBT_COMMON_INVALID_SQL;

	known_penpal_dbt.dbt_scheme		= penpal_scheme;
	known_penpal_dbt.dbt_validate		= dbt_common_validate;
	known_penpal_dbt.dbt_sql_invalid_where	= DBT_COMMON_INVALID_SQL;

	dbt_register("known_relay", &known_relay_dbt);
	dbt_register("known_sender", &known_sender_dbt);
	dbt_register("known_penpal", &known_penpal_dbt);

	acl_update_callback(known_update);

	return 0;
}
