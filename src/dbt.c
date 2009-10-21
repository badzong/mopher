#include "config.h"

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include <string.h>
#include <pthread.h>
#include <errno.h>

#include "mopher.h"

#define DBT_BUCKETS 32

static ht_t *dbt_drivers;
static ht_t *dbt_tables;

static pthread_mutex_t dbt_janitor_mutex = PTHREAD_MUTEX_INITIALIZER;


static hash_t
dbt_driver_hash(dbt_driver_t *dd)
{
	return HASH(dd->dd_name, strlen(dd->dd_name));
}


static int
dbt_driver_match(dbt_driver_t *dd1, dbt_driver_t *dd2)
{
	if(strcmp(dd1->dd_name, dd2->dd_name) == 0) {
		return 1;
	}

	return 0;
}


void
dbt_driver_register(dbt_driver_t *dd)
{
	if((ht_insert(dbt_drivers, dd)) == -1) {
		log_die(EX_SOFTWARE, "dbt_driver_register: ht_insert failed");
	}

	log_info("dbt_driver_register: database driver \"%s\" registered",
		dd->dd_name);

	return;
}


static hash_t
dbt_hash(dbt_t *dbt)
{
	return HASH(dbt->dbt_name, strlen(dbt->dbt_name));
}


static int
dbt_match(dbt_t *dbt1, dbt_t *dbt2)
{
	if(strcmp(dbt1->dbt_name, dbt2->dbt_name)) {
		return 0;
	}

	return 1;
}


static void
dbt_close(dbt_t *dbt)
{
	if ((dbt->dbt_driver->dd_flags & DBT_LOCK))
	{
		if (pthread_mutex_destroy(&dbt->dbt_driver->dd_mutex))
		{
			log_error("dbt_register: ptrhead_mutex_destroy");
		}
	}

	DBT_DB_CLOSE(dbt);

	return;
}

static int
dbt_db_lock(dbt_t *dbt)
{
	if ((dbt->dbt_driver->dd_flags & DBT_LOCK) == 0)
	{
		return 0;
	}

	if (pthread_mutex_lock(&dbt->dbt_driver->dd_mutex))
	{
		log_error("dbt_db_lock: pthread_mutex_lock");
		return -1;
	}

	return 0;
}


static void
dbt_db_unlock(dbt_t *dbt)
{
	if ((dbt->dbt_driver->dd_flags & DBT_LOCK) == 0)
	{
		return;
	}

	if (pthread_mutex_unlock(&dbt->dbt_driver->dd_mutex))
	{
		log_error("dbt_db_unlock: pthread_mutex_unlock");
	}

	return;
}

int
dbt_db_get(dbt_t *dbt, var_t *record, var_t **result)
{
	int r;

	*result = NULL;

	if (dbt_db_lock(dbt))
	{
		return -1;
	}

	r = dbt->dbt_driver->dd_get(dbt, record, result);

	dbt_db_unlock(dbt);

	return r;
}


int
dbt_db_set(dbt_t *dbt, var_t *record)
{
	int r;

	if (dbt_db_lock(dbt))
	{
		return -1;
	}

	client_sync(dbt, record);

	r = dbt->dbt_driver->dd_set(dbt, record);

	dbt_db_unlock(dbt);

	return r;
}


int
dbt_db_del(dbt_t *dbt, var_t *record)
{
	int r;

	if (dbt_db_lock(dbt))
	{
		return -1;
	}

	r = dbt->dbt_driver->dd_del(dbt, record);

	dbt_db_unlock(dbt);

	return r;
}


int
dbt_db_walk(dbt_t *dbt, dbt_db_callback_t callback)
{
	int r;

	if (dbt_db_lock(dbt))
	{
		return -1;
	}

	r = dbt->dbt_driver->dd_walk(dbt, callback);

	dbt_db_unlock(dbt);

	return r;
}

int
dbt_db_sync(dbt_t *dbt)
{
	int r;

	if (dbt->dbt_driver->dd_sync == NULL)
	{
		return 0;
	}

	if (dbt_db_lock(dbt))
	{
		return -1;
	}

	r = dbt->dbt_driver->dd_sync(dbt);

	dbt_db_unlock(dbt);

	return r;
}

