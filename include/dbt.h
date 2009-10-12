#ifndef _DBT_H_
#define _DBT_H_

#include <pthread.h>

#include "var.h"


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

typedef int (*dbt_update_t)(void *dbt);
typedef int (*dbt_validate_t)(void *dbt, var_t *record);

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
} dbt_driver_t;

typedef struct dbt {
	char			*dbt_name;
	char			*dbt_path;
	char			*dbt_host;
	VAR_INT_T		 dbt_port;
	char			*dbt_user;
	char			*dbt_pass;
	char			*dbt_database;
	char			*dbt_table;
	var_t			*dbt_scheme;
	int			 dbt_cleanup_interval;
	int			 dbt_cleanup_schedule;
	int			 dbt_cleanup_deleted;
	char			*dbt_sql_invalid_where;
	dbt_update_t		 dbt_update;
	dbt_validate_t		 dbt_validate;
	char			*dbt_drivername;
	dbt_driver_t		*dbt_driver;
	void			*dbt_handle;
} dbt_t;

#define DBT_DB_OPEN(dbt) (dbt)->dbt_driver->dd_open(dbt)
#define DBT_DB_CLOSE(DBT) (dbt)->dbt_driver->dd_close(dbt)
#define DBT_VALIDATE(dbt, var) (dbt)->dbt_validate(dbt, var)
#define DBT_SCHEDULE_CLEANUP(dbt, now) ((dbt)->dbt_cleanup_schedule = now + (dbt)->dbt_cleanup_interval)

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
void dbt_register(dbt_t *dbt);
void dbt_janitor(int force);
void dbt_init(void);
void dbt_clear();
dbt_t * dbt_lookup(char *name);
#endif /* _DBT_H_ */
