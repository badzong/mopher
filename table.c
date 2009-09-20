#include <malloc.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#include "log.h"
#include "cf.h"
#include "ht.h"
#include "dbt.h"
#include "modules.h"
#include "table.h"
#include "milter.h"

#define TABLES_BUCKETS 32

static ht_t *table_tables;
time_t table_cleanup_cycle;
static pthread_mutex_t table_janitor_mutex = PTHREAD_MUTEX_INITIALIZER;


table_t *
table_create(char *name, var_t *schema, table_update_t update,
	table_validate_t validate, dbt_t *dbt)
{
	table_t *table;

	table = (table_t *) malloc(sizeof(table_t));
	if (table == NULL) {
		log_warning("table_create: malloc");
		return NULL;
	}

	table->t_name = strdup(name);
	table->t_schema = schema;
	table->t_update = update;
	table->t_validate = validate;
	table->t_dbt = dbt;

	return table;
}


void
table_delete(table_t * table)
{
	if(table->t_dbt) {
		dbt_delete(table->t_dbt);
	}

	if(table->t_schema) {
		var_delete(table->t_schema);
	}

	free(table->t_name);
	free(table);

	return;
}


static hash_t
table_hash(table_t *table)
{
	return HASH(table->t_name, strlen(table->t_name));
}


static int
table_match(table_t *t1, table_t *t2)
{
	if(strcmp(t1->t_name, t2->t_name)) {
		return 0;
	}

	return 1;
}


int
table_register(char *name, var_t *schema, table_update_t update,
	table_validate_t validate)
{
	dbt_t *dbt = NULL;
	table_t *table = NULL;

	var_t *v;

	char *db_driver = NULL;
	char *db_path = NULL;
	char *db_host = NULL;
	char *db_user = NULL;
	char *db_pass = NULL;
	char *db_name = NULL;
	char *db_table = NULL;

	/*
	 * Load configuration for this table
	 */

	v = cf_get(VT_STRING, "tables", name, "driver", NULL);
	if (v) {
		db_driver = v->v_data;
	}

	v = cf_get(VT_STRING, "tables", name, "path", NULL);
	if (v) {
		db_path = v->v_data;
	}

	v = cf_get(VT_STRING, "tables", name, "host", NULL);
	if (v) {
		db_host = v->v_data;
	}

	v = cf_get(VT_STRING, "tables", name, "user", NULL);
	if (v) {
		db_user = v->v_data;
	}

	v = cf_get(VT_STRING, "tables", name, "pass", NULL);
	if (v) {
		db_pass = v->v_data;
	}

	v = cf_get(VT_STRING, "tables", name, "name", NULL);
	if (v) {
		db_name = v->v_data;
	}

	v = cf_get(VT_STRING, "tables", name, "table", NULL);
	if (v) {
		db_table = v->v_data;
	}

	/*
	 * Open database
	 */
	log_debug("table_register: open database \"%s\"", name);

	dbt = dbt_open(schema, db_driver, db_path, db_host, db_user, db_pass,
		db_name, db_table);
	if (dbt == NULL) {
		log_warning("table_register: dbt_open for database \"%s\""
			" failed", name);
		goto error;
	}

	table = table_create(name, schema, update, validate, dbt);
	if (table == NULL) {
		log_warning("table_register: table create failed");
		goto error;
	}

	if (ht_insert(table_tables, table)) {
		log_warning("table_register: ht_insert failed");
		goto error;
	}

	return 0;
	

error:

	if (dbt) {
		dbt_delete(dbt);
	}

	if (table) {
		table_delete(table);
	}

	return -1;
}


int
table_cleanup(table_t *table, var_t *record)
{
	int valid;

	valid = table->t_validate(record);
	if (valid == -1) {
		log_warning("table_cleanup: table->t_validate returned non-zero");
		return -1;
	}
	if (valid) {
		return 0;
	}
	if (dbt_del(table->t_dbt, record)) {
		log_warning("table_cleanup: dbt_del failed");
		return -1;
	}

	++table->t_cleanup_deleted;

	return 0;
}


void
table_janitor(int force)
{
	time_t now;
	table_t *table;
	int mutex;

	/*
	 * Check if someone else is doing the dirty work.
	 */
	mutex = pthread_mutex_trylock(&table_janitor_mutex);
	if (mutex == EBUSY) {
		return;
	}
	else if (mutex) {
		log_error("table_janitor: pthread_mutex_trylock");
		return;
	}

	if ((now = time(NULL)) == -1) {
		log_error("table_janitor: time");
		goto exit_unlock;
	}

	/*
	 * Not yet time to cleanup.
	 */
	if (force == 0 && now < table_cleanup_cycle) {
		goto exit_unlock;
	}

	table_cleanup_cycle = now + cf_table_cleanup_interval;

	if (table_cleanup_cycle) {
		log_info("table_janitor: cleanup cycle started");
	}
	else {
		log_info("table_janitor: initial cleanup");
	}

	ht_rewind(table_tables);
	while((table = ht_next(table_tables))) {
		if (table->t_validate == NULL) {
			continue;
		}

		table->t_cleanup_deleted = 0;

		if (dbt_walk(table->t_dbt, (dbt_callback_t) table_cleanup,
			table)) {
			log_warning("table_janitor: dbt_walk failed");
			continue;
		}
		
		log_debug("table_janitor: deleted %d records in \"%s\"",
			table->t_cleanup_deleted, table->t_name);

		if (dbt_sync(table->t_dbt)) {
			log_warning("table_janitor: dbt_sync failed");
		}
	}

	log_debug("table_janitor: terminated");


exit_unlock:

	if (pthread_mutex_unlock(&table_janitor_mutex)) {
		log_warning("table_janitor: pthread_mutex_unlock");
	}

	return;
}


void
table_init(void)
{
	table_tables = ht_create(TABLES_BUCKETS, (ht_hash_t) table_hash,
		(ht_match_t) table_match, (ht_delete_t) table_delete);

	if (table_tables == NULL) {
		log_die(EX_SOFTWARE, "table_init: ht_init failed");
	}

	modules_load(cf_tables_mod_path);

	table_janitor(1);

	return;
}


void
table_clear()
{
	table_janitor(1);

	ht_delete(table_tables);

	return;
}


table_t *
table_lookup(char *name)
{
	table_t lookup;

	memset(&lookup, 0, sizeof(lookup));
	lookup.t_name = name;

	return ht_lookup(table_tables, &lookup);
}
