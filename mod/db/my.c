#include <string.h>
#include <malloc.h>
#include <db.h>
#include <errno.h>
#include <mysql/mysql.h>

#include "mopher.h"

#define MY_QUERY_LEN 2048
#define MY_STRING_LEN 320
#define MY_BUCKETS 64


/*
 * my_query_type is used to loop over my_prepare_xxx callbacks.
 * See my_prepare()
 */
typedef enum my_query_type_t { MY_SELECT, MY_UPDATE, MY_INSERT, MY_DELETE,
	MY_CLEANUP, MY_MAX } my_query_type_t;


/*
 * Buffers are bound to a query by mysql_bind_params/results.
 * See my_query_create()
 */
typedef struct my_buffer {
	char		*mb_name;
	int		 mb_type;
	void		*mb_buffer;
	unsigned long	 mb_length;
	my_bool		 mb_is_null;
	my_bool		 mb_error;
} my_buffer_t;

/*
 * A query consists of the prepared statement, bound params (optional) and
 * bound results (optional).
 */
typedef struct my_query {
	MYSQL_STMT		*my_stmt;
	MYSQL_BIND		*my_params;
	MYSQL_BIND		*my_results;
} my_query_t;

/*
 * The MySQL handle stores the MYSQL * and an array of all prepared queries.
 * The storage table holds my_buffers for the record (see dbt->dbt_scheme).
 */
typedef struct my_handle {
	MYSQL		*my_db;
	my_query_t	*my_query[MY_MAX];
	ht_t		*my_storage;
} my_handle_t;

/*
 * struct sockaddr_storage may vary on different systems ???
 * my_addr_type is set to VARBINARY(sizeof (struct sockaddr_storage) by init.
 */
static char my_addr_type[15];

/*
 * Datatype storage requirements (allocated for my_buffer_t)
 */
static unsigned long my_buffer_length[] = { 0, sizeof (VAR_INT_T),
    sizeof (VAR_FLOAT_T), MY_STRING_LEN, sizeof (struct sockaddr_storage), 0,
    0, 0 };

/*
 * Datatype conversions (var.c <-> MySQL) for mysql_bind_params/results.
 */
static int my_buffer_types[] = { 0, MYSQL_TYPE_LONG, MYSQL_TYPE_DOUBLE,
	MYSQL_TYPE_STRING, MYSQL_TYPE_BLOB, 0, 0, 0 };

/*
 * Datatype conversions (var.c <-> MySQL) for create database statement
 */
static char *my_types[] = { NULL, "INT", "DOUBLE", "VARCHAR(320)", \
	my_addr_type, NULL, NULL, NULL };


/*
 * my_prepare_xxx callbacks
 */
typedef my_query_t *(*my_prepare_callback_t)(dbt_t *dbt, ht_t *storage);

/*
 * dbt_driver storage for dbt_driver_register
 */
static dbt_driver_t dbt_driver;

/*
 * Enum for my_record_split(). See below.
 */
typedef enum my_record_split { RS_FULL, RS_KEYS, RS_VALUES } my_record_split_t;


static void
my_buffer_delete(my_buffer_t *mb)
{
	if (mb->mb_buffer)
	{
		free(mb->mb_buffer);
	}

	free(mb);

	return;
}


static my_buffer_t *
my_buffer_create(char *name, var_type_t type)
{
	my_buffer_t *mb;
	unsigned long length;

	mb = (my_buffer_t *) malloc(sizeof (my_buffer_t));
	if (mb == NULL)
	{
		log_error("my_buffer_create: malloc");
		return NULL;
	}

	memset(mb, 0, sizeof (my_buffer_t));

	length = my_buffer_length[type];

	mb->mb_buffer = malloc(length);
	if (mb->mb_buffer == NULL)
	{
		log_error("my_buffer_create: malloc");
		return NULL;
	}

	mb->mb_name = name;
	mb->mb_length = length;
	mb->mb_type = my_buffer_types[type];

	return mb;
}


static hash_t
my_buffer_hash(my_buffer_t *mb)
{
	return HASH(mb->mb_name, strlen(mb->mb_name));
}


