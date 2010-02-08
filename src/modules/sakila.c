/*
 * The name sakila.c was introduced due to namespace pollution.
 */

#include <string.h>
#include <malloc.h>
#include <db.h>
#include <errno.h>
#include <mysql/mysql.h>

#include <mopher.h>

#define MY_QUERY_LEN 2048
#define MY_STRING_LEN 320
#define MY_BUCKETS 64


/*
 * sakila_query_type is used to loop over sakila_prepare_xxx callbacks.
 * See sakila_prepare()
 */
typedef enum sakila_query_type_t { MY_SELECT, MY_UPDATE, MY_INSERT, MY_DELETE,
	MY_CLEANUP, MY_MAX } sakila_query_type_t;


/*
 * Buffers are bound to a query by mysql_bind_params/results.
 * See sakila_query_create()
 */
typedef struct sakila_buffer {
	char		*mb_name;
	int		 mb_type;
	void		*mb_buffer;
	unsigned long	 mb_length;
	my_bool		 mb_is_null;
	my_bool		 mb_error;
} sakila_buffer_t;

/*
 * A query consists of the prepared statement, bound params (optional) and
 * bound results (optional).
 */
typedef struct sakila_query {
	MYSQL_STMT		*sakila_stmt;
	MYSQL_BIND		*sakila_params;
	MYSQL_BIND		*sakila_results;
} sakila_query_t;

/*
 * The MySQL handle stores the MYSQL * and an array of all prepared queries.
 * The storage table holds sakila_buffers for the record (see dbt->dbt_scheme).
 */
typedef struct sakila_handle {
	MYSQL		*sakila_db;
	sakila_query_t	*sakila_query[MY_MAX];
	ht_t		*sakila_storage;
} sakila_handle_t;

/*
 * struct sockaddr_storage may vary on different systems ???
 * sakila_addr_type is set to VARBINARY(sizeof (struct sockaddr_storage) by init.
 */
static char sakila_addr_type[15];

/*
 * Datatype storage requirements (allocated for sakila_buffer_t)
 */
static unsigned long sakila_buffer_length[] = {
    0,					/* VT_NULL	*/
    0,					/* VT_TABLE	*/
    0,					/* VT_LIST	*/
    sizeof (struct sockaddr_storage),	/* VT_ADDR	*/
    sizeof (VAR_INT_T),			/* VT_INT	*/
    sizeof (VAR_FLOAT_T),		/* VT_FLOAT	*/
    0,					/* VT_POINTER	*/
    MY_STRING_LEN			/* VT_STRING	*/
};

/*
 * Datatype conversions (var.c <-> MySQL) for mysql_bind_params/results.
 */
static int sakila_buffer_types[] = {
    0,					/* VT_NULL	*/
    0,					/* VT_TABLE	*/
    0,					/* VT_LIST	*/
    MYSQL_TYPE_BLOB,			/* VT_ADDR	*/
    MYSQL_TYPE_LONG,			/* VT_INT	*/
    MYSQL_TYPE_DOUBLE,			/* VT_FLOAT	*/
    0,					/* VT_POINTER	*/
    MYSQL_TYPE_STRING			/* VT_STRING	*/
};

/*
 * Datatype conversions (var.c <-> MySQL) for create database statement
 */
static char *sakila_types[] = {
    NULL,				/* VT_NULL	*/
    NULL,				/* VT_TABLE	*/
    NULL,				/* VT_LIST	*/
    sakila_addr_type,			/* VT_ADDR	*/
    "INT",				/* VT_INT	*/
    "DOUBLE",				/* VT_FLOAT	*/
    NULL,				/* VT_POINTER	*/
    "VARCHAR(320)"			/* VT_STRING	*/
};


/*
 * sakila_prepare_xxx callbacks
 */
typedef sakila_query_t *(*sakila_prepare_callback_t)(dbt_t *dbt, ht_t *storage);

