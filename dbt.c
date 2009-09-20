#include <stdio.h>
#include <malloc.h>
#include <string.h>

#include "log.h"
#include "cf.h"
#include "dbt.h"
#include "ht.h"
#include "modules.h"

#define DBT_BUCKETS 64

ht_t	*dbt_drivers;


static dbt_t *
dbt_create(char *driver, char *path, char *host, char *user, char *pass,
	char *name, char *table, dbt_driver_t *dd, void *handle)
{
	dbt_t *dbt;

	dbt = (dbt_t *) malloc(sizeof(dbt_t));
	if (dbt == NULL) {
		log_warning("dbt_create: malloc");
		return NULL;
	}

	dbt->dbt_drivername = driver;
	dbt->dbt_path = path;
	dbt->dbt_host = host;
	dbt->dbt_user = user;
	dbt->dbt_pass = pass;
	dbt->dbt_name = name;
	dbt->dbt_table = table;
	dbt->dbt_driver = dd;
	dbt->dbt_handle = handle;

	return dbt;
}


void
dbt_delete(dbt_t *dbt)
{
	dbt->dbt_driver->dd_close(dbt->dbt_handle);
	free(dbt);

	return;
}


static hash_t
dbt_driver_hash(dbt_driver_t *dd)
{
	return HASH(dd->dd_name, strlen(dd->dd_name));
}


static int
dbt_driver_match(dbt_driver_t *dd1, dbt_driver_t *dd2)
{
	if(strcmp(dd1->dd_name, dd2->dd_name) == 0) {
		return 1;
	}

	return 0;
}


void
dbt_driver_register(dbt_driver_t *dd)
{
	if((ht_insert(dbt_drivers, dd)) == -1) {
		log_die(EX_SOFTWARE, "dbt_driver_register: ht_insert failed");
	}

	log_info("dbt_driver_register: database driver \"%s\" registered",
		dd->dd_name);

	return;
}


void
dbt_init(void)
{
	dbt_drivers = ht_create(DBT_BUCKETS, (ht_hash_t) dbt_driver_hash,
		(ht_match_t) dbt_driver_match, NULL);

	if(dbt_drivers == NULL) {
		log_die(EX_SOFTWARE, "dbt_init: ht_create failed");
	}

	modules_load(cf_dbt_mod_path);

	return;
}


void
dbt_clear(void)
{
	ht_delete(dbt_drivers);

	return;
}

dbt_t *
dbt_open(var_t *schema, char *driver, char *path, char *host, char *user,
	char *pass, char *name, char *table)
{
	dbt_t *dbt;
	dbt_driver_t *dd, lookup;
	void *handle;

	memset(&lookup, 0, sizeof(lookup));

	lookup.dd_name = driver;

	dd = ht_lookup(dbt_drivers, &lookup);
	if (dd == NULL) {
		log_warning("dbt_open: unknown database driver \"%s\"",
			driver);
		return NULL;
	}

	handle = dd->dd_open(schema, path, host, user, pass, name, table);
	if (handle == NULL) {
		log_warning("dbt_open: can't open database");
		return NULL;
	}

	dbt = dbt_create(driver, path, host, user, pass, name, table, dd,
		handle);
	if (dbt == NULL) {
		log_warning("dbt_open: dbt_create failed");
		return NULL;
	}

	return dbt;
}


var_t *
dbt_get(dbt_t *dbt, var_t *record)
{
	return dbt->dbt_driver->dd_get(dbt->dbt_handle, record);
}

int
dbt_set(dbt_t *dbt, var_t *record)
{
	return dbt->dbt_driver->dd_set(dbt->dbt_handle, record);
}

int
dbt_del(dbt_t *dbt, var_t *record)
{
	return dbt->dbt_driver->dd_del(dbt->dbt_handle, record);
}

int
dbt_walk(dbt_t *dbt, dbt_callback_t callback, void *data)
{
	return dbt->dbt_driver->dd_walk(dbt->dbt_handle, callback, data);
}

int
dbt_sync(dbt_t *dbt)
{
	if (dbt->dbt_driver->dd_sync == NULL) {
		return 0;
	}

	return dbt->dbt_driver->dd_sync(dbt->dbt_handle);
}