static int
my_buffer_match(my_buffer_t *mb1, my_buffer_t *mb2)
{
	if (strcmp(mb1->mb_name, mb2->mb_name) == 0)
	{
		return 1;
	}

	return 0;
}


static void
my_query_delete(my_query_t *mq)
{
	if (mq->my_stmt) {
		mysql_stmt_close(mq->my_stmt);
	}

	if (mq->my_params) {
		free(mq->my_params);
	}
		
	if (mq->my_results) {
		free(mq->my_results);
	}
		
	free(mq);

	return;
}

static my_query_t *
my_query_create(MYSQL *db, char *query, ht_t *storage, ll_t *params,
    ll_t *results)
{
	my_query_t *mq = NULL;
	int size;
	int i;
	char *key;
	my_buffer_t lookup, *mb;

	log_debug("my_query_create: create query: %s", query);

	mq = (my_query_t *) malloc(sizeof (my_query_t));
	if (mq == NULL)
	{
		log_error("my_query_create: malloc");
		goto error;
	}

	memset(mq, 0, sizeof (my_query_t));

	/*
	 * Allocate memory for params
	 */
	if (params)
	{
		size = params->ll_size * sizeof (MYSQL_BIND);

		mq->my_params = (MYSQL_BIND *) malloc(size);
		if (mq->my_params == NULL) {
			log_error("my_query_create: malloc");
			goto error;
		}

		memset(mq->my_params, 0, size);
	}

	/*
	 * Allocate memory for results
	 */
	if (results) {
		size = results->ll_size * sizeof (MYSQL_BIND);

		mq->my_results = (MYSQL_BIND *) malloc(size);
		if (mq->my_results == NULL) {
			log_error("my_query_create: malloc");
			goto error;
		}
	
		memset(mq->my_results, 0, size);
	}
	

	/*
	 * Initialize MYSQL_STMT
	 */
	mq->my_stmt = mysql_stmt_init(db);
	if (mq->my_stmt == NULL) {
		log_error("my_query_create: mysql_stmt_init: %s",
			mysql_stmt_error(mq->my_stmt));
		goto error;
	}

	/*
	 * Prepare statement
	 */
	if (mysql_stmt_prepare(mq->my_stmt, query, strlen(query))) {
		log_error("my_query_create: mysql_stmt_prepare: %s",
			mysql_stmt_error(mq->my_stmt));
		goto error;
	}

	/*
	 * Bind params
	 */
	if (params)
	{
		ll_rewind(params);

		for (i = 0; (key = ll_next(params)); ++i)
		{
			lookup.mb_name = key;
			mb = ht_lookup(storage, &lookup);

			if (mb == NULL)
			{
				log_error("my_query_create: no storage entry");
				goto error;
			}

			mq->my_params[i].buffer = mb->mb_buffer;
			mq->my_params[i].buffer_type = mb->mb_type;
			mq->my_params[i].length = &mb->mb_length;
			mq->my_params[i].is_null = &mb->mb_is_null;
			mq->my_params[i].error = &mb->mb_error;
		}

		if (mysql_stmt_bind_param(mq->my_stmt, mq->my_params))
		{
			log_error("my_query_create: mysql_stmt_bind_param: %s", 
			mysql_stmt_error(mq->my_stmt));
			goto error;
		}
	}

	if (results == NULL)
	{
		return mq;
	}
	
	/*
	 * Bind results
	 */
	ll_rewind(results);
	for (i = 0; (key = ll_next(results)); ++i)
	{
		lookup.mb_name = key;
		mb = ht_lookup(storage, &lookup);

		if (mb == NULL) {
			log_error("my_query_create: no storage entry");
			goto error;
		}

		mq->my_results[i].buffer = mb->mb_buffer;
		mq->my_results[i].buffer_type = mb->mb_type;
		mq->my_results[i].length = &mb->mb_length;
		mq->my_results[i].is_null = &mb->mb_is_null;
		mq->my_results[i].error = &mb->mb_error;
	}

	if (mysql_stmt_bind_result(mq->my_stmt, mq->my_results)) {
		log_error("my_query_create: mysql_stmt_bind_result: %s", 
			mysql_stmt_error(mq->my_stmt));
		goto error;
	}

	return mq;


error:
	if (mq) {
		my_query_delete(mq);
	}

	return NULL;
}
	


