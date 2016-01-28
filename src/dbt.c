#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>

// Required for testing
#include <netinet/in.h>

#include <mopher.h>

#define DBT_BUCKETS 64
#define BUFLEN 8192
#define FIELD_MAXLEN 256
#define EXPIRE_SUFFIX "_expire"

#define DBT_STRESS_ROUNDS 100

/*
 * dbt_test_stage1 time limit
 */
#define DBT_TEST_EXPIRE 300

static sht_t *dbt_drivers;
static sht_t *dbt_tables;
static var_t *dbt_default_database;
static int dbt_threads_running;

static int		dbt_janitor_running;
static pthread_t	dbt_janitor_thread;
static pthread_mutex_t	dbt_janitor_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	dbt_janitor_cond = PTHREAD_COND_INITIALIZER;

static char           **dbt_dump_buffer;
static int              dbt_dump_buffer_size;
static pthread_mutex_t  dbt_dump_mutex = PTHREAD_MUTEX_INITIALIZER;

static int dbt_sync = 1;

/*
 * Used to avoid circular include
 */
int sql_db_get(void *dbt, var_t *record, var_t **result);
int sql_db_set(void *dbt, var_t *record);
int sql_db_del(void *dbt, var_t *record);
int sql_db_walk(void *dbt, dbt_db_callback_t callback);
int sql_db_cleanup(void *dbt);

void
dbt_driver_register(dbt_driver_t *dd)
{
	if (dd->dd_use_sql)
	{
		dd->dd_get = sql_db_get;
		dd->dd_set = sql_db_set;
		dd->dd_del = sql_db_del;
		dd->dd_walk = sql_db_walk;
	}

	if((sht_insert(dbt_drivers, dd->dd_name, dd)) == -1)
	{
		log_die(EX_SOFTWARE, "dbt_driver_register: sht_insert for "
		    "driver \"%s\" failed", dd->dd_name);
	}

	log_info("dbt_driver_register: database driver \"%s\" registered",
	    dd->dd_name);

	return;
}


static void
dbt_close(dbt_t *dbt)
{
	if (!dbt->dbt_open)
	{
		return;
	}

	if ((dbt->dbt_driver->dd_flags & DBT_LOCK))
	{
		if(pthread_mutex_destroy(&dbt->dbt_mutex))
		{
			log_sys_error("dbt_close: ptrhead_mutex_destroy");
		}
	}

	DBT_DB_CLOSE(dbt);

	if (dbt->dbt_scheme)
	{
		var_delete(dbt->dbt_scheme);
	}

	dbt->dbt_open = 0;

	return;
}

static int
dbt_lock(dbt_t *dbt)
{
	/*
         * No locking required.
	 */
	if ((dbt->dbt_driver->dd_flags & DBT_LOCK) == 0)
	{
		return 0;
	}

	if (pthread_mutex_lock(&dbt->dbt_mutex))
	{
		log_sys_error("dbt_lock: pthread_mutex_lock");
		return -1;
	}

	return 0;
}

static void
dbt_unlock(dbt_t *dbt)
{
	if ((dbt->dbt_driver->dd_flags & DBT_LOCK) == 0)
	{
		return;
	}

	if (pthread_mutex_unlock(&dbt->dbt_mutex))
	{
		log_sys_error("dbt_unlock: pthread_mutex_unlock");
	}

	return;
}

int
dbt_db_get(dbt_t *dbt, var_t *record, var_t **result)
{
	int r;

	*result = NULL;

	if (dbt_lock(dbt))
	{
		return -1;
	}

	r = dbt->dbt_driver->dd_get(dbt, record, result);
	if (r < 0 && cf_dbt_fatal_errors)
	{
		log_die(EX_SOFTWARE, "fatal database error");
	}

	dbt_unlock(dbt);

	return r;
}


int
dbt_db_set(dbt_t *dbt, var_t *record)
{
	int r;

	if (dbt_lock(dbt))
	{
		return -1;
	}

	if (dbt_sync)
	{
		client_sync(dbt, record);
	}

	r = dbt->dbt_driver->dd_set(dbt, record);
	if (r < 0 && cf_dbt_fatal_errors)
	{
		log_die(EX_SOFTWARE, "fatal database error");
	}

	dbt_unlock(dbt);

	return r;
}


