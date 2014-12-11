#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <mysql/mysql.h>

#include <mopher.h>

#define BUFLEN 4096

typedef struct sakila {
	MYSQL_RES *sk_res;
	MYSQL_ROW  sk_row;
} sakila_t;

static dbt_driver_t dbt_driver;

static sakila_t *
sakila_create(MYSQL_RES *res, MYSQL_ROW row)
{
	sakila_t *sk;

	sk = (sakila_t *) malloc(sizeof(sakila_t));
	if (sk == NULL)
	{
		log_error("sakila_create: malloc failed");
		return NULL;
	}

	sk->sk_res = res;
	sk->sk_row = row;

	return sk;
}

static void
sakila_delete(sakila_t *sk)
{
	if (sk->sk_res)
	{
		mysql_free_result(sk->sk_res);

	}
	free(sk);
}

static sakila_t *
sakila_exec(MYSQL *conn, char *cmd, int *tuples, int *affected)
{
	MYSQL_RES *res = NULL;
	MYSQL_ROW row;
	sakila_t *sk;

	*tuples = 0;
	*affected = 0;

	if (mysql_query(conn, cmd))
	{
		log_error("sakila_exec: %s", mysql_error(conn));
		goto error;
	}

	*affected = mysql_affected_rows(conn);

	res = mysql_store_result(conn);
	if (res != NULL)
	{
		*tuples = mysql_num_rows(res);
		row = mysql_fetch_row(res);
	}

	if (*tuples > 1)
	{
		log_error("sakila_exec: %d rows returned", *tuples);
	}

	sk = sakila_create(res, row);
	if (sk == NULL)
	{
		log_error("sakila_exec: sakila_create failed");
		goto error;
	}

	log_debug("sakila_exec: %s: OK", cmd);

	return sk;

error:
	if (res != NULL)
	{
		mysql_free_result(res);
	}

	return NULL;
}

static void
sakila_free_result(MYSQL *conn, sakila_t *sk)
{
	sakila_delete(sk);
	return;
}

static int
sakila_open(dbt_t *dbt)
{
	MYSQL *conn = NULL;

	conn = mysql_init(NULL);
	if (conn == NULL)
	{
		log_error("sakila_open: mysql_init failed");
		goto error;
	}

	if(!mysql_real_connect(conn, dbt->dbt_host, dbt->dbt_user,
		dbt->dbt_pass, dbt->dbt_database, dbt->dbt_port,
		dbt->dbt_path, CLIENT_FOUND_ROWS))
	{
		log_error("sakila_open: mysql_real_connect: %s",
			mysql_error(conn));
		goto error;
	}

	dbt->dbt_handle = conn;

	return 0;

error:
	if (conn)
	{
		mysql_close(conn);
	}

	return -1;
}

static int
sakila_esc_identifier(MYSQL *conn, char *buffer, int size, char *str)
{
	int n;

	if (strchr(str, '`'))
	{
		log_error("sakila_esc_identifier: bad character ` in str");
		return -1;
	}

	n = snprintf(buffer, size, "`%s`", str);
	if (n >= size)
	{
		log_error("sakila_esc_identifier: buffer exhausted");
		return -1;
	}

	return 0;
}

static int
sakila_esc_value(MYSQL *conn, char *buffer, int size, char *str)
{
	int n;

	n = strlen(str);
	if (n >= size + 2)
	{
		log_error("sakila_esc_value: buffer exhausted");
		return -1;
	}

	buffer[0] = '"';

	n = mysql_real_escape_string(conn, buffer + 1, str, n);
	if (n >= size + 1)
	{
		log_error("sakila_esc_value: buffer exhausted");
		return -1;
		
	}

	buffer[n + 1] = '"';
	buffer[n + 2] = 0;

	return 0;
}

static int
sakila_table_exists(MYSQL *conn, char *table)
{
	char query[BUFLEN];
	sakila_t *sk = NULL;
	int tuples, affected;
	int n;

	n = snprintf(query, sizeof query, "SHOW TABLES LIKE '%s'", table);
	if (n >= sizeof query)
	{
		log_die(EX_SOFTWARE, "sakila_table_exists: buffer exhausted");
	}

	sk = sakila_exec(conn, query, &tuples, &affected);

	n = 0;
	if (sk)
	{
		if (tuples == 1)
		{
			n = 1;
		}
		sakila_delete(sk);
	}

	return n;
}

static char *
sakila_get_value(MYSQL *conn, sakila_t *sk, int field)
{
	return sk->sk_row[field];
}

static void
sakila_close(dbt_t *dbt)
{
	if (dbt->dbt_handle)
	{
		mysql_close(dbt->dbt_handle);
	}

	dbt->dbt_handle = NULL;

	return;
}

int
sakila_init(void)
{
	dbt_driver.dd_name    = "mysql";
	dbt_driver.dd_open    = (dbt_db_open_t) sakila_open;
	dbt_driver.dd_close   = (dbt_db_close_t) sakila_close;
	dbt_driver.dd_flags   = DBT_LOCK;

	// SQL driver
	dbt_driver.dd_use_sql                = 1;
	dbt_driver.dd_sql.sql_t_int          = "BIGINT";
	dbt_driver.dd_sql.sql_t_float        = "NUMERIC(12,2)";
	dbt_driver.dd_sql.sql_t_string       = "VARCHAR(65535)";
	dbt_driver.dd_sql.sql_t_addr         = "VARCHAR(39)";
	dbt_driver.dd_sql.sql_esc_identifier = (sql_escape_t) sakila_esc_identifier;
	dbt_driver.dd_sql.sql_esc_value      = (sql_escape_t) sakila_esc_value;
	dbt_driver.dd_sql.sql_exec           = (sql_exec_t) sakila_exec;
	dbt_driver.dd_sql.sql_table_exists   = (sql_table_exists_t) sakila_table_exists;
	dbt_driver.dd_sql.sql_get_value      = (sql_get_value_t) sakila_get_value;
	dbt_driver.dd_sql.sql_free_result    = (sql_free_result_t) sakila_free_result;

	dbt_driver_register(&dbt_driver);

	return 0;
}