/*
 * dbt_driver storage for dbt_driver_register
 */
static dbt_driver_t dbt_driver;

/*
 * Enum for sakila_record_split(). See below.
 */
typedef enum sakila_record_split { RS_FULL, RS_KEYS, RS_VALUES } sakila_record_split_t;


static void
sakila_buffer_delete(sakila_buffer_t *mb)
{
	if (mb->mb_buffer)
	{
		free(mb->mb_buffer);
	}

	free(mb);

	return;
}


static sakila_buffer_t *
sakila_buffer_create(char *name, var_type_t type)
{
	sakila_buffer_t *mb;
	unsigned long length;

	mb = (sakila_buffer_t *) malloc(sizeof (sakila_buffer_t));
	if (mb == NULL)
	{
		log_error("sakila_buffer_create: malloc");
		return NULL;
	}

	memset(mb, 0, sizeof (sakila_buffer_t));

	length = sakila_buffer_length[type];

	mb->mb_buffer = malloc(length);
	if (mb->mb_buffer == NULL)
	{
		log_error("sakila_buffer_create: malloc");
		return NULL;
	}

	memset(mb->mb_buffer, 0, length);

	mb->mb_name = name;
	mb->mb_length = length;
	mb->mb_type = sakila_buffer_types[type];

	return mb;
}


static hash_t
sakila_buffer_hash(sakila_buffer_t *mb)
{
	return HASH(mb->mb_name, strlen(mb->mb_name));
}


static int
sakila_buffer_match(sakila_buffer_t *mb1, sakila_buffer_t *mb2)
{
	if (strcmp(mb1->mb_name, mb2->mb_name) == 0)
	{
		return 1;
	}

	return 0;
}


static void
sakila_query_delete(sakila_query_t *mq)
{
	if (mq->sakila_stmt) {
		mysql_stmt_close(mq->sakila_stmt);
	}

	if (mq->sakila_params) {
		free(mq->sakila_params);
	}
		
	if (mq->sakila_results) {
		free(mq->sakila_results);
	}
		
	free(mq);

	return;
}

static sakila_query_t *
sakila_query_create(MYSQL *db, char *query, ht_t *storage, ll_t *params,
    ll_t *results)
{
	sakila_query_t *mq = NULL;
	int size;
	int i;
	char *key;
	sakila_buffer_t lookup, *mb;

	log_debug("sakila_query_create: create query: %s", query);

	mq = (sakila_query_t *) malloc(sizeof (sakila_query_t));
	if (mq == NULL)
	{
		log_error("sakila_query_create: malloc");
		goto error;
	}

	memset(mq, 0, sizeof (sakila_query_t));

	/*
	 * Allocate memory for params
	 */
	if (params)
	{
		size = params->ll_size * sizeof (MYSQL_BIND);

		mq->sakila_params = (MYSQL_BIND *) malloc(size);
		if (mq->sakila_params == NULL) {
			log_error("sakila_query_create: malloc");
			goto error;
		}

		memset(mq->sakila_params, 0, size);
	}

	/*
	 * Allocate memory for results
	 */
	if (results) {
		size = results->ll_size * sizeof (MYSQL_BIND);

		mq->sakila_results = (MYSQL_BIND *) malloc(size);
		if (mq->sakila_results == NULL) {
			log_error("sakila_query_create: malloc");
			goto error;
		}
	
		memset(mq->sakila_results, 0, size);
	}
	

	/*
	 * Initialize MYSQL_STMT
	 */
	mq->sakila_stmt = mysql_stmt_init(db);
	if (mq->sakila_stmt == NULL) {
		log_error("sakila_query_create: mysql_stmt_init: %s",
			mysql_stmt_error(mq->sakila_stmt));
		goto error;
	}

	/*
	 * Prepare statement
	 */
	if (mysql_stmt_prepare(mq->sakila_stmt, query, strlen(query))) {
		log_error("sakila_query_create: mysql_stmt_prepare: %s",
			mysql_stmt_error(mq->sakila_stmt));
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
				log_error("sakila_query_create: no storage entry");
				goto error;
			}

			mq->sakila_params[i].buffer = mb->mb_buffer;
			mq->sakila_params[i].buffer_type = mb->mb_type;
			mq->sakila_params[i].length = &mb->mb_length;
			mq->sakila_params[i].is_null = &mb->mb_is_null;
			mq->sakila_params[i].error = &mb->mb_error;
		}

		if (mysql_stmt_bind_param(mq->sakila_stmt, mq->sakila_params))
		{
			log_error("sakila_query_create: mysql_stmt_bind_param: %s", 
			mysql_stmt_error(mq->sakila_stmt));
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
			log_error("sakila_query_create: no storage entry");
			goto error;
		}

		mq->sakila_results[i].buffer = mb->mb_buffer;
		mq->sakila_results[i].buffer_type = mb->mb_type;
		mq->sakila_results[i].length = &mb->mb_length;
		mq->sakila_results[i].is_null = &mb->mb_is_null;
		mq->sakila_results[i].error = &mb->mb_error;
	}

	if (mysql_stmt_bind_result(mq->sakila_stmt, mq->sakila_results)) {
		log_error("sakila_query_create: mysql_stmt_bind_result: %s", 
			mysql_stmt_error(mq->sakila_stmt));
		goto error;
	}

	return mq;


