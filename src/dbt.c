#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>

// Required for testing
#include <netinet/in.h>

#include <mopher.h>

#define DBT_BUCKETS 32
#define BUFLEN 1024
#define KEYLEN 128

static sht_t *dbt_drivers;
static sht_t *dbt_tables;
static int dbt_threads_running;

static int		dbt_janitor_running;
static pthread_t	dbt_janitor_thread;
static pthread_mutex_t	dbt_janitor_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	dbt_janitor_cond = PTHREAD_COND_INITIALIZER;

static char           **dbt_dump_buffer;
static int              dbt_dump_buffer_size;
static pthread_mutex_t  dbt_dump_mutex = PTHREAD_MUTEX_INITIALIZER;

static int dbt_sync = 1;

void
dbt_driver_register(dbt_driver_t *dd)
{
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
		if(pthread_mutex_destroy(&dbt->dbt_driver->dd_mutex))
		{
			log_sys_error("dbt_register: ptrhead_mutex_destroy");
		}
	}

	DBT_DB_CLOSE(dbt);

	if (dbt->dbt_scheme)
	{
		var_delete(dbt->dbt_scheme);
	}

	if (dbt->dbt_sql_invalid_free)
	{
		free(dbt->dbt_sql_invalid_where);
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

	if (pthread_mutex_lock(&dbt->dbt_driver->dd_mutex))
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

	if (pthread_mutex_unlock(&dbt->dbt_driver->dd_mutex))
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

	r = dbt->dbt_driver->dd_sql_cleanup(dbt);

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


static char *
dbt_common_sql(char *name)
{
	char buffer[BUFLEN], *p;
	int len;

	len = snprintf(buffer, sizeof buffer,
	    "`%s_expire` < unix_timestamp()", name);

	if (len >= sizeof buffer)
	{
		log_die(EX_SOFTWARE, "dbt_common_sql: buffer exhausted");
	}

	p = strdup(buffer);
	if (p == NULL)
	{
		log_sys_die(EX_OSERR, "dbt_common_sql: strdup");
	}

	return p;
}

void
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
		log_die(EX_CONFIG, "dbt_register: missing database "
			"configuration for \"%s\"", name);
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
	    "table", &dbt->dbt_table, NULL) == -1)
	{
		log_die(EX_CONFIG,
		    "dbt_register: vtable_dereference failed");
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
	if (dbt->dbt_table == NULL) {
		dbt->dbt_table = name;
	}

	if (dbt->dbt_database == NULL) {
		dbt->dbt_database = BINNAME;
	}

	if (dbt->dbt_cleanup_interval == 0) {
		dbt->dbt_cleanup_interval = cf_dbt_cleanup_interval;
	}

	if (dbt->dbt_sql_invalid_where)
	{
		if (strcmp(dbt->dbt_sql_invalid_where, "COMMON") == 0)
		{
			dbt->dbt_sql_invalid_where =
			    dbt_common_sql(dbt->dbt_name);
			dbt->dbt_sql_invalid_free = 1;
		}
	}

	/*
	 * Lock janitor mutex in case the janitor is already working
	 */
	if (pthread_mutex_lock(&dbt_janitor_mutex))
	{
		log_sys_die(EX_SOFTWARE, "dbt_register: pthread_mutex_lock");
	}

	/*
	 * Store dbt in dbt_tables
	 */
	if (sht_insert(dbt_tables, name, dbt)) {
		log_die(EX_SOFTWARE, "dbt_register: ht_insert failed");
	}

	if (pthread_mutex_unlock(&dbt_janitor_mutex))
	{
		log_sys_die(EX_SOFTWARE, "dbt_register: pthread_mutex_unlock");
	}

	return;
}


