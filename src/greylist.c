#include "config.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#include "mopher.h"

static dbt_t greylist_dbt;

greylist_t *
greylist_delay(greylist_t *gl, int delay)
{
	gl->gl_delay = delay;
	return gl;
}


greylist_t *
greylist_visa(greylist_t *gl, int visa)
{
	gl->gl_visa = visa;
	return gl;
}


greylist_t *
greylist_valid(greylist_t *gl, int valid)
{
	gl->gl_valid = valid;
	return gl;
}


greylist_t *
greylist_create(void)
{
	greylist_t *gl;

	gl = (greylist_t *) malloc(sizeof (greylist_t));
	if (gl == NULL)
	{
		log_die(EX_OSERR, "greylist_create: malloc");
	}

	gl->gl_delay = cf_greylist_default_delay;
	gl->gl_visa = cf_greylist_default_visa;
	gl->gl_valid = cf_greylist_default_valid;

	return gl;
}


void
greylist_delete(greylist_t *gl)
{
	free(gl);

	return;
}


static int
greylist_validate(dbt_t *dbt, var_t *record)
{
	VAR_INT_T *created;
	VAR_INT_T *valid;

	if (var_list_dereference(record, NULL, NULL, NULL, &created, &valid,
		NULL, NULL, NULL, NULL)) {
		log_warning("greylist_valid: var_list_unpack failed");
		return -1;
	}

	/*
	 * dbt->dbt_cleanup_schedule == time(NULL)
	 */
	if (dbt->dbt_cleanup_schedule > *created + *valid) {
		return 0;
	}

	return 1;
}


void
greylist_init(void)
{
	var_t *scheme;

	scheme = var_scheme_create("greylist",
		"hostaddr",	VT_ADDR,	VF_KEEPNAME | VF_KEY, 
		"envfrom",	VT_STRING,	VF_KEEPNAME | VF_KEY,
		"envrcpt",	VT_STRING,	VF_KEEPNAME | VF_KEY,
		"created",	VT_INT,		VF_KEEPNAME,
		"updated",	VT_INT,		VF_KEEPNAME,
		"valid",	VT_INT,		VF_KEEPNAME,
		"delay",	VT_INT,		VF_KEEPNAME,
		"retries",	VT_INT,		VF_KEEPNAME,
		"visa",		VT_INT,		VF_KEEPNAME,
		"passed",	VT_INT,		VF_KEEPNAME,
		NULL);

	if (scheme == NULL) {
		log_die(EX_SOFTWARE, "greylist: init: var_scheme_create "
		    "failed");
	}

	greylist_dbt.dbt_name = "greylist";
	greylist_dbt.dbt_scheme = scheme;
	greylist_dbt.dbt_validate = (dbt_validate_t) greylist_validate;
	greylist_dbt.dbt_sql_invalid_where =
		"`valid` + `created` < unix_timestamp()";

	/*
	 * greylist_valid need to be registered at table
	 * sql bulk del where
	 */

	dbt_register(&greylist_dbt);

	return;
}


void
greylist_clear(void)
{
	var_delete(greylist_dbt.dbt_scheme);

	return;
}


