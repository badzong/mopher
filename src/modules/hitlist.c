#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <config.h>
#include <mopher.h>

#define BUCKETS 64
#define HITLIST_NAME "hitlist"
#define BUFLEN 1024

#define VALUE_SUFFIX "_value"
#define EXPIRE_SUFFIX "_expire"

static sht_t *hitlists;

typedef struct hitlist {
	pthread_mutex_t  hl_mutex;
	char		*hl_name;
	int		 hl_connected;
	dbt_t		 hl_dbt;
	ll_t		*hl_keys;
	VAR_INT_T	 hl_create;
	VAR_INT_T	 hl_update;
	VAR_INT_T	 hl_count;
	VAR_INT_T	 hl_timeout;
	VAR_INT_T	 hl_extend;
	VAR_INT_T	 hl_cleanup;
        char		*hl_sum;
        char		*hl_table;
        char		 hl_value_field[DBT_FIELD_MAX + 1];
        char		 hl_expire_field[DBT_FIELD_MAX + 1];
} hitlist_t;
	

static hitlist_t *
hitlist_create(char *name, ll_t *keys, VAR_INT_T *create, VAR_INT_T *update,
    VAR_INT_T *count, VAR_INT_T *timeout, VAR_INT_T *extend,
    VAR_INT_T *cleanup, char *sum, char *table, char *value_field, char *expire_field)
{
	hitlist_t *hl;

	hl = (hitlist_t *) malloc(sizeof(hitlist_t));
	if (hl == NULL)
	{
		log_sys_error("hitlist_create: malloc");
		return NULL;
	}

	memset(hl, 0, sizeof(hitlist_t));

	if (pthread_mutex_init(&hl->hl_mutex, NULL))
	{
		log_sys_error("hitlist_create: pthread_mutex_lock");
		free(hl);
		return NULL;
	}

	hl->hl_name = name;
	hl->hl_keys = keys;
	hl->hl_sum = sum;
	hl->hl_table = table;

        // Default values go here
	hl->hl_create  = create == NULL?  1: *create;
	hl->hl_update  = update == NULL?  1: *update;
	hl->hl_count   = count == NULL?   1: *count;
	hl->hl_timeout = timeout == NULL? 0: *timeout;
	hl->hl_extend  = extend == NULL?  1: *extend;
	hl->hl_cleanup = cleanup == NULL? 1: *cleanup;

	// Default value field name
	if (value_field == NULL)
	{
		snprintf(hl->hl_value_field, DBT_FIELD_MAX, "%s%s", name, VALUE_SUFFIX);
	}
	else
	{
		// hl->hl_value_field is always \0 terminated because hl is memset to 0 and
		// length is DBT_FIELD_MAX + 1.
		strncpy(hl->hl_value_field, value_field, DBT_FIELD_MAX);
	}

	// Default expire field name
	if (expire_field == NULL)
	{
		snprintf(hl->hl_expire_field, DBT_FIELD_MAX, "%s%s", name, EXPIRE_SUFFIX);
	}
	else
	{
		// hl->hl_expire_field is always \0 terminated because hl is memset to 0 and
		// length is DBT_FIELD_MAX + 1.
		strncpy(hl->hl_expire_field, expire_field, DBT_FIELD_MAX);
	}

        log_debug("hitlist_create: %s: create=%d update=%d count=%d timeout=%d "
        	"extend=%d cleanup=%d%s%s", name, hl->hl_create, hl->hl_update,
        	hl->hl_count, hl->hl_timeout, hl->hl_extend, hl->hl_cleanup,
		sum? " sum=": "", sum? sum: "");

	return hl;
}

