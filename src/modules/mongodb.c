#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

#include <mongoc.h>

#include <mopher.h>


#define MEMDB_BUCKETS 4096

typedef struct mongodb {
	mongoc_client_t *mng_client;
	mongoc_collection_t *mng_collection;
} mongodb_t;

static dbt_driver_t dbt_driver;

static mongodb_t*
mongodb_create(mongoc_client_t *client, mongoc_collection_t *collection)
{
	mongodb_t *mng;

	mng = (mongodb_t *) malloc(sizeof(mongodb_t));
	if (mng == NULL)
	{
		log_error("mongodb_create: malloc failed");
		return NULL;
	}

	mng->mng_client = client;
	mng->mng_collection = collection;

	return mng;
}

static void
mongodb_destroy(mongodb_t *mng)
{
	mongoc_collection_destroy(mng->mng_collection);
	mongoc_client_destroy(mng->mng_client);
	free(mng);

	return;
}

static int
mongodb_open(dbt_t *dbt)
{
	mongodb_t *mng;
	mongoc_client_t *client;
	mongoc_collection_t *collection;
	char *db_name = dbt->dbt_database;
	char *coll_name = dbt->dbt_scheme->v_name;

	client = mongoc_client_new(dbt->dbt_path);
	if (client == NULL)
	{
		log_error("mongodb_open: mongoc_client_new failed");
		return -1;
	}

	collection = mongoc_client_get_collection(client, db_name, coll_name);
	if (collection == NULL)
	{
		log_error("mongodb_open: mongoc_client_get failed");
		return -1;
	}

	mng = mongodb_create(client, collection);
	if (mng == NULL)
	{
		log_error("mongodb_open: mongodb_create failed");
		return -1;
	}
	

	dbt->dbt_handle = mng;

	return 0;
}


static void
mongodb_close(dbt_t *dbt)
{
	mongodb_t *mng = (mongodb_t *) dbt->dbt_handle;
	mongodb_destroy(mng);

	return;
}

static bson_t *
mongodb_create_bson(var_t *record, int is_query)
{
	var_t *item;
	ll_t *ll;
	ll_entry_t *pos;
	int bson_success;
	int n;
	char buffer[1024];
	bson_t *doc = NULL;

	doc = bson_new();
	if (doc == NULL)
	{
		log_error("mongodb_get: bson_new failed");
		goto error;
	}

	if(record->v_type != VT_LIST) {
		log_error("mongodb_create_bson: bad record");
		goto error;
	}

	ll = record->v_data;
	pos = LL_START(ll);

	for (n = 0; (item = ll_next(ll, &pos)); ++n)
	{
		if (is_query && (item->v_flags & VF_KEY) == 0)
		{
			continue;
		}

		switch(item->v_type)
		{
		case VT_INT:
			bson_success = BSON_APPEND_INT32(doc, item->v_name, *(VAR_INT_T *) item->v_data);
			break;

		case VT_FLOAT:
			bson_success = BSON_APPEND_DOUBLE(doc, item->v_name, *(VAR_FLOAT_T *) item->v_data);
			break;

		default:
			if (var_dump_data(item, buffer, sizeof buffer) == -1) {
				log_error("mongodb_create_bson: var_dump_data for %s failed", item->v_name);
				goto error;
			}
			bson_success = BSON_APPEND_UTF8(doc, item->v_name, buffer);
		}

		if(!bson_success) {
			log_error("mongodb_create_bson: BSON_APPEND_* for %s failed", item->v_name);
			goto error;
		}
	}

	return doc;

error:
	if (doc) {
		bson_destroy(doc);
	}

	return NULL;
}