static void
my_close(dbt_t *dbt)
{
	my_handle_t *mh = dbt->dbt_handle;
	my_query_type_t qt;

	for (qt = 0; qt < MY_MAX; ++qt)
	{
		if (mh->my_query[qt])
		{
			my_query_delete(mh->my_query[qt]);
		}
	}

	if (mh->my_storage)
	{
		ht_delete(mh->my_storage);
	}
	
	if (mh->my_db) {
		mysql_close(mh->my_db);
	}

	free(mh);

	dbt->dbt_handle = NULL;

	return;
}


static ll_t *
my_record_split(dbt_t *dbt, my_record_split_t rs)
{
	ll_t *scheme = dbt->dbt_scheme->v_data;
	ll_t *list = NULL;
	var_t *v;
	int r = 0;

	list = ll_create();
	if (list == NULL)
	{
		log_error("my_record_split: ll_create failed");
		goto error;
	}

	for (ll_rewind(scheme); (v = ll_next(scheme)) && r >= 0;)
	{
		if (rs == RS_FULL ||
		   (rs == RS_KEYS && (v->v_flags & VF_KEY)) ||
		   (rs == RS_VALUES && ((v->v_flags &VF_KEY) == 0)))
		{
			r = LL_INSERT(list, v->v_name);
		}
	}

	if (r == -1)
	{
		log_error("my_record_split: LL_INSERT failed");
		goto error;
	}

	return list;

error:

	if (list)
	{
		ll_delete(list, NULL);
	}

	return NULL;
}


static ll_t *
my_record_reverse(ll_t *keys, ll_t *values)
{
	ll_t *reverse = NULL;
	char *key;
	int r = 0;

	reverse = ll_create();
	if (reverse == NULL)
	{
		log_error("my_record_reverse: ll_create failed");
		goto error;
	}

	while ((key = ll_next(values)) && r >= 0)
	{
		r = LL_INSERT(reverse, key);
	}
	
	while ((key = ll_next(keys)) && r >= 0)
	{
		r = LL_INSERT(reverse, key);
	}

	if (r == -1)
	{
		log_error("my_record_reverse: LL_INSERT failed");
		goto error;
	}

	return reverse;


error:
	if (reverse)
	{
		ll_delete(reverse, NULL);
	}

	return NULL;
}


static my_query_t *
my_prepare_select(dbt_t *dbt, ht_t *storage)
{
	my_handle_t *mh = dbt->dbt_handle;
	my_query_t *mq;
	char query[MY_QUERY_LEN];
	int len = 0;
	ll_t *keys = NULL;
	ll_t *full = NULL;
	char *key;

	len = snprintf(query, sizeof query, "SELECT * FROM `%s` WHERE ",
	    dbt->dbt_table);

	keys = my_record_split(dbt, RS_KEYS);
	full = my_record_split(dbt, RS_FULL);

	if (keys == NULL || full == NULL)
	{
		log_error("my_prepare_select: my_record_split failed");
		goto error;
	}
	
	while ((key = ll_next(keys)) && len < sizeof query)
	{
		len += snprintf(query + len, sizeof query - len,
		    "`%s`=? and ", key);
	}

	if (len >= sizeof query)
	{
		log_error("my_prepare_select: buffer exhausted");
		goto error;
	}

	/*
	 * Chop last "and"
	 */
	len -= 5;
	query[len] = 0;

	mq = my_query_create(mh->my_db, query, storage, keys, full);

	if (mq == NULL)
	{
		log_error("my_prepare_select: my_query_create failed");
	}
	
	ll_delete(keys, NULL);
	ll_delete(full, NULL);

	return mq;

error:
	if (keys)
	{
		ll_delete(keys, NULL);
	}
		
	if (full)
	{
		ll_delete(full, NULL);
	}

	return NULL;
		
}


