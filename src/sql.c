#include <string.h>

// Required for testing
#include <netinet/in.h>

#include <mopher.h>

#define BUFLEN 8192

int
sql_columns(void *handle, sql_t *sql, char *buffer, int size, int types, char *join,
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

int
sql_values(void *conn, sql_t *sql, char *buffer, int size, char *join,
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
		else if (v->v_type == VT_STRING)
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

int
sql_key_value(void *conn, sql_t *sql, char *buffer, int size, int types, char *join,
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
		else if (v->v_type == VT_STRING)
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

int
sql_create(void *conn, sql_t *sql, char *buffer, int size, char *tablename, var_t *scheme)
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

	if (sql_columns(conn, sql, keys, sizeof keys, SQL_KEYS, ",", scheme))
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
	
int
sql_select(char *conn, sql_t *sql, char *buffer, int size, char *tablename, var_t *record)
{
	char table[BUFLEN];
	char columns[BUFLEN];
	char where[BUFLEN];
	int n = 0;

	if (sql->sql_esc_identifier(conn, table, sizeof table, tablename))
	{
		log_error("sql_select: escape table failed");
		return -1;
	}

	if (sql_columns(conn, sql, columns, sizeof columns, SQL_ALL, ",", record))
	{
		log_error("sql_select: sql_columns failed");
		return -1;
	}

	if (sql_key_value(conn, sql, where, sizeof where, SQL_KEYS, " AND ", record))
	{
		log_error("sql_select: sql_key_value failed");
		return -1;
	}

	n = snprintf(buffer, size, "SELECT %s FROM %s WHERE %s",
		columns, table, where);
	if (n >= size)
	{
		log_error("sql_select: buffer exhausted");
		return -1;
	}

	return 0;
}

int
sql_insert(void *conn, sql_t *sql, char *buffer, int size, char *tablename, var_t *record)
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

	if (sql_columns(conn, sql, columns, sizeof columns, SQL_ALL, ",", record))
	{
		log_error("sql_insert: sql_columns failed");
		return -1;
	}

	if (sql_values(conn, sql, values, sizeof values, ",", record))
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

int
sql_update(void *conn, sql_t *sql, char *buffer, int size, char *tablename, var_t *record)
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

	if (sql_key_value(conn, sql, set, sizeof set, SQL_VALUES, ",", record) == -1)
	{
		log_error("sql_update: sql_key_value failed");
		return -1;
	}

	if (sql_key_value(conn, sql, where, sizeof where, 1, " AND ", record) == -1)
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

int
sql_delete(void *conn, sql_t *sql, char *buffer, int size, char *tablename, var_t *record)
{
	char table[BUFLEN];
	char where[BUFLEN];
	int n;

	if (sql->sql_esc_identifier(conn, table, sizeof table, tablename))
	{
		log_error("sql_delete: escape table failed");
		return -1;
	}

	if (sql_key_value(conn, sql, where, sizeof where, 1, " AND ", record) == -1)
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

int
sql_cleanup(void *conn, sql_t *sql, char *buffer, int size, char *tablename)
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

var_t *
sql_unpack(void *conn, void *res, sql_t *sql, var_t *scheme)
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

		value = sql->sql_get_value(conn, res, n);
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
sql_db_exec_only(void *conn, sql_t *sql, char *stmt)
{
	int tuples, affected;
	void *result;
	int r = -1;

	result = sql->sql_exec(conn, stmt, &tuples, &affected);
	if (result == NULL)
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
		sql->sql_free_result(conn, result);
	}

	return r;
}

int
sql_db_get(void *conn, sql_t *sql, var_t *scheme, var_t *record, var_t **result)
{
	char query[BUFLEN];
	void *db_result = NULL;
	char *tablename = record->v_name;
	int tuples, affected;
	int r = -1;

	*result = NULL;

	if (tablename == NULL)
	{
		log_error("sql_db_get: record name unset");
		return -1;
	}

	if (sql_select(conn, sql, query, sizeof query, tablename, record))
	{
		log_error("sql_db_get: sql_select failed");
		goto exit;
	}

	db_result = sql->sql_exec(conn, query, &tuples, &affected);
	if (db_result == NULL)
	{
		log_error("sql_db_get: dd_sql_exec failed");
		goto exit;
	}

	if (tuples == 0)
	{
		r = 0;
		goto exit;
	}

	*result = sql_unpack(conn, db_result, sql, scheme);
	if (*result == NULL)
	{
		log_error("sql_db_get: sql_unpack failed");
		goto exit;
	}

	// Successful
	r = 0;

exit:
	if (db_result)
	{
		sql->sql_free_result(conn, db_result);
	}

	return r;
}

int
sql_db_set(void *conn, sql_t *sql, var_t *record)
{
	char query[BUFLEN];
	void *result = NULL;
	char *tablename = record->v_name;
	int tuples, affected;

	if (tablename == NULL)
	{
		log_error("sql_db_set: record name unset");
		return -1;
	}

	// Begin
	if (sql_db_exec_only(conn, sql, "BEGIN") == -1)
	{
		log_error("sql_db_set: sql_db_exec_only failed");
		return -1;
	}

	// Prepare query string
	if (sql_update(conn, sql, query, sizeof query, tablename, record))
	{
		log_error("sql_db_set: sql_update failed");
		goto rollback;
	}


	// Execute query
	affected = sql_db_exec_only(conn, sql, query);
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
	if (sql_insert(conn, sql, query, sizeof query, tablename, record))
	{
		log_error("sql_db_set: sql_insert failed");
		goto rollback;
	}

	// Execute query
	affected = sql_db_exec_only(conn, sql, query);
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
	if (sql_db_exec_only(conn, sql, "COMMIT") == -1)
	{
		log_error("sql_db_set: sql_db_exec_only failed");
		return -1;
	}

	return 0;

rollback:
	if (sql_db_exec_only(conn, sql, "ROLLBACK") == -1)
	{
		log_error("sql_db_set: sql_db_exec_only failed");
	}

	return -1;
}

int
sql_db_del(void *conn, sql_t *sql, var_t *record)
{
	char query[BUFLEN];
	void *result = NULL;
	char *tablename = record->v_name;
	int tuples, affected;

	if (tablename == NULL)
	{
		log_error("sql_db_del: record name unset");
		return -1;
	}

	// Prepare query string
	if (sql_delete(conn, sql, query, sizeof query, tablename, record))
	{
		log_error("sql_db_del: sql_delete failed");
		return -1;
	}

	// Begin
	if (sql_db_exec_only(conn, sql, "BEGIN") == -1)
	{
		log_error("sql_db_del: sql_db_exec_only failed");
		return -1;
	}

	// Execute query
	if (sql_db_exec_only(conn, sql, query) == -1)
	{
		log_error("sql_db_del: sql_db_exec_only failed");
		return -1;
	}

	// Commit
	if (sql_db_exec_only(conn, sql, "COMMIT") == -1)
	{
		log_error("sql_db_del: sql_db_exec_only failed");
		return -1;
	}

	return 0;
}

int
sql_db_cleanup(void *conn, sql_t *sql, char *tablename)
{
	char query[BUFLEN];
	void *result = NULL;
	int tuples, affected;

	if (tablename == NULL)
	{
		log_error("sql_db_cleanup: tablename is NULL");
		return -1;
	}

	// Prepare query string
	if (sql_cleanup(conn, sql, query, sizeof query, tablename))
	{
		log_error("sql_db_cleanup: sql_cleanup failed");
		return -1;
	}

	// Execute query
	result = sql->sql_exec(conn, query, &tuples, &affected);
	if (result == NULL)
	{
		log_error("sql_db_cleanup: sql_exec failed");
		return -1;
	}

	sql->sql_free_result(conn, result);

	return affected;
}

void
sql_open(void *conn, sql_t *sql, var_t *scheme)
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

	if (sql_create(conn, sql, query, sizeof query, tablename, scheme))
	{
		log_die(EX_SOFTWARE, "sql_open: sql_create failed");
	}

	res = sql->sql_exec(conn, query, &tuples, &affected);
	if (res == NULL)
	{
		log_die(EX_SOFTWARE, "sql_open: sql_exec failed");
	}
	
	sql->sql_free_result(conn, res);

	return;
}

#ifdef DEBUG

int
sql_test_esc(void *conn, char *buffer, int size, char *src)
{
	return util_quote(buffer, size, src, "'");
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
	sql.sql_esc_value      = sql_test_esc;
	sql.sql_esc_identifier = sql_test_esc;
	sql.sql_t_int          = "INT";
	sql.sql_t_float        = "FLOAT";
	sql.sql_t_string       = "VARCHAR(255)";
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
		"addr",		VT_ADDR,	VF_KEEPNAME,
		NULL);
	TEST_ASSERT(scheme != NULL, "vlist_scheme failed");

	// Create test record
	record = vlist_record(scheme, &i, &f, str, sin, &i, &f, str, sin);
	TEST_ASSERT(record != NULL, "vlist_record failed");

	// Create Query
	r = sql_create(NULL, &sql, query, sizeof query, "test_table", scheme);
	TEST_ASSERT(r == 0, "sql_select failed");
	strcpy(pattern, "CREATE TABLE 'test_table' ('int_key' INT,'float_key' FLOAT,'string_key' VARCHAR(255),'addr_key' INET,'int' INT,'float' FLOAT,'string' VARCHAR(255),'addr' INET, PRIMARY KEY('int_key','float_key','string_key','addr_key'))");
	//printf("%s\n%s\n\n", query, pattern);
	TEST_ASSERT(strcmp(pattern, query) == 0, "sql_select returned wrong query");

	// Select Query
	r = sql_select(NULL, &sql, query, sizeof query, "test_table", record);
	TEST_ASSERT(r == 0, "sql_select failed");
	snprintf(pattern, sizeof pattern, "SELECT 'int_key','float_key','string_key','addr_key','int','float','string','addr' FROM 'test_table' WHERE 'int_key'='%d' AND 'float_key'='%.2f' AND 'string_key'='foobar' AND 'addr_key'='%d.%d.%d.%d'", n, n*0.7,n,n,n,n);
	//printf("%s\n%s\n\n", query, pattern);
	TEST_ASSERT(strcmp(pattern, query) == 0, "sql_select returned wrong query");

	// Update Query
	r = sql_update(NULL, &sql, query, sizeof query, "test_table", record);
	TEST_ASSERT(r == 0, "sql_update failed");
	snprintf(pattern, sizeof pattern, "UPDATE 'test_table' SET 'int'='%d','float'='%.2f','string'='foobar','addr'='%d.%d.%d.%d' WHERE 'int_key'='%d' AND 'float_key'='%.2f' AND 'string_key'='foobar' AND 'addr_key'='%d.%d.%d.%d'", n,n*0.7,n,n,n,n,n,n*0.7,n,n,n,n);
	//printf("%s\n%s\n\n", query, pattern);
	TEST_ASSERT(strcmp(pattern, query) == 0, "sql_update returned wrong query");

	// Delete Query
	r = sql_delete(NULL, &sql, query, sizeof query, "test_table", record);
	TEST_ASSERT(r == 0, "sql_delete failed");
	snprintf(pattern, sizeof pattern, "DELETE FROM 'test_table' WHERE 'int_key'='%d' AND 'float_key'='%.2f' AND 'string_key'='foobar' AND 'addr_key'='%d.%d.%d.%d'", n,n*0.7,n,n,n,n);
	//printf("%s\n%s\n\n", query, pattern);
	TEST_ASSERT(strcmp(pattern, query) == 0, "sql_update returned wrong query");

	return;
}

#endif
