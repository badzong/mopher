#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <postgresql/libpq-fe.h>

#include <mopher.h>

#define BUFLEN 4096


static dbt_driver_t dbt_driver;

static PGresult *
pgsql_exec(dbt_t *dbt, char *cmd)
{
	PGresult *res = NULL;

	res = PQexec(dbt->dbt_handle, cmd);
	if (PQresultStatus(res) != PGRES_COMMAND_OK)
	{
		log_error("pgsql_exec: %s: %s", dbt->dbt_table,
			PQerrorMessage(dbt->dbt_handle));
		goto error;
	}

	log_debug("pgsql_exec: %s: %s: OK", dbt->dbt_table, cmd);

	return res;

error:
	if (res != NULL)
	{
		PQclear(res);
	}

	return NULL;
}

static int
pgsql_open(dbt_t *dbt)
{
	PGconn *conn = NULL;
	char conninfo[BUFLEN];
	int n;

	n = snprintf(conninfo, sizeof conninfo,
		"host='%s',port='%ld',user='%s',password='%s',dbname='%s'",
		dbt->dbt_host, dbt->dbt_port, dbt->dbt_user, dbt->dbt_pass,
		dbt->dbt_database);

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
pgsql_escape_identifier(PGconn *conn, char *buffer, int size, char *str)
{
	char *esc = NULL;
	int len;
	int r = -1;

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
pgsql_escape_value(PGconn *conn, char *buffer, int size, char *str)
{
	int error;

	PQescapeStringConn(conn, buffer, str, size, &error);
	if (error)
	{
		log_error("pgsql_escape_value: PQescapeStringConn failed");
		return -1;
	}

	return 0;
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

static void
pgsql_unpack()
{
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
	dbt_driver.dd_name	= "posgresql";
	dbt_driver.dd_open	= (dbt_db_open_t)	pgsql_open;
	dbt_driver.dd_close	= (dbt_db_close_t)	pgsql_close;

	//dbt_driver.dd_sql.sql_esc_column = 

	dbt_driver.dd_flags	= DBT_LOCK;

	dbt_driver_register(&dbt_driver);

	return 0;
}
