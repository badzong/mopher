#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <config.h>
#include <mopher.h>

#define BUCKETS 32
#define HITLIST_NAME "hitlist"
#define BUFLEN 1024

static sht_t *hitlists;

typedef struct hitlist {
	pthread_mutex_t  hl_mutex;
	char		*hl_name;
	int		 hl_connected;
	dbt_t		 hl_dbt;
	ll_t		*hl_keys;
	VAR_INT_T	 hl_record;
	VAR_INT_T	 hl_timeout;
	VAR_INT_T	 hl_extend;
} hitlist_t;
	

static hitlist_t *
hitlist_create(char *name, ll_t *keys, int record, int timeout, int extend)
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
	hl->hl_record = record;
	hl->hl_timeout = timeout;
	hl->hl_extend = extend;

	return hl;
}

static var_t *
hitlist_record(hitlist_t *hl, var_t *attrs, int load_data)
{
	ll_entry_t *pos;
	var_t *key;
	char *keystr;
	var_t *v;
	char buffer[BUFLEN];
	var_t *schema = NULL;
	VAR_INT_T zero = 0;

	schema = vlist_create(hl->hl_name, VF_KEEPNAME);
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

		if (vlist_append_new(schema, v->v_type, keystr,
			load_data? v->v_data: NULL, VF_COPY | VF_KEY))
		{
			log_error("hitlist_schema: %s: vlist_append_new"
				" failed", hl->hl_name);
			goto error;
		}
	}

	// Add hits
	if (snprintf(buffer, sizeof buffer, "%s_hits", hl->hl_name) >=
	    sizeof buffer)
	{
		log_error("hitlist_schema: buffer exhausted");
		goto error;
	}
	if (vlist_append_new(schema, VT_INT, buffer, load_data? &zero: NULL,
		VF_COPY))
	{
		log_error("hitlist_schema: %s: vlist_append_new failed",
			hl->hl_name);
		goto error;
	}

	// Add expire
	if (snprintf(buffer, sizeof buffer, "%s_expire", hl->hl_name) >=
	    sizeof buffer)
	{
		log_error("hitlist_schema: buffer exhausted");
		goto error;
	}
	if (vlist_append_new(schema, VT_INT, buffer, load_data? &zero: NULL,
		VF_COPY))
	{
		log_error("hitlist_schema: %s: vlist_append_new failed",
			hl->hl_name);
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
hitlist_lookup(milter_stage_t stage, char *name, var_t *attrs)
{
	hitlist_t *hl;
	var_t *lookup = NULL;
	var_t *record = NULL;
	VAR_INT_T *hits;
	VAR_INT_T *expire;
	int success = -1;

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
		log_error("hitlist_lookup: hitlist_record failed");
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
		if (hl->hl_record)
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
		log_debug("hitlist_lookup: %s record found", name);
	}

	hits = vlist_record_get_combine_key(record, name, "_hits", NULL);
	expire = vlist_record_get_combine_key(record, name, "_expire", NULL);
	if (hits == NULL || expire == NULL)
	{
		log_error("hitlist_lookup: vlist_record_get_combine_keys"
			" failed");
		goto exit;
	}

	// Hit
	++(*hits);

	// Record never expires
	if (hl->hl_timeout == 0)
	{
		*expire = INT_MAX;
	}
	// Record is extended every time a match is found
	else if (hl->hl_extend)
	{
		*expire = time(NULL) + hl->hl_timeout;
	}
	// Record expires after a fixed timeout
	else if (hl->hl_timeout && *expire == 0)
	{
		*expire = time(NULL) + hl->hl_timeout;
	}

	if (dbt_db_set(&hl->hl_dbt, record))
	{
		log_error("hitlist_lookup: dbt_db_set failed");

		goto exit;
	}

	// Add symbol
	if (vtable_set_new(attrs, VT_INT, name, hits, VF_COPY))
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
	VAR_INT_T *record;
	VAR_INT_T *timeout;
	VAR_INT_T *extend;

	if (name == NULL)
	{
		log_die(EX_SOFTWARE, "hitlist_register: name is NULL");
	}

	keys = cf_get_value(VT_LIST, HITLIST_NAME, name, "keys", NULL);
	record = cf_get_value(VT_INT, HITLIST_NAME, name, "record", NULL);
	timeout = cf_get_value(VT_INT, HITLIST_NAME, name, "timeout", NULL);
	extend = cf_get_value(VT_INT, HITLIST_NAME, name, "extend", NULL);

	if (keys == NULL)
	{
		log_die(EX_SOFTWARE, "hitlist_register: %s: need keys", name);
	}

	if (keys->ll_size == 0)
	{
		log_die(EX_CONFIG, "hitlist_register: %s: keys is empty", name);
	}

	hl = hitlist_create(name, keys, *record, *timeout, *extend);
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
