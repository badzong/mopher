#include <string.h>

// Required for testing
#include <netinet/in.h>

#include <mopher.h>

#define BUFLEN 8192

static int
sql_columns(sql_t *sql, void *handle, char *buffer, int size, int types, char *join,
    var_t *scheme)
{
	ll_t *ll;
	ll_entry_t *pos;
	char column[BUFLEN];
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

		if (v->v_name == NULL)
		{
			log_error("sql_columns: v_name cannot be NULL");
			return -1;
		}
		
		// Escape column name
		if (sql->sql_esc_identifier(handle, column, sizeof column,
			v->v_name))
		{
			log_error("sql_columns: escape column string failed");
			return -1;
		}

		n += snprintf(buffer + n, size - n, "%s%s", n? join: "",
			column);
		if (n >= size)
		{
			log_error("sql_columns: buffer exhausted");
			return -1;
		}
	}

	return 0;
}

static int
sql_values(sql_t *sql, void *conn, char *buffer, int size, char *join,
    var_t *record)
{
	ll_t *ll;
	ll_entry_t *pos;
	char value[BUFLEN];
	char value_raw[BUFLEN];
	var_t *v;
	char *vp;
	int n = 0;
	int isnull;

	if(record->v_type != VT_LIST) {
		log_error("sql_values: bad v_type");
		return -1;
	}

	ll = record->v_data;

	pos = LL_START(ll);
	while ((v = ll_next(ll, &pos)) != NULL)
	{
		isnull = 0;
		// Cast value to string
		if (v->v_data == NULL)
		{
			strcpy(value, "NULL");
			isnull = 1;
		}
		else if (v->v_type == VT_STRING || v->v_type == VT_TEXT)
		{
			vp = v->v_data;
		}
		else
		{
			if (var_dump_data(v, value_raw, sizeof value_raw) == -1)
			{
				log_error("sql_values: var_dump_data failed");
				return -1;
			}
			vp = value_raw;
		}

		if (!isnull)
		{
			// Escape value
			if (sql->sql_esc_value(conn, value, sizeof value, vp))
			{
				log_error("sql_values: escape value failed");
				return -1;
			}
		}

		n += snprintf(buffer + n, size - n, "%s%s", n? join: "",
			value);
		if (n >= size)
		{
			log_error("sql_values: buffer exhausted");
			return -1;
		}
	}

	return 0;
}

static int
sql_key_value(sql_t *sql, void *conn, char *buffer, int size, int types, char *join,
    var_t *record)
{
	ll_t *ll;
	ll_entry_t *pos;
	var_t *v = NULL;
	int n = 0;
	char column[BUFLEN];
	char value_raw[BUFLEN];
	char value[BUFLEN];
	char *vp = NULL;
	int isnull;

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
		if (v->v_name == NULL)
		{
			log_error("sql_key_value: v_name cannot be NULL");
			return -1;
		}

		// Escape column name
		if (sql->sql_esc_identifier(conn, column, sizeof column, v->v_name))
		{
			log_error("sql_key_value: escape column string"
				" failed");
			return -1;
		}

		isnull = 0;
		// Cast value to string
		if (v->v_data == NULL)
		{
			strcpy(value, "NULL");
			isnull = 1;
		}
		else if (v->v_type == VT_STRING || v->v_type == VT_TEXT)
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
		if (!isnull)
		{
			if (sql->sql_esc_value(conn, value, sizeof value, vp))
			{
				log_error("sql_key_value: escape value for '%s' failed", vp);
				return -1;
			}
		}

		n += snprintf(buffer + n, size - n, "%s%s=%s",
			n? join: "", column, value);
		if (n >= size)
		{
			log_error("sql_key_value: buffer exhausted");
			return -1;
		}
	}

	return 0;
}

