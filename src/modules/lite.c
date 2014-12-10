#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <sqlite3.h>

#include <mopher.h>

#define BUFLEN 4096

typedef struct lite {
	int    lite_columns;
	char **lite_result;
} lite_t;

static dbt_driver_t dbt_driver;

static lite_t *
lite_create(int columns)
{
	lite_t *l = NULL;
	int size;

	l = (lite_t *) malloc(sizeof (lite_t));
	if (l == NULL)
	{
		goto error;
	}
	memset(l, 0, sizeof(lite_t));

	size = sizeof(char *) * columns;

	l->lite_result = (char **) malloc(size);
	if (l->lite_result == NULL)
	{
		goto error;
	}

	memset(l->lite_result, 0, size);
	l->lite_columns = columns;

	return l;

error:
	if (l != NULL)
	{
		if (l->lite_result != NULL)
		{
			free(l->lite_result);
		}
		free(l);
	}

	log_error("lite_create: malloc failed");
	return NULL;
}

static void
lite_delete(lite_t *l)
{
	int i;

	if (l == NULL)
	{
		return;
	}

	if (l->lite_result == NULL)
	{
		free(l);
		return;
	}

	for (i = 0; i < l->lite_columns; ++i)
	{
		if(l->lite_result[i] != NULL)
		{
			free(l->lite_result[i]);
		}
	}

	free(l->lite_result);
	free(l);

	return;
}

static int
lite_set(lite_t *l, int column, char *str)
{
	if (column >= l->lite_columns)
	{
		log_error("lite_set: value for column to big");
		return -1;
	}
	
	if (str == NULL)
	{
		l->lite_result[column] = NULL;
	}
	else
	{
		l->lite_result[column] = strdup(str);	
		if (l->lite_result[column] == NULL)
		{
			log_error("lite_set: strdup failed");
			return -1;
		}
	}

	return 0;
}

static lite_t *
lite_exec(sqlite3 *db, char *cmd, int *tuples, int *affected)
{
	sqlite3_stmt *stmt = NULL;
	lite_t *l = NULL;
	int row;
	int columns;
	int i;

	*tuples = 0;
	*affected = 0;

	if (sqlite3_prepare(db, cmd, -1, &stmt, NULL) != SQLITE_OK)
	{
		log_error("lite_exec: %s", sqlite3_errmsg(db));
		goto error;
	}

	row = sqlite3_step(stmt);
	if (row != SQLITE_ROW)
	{
		*tuples = 0;
		columns = 0;
	}
	else
	{
		*tuples = 1;
		columns = sqlite3_column_count(stmt);
	}

	*affected = sqlite3_changes(db);

	l = lite_create(columns);
	if (l == NULL)
	{
		log_error("lite_exec: lite_create failed");
		goto error;
	}

	for (i = 0; i < columns; ++i)
	{
		if (lite_set(l, i, sqlite3_column_text(stmt, i)))
		{
			log_error("lite_exec: lite_set failed");
			goto error;
		}
	}

	log_debug("lite_exec: %s: OK", cmd);

	sqlite3_finalize(stmt);

	return l;

error:
	if (stmt != NULL)
	{
		sqlite3_finalize(stmt);
	}

	if (l != NULL)
	{
		lite_delete(l);
	}

	return NULL;
}

static void
lite_free_result(sqlite3 *conn, lite_t *result)
{
	if (result != NULL)
	{
		lite_delete(result);
	}

	return;
}

static int
lite_open(dbt_t *dbt)
{
	sqlite3 *db = NULL;

	if (sqlite3_open(dbt->dbt_path, &db))
	{
		log_error("lite_open: %s: %s", dbt->dbt_table,
			sqlite3_errmsg(db));
		goto error;
	}

	log_debug("lite_open: %s: connection ok", dbt->dbt_table);

	dbt->dbt_handle = db;

	return 0;

error:
	if (db != NULL)
	{
		sqlite3_close(db);
	}

	return -1;
	
}

static int
lite_esc_identifier(sqlite3 *conn, char *buffer, int size, char *str)
{
	return util_quote(buffer, size, str, "\"");
}

static int
lite_esc_value(sqlite3 *conn, char *buffer, int size, char *str)
{
	char *p;
	int n;

	buffer[0] = '\'';
	for (n = 1, p = str; *p && n < size - 1; ++n, ++p)
	{
		if (*p == '\'')
		{
			buffer[n] = '\'';
			++n;
		}
		buffer[n] = *p;
	}

	if (n >= size - 1)
	{
		log_error("lite_esc_value: buffer exhausted");
		return -1;
	}

	buffer[n] = '\'';
	buffer[n + 1] = 0;

	return 0;
}

static int
lite_table_exists(sqlite3 *conn, char *table)
{
	char query[BUFLEN];
	lite_t *res = NULL;
	int tuples, affected;
	int n;

	n = snprintf(query, sizeof query,
		"SELECT name FROM sqlite_master WHERE type='table' AND name='%s';",
		table);
	if (n >= sizeof query)
	{
		log_die(EX_SOFTWARE, "lite_table_exists: buffer exhausted");
	}

	res = lite_exec(conn, query, &tuples, &affected);
	n = tuples == 1? 1: 0;
	lite_delete(res);

	return n;
}

static char *
lite_get_value(sqlite3 *conn, lite_t *result, int field)
{
	if (field >= result->lite_columns)
	{
		log_error("lite_get_value: filed index %d too big", field);
		return NULL;
	}

	return result->lite_result[field];
}

static void
lite_close(dbt_t *dbt)
{
	if (dbt->dbt_handle)
	{
		if (sqlite3_close(dbt->dbt_handle) != SQLITE_OK)
		{
			log_error("lite_close: %s",
				sqlite3_errmsg(dbt->dbt_handle));
		}
	}

	dbt->dbt_handle = NULL;

	return;
}

int
lite_init(void)
{
	dbt_driver.dd_name    = "sqlite3";
	dbt_driver.dd_open    = (dbt_db_open_t) lite_open;
	dbt_driver.dd_close   = (dbt_db_close_t) lite_close;
	dbt_driver.dd_flags   = DBT_LOCK;

	// SQL driver
	dbt_driver.dd_use_sql              = 1;
	dbt_driver.dd_sql.sql_t_int        = "INTEGER";
	dbt_driver.dd_sql.sql_t_float      = "REAL";
	dbt_driver.dd_sql.sql_t_string     = "TEXT";
	dbt_driver.dd_sql.sql_t_addr       = "TEXT";
	dbt_driver.dd_sql.sql_esc_table    = (sql_escape_t) lite_esc_identifier;
	dbt_driver.dd_sql.sql_esc_column   = (sql_escape_t) lite_esc_identifier;
	dbt_driver.dd_sql.sql_esc_value    = (sql_escape_t) lite_esc_value;
	dbt_driver.dd_sql.sql_exec         = (sql_exec_t) lite_exec;
	dbt_driver.dd_sql.sql_table_exists = (sql_table_exists_t) lite_table_exists;
	dbt_driver.dd_sql.sql_get_value    = (sql_get_value_t) lite_get_value;
	dbt_driver.dd_sql.sql_free_result  = (sql_free_result_t) lite_free_result;

	dbt_driver_register(&dbt_driver);

	return 0;
}
