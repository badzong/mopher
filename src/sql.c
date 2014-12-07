#include <string.h>

// Required for testing
#include <netinet/in.h>

#include <mopher.h>

#define BUFLEN 8192

int
sql_columns(sql_t *sql, char *buffer, int size, int types, char *join,
    var_t *scheme)
{
	ll_t *ll;
	ll_entry_t *pos;
	char column[BUFLEN];
	char *cq = sql->sql_q_column;
	var_t *v;
	int n = 0;

	if(scheme->v_type != VT_LIST) {
		log_error("sql_columns: bad v_type");
		return -1;
	}

	ll = scheme->v_data;

	pos = LL_START(ll);
	while ((v = ll_next(ll, &pos)) != NULL)
	{
		// Only key fields
		if ((types & SQL_VALUES) == 0 && (v->v_flags & VF_KEY) == 0)
		{
			continue;
		}
		
		// Only value fields
		if ((types & SQL_KEYS) == 0 && (v->v_flags & VF_KEY))
		{
			continue;
		}
		
		// Escape column name
		if (sql->sql_esc_column(column, sizeof column, v->v_name))
		{
			log_error("sql_columns: escape column string failed");
			return -1;
		}

		n += snprintf(buffer + n, size - n, "%s%s%s%s", n? join: "",
			cq, column, cq);
		if (n >= size)
		{
			log_error("sql_columns: buffer exhausted");
			return -1;
		}
	}

	return 0;
}

int
sql_key_value(sql_t *sql, char *buffer, int size, int types, char *join,
    var_t *record)
{
	ll_t *ll;
	ll_entry_t *pos;
	var_t *v;
	int n = 0;
	char column[BUFLEN];
	char value_raw[BUFLEN];
	char value[BUFLEN];
	char *vp;
	char *cq = sql->sql_q_column;
	char *vq = sql->sql_q_value;

	if(record->v_type != VT_LIST) {
		log_error("sql_key_value: bad v_type");
		return -1;
	}

	ll = record->v_data;

	pos = LL_START(ll);
	while ((v = ll_next(ll, &pos)) != NULL)
	{
		// Only key fields
		if ((types & SQL_VALUES) == 0 && (v->v_flags & VF_KEY) == 0)
		{
			continue;
		}
		
		// Only value fields
		if ((types & SQL_KEYS) == 0 && (v->v_flags & VF_KEY))
		{
			continue;
		}
		
		// Name and value cannot be NULL
		if (v->v_name == NULL || v->v_data == NULL)
		{
			log_error("sql_key_value: key cannot be NULL");
			return -1;
		}

		// Escape column name
		if (sql->sql_esc_column(column, sizeof column, v->v_name))
		{
			log_error("sql_key_value: escape column string"
				" failed");
			return -1;
		}

		// Cast value to string
		if (v->v_type == VT_STRING)
		{
			vp = v->v_data;
		}
		else
		{
			if (var_dump_data(v, value_raw, sizeof value_raw) == -1)
			{
				log_error("sql_key_value: var_dump_data failed");
				return -1;
			}
			vp = value_raw;
		}

		// Escape value
		if (sql->sql_esc_value(value, sizeof value, vp))
		{
			log_error("sql_key_value: escape value failed");
			return -1;
		}

		n += snprintf(buffer + n, size - n, "%s%s%s%s=%s%s%s",
			n? join: "", cq, column, cq, vq, value, vq);
		if (n >= size)
		{
			log_error("sql_key_value: buffer exhausted");
			return -1;
		}
	}

	return 0;
}

int
sql_create(sql_t *sql, char *buffer, int size, char *table, var_t *scheme)
{
	ll_t *ll;
	ll_entry_t *pos;
	char column[BUFLEN];
	char columns[BUFLEN];
	char keys[BUFLEN];
	var_t *v;
	char *tq = sql->sql_q_table;
	char *cq = sql->sql_q_column;
	char *type;
	int n = 0;

	if(scheme->v_type != VT_LIST) {
		log_error("sql_key_value: bad v_type");
		return -1;
	}

	ll = scheme->v_data;

	pos = LL_START(ll);
	while ((v = ll_next(ll, &pos)) != NULL)
	{
		switch(v->v_type)
		{
		case VT_INT:	type = sql->sql_t_int; break;
		case VT_FLOAT:	type = sql->sql_t_float; break;
		case VT_STRING:	type = sql->sql_t_string; break;
		case VT_ADDR:	type = sql->sql_t_addr; break;
		default:
			log_error("sql_create: bad type");
			return -1;
		}

		// Escape column name
		if (sql->sql_esc_column(column, sizeof column, v->v_name))
		{
			log_error("sql_key_value: escape column string"
				" failed");
			return -1;
		}

		n += snprintf(columns + n, sizeof columns - n, "%s%s%s%s %s",
			n? ",": "", cq, column, cq, type);
		if (n >= sizeof columns)
		{
			log_error("sql_create: buffer exhausted");
			return -1;
		}
	}

	if (sql_columns(sql, keys, sizeof keys, SQL_KEYS, ",", scheme))
	{
		log_error("sql_create: sql_columns failed");
		return -1;
	}

	n = snprintf(buffer, size, "CREATE TABLE %s%s%s (%s PRIMARY KEY(%s))",
		tq, table, tq, columns, keys);
	if (n >= size)
	{
		log_error("sql_create: buffer exhausted");
		return -1;
	}

	return 0;
}
	