static int
mongodb_bson_as_record(var_t *scheme, const bson_t *doc, var_t **result)
{
	bson_iter_t iter;
	var_t *item;
	ll_t *ll;
	ll_entry_t *pos;
	int n;
	VAR_INT_T i;
	VAR_FLOAT_T f;
	const char *str;
	void *data;
	unsigned int len;
	void *scan = NULL;

	*result = NULL;

	ll = scheme->v_data;
	pos = LL_START(ll);

	if(!bson_iter_init(&iter, doc))
	{
		log_error("mongodb_bson_as_record: bson_iter_init failed");
		goto error;
	}

	*result = vlist_create(scheme->v_name, VF_KEEPNAME);
	if(*result == NULL)
	{
		log_error("mongodb_bson_as_record: vlist_create failed");
		goto error;
	}

	for (n = 0; (item = ll_next(ll, &pos)); ++n) {
		if (!bson_iter_find(&iter, item->v_name))
		{
			log_error("mongodb_bson_as_record: bson_iter_find for %s failed", item->v_name);
			goto error;
		}

		switch (item->v_type)
		{
		case VT_INT:
			if (!BSON_ITER_HOLDS_INT32(&iter))
			{
				log_error("mongodb_bson_as_record: bad type for %s: need integer", item->v_name);
				goto error;
			}

			i = bson_iter_int32(&iter);
			data = &i;
			break;
				
		case VT_FLOAT:
			if (!BSON_ITER_HOLDS_DOUBLE(&iter))
			{
				log_error("mongodb_bson_as_record: bad type for %s: need double", item->v_name);
				goto error;
			}

			f = bson_iter_double(&iter);
			data = &f;
			break;

		case VT_STRING:
			if (!BSON_ITER_HOLDS_UTF8(&iter))
			{
				log_error("mongodb_bson_as_record: bad type for %s: need utf-8 string", item->v_name);
				goto error;
			}

			str = bson_iter_utf8(&iter, &len);
			data = (void *) str;
			break;

		default:
			if (!BSON_ITER_HOLDS_UTF8(&iter))
			{
				log_error("mongodb_bson_as_record: bad type for %s: need utf-8 string", item->v_name);
				goto error;
			}

			str = bson_iter_utf8(&iter, &len);
			scan = var_scan_data(item->v_type, (char *) str);
			if (scan == NULL)
			{
				log_error("mongodb_bson_as_record: var_scan_data for %s failed", item->v_name);
				goto error;
			}
			data = scan;
			break;
		}

		if (vlist_append_new(*result, item->v_type, item->v_name, data, VF_KEEPNAME | VF_COPYDATA))
		{
			log_error("mongodb_bson_as_record: vlist_append_new for %s failed", item->v_name);
			goto error;
		}

		if (scan) {
			free(scan);
			scan = NULL;
		}
	}


	return 0;

error:
	if (*result)
	{
		var_delete(*result);
		*result = NULL;
	}

	return -1;
}

static int
mongodb_get(dbt_t *dbt, var_t *record, var_t **result)
{
	mongodb_t *mng;
	mongoc_collection_t *collection;
	mongoc_cursor_t *cursor = NULL;
	bson_error_t error;
	bson_t *query = NULL;
	const bson_t *doc;

	mng = dbt->dbt_handle;
	collection = mng->mng_collection;

	*result = NULL;

	query = mongodb_create_bson(record, 1);
	if (query == NULL)
	{
		log_error("mongodb_get: mongodb_create_bson failed");
		goto error;
	}

	cursor = mongoc_collection_find(collection, MONGOC_QUERY_NONE, 0, 1, 0, query, NULL, NULL);	
	if (!mongoc_cursor_next(cursor, &doc)) {
		if (mongoc_cursor_error (cursor, &error)) {
			log_error("mongodb_get: mongodb_cursor_next failed: %s", error.message);
			goto error;
		}

		// No record found
		goto exit;
	}

	if (mongodb_bson_as_record(dbt->dbt_scheme, doc, result)) {
		log_error("mongodb_get: mongodb_bson_as_record failed");
		goto error;
	}

exit:
	bson_destroy(query);
	mongoc_cursor_destroy(cursor);

	return 0;

error:
	if (query)
	{
		bson_destroy(query);
	}
	if (cursor)
	{
		mongoc_cursor_destroy(cursor);
	}

	return -1;
}

static int
mongodb_set(dbt_t *dbt, var_t *record)
{
	mongodb_t *mng;
	mongoc_collection_t *collection;
	bson_error_t error;
	bson_t *query = NULL;
	bson_t *doc = NULL;

	mng = dbt->dbt_handle;
	collection = mng->mng_collection;

	query = mongodb_create_bson(record, 1);
	doc = mongodb_create_bson(record, 0);
	if (query == NULL || doc == NULL)
	{
		log_error("mongodb_set: mongodb_create_bson failed");
		goto error;
	}

	if (!mongoc_collection_find_and_modify(collection, query, NULL, doc, NULL, 0, 1, 0, 0, &error)) {
		log_error("mongodb_set: mongoc_collection_find_and_modify failed: %s", error.message);
		goto error;
	}

	bson_destroy(query);
	bson_destroy(doc);

	return 0;

error:
	if(query)
	{
		bson_destroy(query);
	}
	if(doc)
	{
		bson_destroy(doc);
	}

	return -1;
}