static int
greylist_lookup(var_t *attrs, var_t **record)
{
	var_t *lookup = NULL;
	char *hostaddr;
	char *envfrom;
	char *envrcpt;
	
	if (var_table_dereference(attrs, "milter_hostaddr", &hostaddr,
		"milter_envfrom", &envfrom, "milter_envrcpt", &envrcpt, NULL))
	{
		log_error("greylist_lookup: var_table_dereference failed");
		goto error;
	}

	lookup = var_list_scheme(greylist_dbt.dbt_scheme, hostaddr, envfrom,
		envrcpt, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

	if (lookup == NULL) {
		log_warning("greylist_lookup: var_list_scheme failed");
		goto error;
	}

	if (dbt_db_get(&greylist_dbt, lookup, record))
	{
		log_warning("greylist_lookup: var_db_get failed");
		goto error;
	}

	var_delete(lookup);

	return 0;

error:
	if (lookup)
	{
		var_delete(lookup);
	}
	
	return -1;
}


static int
greylist_add(var_t *attrs, greylist_t *gl)
{
	var_t *record;
	char *hostaddr;
	char *envfrom;
	char *envrcpt;
	time_t now;
	VAR_INT_T created, updated, delay, visa, valid, retries, passed;
	
	if (var_table_dereference(attrs, "milter_hostaddr", &hostaddr,
		"milter_envfrom", &envfrom, "milter_envrcpt", &envrcpt, NULL))
	{
		log_error("greylist_lookup: var_table_dereference failed");
		return -1;
	}

	now = time(NULL);
	if (now == -1) {
		log_warning("greylist: time");
		return -1;
	}

	created = now;
	updated = now;
	delay = gl->gl_delay;
	visa = 0;
	valid = gl->gl_valid;
	retries = 1;
	passed = 0;

	record = var_scheme_refcopy(greylist_dbt.dbt_scheme, hostaddr,
		envfrom, envrcpt, &created, &updated, &valid, &delay, &retries,
		&visa, &passed);

	if (record == NULL) {
		log_warning("greylist_add: var_scheme_refcopy failed");
		return -1;
	}

	if (dbt_db_set(&greylist_dbt, record))
	{
		log_error("greylist_add: dbt_db_set failed");
		return -1;
	}
	
	var_delete(record);

	return 0;
}

greylist_response_t
greylist(var_t *attrs, greylist_t *gl)
{
	var_t *record;
	time_t now;
	VAR_INT_T *created;
	VAR_INT_T *updated;
	VAR_INT_T *delay;
	VAR_INT_T *retries;
	VAR_INT_T *visa;
	VAR_INT_T *passed;
	VAR_INT_T *valid;
	greylist_response_t glr = GL_DELAY;

	if (greylist_lookup(attrs, &record))
	{
		log_error("greylist: greylist_lookup failed");
		goto error;
	}

	if (record == NULL)
	{
		log_info("greylist: create new record");
		goto add;
	}

	if (var_list_dereference(record, NULL, NULL, NULL, &created, &updated,
	    &valid, &delay, &retries, &visa, &passed))
	{
		log_warning("greylist: var_list_unpack failed");
		goto error;
	}

	now = time(NULL);
	if (now == -1)
	{
		log_warning("greylist: time");
		goto error;
	}

	/*
	 * Record expired.
	 */
	if (*updated + *valid < now) {
		log_info("greylist: record expired %d seconds ago",
			now - *created - *valid);
		goto add;
	}

	/*
	 * Delay smaller than requested.
	 */
	if (*delay < gl->gl_delay) {
		log_info("greylist: record delay too small. Extension: %d"
			" seconds", gl->gl_delay);
		*delay = gl->gl_delay;
		*visa = 0;
		goto update;
	}

	/*
	 * Valid visa
	 */
	if (*visa) {
		log_info("greylist: valid visa found. expiry: %d seconds",
			*updated + *valid - now);
		*passed += 1;
		glr = GL_PASS;
		goto update;
	}

	/*
	 * Delay passed
	 */
	if (*created + *delay < now) {
		log_info("greylist: delay passed. create visa for %d seconds",
			gl->gl_visa);
		*visa = gl->gl_visa;
		*valid = gl->gl_visa;
		*passed = 1;
		glr = GL_PASS;
		goto update;
	}

	log_info("greylist: remaining delay: %d seconds retries: %d",
		*created + *delay - now, *retries);

	*retries += 1;


update:
	*updated = now;

	if (dbt_db_set(&greylist_dbt, record)) {
		log_warning("greylist: DBT_DB_SET failed");
		goto error;
	}

	if (record) {
		var_delete(record);
	}

	return glr;

add:

	if (greylist_add(attrs, gl)) {
		log_warning("greylist: greylist_add failed");
		goto error;
	}

	if (record) {
		var_delete(record);
	}

	return glr;

error:

	if (record) {
		var_delete(record);
	}

	return GL_ERROR;
}