int
sql_select(sql_t *sql, char *buffer, int size, char *table, var_t *record)
{
	char columns[BUFLEN];
	char where[BUFLEN];
	int n = 0;
	char *tq = sql->sql_q_table;

	if (sql_columns(sql, columns, sizeof columns, SQL_ALL, ",", record))
	{
		log_error("sql_select: sql_columns failed");
		return -1;
	}

	if (sql_key_value(sql, where, sizeof where, SQL_KEYS, " AND ", record))
	{
		log_error("sql_select: sql_key_value failed");
		return -1;
	}

	n = snprintf(buffer, size, "SELECT %s FROM %s%s%s WHERE %s",
		columns, tq, table, tq, where);
	if (n >= size)
	{
		log_error("sql_select: buffer exhausted");
		return -1;
	}

	return 0;
}

int
sql_update(sql_t *sql, char *buffer, int size, char *table, var_t *record)
{
	char set[BUFLEN];
	char where[BUFLEN];
	int n;
	char *tq = sql->sql_q_table;

	if (sql_key_value(sql, set, sizeof set, SQL_VALUES, ",", record) == -1)
	{
		log_error("sql_select: sql_key_value failed");
		return -1;
	}

	if (sql_key_value(sql, where, sizeof where, 1, " AND ", record) == -1)
	{
		log_error("sql_select: sql_key_value failed");
		return -1;
	}

	n = snprintf(buffer, size, "UPDATE %s%s%s SET %s WHERE %s", tq, table,
		tq, set, where);
	if (n >= size)
	{
		log_error("sql_select: buffer exhausted");
		return -1;
	}

	return 0;
}

int
sql_delete(sql_t *sql, char *buffer, int size, char *table, var_t *record)
{
	char where[BUFLEN];
	int n;
	char *tq = sql->sql_q_table;

	if (sql_key_value(sql, where, sizeof where, 1, " AND ", record) == -1)
	{
		log_error("sql_select: sql_key_value failed");
		return -1;
	}

	n = snprintf(buffer, size, "DELETE FROM %s%s%s WHERE %s", tq, table,
		tq, where);
	if (n >= size)
	{
		log_error("sql_select: buffer exhausted");
		return -1;
	}

	return 0;
}

int
sql_db_get(dbt_t *dbt, var_t *record, var_t **result)
{
	char query[BUFLEN];
	void *db_result = NULL;
	int r = -1;

	if (sql_select(&dbt->dbt_driver->dd_sql, query, sizeof query,
		dbt->dbt_table, record))
	{
		log_error("sql_get: sql_select failed");
		goto exit;
	}

	db_result = dbt->dbt_driver->dd_sql_exec(dbt->dbt_handle, query);
	if (db_result == NULL)
	{
		log_error("sql_get: dd_sql_exec failed");
		goto exit;
	}

	*result = dbt->dbt_driver->dd_sql_unpack(dbt->dbt_handle, db_result);
	if (*result)
	{
		log_error("sql_get: dd_sql_exec failed");
		goto exit;
	}

	// Successful
	r = 0;

exit:
	if (result)
	{
		dbt->dbt_driver->dd_sql_free(dbt->dbt_handle, result);
	}

	return r;
}

int
sql_db_set(dbt_t *dbt, var_t *v)
{
	char query[BUFLEN];
	void *result = NULL;

	// Prepare query string
	if (sql_update(&dbt->dbt_driver->dd_sql, query, sizeof query,
		dbt->dbt_table, v))
	{
		log_error("sql_db_set: sql_update failed");
		return -1;
	}

	// Execute query
	result = dbt->dbt_driver->dd_sql_exec(dbt->dbt_handle, query);
	if (result == NULL)
	{
		log_error("sql_db_set: dd_sql_exec failed");
		return -1;
	}

	dbt->dbt_driver->dd_sql_free(dbt->dbt_handle, result);

	return 0;
}

int
sql_db_del(dbt_t *dbt, var_t *v)
{
	char query[BUFLEN];
	void *result = NULL;

	// Prepare query string
	if (sql_delete(&dbt->dbt_driver->dd_sql, query, sizeof query,
		dbt->dbt_table, v))
	{
		log_error("sql_db_del: sql_delete failed");
		return -1;
	}

	// Execute query
	result = dbt->dbt_driver->dd_sql_exec(dbt->dbt_handle, query);
	if (result == NULL)
	{
		log_error("sql_db_del: dd_sql_exec failed");
		return -1;
	}

	dbt->dbt_driver->dd_sql_free(dbt->dbt_handle, result);

	return 0;
}