int
dbt_common_validate(dbt_t *dbt, var_t *record)
{
	char expire_key[KEYLEN];
	VAR_INT_T *expire = NULL;

	/*
	 * created key example: greylist_created
	 */
	if (snprintf(expire_key, sizeof expire_key, "%s_expire",
	    dbt->dbt_name) >= sizeof expire_key)
	{
		log_error("dbt_common_validate: buffer exhausted");
		return -1;
	}

	/*
	 * Lookup expiry in record.
	 */
	expire = vlist_record_get(record, expire_key);

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
		log_error("Database cleaning: disposed %d stale record%s from "
			"%s table", deleted, deleted > 1 ? "s" : "",
			dbt->dbt_name);
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

		now = ts.tv_sec;

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

			/*
			 * Schedule next cleanup cycle
			 */
			dbt->dbt_cleanup_schedule =
				now + dbt->dbt_cleanup_interval;
		}

		/*
		 * Schedule next run
		 */
		schedule = 0xffffffff;

		sht_start(dbt_tables, &pos);
		while ((dbt = sht_next(dbt_tables, &pos)))
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


static void
dbt_open_database(dbt_t *dbt)
{
	dbt_driver_t *dd;

	/*
	 * Lookup database driver
	 */
	dd = sht_lookup(dbt_drivers, dbt->dbt_drivername);
	if (dd == NULL)
	{
		log_die(EX_CONFIG, "dbt_open_database: unknown database "
		    "driver \"%s\"", dbt->dbt_drivername);
	}

	dbt->dbt_driver = dd;

	/*
	 * Initialize mutex if driver requires locking
	 */
	if ((dd->dd_flags & DBT_LOCK))
	{
		if(pthread_mutexattr_init(&dd->dd_mutexattr))
		{
			log_die(EX_SOFTWARE, "dbt_open_database: ptrhead_mutexattr_init failed");
		}
		if(pthread_mutexattr_settype(&dd->dd_mutexattr, PTHREAD_MUTEX_RECURSIVE))
		{
			log_die(EX_SOFTWARE, "dbt_open_database: ptrhead_mutexattr_settype failed");
		}
		if(pthread_mutex_init(&dd->dd_mutex, &dd->dd_mutexattr))
		{
			log_die(EX_SOFTWARE, "dbt_open_database: ptrhead_mutex_init failed");
		}
	}

	/*
	 * Open database
	 */
	log_debug("dbt_open_database: open \"%s\" using \"%s\"", dbt->dbt_name,
	    dbt->dbt_drivername);

	if (dbt->dbt_driver->dd_open(dbt))
	{
		log_die(EX_CONFIG, "dbt_open_database: can't open \"%s\"",
		    dbt->dbt_name);
	}

	dbt->dbt_open = 1;

	return;
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
		dbt_open_database(dbt);
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

		client_clear();
		server_clear();
	}

	sht_delete(dbt_drivers);
	sht_delete(dbt_tables);

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
static dbt_t dbt_test_table;
static var_t *dbt_test_scheme;

// Set number of threads to test with
#define TEST_THREADS 50
static int dbt_test_threads = TEST_THREADS;
static pthread_t dbt_test_thread[TEST_THREADS];

void
dbt_test_get_scheme(void)
{
	dbt_test_scheme = vlist_scheme("test",
		"test_int_key",		VT_INT,		VF_KEEPNAME | VF_KEY,
		"test_float_key",	VT_FLOAT,	VF_KEEPNAME | VF_KEY,
		"test_string_key",	VT_STRING,	VF_KEEPNAME | VF_KEY,
		"test_addr_key",	VT_ADDR,	VF_KEEPNAME | VF_KEY,
		"test_int",		VT_INT,		VF_KEEPNAME,
		"test_float",		VT_FLOAT,	VF_KEEPNAME,
		"test_string",		VT_STRING,	VF_KEEPNAME,
		"test_addr",		VT_ADDR,	VF_KEEPNAME,
		"test_created",		VT_INT,		VF_KEEPNAME,
		"test_updated",		VT_INT,		VF_KEEPNAME,
		"test_expire",		VT_INT,		VF_KEEPNAME,
		NULL);

	return;
}

typedef struct dbt_test_record {
	VAR_INT_T       tr_int_key;
	VAR_FLOAT_T     tr_float_key;
	char            tr_string_key[20];
	var_sockaddr_t  tr_sockaddr_key;
	VAR_INT_T       tr_int_value;
	VAR_FLOAT_T     tr_float_value;
	char            tr_string_value[20];
	var_sockaddr_t  tr_sockaddr_value;
	VAR_INT_T       tr_test_created;
	VAR_INT_T       tr_test_updated;
	VAR_INT_T       tr_test_expire;
} dbt_test_record_t;