static int
sql_create(sql_t *sql, void *conn, char *buffer, int size, char *tablename, var_t *scheme)
{
	ll_t *ll;
	ll_entry_t *pos;
	char table[BUFLEN];
	char column[BUFLEN];
	char columns[BUFLEN];
	char keys[BUFLEN];
	var_t *v;
	char *type;
	int n = 0;

	if(scheme->v_type != VT_LIST) {
		log_error("sql_create: bad v_type");
		return -1;
	}

	// Escape table
	if (sql->sql_esc_identifier(conn, table, sizeof table, tablename))
	{
		log_error("sql_create: escape table failed");
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
		case VT_TEXT:	type = sql->sql_t_text; break;
		case VT_ADDR:	type = sql->sql_t_addr; break;
		default:
			log_error("sql_create: bad type");
			return -1;
		}

		// Escape column name
		if (sql->sql_esc_identifier(conn, column, sizeof column, v->v_name))
		{
			log_error("sql_create: escape column string failed");
			return -1;
		}

		n += snprintf(columns + n, sizeof columns - n, "%s%s %s",
			n? ",": "", column, type);
		if (n >= sizeof columns)
		{
			log_error("sql_create: buffer exhausted");
			return -1;
		}
	}

	if (sql_columns(sql, conn, keys, sizeof keys, SQL_KEYS, ",", scheme))
	{
		log_error("sql_create: sql_columns failed");
		return -1;
	}

	n = snprintf(buffer, size, "CREATE TABLE %s (%s, PRIMARY KEY(%s))",
		table, columns, keys);
	if (n >= size)
	{
		log_error("sql_create: buffer exhausted");
		return -1;
	}

	return 0;
}
	
static int
sql_select_all(sql_t *sql, void *conn, char *buffer, int size, char *tablename,
    var_t *scheme)
{
	char table[BUFLEN];
	char columns[BUFLEN];
	int n = 0;

	if (sql->sql_esc_identifier(conn, table, sizeof table, tablename))
	{
		log_error("sql_select_all: escape table failed");
		return -1;
	}

	if (sql_columns(sql, conn, columns, sizeof columns, SQL_ALL, ",", scheme))
	{
		log_error("sql_select_all: sql_columns failed");
		return -1;
	}

	n = snprintf(buffer, size, "SELECT %s FROM %s", columns, table);
	if (n >= size)
	{
		log_error("sql_select_all: buffer exhausted");
		return -1;
	}

	return 0;
}

static int
sql_select(sql_t *sql, void *conn, char *buffer, int size, char *tablename, var_t *record)
{
	char all[BUFLEN];
	char where[BUFLEN];
	int n = 0;

	if (sql_select_all(sql, conn, all, sizeof all, tablename, record))
	{
		log_error("sql_select: sql_select_all failed");
		return -1;
	}

	if (sql_key_value(sql, conn, where, sizeof where, SQL_KEYS, " AND ", record))
	{
		log_error("sql_select: sql_key_value failed");
		return -1;
	}

	n = snprintf(buffer, size, "%s WHERE %s", all, where);
	if (n >= size)
	{
		log_error("sql_select: buffer exhausted");
		return -1;
	}

	return 0;
}

static int
sql_insert(sql_t *sql, void *conn, char *buffer, int size, char *tablename, var_t *record)
{
	char table[BUFLEN];
	char columns[BUFLEN];
	char values[BUFLEN];
	int n;

	if (sql->sql_esc_identifier(conn, table, sizeof table, tablename))
	{
		log_error("sql_insert: escape table failed");
		return -1;
	}

	if (sql_columns(sql, conn, columns, sizeof columns, SQL_ALL, ",", record))
	{
		log_error("sql_insert: sql_columns failed");
		return -1;
	}

	if (sql_values(sql, conn, values, sizeof values, ",", record))
	{
		log_error("sql_insert: sql_columns failed");
		return -1;
	}

	n = snprintf(buffer, size, "INSERT INTO %s (%s) VALUES (%s)", table,
		columns, values);
	if (n >= size)
	{
		log_error("sql_insert: buffer exhausted");
		return -1;
	}

	return 0;
}

