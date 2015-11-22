#include <config.h>

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mopher.h>


#define KEYLEN 128


typedef int (*counter_add_t)(dbt_t *dbt, var_t *mailspec);

static dbt_t counter_relay;
static dbt_t counter_origin;
static dbt_t counter_penpal;


static int
counter_lookup(milter_stage_t stage, char *name, var_t *mailspec)
{
	dbt_t *dbt;
	VAR_INT_T *recipients;
	int lookup_penpal, lookup_origin;

	log_message(LOG_DEBUG, mailspec, "counter_lookup: %s", name);

	lookup_penpal = strcmp(name, "counter_penpal") == 0;
	lookup_origin = strcmp(name, "counter_origin") == 0;

	/*
	 * hostaddr_str is not set. See milter_connect for details.
	 */
	if (vtable_is_null(mailspec, "hostaddr_str"))
	{
		log_debug("counter_lookup: hostaddr_str is NULL");

		if (vtable_set_null(mailspec, "counter_relay", VF_KEEPNAME) ||
		    vtable_set_null(mailspec, "counter_penpal", VF_KEEPNAME))
		{
			log_error("counter_lookup: vtable_set_null failed");
			return -1;
		}

		return 0;
	}

	if (lookup_penpal)
	{
		dbt = &counter_penpal;

		/*
		 * Penpal symbols are ambiguous for multi recipient messages
		 * in stages other than MS_ENVRCPT.
		 */
		if (stage == MS_ENVRCPT)
		{
			goto load;
		}

		recipients = vtable_get(mailspec, "recipients");
		if (recipients == NULL)
		{
			log_error("counter_lookup: vtable_get failed");
			return -1;
		}

		if (*recipients == 1)
		{
			goto load;
		}

		log_error("counter_lookup: message has %ld recipients: symbol "
		    "\"%s\" ambiguous", *recipients, name);

		if (vtable_set_new(mailspec, VT_INT, name, NULL, VF_COPYNAME))
		{
			log_error("counter_lookup: vtable_set_new failed");
			return -1;
		}
	}
	else if (lookup_origin)
	{
		dbt = &counter_origin;
	}
	else
	{
		dbt = &counter_relay;
	}


load:

	if (dbt_db_load_into_table(dbt, mailspec))
	{
		log_error( "counter_lookup: dbt_db_load_into_table failed");
		return -1;
	}

	return 0;
}


static int
counter_add_relay_or_origin(char *key_symbol, dbt_t *dbt, var_t *mailspec)
{
	var_t *record;
	char *key;
	VAR_INT_T *received;
	VAR_INT_T created, updated, expire, count;

	if (vtable_dereference(mailspec, key_symbol, &key, "received",
		&received, NULL) != 2)
	{
		log_error("counter_add_penpal: vtable_dereference failed");
		return -1;
	}

	created = *received;
	updated = *received;
	expire  = *received + cf_counter_expire_low;
	count   = 1;

	record = vlist_record(dbt->dbt_scheme, key, &created, &updated,
		&expire, &count);


	if (record == NULL) {
		log_warning("counter_add_penpal: vlist_record failed");
		return -1;
	}

	if (dbt_db_set(dbt, record))
	{
		log_error("counter_add_penpal: dbt_db_set failed");
		var_delete(record);
		return -1;
	}

	log_debug("counter_add_relay: record saved");

	var_delete(record);
	
	return 0;
}


static int
counter_add_relay(dbt_t *dbt, var_t *mailspec)
{
	return counter_add_relay_or_origin("hostaddr_str", dbt, mailspec);
}


static int
counter_add_origin(dbt_t *dbt, var_t *mailspec)
{
	return counter_add_relay_or_origin("origin", dbt, mailspec);
}


