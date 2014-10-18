#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

#include <mopher.h>


#define MEMDB_BUCKETS 4096


static dbt_driver_t dbt_driver;

static hash_t
memdb_record_hash(vp_t *vp)
{
	return HASH(vp->vp_key, vp->vp_klen);
}


static int
memdb_record_match(vp_t *vp1, vp_t *vp2)
{
	if (memcmp(vp1->vp_key, vp2->vp_key, vp1->vp_klen))
	{
		return 0;
	}

	return 1;
}


static void
memdb_record_delete(vp_t *vp)
{
	vp_delete(vp);

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
	vp_t *key = NULL, *data;

	key = vp_pack(record);
	if (key == NULL) {
		log_error("memdb_get: vp_pack failed");
		goto error;
	}

	data = ht_lookup(ht, key);
	if (data == NULL)
	{
		log_debug("memdb_get: no record found");
		goto exit;
	}

	*result = vp_unpack(data, dbt->dbt_scheme);
	if (*result == NULL) {
		log_error("memdb_get: vp_unpack failed");
		goto error;
	}

exit:
	vp_delete(key);

	return 0;

error:

	if (key) {
		vp_delete(key);
	}

	return -1;
}

static int
memdb_set(dbt_t *dbt, var_t *record)
{
	ht_t *ht = dbt->dbt_handle;
	vp_t *vp = NULL;

	vp = vp_pack(record);
	if (vp == NULL) {
		log_error("memdb_set: vp_pack failed");
		goto error;
	}

	ht_remove(ht, vp);
	if (ht_insert(ht, vp))
	{
		log_error("memdb_set: ht_insert failed");
		goto error;
	}

	return 0;

error:
	if (vp) {
		vp_delete(vp);
	}

	return -1;
}


static int
memdb_del(dbt_t *dbt, var_t *record)
{
	ht_t *ht = dbt->dbt_handle;
	vp_t *vp = NULL;

	vp = vp_pack(record);
	if (vp == NULL) {
		log_error("memdb_set: vp_pack failed");
		goto error;
	}

	ht_remove(ht, vp);
	vp_delete(vp);

	return 0;


error:
	if (vp) {
		vp_delete(vp);
	}

	return -1;
}


int
memdb_walk(dbt_t *dbt, dbt_db_callback_t callback)
{
	ht_t *ht = dbt->dbt_handle;
	ht_pos_t pos;
	vp_t *vp;
	var_t *record;

	ht_start(ht, &pos);
	while ((vp = ht_next(ht, &pos)))
	{
		record = vp_unpack(vp, dbt->dbt_scheme);
		if (record == NULL) {
			log_error("memdb_walk: vp_unpack failed");
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
	dbt_driver.dd_name	= "memdb";
	dbt_driver.dd_open	= (dbt_db_open_t)	memdb_open;
	dbt_driver.dd_close	= (dbt_db_close_t)	memdb_close;
	dbt_driver.dd_get	= (dbt_db_get_t)	memdb_get;
	dbt_driver.dd_set	= (dbt_db_set_t)	memdb_set;
	dbt_driver.dd_del	= (dbt_db_del_t)	memdb_del;
	dbt_driver.dd_walk	= (dbt_db_walk_t)	memdb_walk;
	dbt_driver.dd_flags	= DBT_LOCK;

	dbt_driver_register(&dbt_driver);

	return 0;
}