static int
mongodb_del(dbt_t *dbt, var_t *record)
{
	mongodb_t *mng;
	mongoc_collection_t *collection;
	bson_error_t error;
	bson_t *query = NULL;

	mng = dbt->dbt_handle;
	collection = mng->mng_collection;

	query = mongodb_create_bson(record, 1);
	if (query == NULL)
	{
		log_error("mongodb_del: mongodb_create_bson failed");
		goto error;
	}

	if (!mongoc_collection_remove(collection, MONGOC_REMOVE_NONE, query, NULL, &error)) {
		log_error("mongodb_del: !mongoc_collection_remove failed: %s", error.message);
		goto error;
	}

	bson_destroy(query);

	return 0;

error:
	if (query)
	{
		bson_destroy(query);
	}

	return -1;
}

static int
mongodb_walk(dbt_t *dbt, dbt_db_callback_t callback)
{
	mongodb_t *mng;
	mongoc_collection_t *collection;
	mongoc_cursor_t *cursor = NULL;
	bson_t *query = NULL;
	var_t *record = NULL;
	const bson_t *doc;

	mng = dbt->dbt_handle;
	collection = mng->mng_collection;

	query = bson_new();
	cursor = mongoc_collection_find(collection, MONGOC_QUERY_NONE, 0, 0, 0, query, NULL, NULL);	

	while(mongoc_cursor_next(cursor, &doc))
	{
		if (mongodb_bson_as_record(dbt->dbt_scheme, doc, &record)) {
			log_error("sql_db_walk: mongodb_bson_as_record failed");
			goto error;
		}

		if (callback(dbt, record))
		{
			log_error("sql_db_walk: callback failed");
			goto error;
		}

		var_delete(record);
		record = NULL;
	}

	bson_destroy(query);
	mongoc_cursor_destroy(cursor);

	return 0;

error:
	if (record)
	{
		var_delete(record);
	}
	if (query)
	{
		bson_destroy(query);
	}
	if (cursor)
	{
		mongoc_cursor_destroy(cursor);
	}

	return -1;
}

static int
mongodb_expire(dbt_t *dbt)
{
	mongodb_t *mng;
	mongoc_collection_t *collection;
	bson_error_t error;
	bson_t *query = NULL;
	long now = time(NULL);
	long count = -1;
	
	mng = dbt->dbt_handle;
	collection = mng->mng_collection;

	query = BCON_NEW(BCON_UTF8(dbt->dbt_expire_field), "{", "$lt", BCON_INT32(now), "}");

	// FIXME: returned count is always zero.
	count =	(long) mongoc_collection_count(collection, MONGOC_QUERY_NONE, query, 0, 0, NULL, &error);
	if (count == -1)
	{
		log_error("mongodb_expire: mongoc_collection_count failed: %s", error.message);
		goto error;
	}

	if (!mongoc_collection_find_and_modify(collection, query, NULL, NULL, NULL, 1, 0, 0, NULL, &error)) {
		log_error("mongodb_expire: mongoc_collection_find_and_modify failed: %s", error.message);
		goto error;
	}

error:
	bson_destroy(query);

	return count;
}

int
mongodb_init(void)
{
	dbt_driver.dd_name	= "mongodb";
	dbt_driver.dd_init	= (dbt_db_init_t)	mongoc_init;
	dbt_driver.dd_cleanup	= (dbt_db_cleanup_t)	mongoc_cleanup;
	dbt_driver.dd_open	= (dbt_db_open_t)	mongodb_open;
	dbt_driver.dd_close	= (dbt_db_close_t)	mongodb_close;
	dbt_driver.dd_get	= (dbt_db_get_t)	mongodb_get;
	dbt_driver.dd_set	= (dbt_db_set_t)	mongodb_set;
	dbt_driver.dd_del	= (dbt_db_del_t)	mongodb_del;
	dbt_driver.dd_walk	= (dbt_db_walk_t)	mongodb_walk;
	dbt_driver.dd_expire	= (dbt_db_expire_t)	mongodb_expire;
	dbt_driver.dd_flags	= DBT_LOCK;

	dbt_driver_register(&dbt_driver);

	return 0;
}