static my_query_t *
my_prepare_update(dbt_t *dbt, ht_t *storage)
{
	my_handle_t *mh = dbt->dbt_handle;
	my_query_t *mq;
	char query[MY_QUERY_LEN];
	char set[MY_QUERY_LEN];
	char where[MY_QUERY_LEN];
	int len = 0;
	char *key;
	ll_t *keys = NULL;
	ll_t *values = NULL;
	ll_t *reverse = NULL;

	keys = my_record_split(dbt, RS_KEYS);
	values = my_record_split(dbt, RS_VALUES);
	reverse = my_record_reverse(keys, values);
	
	for (ll_rewind(values); (key = ll_next(values)) && len < sizeof set;)
	{
		len += snprintf(set + len, sizeof set - len, "`%s`=?, ",
		    key);
	}

	if (len >= sizeof set)
	{
		log_error("my_prepare_update: buffer exhausted");
		goto error;
	}

	len -= 2;
	set[len] = 0;


	for (len = 0, ll_rewind(keys);
	    (key = ll_next(keys)) && len < sizeof where;)
	{
		len += snprintf(where + len, sizeof where - len,
			"`%s`=? and ", key);
	}

	if (len >= sizeof where)
	{
		log_error("my_prepare_update: buffer exhausted");
		goto error;
	}

	len -= 5;
	where[len] = 0;


	len = snprintf(query, sizeof query, "UPDATE `%s` SET %s WHERE %s",
	    dbt->dbt_table, set, where);
	
	if (len >= sizeof query)
	{
		log_error("my_prepare_update: buffer exhausted");
		goto error;
	}


	mq = my_query_create(mh->my_db, query, storage, reverse, NULL);
	if (mq == NULL)
	{
		log_error("my_prepare_update: my_query_create failed");
	}

	ll_delete(keys, NULL);
	ll_delete(values, NULL);
	ll_delete(reverse, NULL);
	
	return mq;

error:
	
	if(keys)
	{
		ll_delete(keys, NULL);
	}

	if(values)
	{
		ll_delete(values, NULL);
	}

	if(reverse)
	{
		ll_delete(reverse, NULL);
	}

	return NULL;
}


static my_query_t *
my_prepare_insert(dbt_t *dbt, ht_t *storage)
{
	my_handle_t *mh = dbt->dbt_handle;
	my_query_t *mq;
	int i, len = 0;
	char query[MY_QUERY_LEN];
	char table[MY_QUERY_LEN];
	char placeholders[MY_QUERY_LEN];
	char *key;
	ll_t *full = NULL;

	full = my_record_split(dbt, RS_FULL);
	if (full == NULL)
	{
		log_error("my_prepare_insert: my_record_split failed");
		goto error;
	}
	
	/*
	 * '?', '?', '?', ...
	 */
	if (sizeof placeholders < full->ll_size * 3 + 1)
	{
		log_error("my_prepare_insert: buffer exhausted");
		goto error;
	}

	for (placeholders[0] = 0, i = 0; i < full->ll_size; ++i)
	{
		strcat(placeholders, "?, ");
	}

	placeholders[full->ll_size * 3 - 2] = 0;

	for (ll_rewind(full); (key = ll_next(full)) && len < sizeof table;)
	{
		len += snprintf(table + len, sizeof table - len, "`%s`, ",
		    key);
	}

	if (len >= sizeof table)
	{
		log_error("my_prepare_insert: buffer exhausted");
		goto error;
	}

	/*
	 * Chop
	 */
	len -= 2;
	table[len] = 0;

	len = snprintf(query, sizeof query,
	    "INSERT INTO `%s` (%s) VALUES (%s)", dbt->dbt_table, table,
	    placeholders);
	
	if (len >= sizeof query) {
		log_error("my_prepare_insert: buffer exhausted");
		goto error;
	}

	mq = my_query_create(mh->my_db, query, storage, full, NULL);

	if (mq == NULL)
	{
		log_error("my_prepare_insert: my_query_create failed");
	}
	
	ll_delete(full, NULL);

	return mq;

error:

	if (full)
	{
		ll_delete(full, NULL);
	}

	return NULL;
}


