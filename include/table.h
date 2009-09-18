#ifndef _TABLES_H_
#define _TABLES_H_

#include "dbt.h"

typedef int (*table_update_t)(void);
typedef int (*table_cleanup_t)(void);

typedef struct table {
	char		*t_name;
	var_t		*t_schema;
	table_update_t	 t_update;
	table_cleanup_t	 t_cleanup;
	dbt_t		*t_dbt;
} table_t;

/*
 * Prototypes
 */

table_t * table_create(char *name, var_t *schema, table_update_t update,table_cleanup_t cleanup, dbt_t *dbt);
void table_delete(table_t * table);
int table_register(char *name, var_t *schema, table_update_t update,table_cleanup_t cleanup);
void table_init();
void table_close();
table_t * table_lookup(char *name);

#endif /* _TABLES_H_ */
