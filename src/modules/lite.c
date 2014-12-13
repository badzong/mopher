#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <sqlite3.h>

#include <mopher.h>

#define BUFLEN 4096

static dbt_driver_t dbt_driver;


static int
lite_exec(sqlite3 *db, sqlite3_stmt **stmt, char *cmd, int *tuples, int *affected)
{
	*stmt = NULL;
	*tuples = 0;
	*affected = 0;

	if (sqlite3_prepare_v2(db, cmd, -1, stmt, NULL) != SQLITE_OK)
	{
		log_error("lite_exec: %s", sqlite3_errmsg(db));
		goto error;
	}

	switch(sqlite3_step(*stmt))
	{
	case SQLITE_DONE:
		*tuples = 0;
		break;

	case SQLITE_ROW:
		// Value unknown but needs to be true for sql.c
		*tuples = 1;
		break;

	default:
		log_error("lite_exec: %s", sqlite3_errmsg(db));
		goto error;
	}

	*affected = sqlite3_changes(db);

	return 0;

error:
	if (*stmt != NULL)
	{
		sqlite3_finalize(*stmt);
	}

	return -1;
}

static sqlite3_stmt *
lite_get_row(sqlite3 *db, sqlite3_stmt *stmt, int nrow)
{
	// The first row has been stepped in lite_exec!!
	if (nrow == 0)
	{
		return stmt;
	}

	switch(sqlite3_step(stmt))
	{
	case SQLITE_DONE:
		return NULL;

	case SQLITE_ROW:
		return stmt;

	default:
		break;
	}

	log_error("lite_get_row: %s", sqlite3_errmsg(db));
	return NULL;
}

static char *
lite_get_value(sqlite3 *conn, sqlite3_stmt *stmt, int nrow, int field)
{
	return (char *) sqlite3_column_text(stmt, field);
}

static int
lite_open(dbt_t *dbt)
{
	sqlite3 *db = NULL;
	int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
		SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_PRIVATECACHE;

	if (sqlite3_open_v2(dbt->dbt_path, &db, flags, NULL))
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
	sqlite3_stmt *res;
	char query[BUFLEN];
	int tuples, affected;
	int n;

	n = snprintf(query, sizeof query,
		"SELECT name FROM sqlite_master WHERE type='table' AND name='%s';",
		table);
	if (n >= sizeof query)
	{
		log_die(EX_SOFTWARE, "lite_table_exists: buffer exhausted");
	}

	// Execute query and clear result. If we have a tuple, the table
	// exists.
	if (lite_exec(conn, &res, query, &tuples, &affected))
	{
		log_die(EX_SOFTWARE, "lite_table_exists: lite_exec failed");
	}
	sqlite3_finalize(res);

	return tuples;
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
	dbt_driver.dd_use_sql                = 1;
	dbt_driver.dd_sql.sql_t_int          = "INTEGER";
	dbt_driver.dd_sql.sql_t_float        = "REAL";
	dbt_driver.dd_sql.sql_t_string       = "TEXT";
	dbt_driver.dd_sql.sql_t_addr         = "TEXT";
	dbt_driver.dd_sql.sql_esc_identifier = (sql_escape_t) lite_esc_identifier;
	dbt_driver.dd_sql.sql_esc_value      = (sql_escape_t) lite_esc_value;
	dbt_driver.dd_sql.sql_exec           = (sql_exec_t) lite_exec;
	dbt_driver.dd_sql.sql_table_exists   = (sql_table_exists_t) lite_table_exists;
	dbt_driver.dd_sql.sql_free_result    = (sql_free_result_t) sqlite3_finalize;
	dbt_driver.dd_sql.sql_get_row        = (sql_get_row_t) lite_get_row;
	dbt_driver.dd_sql.sql_get_value      = (sql_get_value_t) lite_get_value;

	dbt_driver_register(&dbt_driver);

	return 0;
}