static my_query_t *
my_prepare_delete(dbt_t *dbt, ht_t *storage)
{
	my_handle_t *mh = dbt->dbt_handle;
	my_query_t *mq;
	int len = 0;
	char query[MY_QUERY_LEN];
	char where[MY_QUERY_LEN];
	char *key;
	ll_t *keys;

	keys = my_record_split(dbt, RS_KEYS);
	if (keys == NULL)
	{
		log_error("my_prepare_delete: my_record_split failed");
		goto error;
	}

	for (ll_rewind(keys); (key = ll_next(keys));)
	{
		len += snprintf(where + len, sizeof where - len,
		    "`%s`=? and ", key);
	}

	if (len >= sizeof where) {
		log_error("my_prepare_delete: buffer exhausted");
		goto error;
	}

	len -= 5;
	where[len] = 0;

	len = snprintf(query, sizeof query, "DELETE FROM `%s` WHERE %s",
	    dbt->dbt_table, where);
	
	if (len >= sizeof query) {
		log_error("my_prepare_delete: buffer exhausted");
		goto error;
	}

	mq = my_query_create(mh->my_db, query, storage, keys, NULL);

	if (mq == NULL)
	{
		log_error("my_prepare_delete: my_query_create failed");
	}
	
	ll_delete(keys, NULL);

	return mq;

error:

	if (keys)
	{
		ll_delete(keys, NULL);
	}

	return NULL;
}


static my_query_t *
my_prepare_cleanup(dbt_t *dbt, ht_t *storage)
{
	my_handle_t *mh = dbt->dbt_handle;
	my_query_t *mq;
	char query[MY_QUERY_LEN];
	int len;

	if (dbt->dbt_sql_invalid_where == NULL) {
		log_error("my_prepare_cleanup: dbt_sql_invalid_where is NULL");
		return NULL;
	}

	len = snprintf(query, sizeof query, "DELETE FROM `%s` WHERE %s",
	    dbt->dbt_table, dbt->dbt_sql_invalid_where);
	
	if (len >= sizeof query) {
		log_error("my_prepare_cleanup: buffer exhausted");
		return NULL;
	}

	mq = my_query_create(mh->my_db, query, NULL, NULL, NULL);

	if (mq == NULL)
	{
		log_error("my_prepare_cleanup: my_query_create failed");
	}
	
	return mq;
}


static int
my_prepare(dbt_t *dbt)
{
	my_handle_t *mh = dbt->dbt_handle;
	ll_t *scheme = dbt->dbt_scheme->v_data;
	ht_t *storage = NULL;
	my_query_type_t qt;
	var_t *v;
	my_buffer_t *mb;

	static my_prepare_callback_t callback[] = { my_prepare_select,
	    my_prepare_update, my_prepare_insert, my_prepare_delete,
	    my_prepare_cleanup };

	storage = ht_create(MY_BUCKETS, (ht_hash_t) my_buffer_hash,
	    (ht_match_t) my_buffer_match, (ht_delete_t) my_buffer_delete);

	if (storage == NULL)
	{
		log_error("my_prepare: var_create failed");
		goto error;
	}

	for (ll_rewind(scheme); (v = ll_next(scheme));)
	{
		mb = my_buffer_create(v->v_name, v->v_type);
		if (mb == NULL)
		{
			log_error("my_prepare: my_buffer_create failed");
			goto error;
		}

		if (ht_insert(storage, mb))
		{
			log_error("my_prepare: my_buffer_create failed");
			goto error;
		}
	}

	mh->my_storage = storage;
	
	for (qt = MY_SELECT; qt < MY_MAX; ++qt)
	{
		mh->my_query[qt] = callback[qt](dbt, storage);
		if (mh->my_query[qt] == NULL)
		{
			log_error("my_prepare: query callback failed");
			return -1;
		}
	}

	return 0;


error:
	for (qt = MY_SELECT; qt < MY_MAX; ++qt)
	{
		if (mh->my_query[qt])
		{
			my_query_delete(mh->my_query[qt]);
		}
	}

	if (storage) {
		ht_delete(storage);
	}

	return -1;
}


static int
my_database_exists(MYSQL *db, char *name)
{
	MYSQL_RES *res;
	my_ulonglong rows;

	res = mysql_list_dbs(db, name);
	if (res == NULL) {
		log_error("my_database_exists: mysql_list_dbs: %s",
			mysql_error(db));
		return -1;
	}

	rows = mysql_num_rows(res);

	mysql_free_result(res);

	if (rows == 1) {
		return 1;
	}

	return 0;
}

