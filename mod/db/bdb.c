#include <string.h>
#include <malloc.h>
#include <db.h>
#include <errno.h>

#include "log.h"
#include "dbt.h"
#include "var.h"


typedef struct bdb {
	var_t	*bdb_schema;
	DB	*bdb_db;
} bdb_t;


static dbt_driver_t dbt_driver;


static void
bdb_delete(bdb_t *bdb)
{
	free(bdb);

	return;
}


static bdb_t*
bdb_create(var_t *schema)
{
	bdb_t *bdb = NULL;

	bdb = (bdb_t *) malloc(sizeof(bdb_t));
	if(bdb == NULL) {
		log_warning("bdb_create: malloc");
		goto error;
	}

	memset(bdb, 0, sizeof(bdb_t));

	if (db_create(&bdb->bdb_db, NULL, 0)) {
		log_error("bdb_create: db_create failed");
		goto error;
	}

	bdb->bdb_schema = schema;

	return bdb;


error:

	if(bdb) {
		bdb_delete(bdb);
	}

	return NULL;
}


static void*
bdb_open(var_t *schema, char *path, char *host, char *user, char *pass,
	char *name, char *table)
{
	bdb_t *bdb;
	int r;

	bdb = bdb_create(schema);
	if (bdb == NULL) {
		log_error("bdb_open: bdb_create failed");
		return NULL;
	}

	r = bdb->bdb_db->open(bdb->bdb_db, NULL, path, NULL, DB_BTREE,
		DB_CREATE | DB_THREAD, 0);
	if (r) {
		bdb_delete(bdb);
		log_error("bdb_open: DB->open for \"%s\" failed", path);
		return NULL;
	}
	errno = 0;

	return bdb;
}


static void
bdb_close(bdb_t *bdb)
{
	bdb->bdb_db->close(bdb->bdb_db, 0);
	bdb_delete(bdb);

	return;
}

static var_t *
bdb_get(bdb_t *bdb, var_t *v)
{
	var_t *record;
	var_compact_t *vc = NULL;
	DBT k, d;
	int r;

	vc = var_compress(v);
	if (vc == NULL) {
		log_warning("bdb_get: var_compress failed");
		goto error;
	}

	memset(&k, 0, sizeof(k));
	memset(&d, 0, sizeof(d));

	k.data = vc->vc_key;
	k.size = vc->vc_klen;
	d.flags = DB_DBT_MALLOC;

	r = bdb->bdb_db->get(bdb->bdb_db, NULL, &k, &d, 0);
	switch (r) {
	case 0:
		break;
	case DB_NOTFOUND:
		log_warning("bdb_get: no record found");
		goto error;
	default:
		log_warning("bdb_get: DB->get failed");
		goto error;
	}

	vc->vc_data = d.data;
	vc->vc_dlen = d.size;

	record = var_decompress(vc, bdb->bdb_schema);
	if (record == NULL) {
		log_warning("bdb_get: var_decompress failed");
		goto error;
	}

	var_compact_delete(vc);

	return record;

error:

	if (vc) {
		var_compact_delete(vc);
	}

	return NULL;
}


static int
bdb_set(bdb_t *bdb, var_t *v)
{
	var_compact_t *vc = NULL;
	DBT k, d;
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

	r = bdb->bdb_db->put(bdb->bdb_db, NULL, &k, &d, 0);
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
bdb_del(bdb_t *bdb, var_t *v)
{
	var_compact_t *vc;
	DBT k, d;
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

	r = bdb->bdb_db->del(bdb->bdb_db, NULL, &k, 0);
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
bdb_walk(bdb_t *bdb, dbt_callback_t callback, void *data)
{
	DBC *cursor = NULL;
	DBT k, d;
	int r;
	var_compact_t vc;
	var_t *record;

	if (bdb->bdb_db->cursor(bdb->bdb_db, NULL, &cursor, 0)) {
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

		record = var_decompress(&vc, bdb->bdb_schema);
		if (record == NULL) {
			log_warning("bdb_walk: var_decompress failed");
			goto error;
		}

		if(callback(data, record)) {
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
bdb_sync(bdb_t *bdb)
{
	if (bdb->bdb_db->sync(bdb->bdb_db, 0)) {
		log_warning("bdb_sync: DB->sync failed");
		return -1;
	}

	return 0;
}


int
init(void)
{
	memset(&dbt_driver, 0, sizeof(dbt_driver));

	dbt_driver.dd_name = "bdb";
	dbt_driver.dd_type = DT_FILE;
	dbt_driver.dd_open = (dbt_open_t) bdb_open;
	dbt_driver.dd_close = (dbt_close_t) bdb_close;
	dbt_driver.dd_get = (dbt_get_t) bdb_get;
	dbt_driver.dd_set = (dbt_set_t) bdb_set;
	dbt_driver.dd_del = (dbt_del_t) bdb_del;
	dbt_driver.dd_walk = (dbt_walk_t) bdb_walk;
	dbt_driver.dd_sync = (dbt_sync_t) bdb_sync;

	dbt_driver_register(&dbt_driver);

	return 0;
}