static int
sql_update(sql_t *sql, void *conn, char *buffer, int size, char *tablename, var_t *record)
{
	char table[BUFLEN];
	char set[BUFLEN];
	char where[BUFLEN];
	int n;

	if (sql->sql_esc_identifier(conn, table, sizeof table, tablename))
	{
		log_error("sql_update: escape table failed");
		return -1;
	}

	if (sql_key_value(sql, conn, set, sizeof set, SQL_VALUES, ",", record) == -1)
	{
		log_error("sql_update: sql_key_value failed");
		return -1;
	}

	if (sql_key_value(sql, conn, where, sizeof where, 1, " AND ", record) == -1)
	{
		log_error("sql_update: sql_key_value failed");
		return -1;
	}

	n = snprintf(buffer, size, "UPDATE %s SET %s WHERE %s", table, set,
		where);
	if (n >= size)
	{
		log_error("sql_update: buffer exhausted");
		return -1;
	}

	return 0;
}

static int
sql_delete(sql_t *sql, void *conn, char *buffer, int size, char *tablename, var_t *record)
{
	char table[BUFLEN];
	char where[BUFLEN];
	int n;

	if (sql->sql_esc_identifier(conn, table, sizeof table, tablename))
	{
		log_error("sql_delete: escape table failed");
		return -1;
	}

	if (sql_key_value(sql, conn, where, sizeof where, 1, " AND ", record) == -1)
	{
		log_error("sql_delete: sql_key_value failed");
		return -1;
	}

	n = snprintf(buffer, size, "DELETE FROM %s WHERE %s", table, where);
	if (n >= size)
	{
		log_error("sql_delete: buffer exhausted");
		return -1;
	}

	return 0;
}

static int
sql_cleanup(sql_t *sql, void *conn, char *buffer, int size, char *tablename)
{
	char table[BUFLEN];
	char expire_raw[BUFLEN];
	char expire[BUFLEN];
	int n;
	unsigned long now = time(NULL);

	if (sql->sql_esc_identifier(conn, table, sizeof table, tablename))
	{
		log_error("sql_cleanup: escape table failed");
		return -1;
	}

	n = snprintf(expire_raw, sizeof expire_raw, "%s%s", tablename,
		SQL_EXPIRE);
	if (n >= sizeof expire_raw)
	{
		log_error("sql_cleanup: buffer exhausted");
		return -1;
	}

	if (sql->sql_esc_identifier(conn, expire, sizeof expire, expire_raw))
	{
		log_error("sql_cleanup: escape table failed");
		return -1;
	}
	
	n = snprintf(buffer, size, "DELETE FROM %s WHERE %s < %lu", table,
		expire, now);
	if (n >= size)
	{
		log_error("sql_cleanup: buffer exhausted");
		return -1;
	}

	return 0;
}

static var_t *
sql_unpack(sql_t *sql, void *conn, void *row, int nrow, var_t *scheme)
{
	var_t *v = NULL;
	var_t *item = NULL;
	var_t *record = NULL;
	ll_t *ll;
	ll_entry_t *pos;
	int field_is_key;
	int n;
	char *value;

	if(scheme->v_type != VT_LIST) {
		log_error("sql_unpack: bad type");
		goto error;
	}

	record = vlist_create(scheme->v_name, VF_COPYNAME);
	if(record == NULL) {
		log_error("sql_unpack: var_create failed");
		goto error;
	}

	ll = scheme->v_data;
	pos = LL_START(ll);

	for (n = 0; (item = ll_next(ll, &pos)); ++n) {
		field_is_key = item->v_flags & VF_KEY;

		value = sql->sql_get_value(conn, row, nrow, n);
		if (value == NULL && field_is_key)
		{
			log_error("sql_unpack: refuse to unpack NULL key");
			goto error;
		}

		v = var_scan(item->v_type, item->v_name, value);
		if(v == NULL)
		{
			log_error("sql_unpack: var_scan for field %d: %s failed",
				n, item->v_name);
			goto error;
		}

		// Set VF_KEY for keys
		if (field_is_key)
		{
			v->v_flags |= VF_KEY;
		}

		// Append item to record
		if(vlist_append(record, v) == -1) {
			log_error("sql_unpack: vlist_append failed");
			goto error;
		}

		// Prevent multiple free in case of goto error
		v = NULL;
	}

	return record;


error:

	if (v) {
		var_delete(v);
	}

	if (record) {
		var_delete(record);
	}

	return NULL;
}