#ifdef DEBUG

int
sql_test_esc(char *buffer, int size, char *src)
{
	strncpy(buffer, src, size);
	return 0;
}

void
sql_test(int n)
{
	int r;
	sql_t sql;
	var_t *scheme, *record;
	char query[BUFLEN];
	char pattern[BUFLEN];

	VAR_INT_T i = n;
	VAR_FLOAT_T f = n * 0.7;
	char *str = "foobar";

	var_sockaddr_t sa;
	struct sockaddr_in *sin = (struct sockaddr_in *) &sa;

	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = (n * 0x01010101);

	// Initialize sql_t
	sql.sql_esc_value  = sql_test_esc;
	sql.sql_esc_column = sql_test_esc;
	sql.sql_esc_table  = sql_test_esc;
	sql.sql_q_value    = "'";
	sql.sql_q_column   = "`";
	sql.sql_q_table    = "`";
	sql.sql_t_int      = "INT";
	sql.sql_t_float    = "FLOAT";
	sql.sql_t_string   = "VARCHAR(255)";
	sql.sql_t_addr     = "INET";

	// Create test scheme
	scheme = vlist_scheme("test",
		"int_key",	VT_INT,		VF_KEEPNAME | VF_KEY,
		"float_key",	VT_FLOAT,	VF_KEEPNAME | VF_KEY,
		"string_key",	VT_STRING,	VF_KEEPNAME | VF_KEY,
		"addr_key",	VT_ADDR,	VF_KEEPNAME | VF_KEY,
		"int",		VT_INT,		VF_KEEPNAME,
		"float",	VT_FLOAT,	VF_KEEPNAME,
		"string",	VT_STRING,	VF_KEEPNAME,
		"addr",		VT_ADDR,	VF_KEEPNAME,
		NULL);
	TEST_ASSERT(scheme != NULL, "vlist_scheme failed");

	// Create test record
	record = vlist_record(scheme, &i, &f, str, sin, &i, &f, str, sin);
	TEST_ASSERT(record != NULL, "vlist_record failed");

	// Create Query
	r = sql_create(&sql, query, sizeof query, "test_table", scheme);
	TEST_ASSERT(r == 0, "sql_select failed");
	TEST_ASSERT(strcmp(query, "CREATE TABLE `test_table` (`int_key` INT,`float_key` FLOAT,`string_key` VARCHAR(255),`addr_key` INET,`int` INT,`float` FLOAT,`string` VARCHAR(255),`addr` INET PRIMARY KEY(`int_key`,`float_key`,`string_key`,`addr_key`))") == 0, "sql_create returned wrong query");

	// Select Query
	r = sql_select(&sql, query, sizeof query, "test_table", record);
	TEST_ASSERT(r == 0, "sql_select failed");
	snprintf(pattern, sizeof pattern, "SELECT `int_key`,`float_key`,`string_key`,`addr_key`,`int`,`float`,`string`,`addr` FROM `test_table` WHERE `int_key`='%d' AND `float_key`='%.2f' AND `string_key`='foobar' AND `addr_key`='%d.%d.%d.%d'", n, n*0.7,n,n,n,n);
	//printf("%s\n%s\n\n", query, pattern);
	TEST_ASSERT(strcmp(pattern, query) == 0, "sql_select returned wrong query");

	// Update Query
	r = sql_update(&sql, query, sizeof query, "test_table", record);
	TEST_ASSERT(r == 0, "sql_update failed");
	snprintf(pattern, sizeof pattern, "UPDATE `test_table` SET `int`='%d',`float`='%.2f',`string`='foobar',`addr`='%d.%d.%d.%d' WHERE `int_key`='%d' AND `float_key`='%.2f' AND `string_key`='foobar' AND `addr_key`='%d.%d.%d.%d'", n,n*0.7,n,n,n,n,n,n*0.7,n,n,n,n);
	//printf("%s\n%s\n\n", query, pattern);
	TEST_ASSERT(strcmp(pattern, query) == 0, "sql_update returned wrong query");

	// Delete Query
	r = sql_delete(&sql, query, sizeof query, "test_table", record);
	TEST_ASSERT(r == 0, "sql_delete failed");
	snprintf(pattern, sizeof pattern, "DELETE FROM `test_table` WHERE `int_key`='%d' AND `float_key`='%.2f' AND `string_key`='foobar' AND `addr_key`='%d.%d.%d.%d'", n,n*0.7,n,n,n,n);
	//printf("%s\n%s\n\n", query, pattern);
	TEST_ASSERT(strcmp(pattern, query) == 0, "sql_update returned wrong query");

	return;
}

#endif
