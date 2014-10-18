#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#ifdef BDB_DB185_H
# include <db_185.h>
# define OPEN185 __db185_open
#else
# include <db.h>
# define OPEN185 dbopen
#endif

#include <mopher.h>

#define HOME_LEN 1024


static dbt_driver_t dbt_driver;


static int
bdb_open(dbt_t *dbt)
{
	DB *db = NULL;


	db = OPEN185(dbt->dbt_path, O_RDWR|O_CREAT,
		S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP, DB_BTREE, NULL);
	if (db == NULL)
	{
		log_error("bdb_open: db_open failed");
		return -1;
	}

	dbt->dbt_handle = db;

	return 0;
}


static void
bdb_close(dbt_t *dbt)
{
	DB *db = dbt->dbt_handle;
	
	db->close(db);

	return;
}

static int
bdb_get(dbt_t *dbt, var_t *record, var_t **result)
{
	DB *db = dbt->dbt_handle;
	DBT k, d;
	vp_t *lookup = NULL;
	vp_t rec;
	int r;

	*result = NULL;

	lookup = vp_pack(record);
	if (lookup == NULL) {
		log_warning("bdb_get: vp_pack failed");
		goto error;
	}

	memset(&k, 0, sizeof(k));
	memset(&d, 0, sizeof(d));

	k.data = lookup->vp_key;
	k.size = lookup->vp_klen;

	r = db->get(db, &k, &d, 0);
	switch (r) {
	case 0:
		break;
	case 1:
		log_info("bdb_get: no record found");
		goto exit;
	default:
		log_error("bdb_get: DB->get failed");
		goto error;
	}

	vp_init(&rec, k.data, k.size, d.data, d.size);

	*result = vp_unpack(&rec, dbt->dbt_scheme);
	if (*result == NULL) {
		log_warning("bdb_get: vp_unpack failed");
		goto error;
	}

exit:
	vp_delete(lookup);

	return 0;

error:

	if (lookup) {
		vp_delete(lookup);
	}

	return -1;
}


static int
bdb_set(dbt_t *dbt, var_t *v)
{
	DB *db = dbt->dbt_handle;
	DBT k, d;
	vp_t *vp = NULL;
	int r;

	vp = vp_pack(v);
	if (vp == NULL) {
		log_warning("bdb_set: vp_pack failed");
		goto error;
	}

	memset(&k, 0, sizeof(k));
	memset(&d, 0, sizeof(d));

	k.data = vp->vp_key;
	k.size = vp->vp_klen;
	d.data = vp->vp_data;
	d.size = vp->vp_dlen;

	r = db->put(db, &k, &d, 0);
	if (r) {
		log_warning("bdb_set: DB->put failed");
		goto error;
	}

	vp_delete(vp);

	return 0;

error:
	if (vp) {
		vp_delete(vp);
	}

	return -1;
}


static int
bdb_del(dbt_t *dbt, var_t *v)
{
	DB *db =dbt->dbt_handle;
	DBT k, d;
	vp_t *vp;
	int r;

	vp = vp_pack(v);
	if (vp == NULL) {
		log_warning("bdb_del: vp_pack failed");
		goto error;
	}

	memset(&k, 0, sizeof k);
	memset(&d, 0, sizeof d);

	k.data = vp->vp_key;
	k.size = vp->vp_klen;
	d.data = vp->vp_data;
	d.size = vp->vp_dlen;

	r = db->del(db, &k, 0);
	if (r) {
		log_warning("bdb_del: DB->del failed");
		goto error;
	}

	vp_delete(vp);

	return 0;


error:
	if (vp) {
		vp_delete(vp);
	}

	return -1;
}


int
bdb_walk(dbt_t *dbt, dbt_db_callback_t callback)
{
	DB *db = dbt->dbt_handle;
	DBT k, d;
	int r;
	vp_t vp;
	var_t *record;
	int flags;

	memset(&k, 0, sizeof(k));
	memset(&d, 0, sizeof(d));

	for(flags = R_FIRST; (r = db->seq(db, &k, &d, flags)) == 0; flags = R_NEXT)
	{
		vp_init(&vp, k.data, k.size, d.data, d.size);

		record = vp_unpack(&vp, dbt->dbt_scheme);
		if (record == NULL) {
			log_warning("bdb_walk: vp_unpack failed");
			return -1;
		}

		if(callback(dbt, record)) {
			log_warning("bdb_walk: callback failed");
		}

		var_delete(record);
	}
	if(r != 1) {
		log_warning("bdb_walk: DBC->get failed");
		return -1;
	}

	return 0;
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
	dbt_driver.dd_flags = DBT_LOCK;

	dbt_driver_register(&dbt_driver);

	return 0;
}
