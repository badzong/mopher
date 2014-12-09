#ifndef _SQL_H_
#define _SQL_H_

#define SQL_KEYS	1<<0
#define SQL_VALUES	1<<1
#define SQL_ALL		(SQL_KEYS | SQL_VALUES)

#define SQL_EXPIRE "_expire"

/*
 * Types
 */
typedef int (*sql_escape_t)(void *conn, char *buffer, int size, char *value);

typedef void  *(*sql_exec_t)(void *handle, char *query, int *tuples, int *affected);
typedef int (*sql_table_exists_t)(void *handle, char *table);
typedef char *(*sql_get_value_t)(void *handle, void *result, int field);
typedef void (*sql_free_result_t)(void *handle, void *result);

typedef struct sql {
	char			*sql_t_int;
	char			*sql_t_float;
	char			*sql_t_string;
	char			*sql_t_addr;

	sql_escape_t		 sql_esc_value;
	sql_escape_t		 sql_esc_column;
	sql_escape_t		 sql_esc_table;

	sql_exec_t		 sql_exec;
	sql_table_exists_t	 sql_table_exists;
	sql_get_value_t	 	 sql_get_value;
	sql_free_result_t	 sql_free_result;

	void			*sql_handle;
} sql_t;

/*
 * Prototypes
 */
void sql_open(void *conn, sql_t *sql, var_t *scheme);
int sql_db_get(void *conn, sql_t *sql, var_t *scheme, var_t *record, var_t **result);
int sql_db_set(void *conn, sql_t *sql, var_t *v);
int sql_db_del(void *conn, sql_t *sql, var_t *v);
int sql_db_cleanup(void *conn, sql_t *sql, char *table);
void sql_test(int n);

#endif /* _SQL_H_ */
