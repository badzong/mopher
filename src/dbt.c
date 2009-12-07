#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>

#include "mopher.h"

#define DBT_BUCKETS 32

static sht_t *dbt_drivers;
static sht_t *dbt_tables;

static int		dbt_janitor_running = 1;
static pthread_t	dbt_janitor_thread;
static pthread_attr_t	dbt_janitor_attr;
static pthread_mutex_t	dbt_janitor_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	dbt_janitor_cond = PTHREAD_COND_INITIALIZER;


void
dbt_driver_register(char *name, dbt_driver_t *dd)
{
	if((sht_insert(dbt_drivers, name, dd)) == -1)
	{
		log_die(EX_SOFTWARE, "dbt_driver_register: sht_insert for "
		    "driver \"%s\" failed", name);
	}

	log_info("dbt_driver_register: database driver \"%s\" registered",
	    name);

	return;
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
dbt_register(char *name, dbt_t *dbt)
{
	dbt_driver_t *dd;
	var_t *config;

	/*
	 * Load config table
	 */
	config = cf_get(VT_TABLE, "tables", name, NULL);
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
	 * dbt_name is used by the janitor
	 */
	dbt->dbt_name = name;

	/*
	 * Add some defaults
	 */
	if (dbt->dbt_table == NULL) {
		dbt->dbt_table = name;
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
	dd = sht_lookup(dbt_drivers, dbt->dbt_drivername);
	if (dd == NULL)
	{
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
	log_debug("dbt_register: open database \"%s\"", name);

	if (DBT_DB_OPEN(dbt))
	{
		log_die(EX_CONFIG, "dbt_register: can't open database \"%s\"",
		    name);
	}

	/*
	 * Lock janitor mutex in case the janitor is already working
	 */
	if (pthread_mutex_lock(&dbt_janitor_mutex))
	{
		log_die(EX_SOFTWARE, "dbt_register: pthread_mutex_lock");
	}

	/*
	 * Store dbt in dbt_tables
	 */
	if (sht_insert(dbt_tables, name, dbt)) {
		log_die(EX_SOFTWARE, "dbt_register: ht_insert failed");
	}

	if (pthread_mutex_unlock(&dbt_janitor_mutex))
	{
		log_die(EX_SOFTWARE, "dbt_register: pthread_mutex_unlock");
	}

	return;
}


static int
dbt_janitor_cleanup_sql(dbt_t *dbt)
{
	int deleted;

	deleted = dbt_db_cleanup(dbt);

	if (deleted == -1)
	{
		log_error("dbt_janitor_cleanup_sql: dbt_db_cleanup failed");
	}

	return deleted;
}


static int
dbt_janitor_validate(dbt_t *dbt, var_t *record)
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


static int
dbt_janitor_cleanup_walk(dbt_t *dbt)
{
	dbt->dbt_cleanup_deleted = 0;

	if (dbt_db_walk(dbt, (void *) dbt_janitor_validate)) {
		log_error("dbt_janitor_cleanup_walk: dbt_db_walk failed");
		return -1;
	}
		
	/*
	 * Sync database if driver supports syncing
	 */
	if (dbt->dbt_driver->dd_sync) {
		if (dbt_db_sync(dbt)) {
			log_warning("dbt_janitor: dbt_db_sync failed");
		}
	}

	return dbt->dbt_cleanup_deleted;
}


static int
dbt_janitor_cleanup(time_t now, dbt_t *dbt)
{
	int deleted = 0;

	log_debug("dbt_janitor_cleanup: cleaning up \"%s\"", dbt->dbt_name);

	/*
	 * Check if driver supports SQL
	 */
	if (dbt->dbt_driver->dd_sql_cleanup && dbt->dbt_sql_invalid_where)
	{
		deleted = dbt_janitor_cleanup_sql(dbt);
	}

	/*
	 * Check if driver and table support walking
	 */
	else if (dbt->dbt_driver->dd_walk)
	{
		if (dbt->dbt_validate)
		{
			deleted = dbt_janitor_cleanup_walk(dbt);
		}

		/*
		 * No validate callback registered
		 */
		log_debug("dbt_janitor_cleanup: \"%s\" has no validate"
		    " callback");

		return 0;
	}

	else
	{
		log_error("dbt_janitor_cleanup: can't cleanup database \"%s\":"
		    " Driver supports neither SQL nor walking", dbt->dbt_name);
		return -1;
	}

	/*
	 * dbt_janitor_cleanup_sql or dbt_janitor_cleanup_walk should have
	 * logged already
	 */
	if (deleted == -1)
	{
		return -1;
	}

	log_debug("dbt_janitor: deleted %d stale records from \"%s\"", deleted,
	    dbt->dbt_name);

	return deleted;
}


static void *
dbt_janitor(void *arg)
{
	time_t now;
	dbt_t *dbt;
	int deleted;
	unsigned long schedule;
	struct timespec	ts;
	int r;

	log_debug("dbt_janitor: janitor thread running");

	if (pthread_mutex_lock(&dbt_janitor_mutex))
	{
		log_error("dbt_janitor: pthread_mutex_lock");
		return NULL;
	}

	while(dbt_janitor_running)
	{
		if (util_now(&ts))
		{
			log_error("client_main: util_now failed");
			return NULL;
		}

		now = ts.tv_sec;

		sht_rewind(dbt_tables);
		while ((dbt = sht_next(dbt_tables)))
		{
			/*
			 * Check if table needs a clean up
			 */
			if (now < dbt->dbt_cleanup_schedule)
			{
				continue;
			}

			deleted = dbt_janitor_cleanup(now, dbt);
			if (deleted == -1)
			{
				log_error("dbt_janitor: dbt_janitor_cleanup "
				    "failed");
			}

			/*
			 * Schedule next cleanup cycle
			 */
			DBT_SCHEDULE_CLEANUP(dbt, now);
		}

		/*
		 * Schedule next run
		 */
		schedule = 0xffffffff;

		sht_rewind(dbt_tables);
		while ((dbt = sht_next(dbt_tables)))
		{
			if (dbt->dbt_cleanup_schedule < schedule)
			{
				schedule = dbt->dbt_cleanup_schedule;
			}
		}

		/*
		 * Happens only if no table is registed
		 */
		if (schedule == 0xffffffff)
		{
			schedule = now + 1;
		}

		log_debug("dbt_janitor: sleeping for %lu seconds",
		    schedule - now);

		ts.tv_sec = schedule;

		/*
		 * Suspend execution
		 */
		r = pthread_cond_timedwait(&dbt_janitor_cond,
		    &dbt_janitor_mutex, &ts);

		/*
		 * Signaled wakeup or timeout
		 */
		if (r == 0 || r == ETIMEDOUT)
		{
			continue;
		}

		log_error("client_main: pthread_cond_wait");
		break;
	}

	log_debug("dbt_janitor: shutdown");

	/*
	 * Unlock janitor mutex
	 */
	if (pthread_mutex_unlock(&dbt_janitor_mutex))
	{
		log_error("dbt_janitor: pthread_mutex_unlock");
	}

	return NULL;
}


void
dbt_init(void)
{
	/*
	 * Initialize driver table
	 */
	dbt_drivers = sht_create(DBT_BUCKETS, NULL);
	if(dbt_drivers == NULL) {
		log_die(EX_SOFTWARE, "dbt_init: sht_create failed");
	}

	/*
	 * Load database drivers
	 */
	MODULE_LOAD_DB;

	/*
	 * Initailaize tables
	 */
	dbt_tables = sht_create(DBT_BUCKETS, (sht_delete_t) dbt_close);
	if (dbt_tables == NULL) {
		log_die(EX_SOFTWARE, "dbt_init: ht_init failed");
	}

	/*
	 * Load table modules
	 */
	MODULE_LOAD_TABLE;

	/*
	 * Start table janitor thread
	 */
	if (util_thread_create(&dbt_janitor_thread, &dbt_janitor_attr,
	    dbt_janitor))
	{
		log_die(EX_SOFTWARE, "dbt_init: util_thread_create failed");
	}

	/*
	 * Start sync threads
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
	if (pthread_mutex_lock(&dbt_janitor_mutex))
	{
		log_error("dbt_janitor_clear: pthread_mutex_lock");
	}

	dbt_janitor_running = 0;

	if (pthread_cond_signal(&dbt_janitor_cond))
	{
		log_error("dbt_janitor_clear: pthread_cond_signal");
	}

	if (pthread_mutex_unlock(&dbt_janitor_mutex))
	{
		log_error("dbt_janitor_clear: pthread_mutex_unlock");
	}

	if (pthread_join(dbt_janitor_thread, NULL))
	{
		log_error("dbt_clear: pthread_mutex_join");
	}

	client_clear();
	server_clear();

	sht_delete(dbt_drivers);
	sht_delete(dbt_tables);

	return;
}


dbt_t *
dbt_lookup(char *name)
{
	return sht_lookup(dbt_tables, name);
}
