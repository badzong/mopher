#ifndef _DBA_H_
#define _DBA_H_

#include "var.h"

typedef void *(*dbt_open_t)(var_t *schema, char *path, char *host, char *user,
	char *pass, char *name, char *table);
typedef void (*dbt_close_t)(void *handle);
typedef int (*dbt_ping_t)(void *handle);
typedef int (*dbt_set_t)(void *handle, var_t *record);
typedef var_t *(*dbt_get_t)(void *handle, var_t *record);
typedef int (*dbt_del_t)(void *handle, var_t *record);

typedef enum dbt_type { DT_NULL = 0, DT_FILE, DT_SERVER } dbt_type_t;

typedef struct dbt_driver {
	char		*dd_name;
	dbt_type_t	 dd_type;
	dbt_open_t	 dd_open;
	dbt_close_t	 dd_close;
	dbt_ping_t	 dd_ping;
	dbt_set_t	 dd_set;
	dbt_get_t	 dd_get;
	dbt_del_t	 dd_del;
} dbt_driver_t;

typedef struct dbt {
	char		*dbt_drivername;
	char		*dbt_path;
	char		*dbt_host;
	char		*dbt_user;
	char		*dbt_pass;
	char		*dbt_name;
	char		*dbt_table;
	dbt_driver_t	*dbt_driver;
	void		*dbt_handle;
} dbt_t;

/*
 * Prototypes
 */

void dbt_driver_register(dbt_driver_t *dd);
void dbt_init(void);
dbt_t * dbt_open(var_t *schema, char *driver, char *path, char *host, char *user,char *pass, char *name, char *table);
void dbt_close(dbt_t *dbt);
void dbt_clear(void);

#endif /* _DBA_H_ */