int
dbt_db_del(dbt_t *dbt, var_t *record)
{
	int r;

	if (dbt_lock(dbt))
	{
		return -1;
	}

	r = dbt->dbt_driver->dd_del(dbt, record);
	if (r < 0 && cf_dbt_fatal_errors)
	{
		log_die(EX_SOFTWARE, "fatal database error");
	}

	dbt_unlock(dbt);

	return r;
}


int
dbt_db_walk(dbt_t *dbt, dbt_db_callback_t callback)
{
	int r;

	if(dbt_lock(dbt) == -1)
	{
		log_error("dbt_db_walk: dbt_lock failed");
		return -1;
	}
	
	r = dbt->dbt_driver->dd_walk(dbt, callback);
	if (r < 0 && cf_dbt_fatal_errors)
	{
		log_die(EX_SOFTWARE, "fatal database error");
	}

	dbt_unlock(dbt);

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

	if (dbt_lock(dbt))
	{
		return -1;
	}

	r = dbt->dbt_driver->dd_sync(dbt);

	dbt_unlock(dbt);

	return r;
}

int
dbt_db_cleanup(dbt_t *dbt)
{
	int r;

	if (dbt_lock(dbt))
	{
		return -1;
	}

	if (!dbt->dbt_driver->dd_use_sql)
	{
		log_die(EX_SOFTWARE, "dbt_db_cleanup: can only be called for"
			" SQL drivers");
	}

	r = sql_db_cleanup(dbt);
	if (r < 0 && cf_dbt_fatal_errors)
	{
		log_die(EX_SOFTWARE, "fatal database error");
	}

	dbt_unlock(dbt);

	return r;
}


int
dbt_db_get_from_table(dbt_t *dbt, var_t *attrs, var_t **record)
{
	var_t *lookup = NULL;
	var_t *v, *template;
	ll_t *scheme_ll, *lookup_ll;
	ll_entry_t *scheme_pos, *lookup_pos;

	lookup = VAR_COPY(dbt->dbt_scheme);
	if (lookup == NULL)
	{
		log_error("dbt_db_get_from_table: VAR_COPY failed");
		return -1;
	}

	scheme_ll = dbt->dbt_scheme->v_data;
	scheme_pos = LL_START(scheme_ll);

	lookup_ll = lookup->v_data;
	lookup_pos = LL_START(lookup_ll);

	for (;;)
	{
		template = ll_next(scheme_ll, &scheme_pos);
		v = ll_next(lookup_ll, &lookup_pos);

		if (v == NULL || template == NULL)
		{
			break;
		}

		if ((template->v_flags & VF_KEY) == 0)
		{
			continue;
		}

		v->v_flags |= VF_KEY;

		if (v->v_data != NULL)
		{
			log_error(
			    "dbt_db_get_from_table: scheme contains data");
			goto error;
		}

		v->v_data = vtable_get(attrs, v->v_name);
		if (v->v_data == NULL)
		{
			log_error("dbt_db_get_from_table: required key "
			    "attribute \"%s\" for table \"%s\" not set",
			    v->v_name, dbt->dbt_name);
			goto error;
		}

		v->v_flags |= VF_KEEPDATA;
	}

	if (dbt_db_get(dbt, lookup, record))
	{
		log_warning("dbt_db_get_from_table: dbt_db_get failed");
		goto error;
	}

	if (*record)
	{
		log_debug("dbt_db_get_from_table: record found");
	}
	else
	{
		log_debug("dbt_db_get_from_table: no record");
	}

	var_delete(lookup);

	return 0;


error:

	if (lookup)
	{
		var_delete(lookup);
	}

	return -1;
}


int
dbt_db_load_into_table(dbt_t *dbt, var_t *table)
{
	var_t *record;
	ll_t *list;
	ll_entry_t *pos;
	var_t *v;
	void *data;

	if (dbt_db_get_from_table(dbt, table, &record))
	{
		log_error("dbt_db_load_into_tabe: dbt_db_get_from_table "
		    "failed");
		return -1;
	}

	list = record == NULL ? dbt->dbt_scheme->v_data : record->v_data;

	pos = LL_START(list);
	while ((v = ll_next(list, &pos)))
	{
		/*
		 * Keys are already stored in the table
		 */
		if ((v->v_flags & VF_KEY))
		{
			continue;
		}

		/*
	 	 * If not found set table entries to NULL
	 	 */
		data = record == NULL ? NULL : v->v_data;

		/*
		 * Do not overwrite existing data
		 */
		if (vtable_get(table, v->v_name))
		{
			log_debug("dbt_db_load_into_table: key \"%s\" exists",
			    v->v_name);

			continue;
		}

		if (vtable_set_new(table, v->v_type, v->v_name, data, VF_COPY))
		{
			log_error("dbt_db_load_into_table: var_table_set_new "
			    "failed");

			return -1;
		}
	}

	if (record)
	{
		var_delete(record);
	}

	return 0;
}