static int
my_create_database(MYSQL *db, char *name)
{
	char query[MY_QUERY_LEN];
	int len;

	len = snprintf(query, sizeof query, "CREATE DATABASE `%s`", name);
	if (len >= sizeof query) {
		log_error("my_create_database: buffer exhausted");
		return -1;
	}

	if (mysql_query(db, query)) {
		log_error("my_create_database: mysql_query: %s", 
			mysql_error(db));
		return -1;
	}

	return 0;
}

static int
my_table_exists(MYSQL *db, char *table)
{
	MYSQL_RES *res;
	my_ulonglong rows;

	res = mysql_list_tables(db, table);
	if (res == NULL) {
		log_error("my_table_exists: mysql_list_tables: %s",
			mysql_error(db));
		return -1;
	}

	rows = mysql_num_rows(res);

	mysql_free_result(res);

	if (rows == 1) {
		return 1;
	}

	return 0;
}

static int
my_create_table(MYSQL *db, char *table, var_t *scheme)
{
	char query[MY_QUERY_LEN];
	int len;
	ll_t *list;
	var_t *v;

	if (scheme->v_type != VT_LIST) {
		log_error("my_create_table: scheme as bad type");
		return -1;
	}
	list = scheme->v_data;

	len = snprintf(query, sizeof query, "CREATE TABLE `%s` (", table);

	/*
	 * Add attributes
	 */
	ll_rewind(list);
	while ((v = ll_next(list))) {
		len += snprintf(query + len, sizeof query - len, "`%s` %s, ",
			v->v_name, my_types[v->v_type]);

		if (len >= sizeof query) {
			break;
		}
	}

	/*
	 * Create primary key
	 */
	len += snprintf(query + len, sizeof query - len, "PRIMARY KEY (");

	ll_rewind(list);
	while ((v = ll_next(list))) {
		if ((v->v_flags & VF_KEY) == 0) {
			continue;
		}

		len += snprintf(query + len, sizeof query - len, "%s, ",
			v->v_name);

		if (len >= sizeof query) {
			break;
		}
	}
	len += snprintf(query + len - 2, sizeof query - len - 2, "))");
	
	if (len >= sizeof query) {
		log_error("my_create_table: buffer exhausted");
		return -1;
	}

	if (mysql_query(db, query)) {
		log_error("my_create_table: mysql_query: %s", mysql_error(db));
		return -1;
	}

	return 0;
}

static int
my_open(dbt_t *dbt)
{
	my_handle_t *mh;
	MYSQL *myp;
	my_bool false = 0;
	int r;

	/*
	 * Create a database handle
	 */
	mh = (my_handle_t *) malloc(sizeof(my_handle_t));
	if (mh == NULL) {
		log_error("my_open: malloc");
		return -1;
	}

	memset(mh, 0, sizeof(my_handle_t));

	dbt->dbt_handle = mh;

	/*
	 * Init mysql struct
	 */
	mh->my_db = mysql_init(NULL);
	if (mh->my_db == NULL) {
		log_error("my_open: mysql_init: %s", mysql_error(mh->my_db));
		goto error;
	}
	
	/*
	 * Connect MySQL DBMS
	 */
	myp = mysql_real_connect(mh->my_db, dbt->dbt_host, dbt->dbt_user,
		dbt->dbt_pass, NULL, (unsigned short) dbt->dbt_port,
		dbt->dbt_path, 0);
	if (myp == NULL) {
		log_error("my_open: mysql_real_connect: %s", mysql_error(myp));
		goto error;
	}

	/*
	 * Disable reporting of data truncation
	 */
	if (mysql_options(myp, MYSQL_REPORT_DATA_TRUNCATION, &false))
	{
		log_error("my_open: mysql_options: %s", mysql_error(myp));
		goto error;
	}

	/*
	 * Check if database exists
	 */
	r = my_database_exists(myp, dbt->dbt_database);
	if (r == -1) {
		log_error("my_open: my_database_exists failed: %s",
			mysql_error(myp));
		goto error;
	}

	if (r == 0) {
		r = my_create_database(myp, dbt->dbt_database);
		if (r == -1) {
			log_error("my_open: my_create_database failed");
			goto error;
		}
	}

	/*
	 * Open database
	 */
	if (mysql_select_db(myp, dbt->dbt_database)) {
		log_error("my_open: mysql_select_db: %s", mysql_error(myp));
		goto error;
	}

	
	/*
	 * Check if table exists
	 */
	r = my_table_exists(myp, dbt->dbt_table);
	if (r == -1) {
		log_error("my_open: my_table_exists failed");
		goto error;
	}
	else if (r == 0) {
		if (my_create_table(myp, dbt->dbt_table, dbt->dbt_scheme)) {
			log_error("my_open: my_create_table failed");
			goto error;
		}
	}

	/*
	 * Prepare statements
	 */
	if (my_prepare(dbt)) {
		log_error("my_open: my_prepare failed");
		goto error;
	}

	return 0;


error:

	my_close(dbt);

	return -1;
}


