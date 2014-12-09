#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <postgresql/libpq-fe.h>

#include <mopher.h>

#define BUFLEN 4096


static dbt_driver_t dbt_driver;

static PGresult *
pgsql_exec(PGconn *conn, char *cmd, int *tuples, int *affected)
{
	PGresult *res = NULL;

	*tuples = 0;
	*affected = 0;

	printf("QUERY: %s\n", cmd);

	res = PQexec(conn, cmd);
	switch (PQresultStatus(res))
	{
	case PGRES_COMMAND_OK:
		*affected = atoi(PQcmdTuples(res));
		break;

	case PGRES_TUPLES_OK:
		*tuples = PQntuples(res);
		break;
	default:
		log_error("pgsql_exec: %s", PQerrorMessage(conn));
		goto error;
	}

	log_debug("pgsql_exec: %s: OK", cmd);

	return res;

error:
	if (res != NULL)
	{
		PQclear(res);
	}

	return NULL;
}

static void
pgsql_free_result(PGconn *conn, PGresult *result)
{
	if (result != NULL)
	{
		PQclear(result);
	}

	return;
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
			log_die(EX_SOFTWARE, "pgsql_open: buffer exhausted");
		}
	}

	if (dbt->dbt_port)
	{
		n += snprintf(conninfo + n, sizeof conninfo - n, "port='%ld' ",
			dbt->dbt_port);
		if (n >= sizeof conninfo)
		{
			log_die(EX_SOFTWARE, "pgsql_open: buffer exhausted");
		}
	}

	n += snprintf(conninfo + n, sizeof conninfo - n,
		"user='%s' password='%s' dbname='%s'", dbt->dbt_user,
		dbt->dbt_pass, dbt->dbt_database);

	printf("%s\n", conninfo);

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
	int error;
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
	return 0;

	PQescapeStringConn(conn, buffer, str, size, &error);
	if (error)
	{
		log_error("pgsql_escape_value: PQescapeStringConn failed");
		return -1;
	}

	return 0;
}

static int
pgsql_table_exists(PGconn *conn, char *table)
{
	char query[BUFLEN];
	PGresult *res = NULL;
	int tuples, affected;
	int n;

	n = snprintf(query, sizeof query, "SELECT 'public.%s'::regclass", table);
	if (n >= sizeof query)
	{
		log_die(EX_SOFTWARE, "pgsql_table_exists: buffer exhausted");
	}

	res = pgsql_exec(conn, query, &tuples, &affected);
	n = res == NULL? 0: 1;
	PQclear(res);

	return n;
}

static char *
pgsql_get_value(PGconn *conn, PGresult *result, int field)
{
	return PQgetvalue(result, 0, field);
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
	dbt_driver.dd_name    = "posgresql";
	dbt_driver.dd_open    = (dbt_db_open_t) pgsql_open;
	dbt_driver.dd_close   = (dbt_db_close_t) pgsql_close;
	dbt_driver.dd_flags	= DBT_LOCK;

	// SQL driver
	dbt_driver.dd_use_sql              = 1;
	dbt_driver.dd_sql.sql_t_int        = "INTEGER";
	dbt_driver.dd_sql.sql_t_float      = "FLOAT";
	dbt_driver.dd_sql.sql_t_string     = "VARCHAR(255)";
	dbt_driver.dd_sql.sql_t_addr       = "INET";
	dbt_driver.dd_sql.sql_esc_table    = (sql_escape_t) pgsql_esc_identifier;
	dbt_driver.dd_sql.sql_esc_column   = (sql_escape_t) pgsql_esc_identifier;
	dbt_driver.dd_sql.sql_esc_value    = (sql_escape_t) pgsql_esc_value;
	dbt_driver.dd_sql.sql_exec         = (sql_exec_t) pgsql_exec;
	dbt_driver.dd_sql.sql_table_exists = (sql_table_exists_t) pgsql_table_exists;
	dbt_driver.dd_sql.sql_get_value    = (sql_get_value_t) pgsql_get_value;
	dbt_driver.dd_sql.sql_free_result  = (sql_free_result_t) pgsql_free_result;

	dbt_driver_register(&dbt_driver);

	return 0;
}