static var_t *
hitlist_record(hitlist_t *hl, var_t *attrs, int load_data)
{
	ll_entry_t *pos;
	var_t *key;
	char *keystr;
	var_t *v;
	var_t *schema = NULL;
	VAR_INT_T zero = 0;
	char *name;

	name = hl->hl_table? hl->hl_table: hl->hl_name;

	schema = vlist_create(name, VF_KEEPNAME);
	if (schema == NULL)
	{
		log_error("hitlist_schema: vlist_create failed");
		goto error;
	}

	pos = LL_START(hl->hl_keys);
	while ((key = ll_next(hl->hl_keys, &pos)))
	{
		// Impossible
		if (key->v_data == NULL)
		{
			log_error("hitlist_schema: key is NULL");
			goto error;
		}

		// Bad configured
		if (key->v_type != VT_STRING)
		{
			log_error("hitlist_scheme: bad configuration %s:"
				" hitlist keys must be strings.", hl->hl_name);
			goto error;
		}

		keystr = key->v_data;

		// Variables
		if (keystr[0] == '$')
		{
			v = acl_variable_get(attrs, keystr);
		}

		// Regular symbol
		else
		{
			v = acl_symbol_get(attrs, keystr);
		}

		if (v == NULL)
		{
			log_error("hitlist_scheme: %s: lookup %s failed",
				hl->hl_name, keystr);
			goto error;
		}

		if (load_data && v->v_data == NULL)
		{
			log_error("hitlist_scheme: %s: key %s is NULL",
				hl->hl_name, keystr);
			goto error;
		}

		if (vlist_append_new(schema, v->v_type, keystr,
			load_data? v->v_data: NULL, VF_COPY | VF_KEY))
		{
			log_error("hitlist_schema: %s: vlist_append_new"
				" failed", hl->hl_name);
			goto error;
		}
	}

	// Add value
	if (vlist_append_new(schema, VT_INT, hl->hl_value_field,
	    load_data? &zero: NULL, VF_COPY))
	{
		log_error("hitlist_schema: %s: vlist_append_new failed for %s",
			hl->hl_name, hl->hl_value_field);
		goto error;
	}

	if (vlist_append_new(schema, VT_INT, hl->hl_expire_field,
	    load_data? &zero: NULL, VF_COPY))
	{
		log_error("hitlist_schema: %s: vlist_append_new failed for %s",
			hl->hl_name, hl->hl_expire_field);
		goto error;
	}

	return schema;

error:
	if (schema != NULL)
	{
		var_delete(schema);
	}

	return NULL;
}
	
static int
hitlist_db_open(hitlist_t *hl, var_t *attrs)
{
	var_t *schema = NULL;
	int success = -1;

	if (hl->hl_connected)
	{
		log_debug("hitlist_db_open: database already open");
		success = 0;
		goto exit;
	}

	log_debug("hitlist_db_open: open database %s", hl->hl_name);

	// Need schema
	if (hl->hl_dbt.dbt_scheme == NULL)
	{
		schema = hitlist_record(hl, attrs, 0);
		if (schema == NULL)
		{
			log_error("hitlist_lookup: hitlist_record failed");
			goto exit;
		}

		hl->hl_dbt.dbt_scheme = schema;
		hl->hl_dbt.dbt_validate = dbt_common_validate;
		hl->hl_dbt.dbt_cleanup_interval = hl->hl_cleanup? 0: -1;

		if (strlen(hl->hl_expire_field))
		{
			strncpy(hl->hl_dbt.dbt_expire_field, hl->hl_expire_field, DBT_FIELD_MAX);
		}
	}

        if (dbt_register(hl->hl_name, &hl->hl_dbt))
	{
		log_error("hitlist_db_open: %s: dbt_register failed",
			hl->hl_name);
		goto exit;
	}

	if (dbt_open_database(&hl->hl_dbt))
	{
		log_error("hitlist_db_open: %s: dbt_open_database failed",
			hl->hl_name);
		goto exit;
	}

	hl->hl_connected = 1;
	success = 0;

exit:

	return success;
}

static int
hitlist_sql_safe_update(hitlist_t *hl, var_t *record, char *value_field,
    VAR_INT_T value_diff, char *expire_field, VAR_INT_T expire_diff)
{
	var_t *record_copy = NULL;
	var_t *field;
	int status = -1;

	record_copy = VAR_COPY(record);
	if (record_copy == NULL)
	{
		log_error("hitlist_sql_safe_update: var_copy failed");
		goto exit;
	}

	field = vlist_record_lookup(record_copy, value_field);
	if (field == NULL)
	{
		log_error("hitlist_sql_safe_update: vlist_get for %s failed", value_field);
		goto exit;
	}
	* (VAR_INT_T *) field->v_data = value_diff;
	field->v_flags |= VF_SQL_SAFE_UPDATE;

	field = vlist_record_lookup(record_copy, expire_field);
	if (field == NULL)
	{
		log_error("hitlist_sql_safe_update: vlist_get for %s failed", expire_field);
		goto exit;
	}
	* (VAR_INT_T *) field->v_data = expire_diff;
	field->v_flags |= VF_SQL_SAFE_UPDATE;

	status = dbt_db_set(&hl->hl_dbt, record_copy);
	if (status)
	{
		log_error("hitlist_sql_safe_update: dbt_db_set failed");
	}

exit:
	if (record_copy)
	{
		var_delete(record_copy);
	}

	return status;
}