int
dbt_db_cleanup(dbt_t *dbt)
{
	int r;

	if (dbt_db_lock(dbt))
	{
		return -1;
	}

	r = dbt->dbt_driver->dd_sql_cleanup(dbt);

	dbt_db_unlock(dbt);

	return r;
}


void
dbt_register(dbt_t *dbt)
{
	dbt_driver_t lookup, *dd;
	var_t *config;

	if (dbt->dbt_name == NULL) {
		log_die(EX_SOFTWARE, "dbt_register: no name specified");
	}

	/*
	 * Load config table
	 */
	config = cf_get(VT_TABLE, "tables", dbt->dbt_name, NULL);
	if (config == NULL) {
		log_die(EX_CONFIG, "dbt_register: missing database "
			"configuration for \"%s\"", dbt->dbt_name);
	}

	/*
	 * Fill configuration into dbt
	 */
	if (var_table_dereference(config, "driver", &dbt->dbt_drivername,
		"path", &dbt->dbt_path, "host", &dbt->dbt_host,
		"port", &dbt->dbt_port, "user", &dbt->dbt_user,
		"pass", &dbt->dbt_pass, "database", &dbt->dbt_database,
		"table", &dbt->dbt_table, NULL))
	{
		log_die(EX_CONFIG, "dbt_register: var_table_dereference"
			" failed");
	}

	/*
	 * Add some defaults
	 */
	if (dbt->dbt_table == NULL) {
		dbt->dbt_table = dbt->dbt_name;
	}

	if (dbt->dbt_database == NULL) {
		dbt->dbt_database = BINNAME;
	}

	if (dbt->dbt_cleanup_interval == 0) {
		dbt->dbt_cleanup_interval = cf_dbt_cleanup_interval;
	}


	/*
	 * Lookup database driver
	 */
	memset(&lookup, 0, sizeof lookup);
	lookup.dd_name = dbt->dbt_drivername;

	dd = ht_lookup(dbt_drivers, &lookup);
	if (dd == NULL) {
		log_die(EX_CONFIG, "dbt_register: unknown database driver "
			"\"%s\"", dbt->dbt_drivername);
	}

	dbt->dbt_driver = dd;

	/*
	 * Initialize mutex if driver requires locking
	 */
	if ((dd->dd_flags & DBT_LOCK))
	{
		if (pthread_mutex_init(&dd->dd_mutex, NULL))
		{
			log_die(EX_SOFTWARE, "dbt_register: ptrhead_mutex_init");
		}
	}

	/*
	 * Open database
	 */
	log_debug("dbt_register: open database \"%s\"", dbt->dbt_name);

	if (DBT_DB_OPEN(dbt)) {
		log_die(EX_CONFIG, "dbt_register: can't open database");
	}

	/*
	 * Store dbt in dbt_tables
	 */
	if (ht_insert(dbt_tables, dbt)) {
		log_die(EX_SOFTWARE, "table_register: ht_insert failed");
	}

	return;
}


static int
dbt_cleanup(dbt_t *dbt, var_t *record)
{
	int valid;

	valid = DBT_VALIDATE(dbt, record);
	if (valid == -1) {
		log_error("dbt_cleanup: DBT_VALIDATE failed");
		return -1;
	}
	if (valid) {
		return 0;
	}
	if (dbt_db_del(dbt, record)) {
		log_error("dbt_cleanup: dbt_db_del failed");
		return -1;
	}

	++dbt->dbt_cleanup_deleted;

	return 0;
}