static int
sql_db_exec_only(sql_t *sql, void *conn, char *stmt)
{
	int tuples, affected;
	void *result;
	int r = -1;

	if (sql->sql_exec(conn, &result, stmt, &tuples, &affected))
	{
		log_error("sql_db_exec_only: sql_exec failed");
		goto exit;
	}

	if (tuples)
	{
		log_error("sql_db_exec_only: query returned data");
		goto exit;
	}

	// Successful
	r = affected;

exit:
	if (result)
	{
		sql->sql_free_result(result);
	}

	return r;
}

int
sql_db_get(dbt_t *dbt, var_t *lookup, var_t **record)
{
	char query[BUFLEN];
	void *result = NULL;
	void *row = NULL;
	int tuples, affected;
	int r = -1;

	char *tablename = lookup->v_name;
	void *conn = dbt->dbt_handle;
	sql_t *sql = &dbt->dbt_driver->dd_sql;
	var_t *scheme = dbt->dbt_scheme;

	*record = NULL;

	if (tablename == NULL)
	{
		log_error("sql_db_get: lookup name unset");
		return -1;
	}

	if (sql_select(sql, conn, query, sizeof query, tablename, lookup))
	{
		log_error("sql_db_get: sql_select failed");
		goto exit;
	}

	if (sql->sql_exec(conn, &result, query, &tuples, &affected))
	{
		log_error("sql_db_get: dd_sql_exec failed");
		goto exit;
	}

	if (tuples == 0)
	{
		r = 0;
		goto exit;
	}

	if (tuples != 1)
	{
		log_error("sql_db_get: expected 1 tuple, got %d", tuples);
		goto exit;
	}

	row = sql->sql_get_row(conn, result, 0);
	if (row == NULL)
	{
		log_error("sql_db_get: sql_get_row failed");
		goto exit;
	}

	*record = sql_unpack(sql, conn, row, 0, scheme);
	if (*record == NULL)
	{
		log_error("sql_db_get: sql_unpack failed");
		goto exit;
	}

	// Successful
	r = 0;

exit:
	if (result)
	{
		sql->sql_free_result(result);
	}

	return r;
}

int
sql_db_set(dbt_t *dbt, var_t *record)
{
	char query[BUFLEN];
	int affected;

	char *tablename = record->v_name;
	void *conn = dbt->dbt_handle;
	sql_t *sql = &dbt->dbt_driver->dd_sql;

	if (tablename == NULL)
	{
		log_error("sql_db_set: record name unset");
		return -1;
	}

	// Begin
	if (sql_db_exec_only(sql, conn, "BEGIN") == -1)
	{
		log_error("sql_db_set: sql_db_exec_only failed");
		return -1;
	}

	// Prepare query string
	if (sql_update(sql, conn, query, sizeof query, tablename, record))
	{
		log_error("sql_db_set: sql_update failed");
		goto rollback;
	}


	// Execute query
	affected = sql_db_exec_only(sql, conn, query);
	if (affected == -1)
	{
		log_error("sql_db_set: sql_db_exec_only failed");
		goto rollback;
	}

	// Update successful
	if (affected)
	{
		goto commit;
	}

	// Record needs to be inserted
	if (sql_insert(sql, conn, query, sizeof query, tablename, record))
	{
		log_error("sql_db_set: sql_insert failed");
		goto rollback;
	}

	// Execute query
	affected = sql_db_exec_only(sql, conn, query);
	if (affected == -1)
	{
		log_error("sql_db_set: sql_db_exec_only failed");
		goto rollback;
	}

	if (affected != 1)
	{
		log_error("sql_db_set: %d rows affected. Should be 1", affected);
		goto rollback;
	}

commit:
	if (sql_db_exec_only(sql, conn, "COMMIT") == -1)
	{
		log_error("sql_db_set: sql_db_exec_only failed");
		return -1;
	}

	return 0;

rollback:
	if (sql_db_exec_only(sql, conn, "ROLLBACK") == -1)
	{
		log_error("sql_db_set: sql_db_exec_only failed");
	}

	return -1;
}

