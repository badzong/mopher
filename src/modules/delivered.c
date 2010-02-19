#include <config.h>

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mopher.h>


#define KEYLEN 128


typedef int (*delivered_add_t)(dbt_t *dbt, var_t *mailspec);

static dbt_t delivered_relay;
static dbt_t delivered_penpal;


static int
delivered_lookup(milter_stage_t stage, char *name, var_t *mailspec)
{
	dbt_t *dbt;
	VAR_INT_T *recipients;
	char prefix[] = "delivered_penpal";

	if (strncmp(name, prefix, sizeof prefix - 1) == 0)
	{
		dbt = &delivered_penpal;

		/*
		 * Penpal symbols are ambiguous for multi recipient messages
		 * in stages other than MS_ENVRCPT.
		 */
		if (stage == MS_ENVRCPT)
		{
			goto load;
		}

		recipients = vtable_get(mailspec, "milter_recipients");
		if (recipients == NULL)
		{
			log_error("delivered_lookup: vtable_get failed");
			return -1;
		}

		if (*recipients == 1)
		{
			goto load;
		}

		log_error("delivered_lookup: message has %ld recipients: "
		    "symbol \"%s\" abiguous", *recipients, name);

		if (vtable_set_new(mailspec, VT_INT, name, NULL, VF_COPYNAME))
		{
			log_error("delivered_lookup: vtable_set_new failed");
			return -1;
		}
	}
	else
	{
		dbt = &delivered_relay;
	}


load:

	if (dbt_db_load_into_table(dbt, mailspec))
	{
		log_error(
		    "delivered_lookup: dbt_db_load_into_table failed");
		return -1;
	}

	return 0;
}


static int
delivered_add_relay(dbt_t *dbt, var_t *mailspec)
{
	var_t *record;
	void *hostaddr;
	VAR_INT_T *received;
	VAR_INT_T created, updated, valid, count;

	if (vtable_dereference(mailspec, "milter_hostaddr", &hostaddr,
	    "milter_received", &received, NULL) != 2)
	{
		log_error("delivered_add_penpal: vtable_dereference failed");
		return -1;
	}

	created = *received;
	updated = *received;
	valid   = cf_delivered_valid;
	count   = 1;

	record = vlist_record(dbt->dbt_scheme, hostaddr, &created, &updated,
	    &valid, &count);

	if (record == NULL) {
		log_warning("delivered_add_penpal: vlist_record failed");
		return -1;
	}

	if (dbt_db_set(dbt, record))
	{
		log_error("delivered_add_penpal: dbt_db_set failed");
		var_delete(record);
		return -1;
	}

	var_delete(record);
	
	return 0;
}


static int
delivered_add_penpal(dbt_t *dbt, var_t *mailspec)
{
	var_t *record;
	void *hostaddr;
	char *envfrom;
	char *envrcpt;
	VAR_INT_T *received;
	VAR_INT_T created, updated, valid, count;

	if (vtable_dereference(mailspec, "milter_hostaddr", &hostaddr,
	    "milter_envfrom", &envfrom, "milter_envrcpt", &envrcpt,
	    "milter_received", &received, NULL) != 4)
	{
		log_error("delivered_add_penpal: vtable_dereference failed");
		return -1;
	}

	created = *received;
	updated = *received;
	valid   = cf_delivered_valid;
	count   = 1;

	record = vlist_record(dbt->dbt_scheme, hostaddr, envfrom, envrcpt,
	    &created, &updated, &valid, &count);

	if (record == NULL) {
		log_warning("delivered_add_penpal: vlist_record failed");
		return -1;
	}

	if (dbt_db_set(dbt, record))
	{
		log_error("delivered_add_penpal: dbt_db_set failed");
		var_delete(record);
		return -1;
	}

	var_delete(record);
	
	return 0;
}