void
dbt_test_record(dbt_test_record_t *tr, int n)
{
	struct sockaddr_in *sin;

	bzero(tr, sizeof (dbt_test_record_t));

	// Every thread creates a different record
	tr->tr_int_key = n;
	tr->tr_float_key = n * 0.5;
	snprintf(tr->tr_string_key, 19, "KEY: %d", n);
	sin = (struct sockaddr_in *) &tr->tr_sockaddr_key;
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = (n * 0x10101010);

	tr->tr_int_value = n;
	tr->tr_float_value = n * 0.5;
	snprintf(tr->tr_string_value, 19, "VALUE: %d", n);
	sin = (struct sockaddr_in *) &tr->tr_sockaddr_value;
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = (n * 0x10101010);

	tr->tr_test_created = time(NULL);
	tr->tr_test_updated = tr->tr_test_created;

	// Record expired yesterday
	tr->tr_test_expire = tr->tr_test_created - 86400;
}

void
dbt_test_set(pthread_t *thread)
{
	dbt_test_record_t tr1;
	dbt_test_record_t tr2;
	int n;
	var_t *record1= NULL;
	var_t *record2 = NULL;
	var_t *lookup= NULL;
	var_t *result = NULL;

	// Calculate thread number
	n = thread - dbt_test_thread;

	// Create test record data	
	dbt_test_record(&tr1, n);
	dbt_test_record(&tr2, n + dbt_test_threads);

	// Create records
	record1 = vlist_record(dbt_test_scheme, &tr1.tr_int_key, &tr1.tr_float_key,
		tr1.tr_string_key, &tr1.tr_sockaddr_key, &tr1.tr_int_value,
		&tr1.tr_float_value, tr1.tr_string_value,
		&tr1.tr_sockaddr_value, &tr1.tr_test_created,
		&tr1.tr_test_updated, &tr1.tr_test_expire);
	lookup = vlist_record(dbt_test_scheme, &tr1.tr_int_key, &tr1.tr_float_key,
		tr1.tr_string_key, &tr1.tr_sockaddr_key, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL);
	record2 = vlist_record(dbt_test_scheme, &tr2.tr_int_key, &tr2.tr_float_key,
		tr2.tr_string_key, &tr2.tr_sockaddr_key, &tr2.tr_int_value,
		&tr2.tr_float_value, tr2.tr_string_value,
		&tr2.tr_sockaddr_value, &tr2.tr_test_created,
		&tr2.tr_test_updated, &tr2.tr_test_expire);

	TEST_ASSERT(record1 != NULL && record2 != NULL, "vlist_record failed");
	TEST_ASSERT(dbt_db_set(&dbt_test_table, record1) == 0, "dbt_db_set failed");
	TEST_ASSERT(dbt_db_sync(&dbt_test_table) == 0, "dbt_db_sync failed");
	TEST_ASSERT(dbt_db_get(&dbt_test_table, lookup, &result) == 0, "dbt_db_get failed");
	TEST_ASSERT(result != NULL, "dbt_db_get returned NULL");
	var_delete(result);
	result = NULL;

	TEST_ASSERT(dbt_db_del(&dbt_test_table, lookup) == 0, "dbt_db_del failed");
	TEST_ASSERT(dbt_db_sync(&dbt_test_table) == 0, "dbt_db_sync failed");
	TEST_ASSERT(dbt_db_get(&dbt_test_table, lookup, &result) == 0, "dbt_db_get failed");
	TEST_ASSERT(result == NULL, "dbt_db_del returned deleted record: %s", tr1.tr_string_key);

	// Add records for dbt_test_get()
	TEST_ASSERT(dbt_db_set(&dbt_test_table, record1) == 0, "dbt_db_set failed");
	TEST_ASSERT(dbt_db_sync(&dbt_test_table) == 0, "dbt_db_sync failed");
	TEST_ASSERT(dbt_db_set(&dbt_test_table, record2) == 0, "dbt_db_set failed");
	TEST_ASSERT(dbt_db_sync(&dbt_test_table) == 0, "dbt_db_sync failed");

	var_delete(record1);
	var_delete(record2);
	var_delete(lookup);

	return;
}