void
dbt_janitor(int force)
{
	time_t now;
	dbt_t *dbt;
	int mutex;
	int deleted;

	/*
	 * Check if someone else is doing the dirty work.
	 */
	mutex = pthread_mutex_trylock(&dbt_janitor_mutex);
	if (mutex == EBUSY) {
		return;
	}
	else if (mutex) {
		log_error("dbt_janitor: pthread_mutex_trylock");
		return;
	}

	if ((now = time(NULL)) == -1) {
		log_error("dbt_janitor: time");
		goto exit_unlock;
	}

	ht_rewind(dbt_tables);
	while((dbt = ht_next(dbt_tables))) {
		/*
		 * Check if table needs a clean up
		 */
		if (force == 0 && now < dbt->dbt_cleanup_schedule)
		{
			continue;
		}

		log_info("dbt_janitor: cleaning up \"%s\"", dbt->dbt_name);

		/*
		 * Check if driver supports SQL
		 */
		if (dbt->dbt_driver->dd_sql_cleanup &&
		    dbt->dbt_sql_invalid_where)
		{
			deleted = dbt_db_cleanup(dbt);

			if (deleted == -1) {
				log_error("dbt_janitor: dbt_db_cleanup "
					"failed");
			}
			else {
				log_debug("dbt_janitor: deleted %d stale "
					"records from \"%s\"", deleted,
					dbt->dbt_name);
			}

			DBT_SCHEDULE_CLEANUP(dbt, now);

			continue;
		}

		/*
		 * Check if driver supports walking
		 */
		if (dbt->dbt_driver->dd_walk == NULL) {
			log_warning("dbt_janitor: can't cleanup database "
				"\"%s\": Driver supports neither SQL nor "
				"walking", dbt->dbt_name);
			continue;
		}
			
		/*
		 * No validate callback registered
		 */
		if (dbt->dbt_validate == NULL) {
			continue;
		}

		dbt->dbt_cleanup_deleted = 0;

		if (dbt_db_walk(dbt, (void *) dbt_cleanup)) {
			log_error("dbt_janitor: dbt_db_walk failed");
			continue;
		}
		
		log_debug("dbt_janitor: deleted %d stale records from \"%s\"",
			dbt->dbt_cleanup_deleted, dbt->dbt_name);

		/*
		 * Sync database if driver supports syncing
		 */
		if (dbt->dbt_driver->dd_sync) {
			if (dbt_db_sync(dbt)) {
				log_warning("dbt_janitor: dbt_db_sync failed");
			}
		}

		DBT_SCHEDULE_CLEANUP(dbt, now);
	}


exit_unlock:

	if (pthread_mutex_unlock(&dbt_janitor_mutex)) {
		log_warning("dbt_janitor: pthread_mutex_unlock");
	}

	return;
}


void
dbt_init(void)
{
	/*
	 * Initialize driver table
	 */
	dbt_drivers = ht_create(DBT_BUCKETS, (ht_hash_t) dbt_driver_hash,
		(ht_match_t) dbt_driver_match, NULL);

	if(dbt_drivers == NULL) {
		log_die(EX_SOFTWARE, "dbt_init: ht_create failed");
	}

	/*
	 * Load database drivers
	 */
	modules_load(cf_dbt_mod_path);

	/*
	 * Initailaize tables
	 */
	dbt_tables = ht_create(DBT_BUCKETS, (ht_hash_t) dbt_hash,
		(ht_match_t) dbt_match, (ht_delete_t) dbt_close);

	if (dbt_tables == NULL) {
		log_die(EX_SOFTWARE, "dbt_init: ht_init failed");
	}

	/*
	 * Load table modules
	 */
	modules_load(cf_tables_mod_path);

	/*
	 * Cleanup databases
	 */
	dbt_janitor(1);

	/*
	 * Start sync thread
	 */
	if (server_init())
	{
		log_die(EX_SOFTWARE, "dbt_init: server_init failed");
	}

	if (client_init())
	{
		log_die(EX_SOFTWARE, "dbt_init: client_init failed");
	}

	return;
}


void
dbt_clear()
{
	dbt_janitor(1);

	client_clear();
	server_clear();

	ht_delete(dbt_drivers);
	ht_delete(dbt_tables);

	return;
}


dbt_t *
dbt_lookup(char *name)
{
	dbt_t lookup;

	memset(&lookup, 0, sizeof lookup);
	lookup.dbt_name = name;

	return ht_lookup(dbt_tables, &lookup);
}