int
dbt_register(char *name, dbt_t *dbt)
{
	var_t *config;
	char *config_key;

	config_key = dbt->dbt_config_key == NULL? name: dbt->dbt_config_key;

	/*
	 * Load config table
	 */
	config = cf_get(VT_TABLE, "table", config_key, NULL);
	if (config == NULL) {
		if (dbt_default_database != NULL)
		{
			config = dbt_default_database;
		}
		else
		{
			log_error("dbt_register: missing database configuration for"
				" \"%s\" and default_database is not set", name);
			return -1;
		}
	}

	/*
	 * Fill configuration into dbt. CAVEAT: No real error checking. A bad
	 * or incomplete configuration should be determined by the database
	 * driver.
	 */
	if (vtable_dereference(config, "driver", &dbt->dbt_drivername,
	    "path", &dbt->dbt_path, "host", &dbt->dbt_host,
	    "port", &dbt->dbt_port, "user", &dbt->dbt_user,
	    "pass", &dbt->dbt_pass, "database", &dbt->dbt_database,
	    NULL) == -1)
	{
		log_error("dbt_register: vtable_dereference failed");
		return -1;
	}

	/*
	 * dbt_name is used by the janitor
	 */
	dbt->dbt_name = name;

	/*
	 * Set driver to NULL (filled in upon connection)
	 */
	dbt->dbt_driver = NULL;

	/*
	 * Add some defaults
	 */
	if (dbt->dbt_database == NULL) {
		dbt->dbt_database = BINNAME;
	}

	if (dbt->dbt_cleanup_interval == 0) {
		dbt->dbt_cleanup_interval = cf_dbt_cleanup_interval;
	}

	if (strlen(dbt->dbt_expire_field) == 0)
	{
		snprintf(dbt->dbt_expire_field, DBT_FIELD_MAX, "%s%s", dbt->dbt_name,
			EXPIRE_SUFFIX);
	}

	/*
	 * Lock janitor mutex in case the janitor is already working
	 */
	if (pthread_mutex_lock(&dbt_janitor_mutex))
	{
		log_sys_error("dbt_register: pthread_mutex_lock");
		return -1;
	}

	/*
	 * Store dbt in dbt_tables
	 */
	if (sht_insert(dbt_tables, name, dbt)) {
		log_error("dbt_register: ht_insert failed");
		return -1;
	}

	if (pthread_mutex_unlock(&dbt_janitor_mutex))
	{
		log_sys_error("dbt_register: pthread_mutex_unlock");
		return -1;
	}

	return 0;
}


