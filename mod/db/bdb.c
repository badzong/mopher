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
	var_record_t *vr = NULL;
	DBT k, d;
	int r;

	vr = var_record_pack(v);
	if (vr == NULL) {
		log_warning("bdb_get: var_record_pack failed");
		goto error;
	}

	memset(&k, 0, sizeof(k));
	memset(&d, 0, sizeof(d));

	k.data = vr->vr_key;
	k.size = vr->vr_klen;
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

	vr->vr_data = d.data;
	vr->vr_dlen = d.size;

	record = var_record_unpack(vr, bdb->bdb_schema);
	if (record == NULL) {
		log_warning("bdb_get: var_record_unpack failed");
		goto error;
	}

	var_record_delete(vr);

	return record;

error:

	if (vr) {
		var_record_delete(vr);
	}

	return NULL;
}


static int
bdb_set(bdb_t *bdb, var_t *v)
{
	var_record_t *vr = NULL;
	DBT k, d;
	int r;

	vr = var_record_pack(v);
	if (vr == NULL) {
		log_warning("bdb_set: var_record_pack failed");
		goto error;
	}

	memset(&k, 0, sizeof(k));
	memset(&d, 0, sizeof(d));

	k.data = vr->vr_key;
	k.size = vr->vr_klen;
	d.data = vr->vr_data;
	d.size = vr->vr_dlen;

	r = bdb->bdb_db->put(bdb->bdb_db, NULL, &k, &d, 0);
	if (r) {
		log_warning("bdb_set: DB->put failed");
		goto error;
	}

	var_record_delete(vr);

	return 0;

error:
	if (vr) {
		var_record_delete(vr);
	}

	return -1;
}


static int
bdb_del(bdb_t *bdb, var_t *v)
{
	var_record_t *vr;
	DBT k, d;
	int r;

	vr = var_record_pack(v);
	if (vr == NULL) {
		log_warning("bdb_del: var_record_pack failed");
		return -1;
	}

	memset(&k, 0, sizeof(k));
	memset(&d, 0, sizeof(d));

	k.data = vr->vr_key;
	k.size = vr->vr_klen;
	d.data = vr->vr_data;
	d.size = vr->vr_dlen;

	r = bdb->bdb_db->del(bdb->bdb_db, NULL, &k, 0);
	if (r) {
		log_warning("bdb_del: DB->del failed");
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

	dbt_driver_register(&dbt_driver);

	return 0;
}