static int
hitlist_lookup(milter_stage_t stage, char *name, var_t *attrs)
{
	hitlist_t *hl;
	var_t *lookup = NULL;
	var_t *record = NULL;
	VAR_INT_T *value;
	VAR_INT_T *expire;
	var_t *addition;
	int success = -1;

	// Used for SQL_SAFE_UPDATE
	VAR_INT_T value_diff = 0;
	VAR_INT_T expire_diff = 0;
	int update_record = 0;

	hl = sht_lookup(hitlists, name);
	if (hl == NULL)
	{
		log_error("Unknown hitlist: %s", name);
		goto exit;
	}

	// If the DB is not open yet, there's a race on hl->hl_connected.
	if (pthread_mutex_lock(&hl->hl_mutex))
	{
		log_sys_error("hitlist_db_open: pthread_mutex_lock");
		goto exit;
	}

	// Open Database
	if (!hl->hl_connected)
	{
		if (hitlist_db_open(hl, attrs))
		{
			log_error("hitlist_lookup: hitlist_db_open failed");
			goto exit;
		}
	}

	// Create lookup record
	lookup = hitlist_record(hl, attrs, 1);
	if (lookup == NULL)
	{
		// Happens if a key is not set or NULL
		log_error("hitlist_lookup: hitlist_record failed");
		vtable_set_null(attrs, name, VF_COPYNAME);

		// Failing would lead to acl termination
		success = 0;
		goto exit;
	}

	if (dbt_db_get(&hl->hl_dbt, lookup, &record))
	{
		log_error("hitlist_lookup: dbt_db_get failed");
		goto exit;
	}

	if (record == NULL)
	{
		// Add new record
		if (hl->hl_create)
		{
			log_debug("hitlist_lookup: %s add record", name);
			record = lookup;
			lookup = NULL;
		}
		// No record found, set symbol to null
		else
		{
			vtable_set_null(attrs, name, VF_COPYNAME);
			log_debug("hitlist_lookup: %s no record", name);
			success = 0;
			goto exit;
		}
	}
	else
	{
		update_record = 1;
		log_debug("hitlist_lookup: %s record found", name);
	}

	value = vlist_record_get(record, hl->hl_value_field);
	expire = vlist_record_get(record, hl->hl_expire_field);
	if (value == NULL || expire == NULL)
	{
		log_error("hitlist_lookup: vlist_record_get failed");
		goto exit;
	}

	// Count
       	if (hl->hl_count)
       	{
		++(*value);
		value_diff = 1;
       	}

	// Sum
        else if (hl->hl_sum)
        {
		addition = vtable_lookup(attrs, hl->hl_sum);
		if (addition == NULL)
		{
			log_error("hitlist: sum field %s is undefined", hl->hl_sum);
			goto exit;
		}

		if (addition->v_type != VT_INT)
		{
			log_error("hitlist: sum field %s must be integer", hl->hl_sum);
			goto exit;
		}

		*value += *(VAR_INT_T *) addition->v_data;
		value_diff = *(VAR_INT_T *) addition->v_data;
        }

        if (hl->hl_update)
        {

		// Record never expires
		if (hl->hl_timeout == 0)
		{
			*expire = INT_MAX;
			expire_diff = 0;
		}
		// Record is extended every time a match is found
		else if (hl->hl_extend)
		{
			*expire = time(NULL) + hl->hl_timeout;
			expire_diff = hl->hl_timeout;
		}
		// Record expires after a fixed timeout
		else if (hl->hl_timeout && *expire == 0)
		{
			*expire = time(NULL) + hl->hl_timeout;
			expire_diff = hl->hl_timeout;
		}

		// SQL_SAFE_UPDATE
		if(update_record && hl->hl_dbt.dbt_driver->dd_use_sql)
		{
			if (hitlist_sql_safe_update(hl, record, hl->hl_value_field, value_diff,
				hl->hl_expire_field, expire_diff))
			{
				log_error("hitlist_lookup: hitlist_sql_safe_update failed");
				goto exit;
			}
		}
		else
		{
			if (dbt_db_set(&hl->hl_dbt, record))
			{
				log_error("hitlist_lookup: dbt_db_set failed");
				goto exit;
			}
		}
        }

	// Add symbol
	if (vtable_set_new(attrs, VT_INT, name, value, VF_COPY))
	{
		log_error("hitlist_lookup: vtable_set_new failed");

		goto exit;
	}

	success = 0;

exit:
	if (lookup != NULL)
	{
		var_delete(lookup);
	}

	if (record != NULL)
	{
		var_delete(record);
	}

	if (pthread_mutex_unlock(&hl->hl_mutex))
	{
		log_sys_error("hitlist_db_open: pthread_mutex_unlock");
	}

	return success;
}

