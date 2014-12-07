#ifndef _SQL_H_
#define _SQL_H_

#define SQL_KEYS	1<<0
#define SQL_VALUES	1<<1
#define SQL_ALL		(SQL_KEYS | SQL_VALUES)

/*
 * Types
 */
typedef int (*sql_esc_value_t)(char *buffer, int size, char *value);
typedef int (*sql_esc_column_t)(char *buffer, int size, char *column);
typedef int (*sql_esc_table_t)(char *buffer, int size, char *table);

typedef void  *(*sql_exec_t)(void *handle, char *query);
typedef var_t *(*sql_unpack_t)(void *handle, void *result);
typedef void   (*sql_free_t)(void *handle, void *result);


typedef struct sql {
	char			*sql_q_value;
	char			*sql_q_column;
	char			*sql_q_table;
	char			*sql_t_int;
	char			*sql_t_float;
	char			*sql_t_string;
	char			*sql_t_addr;

	sql_esc_value_t		 sql_esc_value;
	sql_esc_column_t	 sql_esc_column;
	sql_esc_table_t		 sql_esc_table;

	sql_exec_t		 sql_exec;
	sql_unpack_t	 	 sql_unpack;
	sql_free_t		 sql_free;
} sql_t;

/*
 * Prototypes
 */
void sql_test(int n);

#endif /* _SQL_H_ */