int
sql_db_del(dbt_t *dbt, var_t *record)
{
	char query[BUFLEN];

	char *tablename = record->v_name;
	void *conn = dbt->dbt_handle;
	sql_t *sql = &dbt->dbt_driver->dd_sql;

	if (tablename == NULL)
	{
		log_error("sql_db_del: record name unset");
		return -1;
	}

	// Prepare query string
	if (sql_delete(sql, conn, query, sizeof query, tablename, record))
	{
		log_error("sql_db_del: sql_delete failed");
		return -1;
	}

	// Begin
	if (sql_db_exec_only(sql, conn, "BEGIN") == -1)
	{
		log_error("sql_db_del: sql_db_exec_only failed");
		return -1;
	}

	// Execute query
	if (sql_db_exec_only(sql, conn, query) == -1)
	{
		log_error("sql_db_del: sql_db_exec_only failed");
		return -1;
	}

	// Commit
	if (sql_db_exec_only(sql, conn, "COMMIT") == -1)
	{
		log_error("sql_db_del: sql_db_exec_only failed");
		return -1;
	}

	return 0;
}

int
sql_db_walk(dbt_t *dbt, dbt_db_callback_t callback)
{
	char query[BUFLEN];
	void *result = NULL;
	int tuples, affected;
	void *row;
	var_t *record;
	int i;

	void *conn = dbt->dbt_handle;
	sql_t *sql = &dbt->dbt_driver->dd_sql;
	var_t *scheme = dbt->dbt_scheme;

	if (sql_select_all(sql, conn, query, sizeof query, dbt->dbt_table, scheme))
	{
		log_error("sql_db_walk: sql_cleanup failed");
		goto error;
	}

	// Execute query
	if (sql->sql_exec(conn, &result, query, &tuples, &affected))
	{
		log_error("sql_db_cleanup: sql_exec failed");
		goto error;
	}

	for (i = 0; (row = sql->sql_get_row(conn, result, i)) != NULL; ++i)
	{
		record = sql_unpack(sql, conn, row, i, scheme);
		if (record == NULL)
		{
			log_error("sql_db_walk: sql_unpack failed");
			goto error;
		}

		if (callback(dbt, record))
		{
			log_error("sql_db_walk: callback failed");
			goto error;
		}

		var_delete(record);
	}

	sql->sql_free_result(result);
	return 0;
	
error:
	if (result)
	{
		sql->sql_free_result(result);
	}

	return -1;
}

int
sql_db_cleanup(dbt_t *dbt)
{
	char query[BUFLEN];
	void *result = NULL;
	int tuples, affected;

	void *conn = dbt->dbt_handle;
	sql_t *sql = &dbt->dbt_driver->dd_sql;

	// Prepare query string
	if (sql_cleanup(sql, conn, query, sizeof query, dbt->dbt_table))
	{
		log_error("sql_db_cleanup: sql_cleanup failed");
		return -1;
	}

	// Execute query
	if (sql->sql_exec(conn, &result, query, &tuples, &affected))
	{
		log_error("sql_db_cleanup: sql_exec failed");
		return -1;
	}

	sql->sql_free_result(result);

	return affected;
}

void
sql_open(sql_t *sql, void *conn, var_t *scheme)
{
	char query[BUFLEN];
	char *tablename = scheme->v_name;
	void *res;
	int tuples, affected;

	if (tablename == NULL)
	{
		log_die(EX_SOFTWARE, "sql_open: schema->v_name cannot be NULL");
	}

	if (sql->sql_table_exists(conn, tablename))
	{
		log_debug("sql_open: table %s exists", tablename);
		return;
	}

	if (sql_create(sql, conn, query, sizeof query, tablename, scheme))
	{
		log_die(EX_SOFTWARE, "sql_open: sql_create failed");
	}

	if (sql->sql_exec(conn, &res, query, &tuples, &affected))
	{
		log_die(EX_SOFTWARE, "sql_open: sql_exec failed");
	}
	
	sql->sql_free_result(res);

	return;
}

#ifdef DEBUG

static int
sql_test_esc(void *conn, char *buffer, int size, char *src)
{
	return util_quote(buffer, size, src, "'");
}

