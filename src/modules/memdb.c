#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <mopher.h>


#define MEMDB_BUCKETS 4096


static dbt_driver_t dbt_driver;

static hash_t
memdb_record_hash(var_compact_t *vc)
{
	return HASH(vc->vc_key, vc->vc_klen);
}


static int
memdb_record_match(var_compact_t *vc1, var_compact_t *vc2)
{
	if (memcmp(vc1->vc_key, vc2->vc_key, vc1->vc_klen))
	{
		return 0;
	}

	return 1;
}


static void
memdb_record_delete(var_compact_t *vc)
{
	var_compact_delete(vc);

	return;
}


static int
memdb_open(dbt_t *dbt)
{
	ht_t *ht;

	ht = ht_create(MEMDB_BUCKETS, (ht_hash_t) memdb_record_hash,
	    (ht_match_t) memdb_record_match,
	    (ht_delete_t) memdb_record_delete);

	if (ht == NULL)
	{
		log_error("memdb_open: ht_create failed");
		return -1;
	}

	dbt->dbt_handle = ht;

	return 0;
}


static void
memdb_close(dbt_t *dbt)
{
	ht_t *ht = dbt->dbt_handle;

	ht_delete(ht);

	return;
}

static int
memdb_get(dbt_t *dbt, var_t *record, var_t **result)
{
	ht_t *ht = dbt->dbt_handle;
	var_compact_t *key = NULL, *data;

	key = var_compress(record);
	if (key == NULL) {
		log_error("memdb_get: var_compress failed");
		goto error;
	}

	data = ht_lookup(ht, key);
	if (data == NULL)
	{
		log_debug("memdb_get: no record found");
		goto exit;
	}

	*result = var_decompress(data, dbt->dbt_scheme);
	if (*result == NULL) {
		log_error("memdb_get: var_decompress failed");
		goto error;
	}

exit:
	var_compact_delete(key);

	return 0;

error:

	if (key) {
		var_compact_delete(key);
	}

	return -1;
}


static int
memdb_set(dbt_t *dbt, var_t *record)
{
	ht_t *ht = dbt->dbt_handle;
	var_compact_t *vc = NULL;

	vc = var_compress(record);
	if (vc == NULL) {
		log_error("memdb_set: var_compress failed");
		goto error;
	}

	ht_remove(ht, vc);
	if (ht_insert(ht, vc))
	{
		log_error("memdb_set: ht_insert failed");
		goto error;
	}

	return 0;

error:
	if (vc) {
		var_compact_delete(vc);
	}

	return -1;
}


static int
memdb_del(dbt_t *dbt, var_t *record)
{
	ht_t *ht = dbt->dbt_handle;
	var_compact_t *vc = NULL;

	vc = var_compress(record);
	if (vc == NULL) {
		log_error("memdb_set: var_compress failed");
		goto error;
	}

	ht_remove(ht, vc);
	var_compact_delete(vc);

	return 0;


error:
	if (vc) {
		var_compact_delete(vc);
	}

	return -1;
}


int
memdb_walk(dbt_t *dbt, dbt_db_callback_t callback)
{
	ht_t *ht = dbt->dbt_handle;
	ht_pos_t pos;
	var_compact_t *vc;
	var_t *record;

	ht_start(ht, &pos);
	while ((vc = ht_next(ht, &pos)))
	{
		record = var_decompress(vc, dbt->dbt_scheme);
		if (record == NULL) {
			log_error("memdb_walk: var_decompress failed");
			return -1;
		}

		if(callback(dbt, record)) {
			log_error("memdb_walk: callback failed");
		}

		var_delete(record);
	}

	return 0;
}


int
memdb_init(void)
{
	dbt_driver.dd_open	= (dbt_db_open_t)	memdb_open;
	dbt_driver.dd_close	= (dbt_db_close_t)	memdb_close;
	dbt_driver.dd_get	= (dbt_db_get_t)	memdb_get;
	dbt_driver.dd_set	= (dbt_db_set_t)	memdb_set;
	dbt_driver.dd_del	= (dbt_db_del_t)	memdb_del;
	dbt_driver.dd_walk	= (dbt_db_walk_t)	memdb_walk;
	dbt_driver.dd_flags	= DBT_LOCK;

	dbt_driver_register("memdb", &dbt_driver);

	return 0;
}