void
dbt_test_get(pthread_t *thread)
{
	dbt_test_record_t tr1;
	dbt_test_record_t tr2;
	int n;
	var_t *lookup= NULL;
	var_t *result = NULL;

	return;

	// Calculate thread number
	n = thread - dbt_test_thread;

	// Create record data	
	dbt_test_record(&tr1, n);
	dbt_test_record(&tr2, n + dbt_test_threads);

	// Lookup record 1
	lookup = vlist_record(dbt_test_scheme, &tr1.tr_int_key, &tr1.tr_float_key,
		tr1.tr_string_key, &tr1.tr_sockaddr_key, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL);
	TEST_ASSERT(lookup != NULL, "vlist_record failed");
	TEST_ASSERT(dbt_db_get(&dbt_test_table, lookup, &result) == 0, "dbt_db_get failed");
	TEST_ASSERT(result != NULL, "returned result is NULL");
	var_delete(lookup);
	var_delete(result);

	// Lookup record 2
	lookup = vlist_record(dbt_test_scheme, &tr2.tr_int_key, &tr2.tr_float_key,
		tr2.tr_string_key, &tr2.tr_sockaddr_key, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL);
	TEST_ASSERT(lookup != NULL, "vlist_record failed");
	TEST_ASSERT(dbt_db_get(&dbt_test_table, lookup, &result) == 0, "dbt_db_get failed");
	TEST_ASSERT(result != NULL, "returned result is NULL");
	var_delete(lookup);
	var_delete(result);

	return;
}

void
dbt_test_prepare(char *config_key, char *driver)
{
	// Create test scheme
	dbt_test_get_scheme();
	
	// Init dbt_test_table
	dbt_test_table.dbt_scheme = dbt_test_scheme;
	dbt_test_table.dbt_validate = dbt_common_validate;
	dbt_test_table.dbt_sql_invalid_where = DBT_COMMON_INVALID_SQL;
	dbt_test_table.dbt_config_key = config_key;
	dbt_test_table.dbt_cleanup_schedule = time(NULL);

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

	return;
}

void
dbt_test_finalize(void)
{
	dbt_clear();
	module_clear();
	cf_clear();

	return;
}

void
dbt_test_driver(char *config, char *driver)
{
	int i;

	if (!module_exists(driver))
	{
		log_crit("dbt_test_driver: %s not installed", driver);
		return;
	}

	/*
	 * Multi-threaded write test
	 */
	dbt_test_prepare(config, driver);

	i = dbt_janitor_cleanup(time(NULL), &dbt_test_table);

	// Start threads
	bzero(dbt_test_thread, sizeof dbt_test_thread);
	for (i = 0; i < dbt_test_threads; ++i)
	{
		util_thread_create(dbt_test_thread + i, dbt_test_set,
			// HACK: Pass the thread pointer to thread
			//       Used to calculate thread number
			dbt_test_thread + i);
	}

	// Join threads
	for (i = 0; i < dbt_test_threads; ++i)
	{
		util_thread_join(dbt_test_thread[i]);
	}

	// Check cleanup
	TEST_ASSERT(dbt_janitor_cleanup(time(NULL), &dbt_test_table), "dbt_db_cleanup failed");

	dbt_test_finalize();


	// MemDB is not persistent
	if (strcmp(driver, "memdb.so") == 0)
	{
		return;
	}

	/*
	 * Multi-threaded read test
	 */
	dbt_test_prepare(config, driver);

	// Start threads
	bzero(dbt_test_thread, sizeof dbt_test_thread);
	for (i = 0; i < dbt_test_threads; ++i)
	{
		util_thread_create(dbt_test_thread + i, dbt_test_get,
			// HACK: Pass the thread pointer to thread
			//       Used to calculate thread number
			dbt_test_thread + i);
	}

	// Join threads
	for (i = 0; i < dbt_test_threads; ++i)
	{
		util_thread_join(dbt_test_thread[i]);
	}

	dbt_test_finalize();

	return;
}

void
dbt_test(void)
{
	dbt_test_driver("test_memdb", "memdb.so");
	dbt_test_driver("test_bdb", "bdb.so");
	dbt_test_driver("test_mysql", "sakila.so");
}

#endif