void
sql_test(int n)
{
	sql_t sql;
	var_t *scheme, *record;
	char query[BUFLEN];
	char pattern[BUFLEN];

	VAR_INT_T i = n;
	VAR_FLOAT_T f = n * 0.7;
	char *str = "foobar";
	char *txt = "barfoo";

	var_sockaddr_t sa;
	struct sockaddr_in *sin = (struct sockaddr_in *) &sa;

	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = (n * 0x01010101);

	// Initialize sql_t
	sql.sql_esc_value      = sql_test_esc;
	sql.sql_esc_identifier = sql_test_esc;
	sql.sql_t_int          = "INT";
	sql.sql_t_float        = "FLOAT";
	sql.sql_t_string       = "VARCHAR(255)";
	sql.sql_t_text         = "TEXT";
	sql.sql_t_addr         = "INET";

	// Create test scheme
	scheme = vlist_scheme("test",
		"int_key",	VT_INT,		VF_KEEPNAME | VF_KEY,
		"float_key",	VT_FLOAT,	VF_KEEPNAME | VF_KEY,
		"string_key",	VT_STRING,	VF_KEEPNAME | VF_KEY,
		"addr_key",	VT_ADDR,	VF_KEEPNAME | VF_KEY,
		"int",		VT_INT,		VF_KEEPNAME,
		"float",	VT_FLOAT,	VF_KEEPNAME,
		"string",	VT_STRING,	VF_KEEPNAME,
		"text",		VT_TEXT,	VF_KEEPNAME,
		"addr",		VT_ADDR,	VF_KEEPNAME,
		NULL);
	TEST_ASSERT(scheme != NULL);

	// Create test record
	TEST_ASSERT((record = vlist_record(scheme, &i, &f, str, sin, &i, &f, str, txt, sin)) != NULL);

	// Create Query
	TEST_ASSERT(sql_create(&sql, NULL, query, sizeof query, "test_table", scheme) == 0);
	strcpy(pattern, "CREATE TABLE 'test_table' ('int_key' INT,'float_key' FLOAT,'string_key' VARCHAR(255),'addr_key' INET,'int' INT,'float' FLOAT,'string' VARCHAR(255),'text' TEXT,'addr' INET, PRIMARY KEY('int_key','float_key','string_key','addr_key'))");
	//printf("%s\n%s\n\n", query, pattern);
	TEST_ASSERT(strcmp(pattern, query) == 0);

	// Select Query
	TEST_ASSERT(sql_select(&sql, NULL, query, sizeof query, "test_table", record) == 0);
	snprintf(pattern, sizeof pattern, "SELECT 'int_key','float_key','string_key','addr_key','int','float','string','text','addr' FROM 'test_table' WHERE 'int_key'='%d' AND 'float_key'='%.2f' AND 'string_key'='foobar' AND 'addr_key'='%d.%d.%d.%d'", n, n*0.7,n,n,n,n);
	//printf("%s\n%s\n\n", query, pattern);
	TEST_ASSERT(strcmp(pattern, query) == 0);

	// Update Query
	TEST_ASSERT(sql_update(&sql, NULL, query, sizeof query, "test_table", record) == 0);
	snprintf(pattern, sizeof pattern, "UPDATE 'test_table' SET 'int'='%d','float'='%.2f','string'='foobar','text'='barfoo','addr'='%d.%d.%d.%d' WHERE 'int_key'='%d' AND 'float_key'='%.2f' AND 'string_key'='foobar' AND 'addr_key'='%d.%d.%d.%d'", n,n*0.7,n,n,n,n,n,n*0.7,n,n,n,n);
	//printf("%s\n%s\n\n", query, pattern);
	TEST_ASSERT(strcmp(pattern, query) == 0);

	// Delete Query
	TEST_ASSERT(sql_delete(&sql, NULL, query, sizeof query, "test_table", record) == 0);
	snprintf(pattern, sizeof pattern, "DELETE FROM 'test_table' WHERE 'int_key'='%d' AND 'float_key'='%.2f' AND 'string_key'='foobar' AND 'addr_key'='%d.%d.%d.%d'", n,n*0.7,n,n,n,n);
	//printf("%s\n%s\n\n", query, pattern);
	TEST_ASSERT(strcmp(pattern, query) == 0);

	var_delete(scheme);
	var_delete(record);

	return;
}

#endif
