#include <string.h>
#include <malloc.h>
#include <db.h>
#include <errno.h>

#include "mopher.h"


static dbt_driver_t dbt_driver;


static int
bdb_open(dbt_t *dbt)
{
	DB *db = NULL;
	int r;

	if (db_create(&db, NULL, 0)) {
		log_error("bdb_open: db_create failed");
		goto error;
	}

	r = db->open(db, NULL, dbt->dbt_path, dbt->dbt_table,
		DB_BTREE, DB_CREATE | DB_THREAD, 0);
	if (r) {
		log_error("bdb_open: DB->open for \"%s\" failed",
			dbt->dbt_path);
		goto error;
	}
	errno = 0;

	dbt->dbt_handle = db;

	return 0;

error:

	if (db) {
		db->close(db, 0);
	}

	return -1;
}


static void
bdb_close(dbt_t *dbt)
{
	DB *db = dbt->dbt_handle;
	
	db->close(db, 0);

	return;
}

static int
bdb_get(dbt_t *dbt, var_t *record, var_t **result)
{
	DB *db = dbt->dbt_handle;
	DBT k, d;
	var_compact_t *vc = NULL;
	int r;

	*result = NULL;

	vc = var_compress(record);
	if (vc == NULL) {
		log_warning("bdb_get: var_compress failed");
		goto error;
	}

	memset(&k, 0, sizeof(k));
	memset(&d, 0, sizeof(d));

	k.data = vc->vc_key;
	k.size = vc->vc_klen;
	d.flags = DB_DBT_MALLOC;

	r = db->get(db, NULL, &k, &d, 0);
	switch (r) {
	case 0:
		break;
	case DB_NOTFOUND:
		log_info("bdb_get: no record found");
		goto exit;
	default:
		log_error("bdb_get: DB->get failed");
		goto error;
	}

	vc->vc_data = d.data;
	vc->vc_dlen = d.size;

	*result = var_decompress(vc, dbt->dbt_scheme);
	if (*result == NULL) {
		log_warning("bdb_get: var_decompress failed");
		goto error;
	}

exit:
	var_compact_delete(vc);

	return 0;

error:

	if (vc) {
		var_compact_delete(vc);
	}

	return -1;
}


static int
bdb_set(dbt_t *dbt, var_t *v)
{
	DB *db = dbt->dbt_handle;
	DBT k, d;
	var_compact_t *vc = NULL;
	int r;

	vc = var_compress(v);
	if (vc == NULL) {
		log_warning("bdb_set: var_compress failed");
		goto error;
	}

	memset(&k, 0, sizeof(k));
	memset(&d, 0, sizeof(d));

	k.data = vc->vc_key;
	k.size = vc->vc_klen;
	d.data = vc->vc_data;
	d.size = vc->vc_dlen;

	r = db->put(db, NULL, &k, &d, 0);
	if (r) {
		log_warning("bdb_set: DB->put failed");
		goto error;
	}

	var_compact_delete(vc);

	return 0;

error:
	if (vc) {
		var_compact_delete(vc);
	}

	return -1;
}


static int
bdb_del(dbt_t *dbt, var_t *v)
{
	DB *db =dbt->dbt_handle;
	DBT k, d;
	var_compact_t *vc;
	int r;

	vc = var_compress(v);
	if (vc == NULL) {
		log_warning("bdb_del: var_compress failed");
		goto error;
	}

	memset(&k, 0, sizeof(k));
	memset(&d, 0, sizeof(d));

	k.data = vc->vc_key;
	k.size = vc->vc_klen;
	d.data = vc->vc_data;
	d.size = vc->vc_dlen;

	r = db->del(db, NULL, &k, 0);
	if (r) {
		log_warning("bdb_del: DB->del failed");
		goto error;
	}

	var_compact_delete(vc);

	return 0;


error:
	if (vc) {
		var_compact_delete(vc);
	}

	return -1;
}


int
bdb_walk(dbt_t *dbt, dbt_db_callback_t callback)
{
	DB *db = dbt->dbt_handle;
	DBC *cursor = NULL;
	DBT k, d;
	int r;
	var_compact_t vc;
	var_t *record;

	if (db->cursor(db, NULL, &cursor, 0)) {
		log_warning("bdb_walk: DB->cursor failed");
		goto error;
	}

	memset(&k, 0, sizeof(k));
	memset(&d, 0, sizeof(d));

	while((r = cursor->get(cursor, &k, &d, DB_NEXT)) == 0) {
		vc.vc_key = k.data;
		vc.vc_klen = k.size;
		vc.vc_data = d.data;
		vc.vc_dlen = d.size;

		record = var_decompress(&vc, dbt->dbt_scheme);
		if (record == NULL) {
			log_warning("bdb_walk: var_decompress failed");
			goto error;
		}

		if(callback(dbt, record)) {
			log_warning("bdb_walk: callback failed");
		}

		var_delete(record);
	}
	if(r != DB_NOTFOUND) {
		log_warning("bdb_walk: DBC->get failed");
		goto error;
	}

	cursor->close(cursor);

	return 0;


error:

	if (cursor) {
		cursor->close(cursor);
	}

	return -1;
}


static int
bdb_sync(dbt_t *dbt)
{
	DB *db = dbt->dbt_handle;

	if (db->sync(db, 0)) {
		log_warning("bdb_sync: DB->sync failed");
		return -1;
	}

	return 0;
}


int
bdb_init(void)
{
	dbt_driver.dd_name = "bdb";
	dbt_driver.dd_open = (dbt_db_open_t) bdb_open;
	dbt_driver.dd_close = (dbt_db_close_t) bdb_close;
	dbt_driver.dd_get = (dbt_db_get_t) bdb_get;
	dbt_driver.dd_set = (dbt_db_set_t) bdb_set;
	dbt_driver.dd_del = (dbt_db_del_t) bdb_del;
	dbt_driver.dd_walk = (dbt_db_walk_t) bdb_walk;
	dbt_driver.dd_sync = (dbt_db_sync_t) bdb_sync;

	dbt_driver_register(&dbt_driver);

	return 0;
}
