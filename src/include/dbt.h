#ifndef _DBT_H_
#define _DBT_H_

#include <pthread.h>

#include <var.h>


#define DBT_LOCK	1<<0

typedef int (*dbt_db_open_t)(void *dbt);
typedef void (*dbt_db_close_t)(void *dbt);
typedef int (*dbt_db_set_t)(void *dbt, var_t *record);
typedef int (*dbt_db_get_t)(void *dbt, var_t *record, var_t **result);
typedef int (*dbt_db_del_t)(void *dbt, var_t *record);
typedef int (*dbt_db_sql_cleanup_t)(void *dbt);
typedef int (*dbt_db_sync_t)(void *dbt);

typedef int (*dbt_db_callback_t)(void *dbt, var_t *record);
typedef int (*dbt_db_walk_t)(void *dbt, dbt_db_callback_t callback);

typedef struct dbt_driver {
	char			*dd_name;
	dbt_db_open_t		 dd_open;
	dbt_db_close_t		 dd_close;
	dbt_db_set_t		 dd_set;
	dbt_db_get_t		 dd_get;
	dbt_db_del_t		 dd_del;
	dbt_db_walk_t		 dd_walk;
	dbt_db_sync_t	 	 dd_sync;
	dbt_db_sql_cleanup_t	 dd_sql_cleanup;
	int			 dd_flags;
	pthread_mutex_t		 dd_mutex;
	pthread_mutexattr_t	 dd_mutexattr;
} dbt_driver_t;

typedef struct dbt {
	char			 *dbt_name;
	char			 *dbt_config_key;
	char			 *dbt_path;
	char			 *dbt_host;
	VAR_INT_T		  dbt_port;
	char			 *dbt_user;
	char			 *dbt_pass;
	char			 *dbt_database;
	char			 *dbt_table;
	var_t			 *dbt_scheme;
	int			  dbt_cleanup_interval;
	int			  dbt_cleanup_schedule;
	int			  dbt_cleanup_deleted;
	char			 *dbt_sql_invalid_where;
	int			  dbt_sql_invalid_free;
	int			(*dbt_validate)(struct dbt *, var_t *);
	char			 *dbt_drivername;
	dbt_driver_t		 *dbt_driver;
	void			 *dbt_handle;
	int			  dbt_open;
} dbt_t;

typedef int (*dbt_validate_t)(dbt_t *dbt, var_t *record);

#define DBT_DB_OPEN(dbt) (dbt)->dbt_driver->dd_open(dbt)
#define DBT_DB_CLOSE(DBT) (dbt)->dbt_driver->dd_close(dbt)
#define DBT_VALIDATE(dbt, var) (dbt)->dbt_validate(dbt, var)

#define DBT_COMMON_INVALID_SQL "COMMON"

/*
 * Prototypes
 */

void dbt_driver_register(dbt_driver_t *dd);
int dbt_db_get(dbt_t *dbt, var_t *record, var_t **result);
int dbt_db_set(dbt_t *dbt, var_t *record);
int dbt_db_del(dbt_t *dbt, var_t *record);
int dbt_db_walk(dbt_t *dbt, dbt_db_callback_t callback);
int dbt_db_sync(dbt_t *dbt);
int dbt_db_cleanup(dbt_t *dbt);
int dbt_db_get_from_table(dbt_t *dbt, var_t *attrs, var_t **record);
int dbt_db_load_into_table(dbt_t *dbt, var_t *table);
void dbt_register(char *name, dbt_t *dbt);
int dbt_common_validate(dbt_t *dbt, var_t *record);
void dbt_open_databases(void);
void dbt_init(int start_threads);
void dbt_clear();
dbt_t * dbt_lookup(char *name);
int dbt_dump(char **dump, char *tablename);
void dbt_test(void);

#endif /* _DBT_H_ */