static int
counter_add_penpal(dbt_t *dbt, var_t *mailspec)
{
	var_t *record;
	char *origin;
	char *envfrom;
	char *envrcpt;
	VAR_INT_T *received;
	VAR_INT_T created, updated, expire, count;

	if (vtable_dereference(mailspec, "origin", &origin,
	    "envfrom_addr", &envfrom, "envrcpt_addr", &envrcpt,
	    "received", &received, NULL) != 4)
	{
		log_error("counter_add_penpal: vtable_dereference failed");
		return -1;
	}

	created = *received;
	updated = *received;
	expire  = *received + cf_counter_expire_low;
	count   = 1;

	record = vlist_record(dbt->dbt_scheme, origin, envfrom, envrcpt,
	    &created, &updated, &expire, &count);

	if (record == NULL) {
		log_warning("counter_add_penpal: vlist_record failed");
		return -1;
	}

	if (dbt_db_set(dbt, record))
	{
		log_error("counter_add_penpal: dbt_db_set failed");
		var_delete(record);
		return -1;
	}

	log_debug("counter_add_penpal: record saved");

	var_delete(record);
	
	return 0;
}


static int
counter_update_record(dbt_t *dbt, char *prefix, var_t *mailspec, counter_add_t add)
{
	var_t *record = NULL;
	VAR_INT_T *updated, *expire, *count, *received, r;
	char updated_key[KEYLEN];
	char expire_key[KEYLEN];

	if (vlist_record_keys_missing(dbt->dbt_scheme, mailspec))
	{
		log_debug("counter_update_record: required keys for "
		    "dbt_db_get_from_table() missing");
		return 0;
	}

	r  = snprintf(updated_key, sizeof updated_key, "%s_updated", prefix)
	    >= sizeof updated_key;
	r |= snprintf(expire_key, sizeof expire_key, "%s_expire", prefix)
	    >= sizeof expire_key;

	if (r)
	{
		log_error("counter_update_record: buffer exhausted");
		goto error;
	}

	if (dbt_db_get_from_table(dbt, mailspec, &record))
	{
		log_error( "counter_update_record: dbt_db_get_from_table failed");
		goto error;
	}

	if (record == NULL)
	{
		log_info("counter_update_record: create new record in %s",
		    prefix);
		return add(dbt, mailspec);
	}

	received = vtable_get(mailspec, "received");
	if (received == NULL)
	{
		log_error("counter_update_record: milter_received not set");
		goto error;
	}

	updated	= vlist_record_get(record, updated_key);
	expire	= vlist_record_get(record, expire_key);
	count	= vlist_record_get(record, prefix);

	log_message(LOG_NOTICE, mailspec, "counter: %s=%ld", prefix, *count);

	if (updated == NULL || expire == NULL || count == NULL)
	{
		log_error("counter_update_record: vlist_record_get failed");
		goto error;
	}

	*updated = *received;
	++(*count);

	if (*count > cf_counter_threshold)
	{
		*expire = *received + cf_counter_expire_high;
	}
	else
	{
		*expire = *received + cf_counter_expire_low;
	}

	/*
	 * Return value used for logging
	 */
	r = *count;

	if (dbt_db_set(dbt, record))
	{
		log_error("counter_update_record: dbt_db_set failed");
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
counter_update(milter_stage_t stage, acl_action_type_t at, var_t *mailspec)
{
	int count;
	VAR_INT_T *action;
	VAR_INT_T *laststage;

	if (stage != MS_CLOSE)
	{
		return 0;
	}

	/*
	 * hostaddr_str is not set. See milter_connect for details.
	 */
	if (vtable_is_null(mailspec, "hostaddr_str"))
	{
		log_debug("counter_update: hostaddr_str is NULL");
		return 0;
	}

	if (vtable_dereference(mailspec, "action", &action,
	    "laststage", &laststage, NULL) != 2)
	{
		log_error("counter_update: vtable_dereference failed");
		return -1;
	}

	/*
	 * Action needs to be ACCEPT in any stage or CONTINUE or NONE at EOM
	 */
	switch(*action)
	{
	case ACL_ACCEPT:
		break;

	case ACL_CONTINUE:
	case ACL_NONE:
		if (*laststage == MS_EOM)
		{
			break;
		}

	default:
		return 0;
	}

	count = counter_update_record(&counter_relay, "counter_relay",
	    mailspec, counter_add_relay);
	if (count == -1)
	{
		log_error("counter_update: counter_update_record failed");
		return -1;
	}

	count = counter_update_record(&counter_origin, "counter_origin",
	    mailspec, counter_add_origin);
	if (count == -1)
	{
		log_error("counter_update: counter_update_record failed");
		return -1;
	}

	count = counter_update_record(&counter_penpal, "counter_penpal", mailspec,
	    counter_add_penpal);
	if (count == -1)
	{
		log_error("counter_update: counter_update_record failed");
		return -1;
	}

	return 0;
}


int
counter_init(void)
{
	var_t *relay_scheme;
	var_t *origin_scheme;
	var_t *penpal_scheme;

	relay_scheme = vlist_scheme("counter_relay",
		"hostaddr_str",			VT_STRING,	VF_KEEPNAME | VF_KEY,
		"counter_relay_created",	VT_INT,		VF_KEEPNAME,
		"counter_relay_updated",	VT_INT,		VF_KEEPNAME,
		"counter_relay_expire",		VT_INT,		VF_KEEPNAME,
		"counter_relay",		VT_INT,		VF_KEEPNAME,
		NULL);

	origin_scheme = vlist_scheme("counter_origin",
		"origin",			VT_STRING,	VF_KEEPNAME | VF_KEY,
		"counter_origin_created",	VT_INT,		VF_KEEPNAME,
		"counter_origin_updated",	VT_INT,		VF_KEEPNAME,
		"counter_origin_expire",	VT_INT,		VF_KEEPNAME,
		"counter_origin",		VT_INT,		VF_KEEPNAME,
		NULL);

	penpal_scheme = vlist_scheme("counter_penpal",
		"origin",			VT_STRING,	VF_KEEPNAME | VF_KEY,
		"envfrom_addr",			VT_STRING,	VF_KEEPNAME | VF_KEY,
		"envrcpt_addr",			VT_STRING,	VF_KEEPNAME | VF_KEY,
		"counter_penpal_created",	VT_INT,		VF_KEEPNAME,
		"counter_penpal_updated",	VT_INT,		VF_KEEPNAME,
		"counter_penpal_expire",	VT_INT,		VF_KEEPNAME,
		"counter_penpal",		VT_INT,		VF_KEEPNAME,
		NULL);

	if (relay_scheme == NULL || origin_scheme == NULL || penpal_scheme == NULL)
	{
		log_die(EX_SOFTWARE, "counter_init: vlist_scheme failed");
	}

	counter_relay.dbt_scheme			= relay_scheme;
	counter_relay.dbt_validate			= dbt_common_validate;
	if (dbt_register("counter_relay", &counter_relay))
	{
		log_die(EX_SOFTWARE, "counter_init: dbt_register failed");
	}

	counter_origin.dbt_scheme			= origin_scheme;
	counter_origin.dbt_validate			= dbt_common_validate;
	if (dbt_register("counter_origin", &counter_origin))
	{
		log_die(EX_SOFTWARE, "counter_init: dbt_register failed");
	}

	counter_penpal.dbt_scheme			= penpal_scheme;
	counter_penpal.dbt_validate			= dbt_common_validate;
	if(dbt_register("counter_penpal", &counter_penpal))
	{
		log_die(EX_SOFTWARE, "counter_init: dbt_register failed");
	}

	acl_symbol_register("counter_relay", MS_ANY, counter_lookup, AS_CACHE);
	acl_symbol_register("counter_origin", MS_ANY, counter_lookup, AS_CACHE);

	/*
	 * counter penpal is not cached due to abiguity in multi recipient
	 * messages.
	 */
	acl_symbol_register("counter_penpal", MS_OFF_ENVRCPT, counter_lookup,
	    AS_NOCACHE);

	acl_update_callback(counter_update);

	return 0;
}