int
dbt_common_validate(dbt_t *dbt, var_t *record)
{
	VAR_INT_T *expire = NULL;

	/*
	 * Lookup expiry in record.
	 */
	expire = vlist_record_get(record, dbt->dbt_expire_field);
	if (expire == NULL)
	{
		log_die(EX_SOFTWARE, "dbt_common_vaildate: table \"%s\" must "
		    "set %s_expire to use dbt_common_validate", dbt->dbt_name,
		    dbt->dbt_name, dbt->dbt_name);
	}

	/*
	 * dbt->dbt_cleanup_schedule == time(NULL)
	 */
	if (dbt->dbt_cleanup_schedule > *expire)
	{
		return 0;
	}

	return 1;
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
	int expire;

	expire = DBT_VALIDATE(dbt, record);
	if (expire == -1) {
		log_error("dbt_cleanup: DBT_VALIDATE failed");
		return -1;
	}
	if (expire) {
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
	 * Check if driver is registered
	 */
	if (dbt->dbt_driver == NULL)
	{
		log_debug("dbt_janitor_cleanup: database \"%s\" not connected "
		    "yet", dbt->dbt_name);
		return 0;
	}

	/*
	 * Check if driver supports SQL
	 */
	if (dbt->dbt_driver->dd_use_sql)
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
		 * No validate callback registered. Not cleaning.
		 */
		else
		{
			log_debug("dbt_janitor_cleanup: database \"%s\" has "
			    "no validate callback. Not cleaning.",
			    dbt->dbt_name);
			deleted = 0;
		}
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

	if (deleted)
	{
		log_debug("dbt_janitor_cleanup: disposed %d stale record%s "
			"from %s", deleted, deleted > 1 ? "s" : "", dbt->dbt_name);
	}

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
	ht_pos_t pos;
	char log_message[BUFLEN];
	int log_len;

	log_debug("dbt_janitor: janitor thread running");

	dbt_janitor_running = 1;

	if (pthread_mutex_lock(&dbt_janitor_mutex))
	{
		log_sys_error("dbt_janitor: pthread_mutex_lock");
		return NULL;
	}

	while(dbt_janitor_running)
	{
		if (util_now(&ts))
		{
			log_error("dbt_janitor: util_now failed");
			return NULL;
		}

		/*
 		 * Call watchdog to log stale connections
		 */
		if (cf_watchdog_stage_timeout)
		{
			watchdog_check();
		}

		now = ts.tv_sec;

		// Clear log message
		log_message[0] = 0;
		log_len = 0;

		sht_start(dbt_tables, &pos);
		while ((dbt = sht_next(dbt_tables, &pos)))
		{
			if (dbt->dbt_cleanup_interval == -1)
			{
				continue;
			}

			if (dbt->dbt_cleanup_schedule == 0)
			{
				dbt->dbt_cleanup_schedule = now;
			}

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

			if (deleted > 0)
			{
				log_len += snprintf(log_message + log_len,
					sizeof log_message - log_len, " %s=%d",
					dbt->dbt_name, deleted);
				if (log_len >= sizeof log_message)
				{
					log_error("dbt_janitor: log buffer exhausted");
					log_len = 0;
					log_message[0] = 0;
				}
			}

			/*
			 * Schedule next cleanup cycle
			 */
			dbt->dbt_cleanup_schedule =
				now + dbt->dbt_cleanup_interval;
		}

		if (log_len)
		{
			log_error("database cleanup: deleted:%s",
				log_message);
		}

		/*
		 * Schedule next run
		 */
		schedule = 0xffffffff;

		sht_start(dbt_tables, &pos);
		while ((dbt = sht_next(dbt_tables, &pos)))
		{
			if (dbt->dbt_cleanup_interval == -1)
			{
				continue;
			}

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

		log_sys_error("client_main: pthread_cond_timedwait");
		break;
	}

	log_debug("dbt_janitor: shutdown");

	/*
	 * Unlock janitor mutex
	 */
	if (pthread_mutex_unlock(&dbt_janitor_mutex))
	{
		log_sys_error("dbt_janitor: pthread_mutex_unlock");
	}

	return NULL;
}


int
dbt_open_database(dbt_t *dbt)
{
	dbt_driver_t *dd;

	/*
	 * Lookup database driver
	 */
	dd = sht_lookup(dbt_drivers, dbt->dbt_drivername);
	if (dd == NULL)
	{
		log_error("dbt_open_database: unknown database driver \"%s\"",
			dbt->dbt_drivername);
		return -1;
	}

	dbt->dbt_driver = dd;

	/*
	 * Initialize mutex if driver requires locking
	 */
	if ((dd->dd_flags & DBT_LOCK))
	{
		if(pthread_mutexattr_init(&dbt->dbt_mutexattr))
		{
			log_error("dbt_open_database: ptrhead_mutexattr_init failed");
			return -1;
		}
		if(pthread_mutexattr_settype(&dbt->dbt_mutexattr, PTHREAD_MUTEX_RECURSIVE))
		{
			log_error("dbt_open_database: ptrhead_mutexattr_settype failed");
			return -1;
		}
		if(pthread_mutex_init(&dbt->dbt_mutex, &dbt->dbt_mutexattr))
		{
			log_error("dbt_open_database: ptrhead_mutex_init failed");
			return -1;
		}
	}

	/*
	 * Open database
	 */
	log_debug("dbt_open_database: open table %s (%s)", dbt->dbt_name, dbt->dbt_drivername);

	if (dbt->dbt_driver->dd_open(dbt))
	{
		log_error("dbt_open_database: can't open \"%s\"",
			dbt->dbt_name);
		return -1;
	}

	// Create tables in SQL databases
	if (dbt->dbt_driver->dd_use_sql)
	{
		sql_open(&dbt->dbt_driver->dd_sql, dbt->dbt_handle,
			dbt->dbt_scheme);
	}

	dbt->dbt_open = 1;

	return 0;
}


void
dbt_open_databases(void)
{
	dbt_t *dbt;
	ht_pos_t pos;

	/*
	 * Open all tables
	 */
	sht_start(dbt_tables, &pos);
	while ((dbt = sht_next(dbt_tables, &pos)))
	{
		if (dbt_open_database(dbt))
		{
			log_die(EX_SOFTWARE, "dbt_init: cannot open database "
				"%s", dbt->dbt_name);
		}
	}

	/*
	 * Start table janitor thread
	 */
	if (util_thread_create(&dbt_janitor_thread, dbt_janitor, NULL))
	{
		log_die(EX_SOFTWARE, "dbt_init: util_thread_create failed");
	}

	return;
}


void
dbt_init(int start_threads)
{
	/*
	 * Initialize driver table
	 */
	dbt_drivers = sht_create(DBT_BUCKETS, NULL);
	if(dbt_drivers == NULL) {
		log_die(EX_SOFTWARE, "dbt_init: sht_create failed");
	}

	/*
	 * Initailaize tables
	 */
	dbt_tables = sht_create(DBT_BUCKETS, (sht_delete_t) dbt_close);
	if (dbt_tables == NULL) {
		log_die(EX_SOFTWARE, "dbt_init: ht_init failed");
	}

	/*
	 * Load database defaults
	 */
	dbt_default_database = cf_get(VT_TABLE, "default_database", NULL);
	if (dbt_default_database == NULL) {
		log_debug("dbt_init: no default_database found");
	}

	/*
	 * Start sync threads
	 */
	if (start_threads)
	{
		if (server_init())
		{
			log_die(EX_SOFTWARE, "dbt_init: server_init failed");
		}

		if (client_init())
		{
			log_die(EX_SOFTWARE, "dbt_init: client_init failed");
		}

		dbt_threads_running = 1;
	}

	return;
}


void
dbt_clear()
{
	if (dbt_threads_running)
	{
		dbt_threads_running = 0;

		client_clear();
		server_clear();
	}

	if (dbt_janitor_running)
	{
		if (pthread_mutex_lock(&dbt_janitor_mutex))
		{
			log_sys_error("dbt_janitor_clear: pthread_mutex_lock");
		}

		dbt_janitor_running = 0;

		if (pthread_cond_signal(&dbt_janitor_cond))
		{
			log_sys_error("dbt_janitor_clear: pthread_cond_signal");
		}

		if (pthread_mutex_unlock(&dbt_janitor_mutex))
		{
			log_sys_error("dbt_janitor_clear: pthread_mutex_unlock");
		}

		util_thread_join(dbt_janitor_thread);
	}

	if (dbt_drivers)
	{
		sht_delete(dbt_drivers);
	}

	if (dbt_tables)
	{
		sht_delete(dbt_tables);
	}

	return;
}


dbt_t *
dbt_lookup(char *name)
{
	return sht_lookup(dbt_tables, name);
}


/*
 * CAVEAT: dbt_dump_record is not thread safe. Needs to run in locked
 * context.
 */
int
dbt_dump_record(dbt_t *dbt, var_t *record)
{
	char buffer[BUFLEN];
	int len;

	len = var_dump_data(record, buffer, sizeof buffer);
	if (len == -1)
	{
		return -1;
	}

	*dbt_dump_buffer = realloc(*dbt_dump_buffer, dbt_dump_buffer_size + len + 2);
	if (*dbt_dump_buffer == NULL)
	{
		log_sys_error("dbt_dump_record: realloc");
		return -1;
	}

	snprintf(*dbt_dump_buffer + dbt_dump_buffer_size, len + 2, "%s\n", buffer);

	// Add the record an a trailing newline
	dbt_dump_buffer_size += len + 1;

	return 0;
}

int
dbt_dump(char **dump, char *tablename)
{
	dbt_t *table;

	table = dbt_lookup(tablename);
	if (table == NULL)
	{
		log_error("dbt_dump: dbt_lookup failed for table: %s", tablename);
		return -1;
	}

	/*
	 * arguments to dbt_dump_record are passed through static globals
	 * hence dbt_dump is mutex.
	 */
	if (pthread_mutex_lock(&dbt_dump_mutex))
	{
		log_sys_error("dbt_dump: pthread_mutex_lock");
		return -1;
	}

	/*
	 * For safety we require *dump to be NULL.
	 */
	if (*dump)
	{
		dbt_dump_buffer_size = -1;
		goto error;
	}

	dbt_dump_buffer = dump;
	dbt_dump_buffer_size = 0;

	dbt_db_walk(table, (dbt_db_callback_t) dbt_dump_record);

	if (dbt_dump_buffer_size == -1)
	{
		if (*dbt_dump_buffer)
		{
			free(*dbt_dump_buffer);
			*dbt_dump_buffer = NULL;
		}

		log_error("dbt_dump: dbt_dump_record failed");

		goto error;
	}

error:
	if (pthread_mutex_unlock(&dbt_dump_mutex))
	{
		log_sys_error("dbt_dump: pthread_mutex_unlock");
	}

	return dbt_dump_buffer_size;
}

#ifdef DEBUG

static int dbt_test_stage;
static int dbt_test_run_stage2;

static dbt_t dbt_test_table;
static var_t *dbt_test_scheme;
static time_t dbt_test_time;
static sht_t dbt_test_ht;
static pthread_mutex_t dbt_test_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct dbt_test_record {
	VAR_INT_T       tr_int_key;
	VAR_FLOAT_T     tr_float_key;
	char            tr_string_key[20];
	var_sockaddr_t  tr_sockaddr_key;
	VAR_INT_T       tr_int_value;
	VAR_FLOAT_T     tr_float_value;
	char            tr_string_value[20];
	char            tr_blob1_raw[256];
	char            tr_blob2_raw[256];
	blob_t         *tr_blob1;
	blob_t         *tr_blob2;
	var_sockaddr_t  tr_sockaddr_value;
	char           *tr_chars;
	char           *tr_null;
	VAR_INT_T       tr_test_created;
	VAR_INT_T       tr_test_updated;
	VAR_INT_T       tr_test_expire;
} dbt_test_record_t;

var_t *
dbt_test_record(dbt_test_record_t *tr, var_t *scheme, int n, int expire)
{
	struct sockaddr_in *sin;

	memset(tr, 0, sizeof (dbt_test_record_t));

	// Every thread creates a different record
	tr->tr_int_key = n;
	tr->tr_float_key = n * 0.5;
	snprintf(tr->tr_string_key, 19, "KEY: %d", n);
	sin = (struct sockaddr_in *) &tr->tr_sockaddr_key;
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = (n * 0x01010101);

	tr->tr_int_value = n;
	tr->tr_float_value = n * 0.5;
	snprintf(tr->tr_string_value, 19, "VALUE: %d", n);
	sin = (struct sockaddr_in *) &tr->tr_sockaddr_value;
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = (n * 0x01010101);

	char *text = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	blob_init(tr->tr_blob1_raw, sizeof tr->tr_blob1_raw, (void *) &n, sizeof n);
	blob_init(tr->tr_blob2_raw, sizeof tr->tr_blob2_raw, (void *) text, strlen(text) + 1);
	tr->tr_blob1 = (blob_t *) tr->tr_blob1_raw;
	tr->tr_blob2 = (blob_t *) tr->tr_blob2_raw;

	tr->tr_chars = "'\"~!@#$%^&*()_";
	tr->tr_null = NULL;

	tr->tr_test_created = dbt_test_time;
	tr->tr_test_updated = dbt_test_time;

	tr->tr_test_expire = dbt_test_time + expire;

	return vlist_record(scheme, &tr->tr_int_key, &tr->tr_float_key,
		tr->tr_string_key, &tr->tr_sockaddr_key, &tr->tr_int_value,
		&tr->tr_float_value, tr->tr_string_value,
		tr->tr_blob1, tr->tr_blob2,
		&tr->tr_sockaddr_value, tr->tr_chars,
		tr->tr_null, &tr->tr_test_created, &tr->tr_test_updated,
		&tr->tr_test_expire);
}

void
dbt_test_stage1(int n)
{
	dbt_test_record_t tr1;
	dbt_test_record_t tr2;
	var_t *record1= NULL;
	var_t *record2 = NULL;
	var_t *lookup= NULL;
	var_t *result = NULL;
	char rec1_str[BUFLEN];
	char rec2_str[BUFLEN];
	char rec_match[BUFLEN];
	int i;

	// Create test record data	
	record1 = dbt_test_record(&tr1, dbt_test_scheme, n, DBT_TEST_EXPIRE);
	record2 = dbt_test_record(&tr2, dbt_test_scheme, n + 10000, DBT_TEST_EXPIRE);
	TEST_ASSERT(record1 != NULL && record2 != NULL);
	TEST_ASSERT(var_dump(record1, rec1_str, sizeof rec1_str) > 0);
	TEST_ASSERT(var_dump(record2, rec2_str, sizeof rec2_str) > 0);

	// Add record strings to test hash table (used in stage 2).
	pthread_mutex_lock(&dbt_test_mutex);
	TEST_ASSERT(sht_insert(&dbt_test_ht, rec1_str, rec1_str) == 0);
	TEST_ASSERT(sht_insert(&dbt_test_ht, rec2_str, rec2_str) == 0);
	//printf("1: %s\n2: %s\n\n", rec1_str, rec2_str);
	pthread_mutex_unlock(&dbt_test_mutex);

	// Lookup record
	lookup = vlist_record(dbt_test_scheme, &tr1.tr_int_key, &tr1.tr_float_key,
		tr1.tr_string_key, &tr1.tr_sockaddr_key, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

	// Stress test
	for (i = 0; i < DBT_STRESS_ROUNDS; ++i)
	{
		// Insert
		TEST_ASSERT(dbt_db_set(&dbt_test_table, record1) == 0);
		TEST_ASSERT(dbt_db_sync(&dbt_test_table) == 0);

		// Select
		TEST_ASSERT(dbt_db_get(&dbt_test_table, lookup, &result) == 0);
		TEST_ASSERT(result != NULL);
		TEST_ASSERT(var_dump(result, rec_match, sizeof rec_match) > 0);
		TEST_ASSERT(strcmp(rec1_str, rec_match) == 0);
		var_delete(result);
		result = NULL;

		// Update
		TEST_ASSERT(dbt_db_set(&dbt_test_table, record1) == 0);
		TEST_ASSERT(dbt_db_sync(&dbt_test_table) == 0);
		TEST_ASSERT(dbt_db_get(&dbt_test_table, lookup, &result) == 0);
		TEST_ASSERT(result != NULL);
		TEST_ASSERT(var_dump(result, rec_match, sizeof rec_match) > 0);
		TEST_ASSERT(strcmp(rec1_str, rec_match) == 0);
		var_delete(result);
		result = NULL;

		// Delete
		TEST_ASSERT(dbt_db_del(&dbt_test_table, lookup) == 0);
		TEST_ASSERT(dbt_db_get(&dbt_test_table, lookup, &result) == 0);
		TEST_ASSERT(result == NULL);
		TEST_ASSERT(dbt_db_sync(&dbt_test_table) == 0);
	}

	// Add records for dbt_test_stage2()
	TEST_ASSERT(dbt_db_set(&dbt_test_table, record1) == 0);
	TEST_ASSERT(dbt_db_sync(&dbt_test_table) == 0);
	TEST_ASSERT(dbt_db_set(&dbt_test_table, record2) == 0);
	TEST_ASSERT(dbt_db_sync(&dbt_test_table) == 0);

	var_delete(record1);
	var_delete(record2);
	var_delete(lookup);

	return;
}

int
dbt_test_walk(dbt_t *dbt, var_t *record)
{
	char rec_match[BUFLEN];

	TEST_ASSERT(var_dump(record, rec_match, sizeof rec_match) > 0);
	TEST_ASSERT(sht_lookup(&dbt_test_ht, rec_match) != NULL);

	return 0;
}

void
dbt_test_stage2(int n)
{
	dbt_test_record_t tr1;
	dbt_test_record_t tr2;
	var_t *record1= NULL;
	var_t *record2= NULL;
	var_t *result = NULL;
	char rec1_str[BUFLEN];
	char rec2_str[BUFLEN];
	char rec_match[BUFLEN];

	// Create records
	record1 = dbt_test_record(&tr1, dbt_test_scheme, n, DBT_TEST_EXPIRE);
	record2 = dbt_test_record(&tr2, dbt_test_scheme, n + 10000, DBT_TEST_EXPIRE);
	TEST_ASSERT(record1 != NULL && record2 != NULL);
	TEST_ASSERT(var_dump(record1, rec1_str, sizeof rec1_str) > 0);
	TEST_ASSERT(var_dump(record2, rec2_str, sizeof rec2_str) > 0);

	// Walk table
	TEST_ASSERT(dbt_db_walk(&dbt_test_table, (void *) dbt_test_walk) == 0);

	// Lookup record 1
	TEST_ASSERT(dbt_db_get(&dbt_test_table, record1, &result) == 0);
	TEST_ASSERT(result != NULL);
	TEST_ASSERT(var_dump(result, rec_match, sizeof rec_match) > 0);
	TEST_ASSERT(strcmp(rec1_str, rec_match) == 0);
	var_delete(record1);
	var_delete(result);

	// Lookup record 2
	TEST_ASSERT(dbt_db_get(&dbt_test_table, record2, &result) == 0);
	TEST_ASSERT(result != NULL);
	TEST_ASSERT(var_dump(result, rec_match, sizeof rec_match) > 0);
	TEST_ASSERT(strcmp(rec2_str, rec_match) == 0);
	var_delete(record2);
	var_delete(result);

	return;
}

int
dbt_test_init(char *config_key, char *driver, int run_stage2)
{
	// Check if driver exists
	if (!module_exists(driver))
	{
		log_crit("dbt_test_prepare: %s not installed", driver);
		return -1;
	}

	// Create test scheme
	dbt_test_scheme = vlist_scheme("test",
		"test_int_key",		VT_INT,		VF_KEEPNAME | VF_KEY,
		"test_float_key",	VT_FLOAT,	VF_KEEPNAME | VF_KEY,
		"test_string_key",	VT_STRING,	VF_KEEPNAME | VF_KEY,
		"test_addr_key",	VT_ADDR,	VF_KEEPNAME | VF_KEY,
		"test_int",		VT_INT,		VF_KEEPNAME,
		"test_float",		VT_FLOAT,	VF_KEEPNAME,
		"test_string",		VT_STRING,	VF_KEEPNAME,
		"test_blob1",		VT_BLOB,	VF_KEEPNAME,
		"test_blob2",		VT_BLOB,	VF_KEEPNAME,
		"test_addr",		VT_ADDR,	VF_KEEPNAME,
		"test_chars",		VT_STRING,	VF_KEEPNAME,
		"test_null",		VT_STRING,	VF_KEEPNAME,
		"test_created",		VT_INT,		VF_KEEPNAME,
		"test_updated",		VT_INT,		VF_KEEPNAME,
		"test_expire",		VT_INT,		VF_KEEPNAME,
		NULL);

	dbt_test_run_stage2 = run_stage2;
	
	// Runs only once before stage 1
	if (dbt_test_stage == 0)
	{
		// Make sure stage1 and stage2 use the same value for test_time
		dbt_test_time = time(NULL);

		// Recordlist for db_walk testing
		sht_init(&dbt_test_ht, 1024, NULL);
	}

	++dbt_test_stage;

	// Init dbt_test_table
	dbt_test_table.dbt_scheme = dbt_test_scheme;
	dbt_test_table.dbt_validate = dbt_common_validate;
	dbt_test_table.dbt_config_key = config_key;
	dbt_test_table.dbt_cleanup_schedule = dbt_test_time;

	// Init prerequisites
	cf_init();
	dbt_init(0);

	// Load database drivers
	module_init(0, driver, NULL);

	// Register test table
	dbt_register("test", &dbt_test_table);

	// Set dbt_sync to 0 to avoid calling client_sync
	dbt_sync = 0;

	// Open database
	dbt_open_database(&dbt_test_table);

	// Clean table
	dbt_janitor_cleanup(time(NULL), &dbt_test_table);

	return 0;
}

void
dbt_test_clear(void)
{
	// Both stages finished
	if (dbt_test_stage == 2 || dbt_test_run_stage2 == 0)
	{
		sht_clear(&dbt_test_ht);
		dbt_test_stage = 0;
	}

	dbt_clear();
	module_clear();
	cf_clear();

	return;
}

int
dbt_test_memdb_init(void)
{
	return dbt_test_init("test_memdb", "memdb.so", 0);
}

int
dbt_test_bdb_init(void)
{
	return dbt_test_init("test_bdb", "bdb.so", 1);
}

int
dbt_test_lite_init(void)
{
	return dbt_test_init("test_lite", "lite.so", 1);
}

int
dbt_test_pgsql_init(void)
{
	return dbt_test_init("test_pgsql", "pgsql.so", 1);
}

int
dbt_test_mysql_init(void)
{
	return dbt_test_init("test_mysql", "sakila.so", 1);
}

#endif
