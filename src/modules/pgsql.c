#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <postgresql/libpq-fe.h>

#include <mopher.h>

#define BUFLEN 4096


static dbt_driver_t dbt_driver;


static int
pgsql_exec(PGconn *conn, PGresult **result, char *cmd, int *tuples, int *affected)
{
	*result = NULL;
	*tuples = 0;
	*affected = 0;

	*result = PQexec(conn, cmd);
	switch (PQresultStatus(*result))
	{
	case PGRES_COMMAND_OK:
		*affected = atoi(PQcmdTuples(*result));
		break;

	case PGRES_TUPLES_OK:
		*tuples = PQntuples(*result);
		break;
	default:
		log_error("pgsql_exec: %s", PQerrorMessage(conn));
		goto error;
	}

	log_debug("pgsql_exec: %s: OK", cmd);

	return 0;

error:
	if (*result != NULL)
	{
		PQclear(*result);
	}

	return -1;
}

static PGresult *
pgsql_get_row(PGconn *conn, PGresult *result, int row)
{
	if (PQntuples(result) <= row)
	{
		return NULL;
	}

	return result;
}

static char *
pgsql_get_value(PGconn *conn, PGresult *result, int row, int field)
{
	if (PQgetisnull(result, row, field))
	{
		return NULL;
	}

	return PQgetvalue(result, row, field);
}

static int
pgsql_open(dbt_t *dbt)
{
	PGconn *conn = NULL;
	char conninfo[BUFLEN];
	int n = 0;

	// Safety first
	memset(conninfo, 0, sizeof conninfo);

	if (dbt->dbt_host != NULL)
	{
		n += snprintf(conninfo + n, sizeof conninfo - n, "host='%s' ",
			dbt->dbt_host);
		if (n >= sizeof conninfo)
		{
			log_error("pgsql_open: buffer exhausted");
			goto error;
		}
	}

	if (dbt->dbt_port)
	{
		n += snprintf(conninfo + n, sizeof conninfo - n, "port='%ld' ",
			dbt->dbt_port);
		if (n >= sizeof conninfo)
		{
			log_error("pgsql_open: buffer exhausted");
			goto error;
		}
	}

	n += snprintf(conninfo + n, sizeof conninfo - n,
		"user='%s' password='%s' dbname='%s'", dbt->dbt_user,
		dbt->dbt_pass, dbt->dbt_database);

	if (n >= sizeof conninfo)
	{
		log_error("pgsql_open: buffer exhausted");
		goto error;
	}

	log_debug("pgsql_open: %s", conninfo);

	conn = PQconnectdb(conninfo);
	if (PQstatus(conn) != CONNECTION_OK)
	{
		log_error("pgsql_open: %s: %s", dbt->dbt_table,
			PQerrorMessage(conn));
		goto error;
	}

	log_debug("pgsql_open: %s: connection ok", dbt->dbt_table);

	dbt->dbt_handle = conn;

	return 0;

error:
	if (conn != NULL)
	{
		PQfinish(conn);
	}

	return -1;
	
}

static int
pgsql_esc_identifier(PGconn *conn, char *buffer, int size, char *str)
{
	char *esc = NULL;
	int len;
	int r = -1;

	if (str == NULL)
	{
		return -1;
	}

	esc = PQescapeIdentifier(conn, str, strlen(str));
	if (esc == NULL)
	{
		log_error("pgsql_escape_identifier: PQescapeIdentifier failed");
		goto exit;
	}

	len = strlen(esc);
	if (len >= size)
	{
		log_error("pgsql_escape_identifier: PQescapeIdentifier failed");
		goto exit;
	}

	strcpy(buffer, esc);

	// Success
	r = 0;
	
exit:
	if (esc)
	{
		free(esc);
	}

	return r;
}

static int
pgsql_esc_value(PGconn *conn, char *buffer, int size, char *str)
{
	char *literal;

	if (str == NULL)
	{
		return -1;
	}

	literal = PQescapeLiteral(conn, str, strlen(str));
	if (literal == NULL)
	{
		log_error("pgsql_escape_value: PQescapeLiteral failed");
		return -1;
	}

	if (size <= strlen(literal))
	{
		log_error("pgsql_escape_value: Buffer exhausted");
		return -1;
	}

	strcpy(buffer, literal);
	PQfreemem(literal);

	return 0;
}

static int
pgsql_table_exists(PGconn *conn, char *table)
{
	PGresult *res;
	char query[BUFLEN];
	int tuples, affected;
	int n;

	n = snprintf(query, sizeof query, "SELECT c.relname FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace WHERE n.nspname=\'public\' AND c.relname=\'%s\'", table);
	if (n >= sizeof query)
	{
		log_die(EX_SOFTWARE, "pgsql_table_exists: buffer exhausted");
	}

	// Execute query and clear result. If we have a tuple, the table
	// exists.
	if (pgsql_exec(conn, &res, query, &tuples, &affected))
	{
		log_die(EX_SOFTWARE, "pgsql_table_exists: pgsql_exec failed");
	}
	PQclear(res);

	return tuples;
}

static void
pgsql_close(dbt_t *dbt)
{
	if (dbt->dbt_handle)
	{
		PQfinish(dbt->dbt_handle);
	}

	dbt->dbt_handle = NULL;

	return;
}

int
pgsql_init(void)
{
	dbt_driver.dd_name    = "postgresql";
	dbt_driver.dd_open    = (dbt_db_open_t) pgsql_open;
	dbt_driver.dd_close   = (dbt_db_close_t) pgsql_close;
	dbt_driver.dd_flags	= DBT_LOCK;

	// SQL driver
	dbt_driver.dd_use_sql                = 1;
	dbt_driver.dd_sql.sql_t_int          = "BIGINT";
	dbt_driver.dd_sql.sql_t_float        = "NUMERIC(12,2)";
	dbt_driver.dd_sql.sql_t_string       = "TEXT";
	dbt_driver.dd_sql.sql_t_addr         = "INET";
	dbt_driver.dd_sql.sql_esc_identifier = (sql_escape_t) pgsql_esc_identifier;
	dbt_driver.dd_sql.sql_esc_value      = (sql_escape_t) pgsql_esc_value;
	dbt_driver.dd_sql.sql_exec           = (sql_exec_t) pgsql_exec;
	dbt_driver.dd_sql.sql_table_exists   = (sql_table_exists_t) pgsql_table_exists;
	dbt_driver.dd_sql.sql_free_result    = (sql_free_result_t) PQclear;
	dbt_driver.dd_sql.sql_get_row        = (sql_get_row_t) pgsql_get_row;
	dbt_driver.dd_sql.sql_get_value      = (sql_get_value_t) pgsql_get_value;

	dbt_driver_register(&dbt_driver);

	return 0;
}
