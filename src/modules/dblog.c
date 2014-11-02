#include <config.h>
#include <time.h>
#include <mopher.h>

#define BUFLEN 8192
#define KEYLEN 128

static dbt_t dblog_dbt;

static int
dblog_dump_string(var_t *mailspec, char *key)
{
	char newkey[KEYLEN];
	char buffer[BUFLEN];
	var_t *item;

	item = vtable_lookup(mailspec, key);
	if (item == NULL)
	{
		log_debug("dblog_dump_string: item \"%s\" not found", key);
		return -1;
	}

	if (var_dump_data(item, buffer, sizeof buffer) == -1)
	{
		log_error("dblog_dump_string: var_dump_data failed");
		return -1;
	}

	snprintf(newkey, sizeof newkey, "%s_str", key);

	if (vtable_set_new(mailspec, VT_STRING, newkey, buffer,
		VF_COPY | VF_EXP_FREE) == -1)
	{
		log_error("dblog_dump_string: vtable_set_new failed");
		return -1;
	}

	return 0;
}

static int
dblog_update(milter_stage_t stage, acl_action_type_t at, var_t *mailspec)
{
	var_t *record = NULL;
	VAR_INT_T expire;

	if (stage != MS_CLOSE)
	{
		return 0;
	}

	dblog_dump_string(mailspec, "recipient_list");
	dblog_dump_string(mailspec, "spamd_symbols");
	dblog_dump_string(mailspec, "dnsbl");

	expire = time(NULL) + cf_dblog_expire;

	if (vtable_set_new(mailspec, VT_INT, "dblog_expire", &expire,
		VF_KEEPNAME | VF_COPYDATA))
	{
		log_error("dblog_update: vtable_set_new failed");
		return -1;
	}

	record = vlist_record_from_table(dblog_dbt.dbt_scheme, mailspec);
	if (record == NULL)
	{
		log_error("dblog_update: vlist_record_from_table failed");
		goto error;
	}

	if (dbt_db_set(&dblog_dbt, record))
	{
		log_error("dblog_udpate: dbt_db_set failed");
		goto error;
	}

	var_delete(record);

	return 0;

error:
	if(record)
	{
		var_delete(record);
	}

	return -1;
}


int
dblog_init(void)
{
	var_t *scheme;

	if (!cf_dblog)
	{
		return 0;
	}

	scheme = vlist_scheme("dblog",
		"milter_id",		VT_INT,		VF_KEEPNAME | VF_KEY,
		"received",		VT_INT,		VF_KEEPNAME,
		"hostaddr_str",		VT_STRING,	VF_KEEPNAME,
		"hostname",		VT_STRING,	VF_KEEPNAME,
		"helo",			VT_STRING,	VF_KEEPNAME,
		"greylist_src",		VT_STRING,	VF_KEEPNAME,
		"envfrom_addr",		VT_STRING,	VF_KEEPNAME,
		"envrcpt_addr",		VT_STRING,	VF_KEEPNAME,
		"recipients",		VT_INT,		VF_KEEPNAME,
		"recipient_list_str",	VT_STRING,	VF_KEEPNAME,
		"message_size",		VT_INT,		VF_KEEPNAME,
		"queueid",		VT_STRING,	VF_KEEPNAME,
		"message_id",		VT_STRING,	VF_KEEPNAME,
		"acl_matched",		VT_INT,		VF_KEEPNAME,
		"acl_stage_matched",	VT_INT,		VF_KEEPNAME,
		"acl_rule",		VT_INT,		VF_KEEPNAME,
		"acl_line",		VT_INT,		VF_KEEPNAME,
		"acl_response",		VT_STRING,	VF_KEEPNAME,
		"greylist_delayed",	VT_INT,		VF_KEEPNAME,
		"greylist_created",	VT_INT,		VF_KEEPNAME,
		"greylist_expire",	VT_INT,		VF_KEEPNAME,
		"greylist_connections",	VT_INT,		VF_KEEPNAME,
		"greylist_deadline",	VT_INT,		VF_KEEPNAME,
		"greylist_delay",	VT_INT,		VF_KEEPNAME,
		"greylist_attempts",	VT_INT,		VF_KEEPNAME,
		"greylist_defer",	VT_INT,		VF_KEEPNAME,
		"greylist_visa",	VT_INT,		VF_KEEPNAME,
		"greylist_passed",	VT_INT,		VF_KEEPNAME,
		"tarpit_delayed",	VT_INT,		VF_KEEPNAME,
		"counter_relay",	VT_INT,		VF_KEEPNAME,
		"counter_penpal",	VT_INT,		VF_KEEPNAME,
		"spf",			VT_STRING,	VF_KEEPNAME,
		"spf_reason",		VT_STRING,	VF_KEEPNAME,
		"spamd_spam",		VT_INT,		VF_KEEPNAME,
		"spamd_score",		VT_FLOAT,	VF_KEEPNAME,
		"spamd_symbols_str",	VT_STRING,	VF_KEEPNAME,
		"dnsbl_str",		VT_STRING,	VF_KEEPNAME,
		"dblog_expire",		VT_INT,		VF_KEEPNAME,
		NULL);

	if (scheme == NULL)
	{
		log_die(EX_SOFTWARE, "counter_init: vlist_scheme failed");
	}

	dblog_dbt.dbt_scheme		= scheme;
	dblog_dbt.dbt_validate		= dbt_common_validate;
	dblog_dbt.dbt_sql_invalid_where	= DBT_COMMON_INVALID_SQL;

	dbt_register("dblog", &dblog_dbt);

	acl_update_callback(dblog_update);

	return 0;
}
