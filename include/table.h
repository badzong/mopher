#ifndef _TABLES_H_
#define _TABLES_H_

#include "dbt.h"

typedef int (*table_update_t)(void);
typedef int (*table_validate_t)(var_t *record);

typedef struct table {
	char		*t_name;
	var_t		*t_schema;
	table_update_t	 t_update;
	table_validate_t t_validate;
	int		 t_cleanup_deleted;
	dbt_t		*t_dbt;
} table_t;

extern time_t table_cleanup_cycle;

/*
 * Prototypes
 */

table_t * table_create(char *name, var_t *schema, table_update_t update,table_validate_t validate, dbt_t *dbt);
void table_delete(table_t * table);
int table_register(char *name, var_t *schema, table_update_t update,table_validate_t validate);
int table_cleanup(table_t *table, var_t *record);
void table_janitor(int force);
void table_init(void);
void table_clear();
table_t * table_lookup(char *name);

#endif /* _TABLES_H_ */