static int
my_load_storage(ht_t *storage, var_t *record)
{
	ll_t *list = record->v_data;
	var_t *item;
	void *src, *dst;
	int size;
	my_buffer_t lookup, *mb;

	for (ll_rewind(list); (item = ll_next(list));)
	{
		src = item->v_data;
		if (src == NULL)
		{
			continue;
		}

		lookup.mb_name = item->v_name;
		mb = ht_lookup(storage, &lookup);
		if (mb == NULL)
		{
			log_error("my_load_storage: no value for \"%s\"",
			    item->v_name);
			return -1;
		}

		size = var_data_size(item);
		dst = mb->mb_buffer;

		memcpy(dst, src, size);
	}

	return 0;
}


var_t *
my_unload_storage(dbt_t *dbt, ht_t *storage)
{
	ll_t *scheme = dbt->dbt_scheme->v_data;
	var_t *record = NULL;
	var_t *v;
	my_buffer_t lookup, *mb;

	record = var_create(VT_LIST, dbt->dbt_scheme->v_name, NULL,
	    VF_KEEPNAME | VF_CREATE);

	if (record == NULL)
	{
		log_error("my_unload_record: var_create failed");
		goto error;
	}

	for (ll_rewind(scheme); (v = ll_next(scheme));)
	{
		lookup.mb_name = v->v_name;
		mb = ht_lookup(storage, &lookup);
		if (mb == NULL)
		{
			log_error("my_unload_record: no value for \"%s\"",
			    v->v_name);
			goto error;
		}

		if (var_list_append_new(record, v->v_type, v->v_name,
		    mb->mb_buffer, VF_COPY))
		{
			log_error("my_unload_record: var_list_append_new "
			    "failed");
			goto error;
		}
	}

	return record;

error:
	if (record)
	{
		var_delete(record);
	}

	return NULL;
}


static var_t *
my_get(dbt_t *dbt, var_t *record)
{
	my_handle_t *mh = dbt->dbt_handle;
	MYSQL_STMT *stmt = mh->my_query[MY_SELECT]->my_stmt;
	ht_t *storage = mh->my_storage;
	var_t *copy;
	int r;

	if (my_load_storage(storage, record))
	{
		log_error("my_get: my_load_storage failed");
		return NULL;
	}

	if (mysql_stmt_execute(stmt))
	{
		log_error("my_get: mysql_stmt_execute: %s",
		    mysql_stmt_error(stmt));
		return NULL;
	}

	r = mysql_stmt_fetch(stmt);
	switch (r)
	{
	case 0:
	case 101:
		break;

	default:
		log_error("my_get: mysql_stmt_fetch: %s",
		    mysql_stmt_error(stmt));

		return NULL;
	}

	mysql_stmt_free_result(stmt);

	if (mysql_stmt_reset(stmt))
	{
		log_error("my_get: mysql_stmt_reset: %s",
		    mysql_stmt_error(stmt));
	}
	
	copy = my_unload_storage(dbt, storage);
	if (copy == NULL)
	{
		log_error("my_get: my_uload_record failed");
		return NULL;
	}
	
	return copy;
}