error:
	if (mq) {
		sakila_query_delete(mq);
	}

	return NULL;
}
	


static void
sakila_close(dbt_t *dbt)
{
	sakila_handle_t *mh = dbt->dbt_handle;
	sakila_query_type_t qt;

	for (qt = 0; qt < MY_MAX; ++qt)
	{
		if (mh->sakila_query[qt])
		{
			sakila_query_delete(mh->sakila_query[qt]);
		}
	}

	if (mh->sakila_storage)
	{
		ht_delete(mh->sakila_storage);
	}
	
	if (mh->sakila_db) {
		mysql_close(mh->sakila_db);
	}

	free(mh);

	dbt->dbt_handle = NULL;

	return;
}


static ll_t *
sakila_record_split(dbt_t *dbt, sakila_record_split_t rs)
{
	ll_t *scheme = dbt->dbt_scheme->v_data;
	ll_t *list = NULL;
	var_t *v;
	int r = 0;

	list = ll_create();
	if (list == NULL)
	{
		log_error("sakila_record_split: ll_create failed");
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
		log_error("sakila_record_split: LL_INSERT failed");
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
sakila_record_reverse(ll_t *keys, ll_t *values)
{
	ll_t *reverse = NULL;
	char *key;
	int r = 0;

	reverse = ll_create();
	if (reverse == NULL)
	{
		log_error("sakila_record_reverse: ll_create failed");
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
		log_error("sakila_record_reverse: LL_INSERT failed");
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


static sakila_query_t *
sakila_prepare_select(dbt_t *dbt, ht_t *storage)
{
	sakila_handle_t *mh = dbt->dbt_handle;
	sakila_query_t *mq;
	char query[MY_QUERY_LEN];
	int len = 0;
	ll_t *keys = NULL;
	ll_t *full = NULL;
	char *key;

	len = snprintf(query, sizeof query, "SELECT * FROM `%s` WHERE ",
	    dbt->dbt_table);

	keys = sakila_record_split(dbt, RS_KEYS);
	full = sakila_record_split(dbt, RS_FULL);

	if (keys == NULL || full == NULL)
	{
		log_error("sakila_prepare_select: sakila_record_split failed");
		goto error;
	}
	
	while ((key = ll_next(keys)) && len < sizeof query)
	{
		len += snprintf(query + len, sizeof query - len,
		    "`%s`=? and ", key);
	}

	if (len >= sizeof query)
	{
		log_error("sakila_prepare_select: buffer exhausted");
		goto error;
	}

	/*
	 * Chop last "and"
	 */
	len -= 5;
	query[len] = 0;

	mq = sakila_query_create(mh->sakila_db, query, storage, keys, full);

	if (mq == NULL)
	{
		log_error("sakila_prepare_select: sakila_query_create failed");
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


static sakila_query_t *
sakila_prepare_update(dbt_t *dbt, ht_t *storage)
{
	sakila_handle_t *mh = dbt->dbt_handle;
	sakila_query_t *mq;
	char query[MY_QUERY_LEN];
	char set[MY_QUERY_LEN];
	char where[MY_QUERY_LEN];
	int len = 0;
	char *key;
	ll_t *keys = NULL;
	ll_t *values = NULL;
	ll_t *reverse = NULL;

	keys = sakila_record_split(dbt, RS_KEYS);
	values = sakila_record_split(dbt, RS_VALUES);
	reverse = sakila_record_reverse(keys, values);
	
	for (ll_rewind(values); (key = ll_next(values)) && len < sizeof set;)
	{
		len += snprintf(set + len, sizeof set - len, "`%s`=?, ",
		    key);
	}

	if (len >= sizeof set)
	{
		log_error("sakila_prepare_update: buffer exhausted");
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
		log_error("sakila_prepare_update: buffer exhausted");
		goto error;
	}

	len -= 5;
	where[len] = 0;


	len = snprintf(query, sizeof query, "UPDATE `%s` SET %s WHERE %s",
	    dbt->dbt_table, set, where);
	
	if (len >= sizeof query)
	{
		log_error("sakila_prepare_update: buffer exhausted");
		goto error;
	}


	mq = sakila_query_create(mh->sakila_db, query, storage, reverse, NULL);
	if (mq == NULL)
	{
		log_error("sakila_prepare_update: sakila_query_create failed");
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


static sakila_query_t *
sakila_prepare_insert(dbt_t *dbt, ht_t *storage)
{
	sakila_handle_t *mh = dbt->dbt_handle;
	sakila_query_t *mq;
	int i, len = 0;
	char query[MY_QUERY_LEN];
	char table[MY_QUERY_LEN];
	char placeholders[MY_QUERY_LEN];
	char *key;
	ll_t *full = NULL;

	full = sakila_record_split(dbt, RS_FULL);
	if (full == NULL)
	{
		log_error("sakila_prepare_insert: sakila_record_split failed");
		goto error;
	}
	
	/*
	 * '?', '?', '?', ...
	 */
	if (sizeof placeholders < full->ll_size * 3 + 1)
	{
		log_error("sakila_prepare_insert: buffer exhausted");
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
		log_error("sakila_prepare_insert: buffer exhausted");
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
		log_error("sakila_prepare_insert: buffer exhausted");
		goto error;
	}

	mq = sakila_query_create(mh->sakila_db, query, storage, full, NULL);

	if (mq == NULL)
	{
		log_error("sakila_prepare_insert: sakila_query_create failed");
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


static sakila_query_t *
sakila_prepare_delete(dbt_t *dbt, ht_t *storage)
{
	sakila_handle_t *mh = dbt->dbt_handle;
	sakila_query_t *mq;
	int len = 0;
	char query[MY_QUERY_LEN];
	char where[MY_QUERY_LEN];
	char *key;
	ll_t *keys;

	keys = sakila_record_split(dbt, RS_KEYS);
	if (keys == NULL)
	{
		log_error("sakila_prepare_delete: sakila_record_split failed");
		goto error;
	}

	for (ll_rewind(keys); (key = ll_next(keys));)
	{
		len += snprintf(where + len, sizeof where - len,
		    "`%s`=? and ", key);
	}

	if (len >= sizeof where) {
		log_error("sakila_prepare_delete: buffer exhausted");
		goto error;
	}

	len -= 5;
	where[len] = 0;

	len = snprintf(query, sizeof query, "DELETE FROM `%s` WHERE %s",
	    dbt->dbt_table, where);
	
	if (len >= sizeof query) {
		log_error("sakila_prepare_delete: buffer exhausted");
		goto error;
	}

	mq = sakila_query_create(mh->sakila_db, query, storage, keys, NULL);

	if (mq == NULL)
	{
		log_error("sakila_prepare_delete: sakila_query_create failed");
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


static sakila_query_t *
sakila_prepare_cleanup(dbt_t *dbt, ht_t *storage)
{
	sakila_handle_t *mh = dbt->dbt_handle;
	sakila_query_t *mq;
	char query[MY_QUERY_LEN];
	int len;

	if (dbt->dbt_sql_invalid_where == NULL) {
		log_error("sakila_prepare_cleanup: dbt_sql_invalid_where is NULL");
		return NULL;
	}

	len = snprintf(query, sizeof query, "DELETE FROM `%s` WHERE %s",
	    dbt->dbt_table, dbt->dbt_sql_invalid_where);
	
	if (len >= sizeof query) {
		log_error("sakila_prepare_cleanup: buffer exhausted");
		return NULL;
	}

	mq = sakila_query_create(mh->sakila_db, query, NULL, NULL, NULL);

	if (mq == NULL)
	{
		log_error("sakila_prepare_cleanup: sakila_query_create failed");
	}
	
	return mq;
}


static int
sakila_prepare(dbt_t *dbt)
{
	sakila_handle_t *mh = dbt->dbt_handle;
	ll_t *scheme = dbt->dbt_scheme->v_data;
	ht_t *storage = NULL;
	sakila_query_type_t qt;
	var_t *v;
	sakila_buffer_t *mb;

	static sakila_prepare_callback_t callback[] = { sakila_prepare_select,
	    sakila_prepare_update, sakila_prepare_insert, sakila_prepare_delete,
	    sakila_prepare_cleanup };

	storage = ht_create(MY_BUCKETS, (ht_hash_t) sakila_buffer_hash,
	    (ht_match_t) sakila_buffer_match, (ht_delete_t) sakila_buffer_delete);

	if (storage == NULL)
	{
		log_error("sakila_prepare: var_create failed");
		goto error;
	}

	for (ll_rewind(scheme); (v = ll_next(scheme));)
	{
		mb = sakila_buffer_create(v->v_name, v->v_type);
		if (mb == NULL)
		{
			log_error("sakila_prepare: sakila_buffer_create failed");
			goto error;
		}

		if (ht_insert(storage, mb))
		{
			log_error("sakila_prepare: sakila_buffer_create failed");
			goto error;
		}
	}

	mh->sakila_storage = storage;
	
	for (qt = MY_SELECT; qt < MY_MAX; ++qt)
	{
		mh->sakila_query[qt] = callback[qt](dbt, storage);
		if (mh->sakila_query[qt] == NULL)
		{
			log_error("sakila_prepare: query callback failed");
			return -1;
		}
	}

	return 0;


error:
	for (qt = MY_SELECT; qt < MY_MAX; ++qt)
	{
		if (mh->sakila_query[qt])
		{
			sakila_query_delete(mh->sakila_query[qt]);
		}
	}

	if (storage) {
		ht_delete(storage);
	}

	return -1;
}


static int
sakila_database_exists(MYSQL *db, char *name)
{
	MYSQL_RES *res;
	my_ulonglong rows;

	res = mysql_list_dbs(db, name);
	if (res == NULL) {
		log_error("sakila_database_exists: mysql_list_dbs: %s",
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
sakila_create_database(MYSQL *db, char *name)
{
	char query[MY_QUERY_LEN];
	int len;

	len = snprintf(query, sizeof query, "CREATE DATABASE `%s`", name);
	if (len >= sizeof query) {
		log_error("sakila_create_database: buffer exhausted");
		return -1;
	}

	if (mysql_query(db, query)) {
		log_error("sakila_create_database: mysql_query: %s", 
			mysql_error(db));
		return -1;
	}

	return 0;
}

static int
sakila_table_exists(MYSQL *db, char *table)
{
	MYSQL_RES *res;
	my_ulonglong rows;

	res = mysql_list_tables(db, table);
	if (res == NULL) {
		log_error("sakila_table_exists: mysql_list_tables: %s",
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
sakila_create_table(MYSQL *db, char *table, var_t *scheme)
{
	char query[MY_QUERY_LEN];
	int len;
	ll_t *list;
	var_t *v;

	if (scheme->v_type != VT_LIST) {
		log_error("sakila_create_table: scheme as bad type");
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
			v->v_name, sakila_types[v->v_type]);

		if (len >= sizeof query) {
			break;
		}
	}

	/*
	 * Create primary key.
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
	len += snprintf(query + len - 2, sizeof query - len - 2,
	    ")) CHARACTER SET ascii ENGINE=InnoDB");
	
	if (len >= sizeof query) {
		log_error("sakila_create_table: buffer exhausted");
		return -1;
	}

	if (mysql_query(db, query)) {
		log_error("sakila_create_table: mysql_query: %s", mysql_error(db));
		return -1;
	}

	return 0;
}

static int
sakila_open(dbt_t *dbt)
{
	sakila_handle_t *mh;
	MYSQL *myp;
	my_bool false = 0;
	int r;

	/*
	 * Create a database handle
	 */
	mh = (sakila_handle_t *) malloc(sizeof(sakila_handle_t));
	if (mh == NULL) {
		log_error("sakila_open: malloc");
		return -1;
	}

	memset(mh, 0, sizeof(sakila_handle_t));

	dbt->dbt_handle = mh;

	/*
	 * Init mysql struct
	 */
	mh->sakila_db = mysql_init(NULL);
	if (mh->sakila_db == NULL) {
		log_error("sakila_open: mysql_init: %s", mysql_error(mh->sakila_db));
		goto error;
	}
	
	/*
	 * Connect MySQL DBMS
	 */
	myp = mysql_real_connect(mh->sakila_db, dbt->dbt_host, dbt->dbt_user,
		dbt->dbt_pass, NULL, (unsigned short) dbt->dbt_port,
		dbt->dbt_path, 0);
	if (myp == NULL) {
		log_error("sakila_open: mysql_real_connect: %s", mysql_error(myp));
		goto error;
	}

	/*
	 * Disable reporting of data truncation
	 */
	if (mysql_options(myp, MYSQL_REPORT_DATA_TRUNCATION, &false))
	{
		log_error("sakila_open: mysql_options: %s", mysql_error(myp));
		goto error;
	}

	/*
	 * Check if database exists
	 */
	r = sakila_database_exists(myp, dbt->dbt_database);
	if (r == -1) {
		log_error("sakila_open: sakila_database_exists failed: %s",
			mysql_error(myp));
		goto error;
	}

	if (r == 0) {
		r = sakila_create_database(myp, dbt->dbt_database);
		if (r == -1) {
			log_error("sakila_open: sakila_create_database failed");
			goto error;
		}
	}

	/*
	 * Open database
	 */
	if (mysql_select_db(myp, dbt->dbt_database)) {
		log_error("sakila_open: mysql_select_db: %s", mysql_error(myp));
		goto error;
	}

	
	/*
	 * Check if table exists
	 */
	r = sakila_table_exists(myp, dbt->dbt_table);
	if (r == -1) {
		log_error("sakila_open: sakila_table_exists failed");
		goto error;
	}
	else if (r == 0) {
		if (sakila_create_table(myp, dbt->dbt_table, dbt->dbt_scheme)) {
			log_error("sakila_open: sakila_create_table failed");
			goto error;
		}
	}

	/*
	 * Prepare statements
	 */
	if (sakila_prepare(dbt)) {
		log_error("sakila_open: sakila_prepare failed");
		goto error;
	}

	return 0;


error:

	sakila_close(dbt);

	return -1;
}


static int
sakila_load_storage(ht_t *storage, var_t *record)
{
	ll_t *list = record->v_data;
	var_t *item;
	void *src, *dst;
	int size;
	sakila_buffer_t lookup, *mb;

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
			log_error("sakila_load_storage: no value for \"%s\"",
			    item->v_name);
			return -1;
		}

		size = var_data_size(item);
		dst = mb->mb_buffer;
		mb->mb_length = size;

		memcpy(dst, src, size);
	}

	return 0;
}


var_t *
sakila_unload_storage(dbt_t *dbt, ht_t *storage)
{
	ll_t *scheme = dbt->dbt_scheme->v_data;
	var_t *record = NULL;
	var_t *v;
	sakila_buffer_t lookup, *mb;

	record = var_create(VT_LIST, dbt->dbt_scheme->v_name, NULL,
	    VF_KEEPNAME | VF_CREATE);

	if (record == NULL)
	{
		log_error("sakila_unload_record: var_create failed");
		goto error;
	}

	for (ll_rewind(scheme); (v = ll_next(scheme));)
	{
		lookup.mb_name = v->v_name;
		mb = ht_lookup(storage, &lookup);
		if (mb == NULL)
		{
			log_error("sakila_unload_record: no value for \"%s\"",
			    v->v_name);
			goto error;
		}

		if (vlist_append_new(record, v->v_type, v->v_name,
		    mb->mb_buffer, VF_COPY))
		{
			log_error("sakila_unload_record: vlist_append_new "
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


static int
sakila_get(dbt_t *dbt, var_t *record, var_t **result)
{
	sakila_handle_t *mh = dbt->dbt_handle;
	MYSQL_STMT *stmt = mh->sakila_query[MY_SELECT]->sakila_stmt;
	ht_t *storage = mh->sakila_storage;
	int r;

	if (sakila_load_storage(storage, record))
	{
		log_error("sakila_get: sakila_load_storage failed");
		return -1;
	}

	if (mysql_stmt_execute(stmt))
	{
		log_error("sakila_get: mysql_stmt_execute: %s",
		    mysql_stmt_error(stmt));
		return -1;
	}

	r = mysql_stmt_fetch(stmt);
	switch (r)
	{
	case 0:
		break;

	case MYSQL_NO_DATA:
		log_debug("sakila_get: no record found");
		goto exit;

	default:
		log_error("sakila_get: mysql_stmt_fetch: %s",
		    mysql_stmt_error(stmt));

		return -1;
	}

	*result = sakila_unload_storage(dbt, storage);
	if (*result == NULL)
	{
		log_error("sakila_get: sakila_uload_record failed");
		return -1;
	}

exit:	
	mysql_stmt_free_result(stmt);

	if (mysql_stmt_reset(stmt))
	{
		log_error("sakila_get: mysql_stmt_reset: %s",
		    mysql_stmt_error(stmt));
	}
	
	return 0;
}


static int
sakila_set(dbt_t *dbt, var_t *record)
{
	sakila_handle_t *mh = dbt->dbt_handle;
	MYSQL_STMT *stmt = mh->sakila_query[MY_UPDATE]->sakila_stmt;
	ht_t *storage = mh->sakila_storage;
	my_ulonglong affected;

	if (sakila_load_storage(storage, record))
	{
		log_error("sakila_set: sakila_load_storage failed");
		return -1;
	}

	if (mysql_stmt_execute(stmt))
	{
		log_error("sakila_set: mysql_stmt_execute: %s",
		    mysql_stmt_error(stmt));
		return -1;
	}

	/*
	 * Update successful
	 */
	affected = mysql_stmt_affected_rows(stmt);

	if (mysql_stmt_reset(stmt))
	{
		log_error("sakila_get: mysql_stmt_reset: %s",
		    mysql_stmt_error(stmt));
	}
	
	if (affected == 1)
	{
		return 0;
	}
	else if (affected)
	{
		log_error("sakila_set: update affected %lu rows. Expected 0/1!",
		    affected);
		return -1;
	}


	log_debug("sakila_set: insert new record");

	/*
	 * Record does not exist -> INSERT
	 */
	stmt = mh->sakila_query[MY_INSERT]->sakila_stmt;

	if (mysql_stmt_execute(stmt))
	{
		log_error("sakila_set: mysql_stmt_execute: %s",
		    mysql_stmt_error(stmt));
		return -1;
	}

	affected = mysql_stmt_affected_rows(stmt);

	if (mysql_stmt_reset(stmt))
	{
		log_error("sakila_get: mysql_stmt_reset: %s",
		    mysql_stmt_error(stmt));
	}
	
	if (affected != 1)
	{
		log_error("sakila_set: insert affected %lu rows. Expected 1!",
		    affected);
		return -1;
	}

	return 0;
}


static int
sakila_del(dbt_t *dbt, var_t *record)
{
	sakila_handle_t *mh = dbt->dbt_handle;
	MYSQL_STMT *stmt = mh->sakila_query[MY_DELETE]->sakila_stmt;
	ht_t *storage = mh->sakila_storage;
	my_ulonglong affected;

	if (sakila_load_storage(storage, record))
	{
		log_error("sakila_get: sakila_load_storage failed");
		return -1;
	}

	if (mysql_stmt_execute(stmt))
	{
		log_error("sakila_get: mysql_stmt_execute: %s",
		    mysql_stmt_error(stmt));
		return -1;
	}

	affected = mysql_stmt_affected_rows(stmt);

	if (mysql_stmt_reset(stmt))
	{
		log_error("sakila_get: mysql_stmt_reset: %s",
		    mysql_stmt_error(stmt));
	}
	
	log_debug("sakila_del: deleted %lu rows", affected);

	return 0;
}


static int
sakila_cleanup(dbt_t *dbt)
{
	sakila_handle_t *mh = dbt->dbt_handle;
	MYSQL_STMT *stmt = mh->sakila_query[MY_CLEANUP]->sakila_stmt;
	my_ulonglong affected;

	if (mysql_stmt_execute(stmt))
	{
		log_error("sakila_cleanup: mysql_stmt_execute: %s",
		    mysql_stmt_error(stmt));
		return -1;
	}

	affected = mysql_stmt_affected_rows(stmt);

	if (mysql_stmt_reset(stmt))
	{
		log_error("sakila_cleanup: mysql_stmt_reset: %s",
		    mysql_stmt_error(stmt));
	}
	
	log_debug("sakila_cleanup: deleted %lu rows", affected);

	return affected;
}


int
sakila_init(void)
{
	snprintf(sakila_addr_type, sizeof sakila_addr_type, "VARBINARY(%lu)",
	    sizeof (struct sockaddr_storage));

	dbt_driver.dd_open = (dbt_db_open_t) sakila_open;
	dbt_driver.dd_close = (dbt_db_close_t) sakila_close;
	dbt_driver.dd_get = (dbt_db_get_t) sakila_get;
	dbt_driver.dd_set = (dbt_db_set_t) sakila_set;
	dbt_driver.dd_del = (dbt_db_del_t) sakila_del;
	dbt_driver.dd_sql_cleanup = (dbt_db_sql_cleanup_t) sakila_cleanup;
	dbt_driver.dd_flags = DBT_LOCK;

	dbt_driver_register("mysql", &dbt_driver);

	return 0;
}


void
sakila_fini(void)
{
	mysql_library_end();

	return;
}