static int
delivered_update_record(dbt_t *dbt, char *prefix, var_t *mailspec,
    delivered_add_t add)
{
	var_t *record = NULL;
	VAR_INT_T *updated, *count, *received, r;
	char updated_key[KEYLEN];

	if (snprintf(updated_key, sizeof updated_key, "%s_updated", prefix)
	    >= sizeof updated_key)
	{
		log_error("delivered_update_record: buffer exhausted");
		goto error;
	}

	if (dbt_db_get_from_table(dbt, mailspec, &record))
	{
		log_error(
		    "delivered_update_record: dbt_db_get_from_table failed");
		goto error;
	}

	if (record == NULL)
	{
		log_info("delivered_update_record: create new record ");
		return add(dbt, mailspec);
	}

	received = vtable_get(mailspec, "milter_received");
	if (received == NULL)
	{
		log_error("delivered_update_record: milter_received not set");
		goto error;
	}

	updated	= vlist_record_get(record, updated_key);
	count	= vlist_record_get(record, prefix);

	if (updated == NULL || count == NULL)
	{
		log_error("delivered_update_record: vlist_record_get failed");
		goto error;
	}

	*updated = *received;
	*count += 1;

	/*
	 * Return value used for logging
	 */
	r = *count;

	if (dbt_db_set(dbt, record))
	{
		log_error("delivered_update_record: dbt_db_set failed");
		goto error;
	}

	var_delete(record);

	return r;

error:

	if (record)
	{
		var_delete(record);
	}

	return -1;
}


static int
delivered_update(milter_stage_t stage, acl_action_type_t at, var_t *mailspec)
{
	int count;
	VAR_INT_T *action;
	VAR_INT_T *laststage;

	if (stage != MS_CLOSE)
	{
		return 0;
	}

	if (vtable_dereference(mailspec, "milter_action", &action,
	    "milter_laststage", &laststage, NULL) != 2)
	{
		log_error("delivered_update: vtable_dereference failed");
		return -1;
	}

	/*
	 * Action needs to be ACCEPT in any stage or CONTINUE at EOM
	 */
	if (!(*action == ACL_ACCEPT ||
	    (*laststage == MS_EOM && *action == ACL_CONTINUE)))
	{
		return 0;
	}

	count = delivered_update_record(&delivered_relay, "delivered_relay",
	    mailspec, delivered_add_relay);

	if (count == -1)
	{
		log_error("delivered_update: delivered_update_record failed");
		return -1;
	}

	log_debug("delivered_update: relay seen %d times", count);

	count = delivered_update_record(&delivered_penpal, "delivered_penpal",
	    mailspec, delivered_add_penpal);

	if (count == -1)
	{
		log_error("delivered_update: delivered_update_record failed");
		return -1;
	}

	log_debug("delivered_update: penpal seen %d times", count);

	return 0;
}


int
delivered_init(void)
{
	var_t *relay_scheme;
	var_t *penpal_scheme;

	relay_scheme = vlist_scheme("delivered_relay",
		"milter_hostaddr",	VT_ADDR,	VF_KEEPNAME | VF_KEY,
		"delivered_relay_created",	VT_INT,		VF_KEEPNAME,
		"delivered_relay_updated",	VT_INT,		VF_KEEPNAME,
		"delivered_relay_valid",	VT_INT,		VF_KEEPNAME,
		"delivered_relay",		VT_INT,		VF_KEEPNAME,
		NULL);

	penpal_scheme = vlist_scheme("delivered_penpal",
		"milter_hostaddr",	VT_ADDR,	VF_KEEPNAME | VF_KEY,
		"milter_envfrom",	VT_STRING,	VF_KEEPNAME | VF_KEY,
		"milter_envrcpt",	VT_STRING,	VF_KEEPNAME | VF_KEY,
		"delivered_penpal_created",	VT_INT,		VF_KEEPNAME,
		"delivered_penpal_updated",	VT_INT,		VF_KEEPNAME,
		"delivered_penpal_valid",	VT_INT,		VF_KEEPNAME,
		"delivered_penpal",		VT_INT,		VF_KEEPNAME,
		NULL);

	if (relay_scheme == NULL ||
	    penpal_scheme == NULL)
	{
		log_die(EX_SOFTWARE, "delivered_init: vlist_scheme failed");
	}

	delivered_relay.dbt_scheme		= relay_scheme;
	delivered_relay.dbt_validate		= dbt_common_validate;
	delivered_relay.dbt_sql_invalid_where	= DBT_COMMON_INVALID_SQL;

	delivered_penpal.dbt_scheme		= penpal_scheme;
	delivered_penpal.dbt_validate		= dbt_common_validate;
	delivered_penpal.dbt_sql_invalid_where	= DBT_COMMON_INVALID_SQL;

	dbt_register("delivered_relay", &delivered_relay);
	dbt_register("delivered_penpal", &delivered_penpal);

	acl_symbol_register("delivered_relay", MS_ANY, delivered_lookup,
	    AS_CACHE);

	/*
	 * delivered penpal is not cached due to abiguity in multi recipient
	 * messages.
	 */
	acl_symbol_register("delivered_penpal", MS_OFF_ENVRCPT,
	    delivered_lookup, AS_NOCACHE);

	acl_update_callback(delivered_update);

	return 0;
}