static int
my_set(dbt_t *dbt, var_t *record)
{
	my_handle_t *mh = dbt->dbt_handle;
	MYSQL_STMT *stmt = mh->my_query[MY_UPDATE]->my_stmt;
	ht_t *storage = mh->my_storage;
	my_ulonglong affected;

	if (my_load_storage(storage, record))
	{
		log_error("my_set: my_load_storage failed");
		return -1;
	}

	if (mysql_stmt_execute(stmt))
	{
		log_error("my_set: mysql_stmt_execute: %s",
		    mysql_stmt_error(stmt));
		return -1;
	}

	/*
	 * Update successful
	 */
	affected = mysql_stmt_affected_rows(stmt);

	if (mysql_stmt_reset(stmt))
	{
		log_error("my_get: mysql_stmt_reset: %s",
		    mysql_stmt_error(stmt));
	}
	
	if (affected == 1)
	{
		return 0;
	}
	else if (affected)
	{
		log_error("my_set: update affected %lu rows. Expected 0/1!",
		    affected);
		return -1;
	}


	log_debug("my_set: insert new record");

	/*
	 * Record does not exist -> INSERT
	 */
	stmt = mh->my_query[MY_INSERT]->my_stmt;

	if (mysql_stmt_execute(stmt))
	{
		log_error("my_set: mysql_stmt_execute: %s",
		    mysql_stmt_error(stmt));
		return -1;
	}

	affected = mysql_stmt_affected_rows(stmt);

	if (mysql_stmt_reset(stmt))
	{
		log_error("my_get: mysql_stmt_reset: %s",
		    mysql_stmt_error(stmt));
	}
	
	if (affected != 1)
	{
		log_error("my_set: insert affected %lu rows. Expected 1!",
		    affected);
		return -1;
	}

	return 0;
}


static int
my_del(dbt_t *dbt, var_t *record)
{
	my_handle_t *mh = dbt->dbt_handle;
	MYSQL_STMT *stmt = mh->my_query[MY_DELETE]->my_stmt;
	ht_t *storage = mh->my_storage;
	my_ulonglong affected;

	if (my_load_storage(storage, record))
	{
		log_error("my_get: my_load_storage failed");
		return -1;
	}

	if (mysql_stmt_execute(stmt))
	{
		log_error("my_get: mysql_stmt_execute: %s",
		    mysql_stmt_error(stmt));
		return -1;
	}

	affected = mysql_stmt_affected_rows(stmt);

	if (mysql_stmt_reset(stmt))
	{
		log_error("my_get: mysql_stmt_reset: %s",
		    mysql_stmt_error(stmt));
	}
	
	log_debug("my_del: deleted %lu rows", affected);

	return 0;
}


static int
my_cleanup(dbt_t *dbt)
{
	my_handle_t *mh = dbt->dbt_handle;
	MYSQL_STMT *stmt = mh->my_query[MY_CLEANUP]->my_stmt;
	my_ulonglong affected;

	if (mysql_stmt_execute(stmt))
	{
		log_error("my_cleanup: mysql_stmt_execute: %s",
		    mysql_stmt_error(stmt));
		return -1;
	}

	affected = mysql_stmt_affected_rows(stmt);

	if (mysql_stmt_reset(stmt))
	{
		log_error("my_cleanup: mysql_stmt_reset: %s",
		    mysql_stmt_error(stmt));
	}
	
	log_debug("my_cleanup: deleted %lu rows", affected);

	return affected;
}


int
init(void)
{
	snprintf(my_addr_type, sizeof my_addr_type, "VARBINARY(%lu)",
	    sizeof (struct sockaddr_storage));

	dbt_driver.dd_name = "mysql";
	dbt_driver.dd_open = (dbt_db_open_t) my_open;
	dbt_driver.dd_close = (dbt_db_close_t) my_close;
	dbt_driver.dd_get = (dbt_db_get_t) my_get;
	dbt_driver.dd_set = (dbt_db_set_t) my_set;
	dbt_driver.dd_del = (dbt_db_del_t) my_del;
	dbt_driver.dd_sql_cleanup = (dbt_db_sql_cleanup_t) my_cleanup;

	dbt_driver_register(&dbt_driver);

	return 0;
}
