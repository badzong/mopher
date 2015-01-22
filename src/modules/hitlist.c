#include <stdlib.h>
#include <string.h>

#include <config.h>
#include <mopher.h>

#define BUCKETS 32
#define HITLIST_NAME "hitlist"
#define BUFLEN 1024

static sht_t *hitlists;

typedef struct hitlist {
	char  *hl_name;
	ll_t  *hl_keys;
	int    hl_connected;
	dbt_t  hl_dbt;
} hitlist_t;
	

static hitlist_t *
hitlist_create(char *name, ll_t *keys)
{
	hitlist_t *hl;

	hl = (hitlist_t *) malloc(sizeof(hitlist_t));
	if (hl == NULL)
	{
		log_sys_error("hitlist_create: malloc");
		return NULL;
	}

	memset(hl, 0, sizeof(hitlist_t));

	hl->hl_name = name;
	hl->hl_keys = keys;

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

	// Add timeout
	if (snprintf(buffer, sizeof buffer, "%s_timeout", hl->hl_name) >=
	    sizeof buffer)
	{
		log_error("hitlist_schema: buffer exhausted");
		goto error;
	}
	if (vlist_append_new(schema, VT_INT, buffer, NULL, VF_COPYNAME))
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
	if (vlist_append_new(schema, VT_INT, buffer, NULL, VF_COPYNAME))
	{
		log_error("hitlist_schema: %s: vlist_append_new failed",
			hl->hl_name);
		goto error;
	}

	// Add hits
	if (snprintf(buffer, sizeof buffer, "%s_hits", hl->hl_name) >=
	    sizeof buffer)
	{
		log_error("hitlist_schema: buffer exhausted");
		goto error;
	}
	if (vlist_append_new(schema, VT_INT, buffer, NULL, VF_COPYNAME))
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

	// Need schema
	if (hl->hl_dbt.dbt_scheme == NULL)
	{
		schema = hitlist_record(hl, attrs, 0);
		if (schema == NULL)
		{
			log_error("hitlist_lookup: hitlist_record failed");
			return -1;
		}

		hl->hl_dbt.dbt_scheme = schema;
		hl->hl_dbt.dbt_validate = dbt_common_validate;
	}

        if (dbt_register(hl->hl_name, &hl->hl_dbt))
	{
		log_error("hitlist_db_open: %s: dbt_register failed",
			hl->hl_name);
		return -1;
	}

	if (dbt_open_database(&hl->hl_dbt))
	{
		log_error("hitlist_db_open: %s: dbt_open_database failed",
			hl->hl_name);
		return -1;
	}

	hl->hl_connected = 1;

	return 0;
}

static int
hitlist_lookup(milter_stage_t stage, char *name, var_t *attrs)
{
	hitlist_t *hl;
	var_t *lookup = NULL;
	var_t *record = NULL;
	char buffer[BUFLEN];
	VAR_INT_T *hits;
	VAR_INT_T *timeout;
	VAR_INT_T *expire;
	int success = -1;

	hl = sht_lookup(hitlists, name);
	if (hl == NULL)
	{
		log_error("Unknown hitlist: %s", name);
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
		vtable_set_null(attrs, name, VF_COPYNAME);
		log_debug("hitlist_lookup: %s no record", name);

		goto exit;
	}

	log_debug("hitlist_lookup: %s record found", name);

	// Update record
	if (snprintf(buffer, sizeof buffer, "%s_hits", name) >= sizeof buffer)
	{
		log_error("hitlist_lookup: buffer exhausted");

		goto exit;
	}

	hits = vlist_record_get_combine_key(record, name, "_hits", NULL);
	timeout = vlist_record_get_combine_key(record, name, "_timeout", NULL);
	expire = vlist_record_get_combine_key(record, name, "_expire", NULL);
	if (hits == NULL || timeout == NULL || expire == NULL)
	{
		log_error("hitlist_lookup: vlist_record_get_combine_keys"
			" failed");

		goto exit;
	}

	// Hit
	++(*hits);

	// Calculate new expire timestamp
	*expire += *timeout;

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

	return success;
}

int
hitlist_register(char *name, ll_t *keys)
{
	hitlist_t *hl;

	if (name == NULL || keys == NULL)
	{
		log_die(EX_SOFTWARE, "hitlist_register: name or keys is NULL");
	}

	if (keys->ll_size == 0)
	{
		log_die(EX_CONFIG, "hitlist_register: %s has no attributes");
	}

	hl = hitlist_create(name, keys);
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
		if (hitlist_register(v->v_name, v->v_data))
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