int
hitlist_register(char *name)
{
	hitlist_t *hl;
	ll_t *keys;
	VAR_INT_T *create;
	VAR_INT_T *update;
	VAR_INT_T *count;
	VAR_INT_T *timeout;
	VAR_INT_T *extend;
	VAR_INT_T *cleanup;
        char *sum;
        char *table;
	char *value_field;
	char *expire_field;

	if (name == NULL)
	{
		log_die(EX_SOFTWARE, "hitlist_register: name is NULL");
	}

	keys = cf_get_value(VT_LIST, HITLIST_NAME, name, "keys", NULL);
	create = cf_get_value(VT_INT, HITLIST_NAME, name, "create", NULL);
	update = cf_get_value(VT_INT, HITLIST_NAME, name, "update", NULL);
	count = cf_get_value(VT_INT, HITLIST_NAME, name, "count", NULL);
	timeout = cf_get_value(VT_INT, HITLIST_NAME, name, "timeout", NULL);
	extend = cf_get_value(VT_INT, HITLIST_NAME, name, "extend", NULL);
	cleanup = cf_get_value(VT_INT, HITLIST_NAME, name, "cleanup", NULL);
	sum = cf_get_value(VT_STRING, HITLIST_NAME, name, "sum", NULL);
	table = cf_get_value(VT_STRING, HITLIST_NAME, name, "table", NULL);
	value_field = cf_get_value(VT_STRING, HITLIST_NAME, name, "value_field", NULL);
	expire_field = cf_get_value(VT_STRING, HITLIST_NAME, name, "expire_field", NULL);

	if (keys == NULL)
	{
		log_die(EX_SOFTWARE, "hitlist_register: %s: need keys", name);
	}

	if (keys->ll_size == 0)
	{
		log_die(EX_CONFIG, "hitlist_register: %s: keys is empty", name);
	}

	hl = hitlist_create(name, keys, create, update, count, timeout, extend,
	   cleanup, sum, table, value_field, expire_field);
	if (hl == NULL)
	{
		log_die(EX_SOFTWARE, "hitlist_register: hl_create failed");
	}

	if (sht_insert(hitlists, name, hl))
	{
		log_die(EX_SOFTWARE, "hitlist_register: sht_insert failed");
	}

	acl_symbol_register(name, MS_ANY, hitlist_lookup, AS_CACHE);
	
	return 0;
}

int
hitlist_init(void)
{
	var_t *hitlist;
	ht_t *config;
	ht_pos_t pos;
	var_t *v;

	hitlist = cf_get(VT_TABLE, HITLIST_NAME, NULL);
	if (hitlist == NULL)
	{
		log_notice("hitlist_init: no lists configured");
		return 0;
	}

	hitlists = sht_create(BUCKETS, free);
	if (hitlists == NULL)
	{
		log_die(EX_SOFTWARE, "hitlist_init: sht_create failed");
	}

	config = hitlist->v_data;
	ht_start(config, &pos);
	while ((v = ht_next(config, &pos)))
	{
		if (hitlist_register(v->v_name))
		{
			log_error("hitlist_init: hitlist_register failed");
			return -1;
		}
		
	}

	return 0;
}

void
hitlist_fini(void)
{
	if (hitlists != NULL)
	{
		sht_delete(hitlists);
	}

	return;
}
