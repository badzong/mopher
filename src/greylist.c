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


void
greylist_init(void)
{
	var_t *scheme;

	scheme = vlist_scheme("greylist",
		"milter_hostaddr",	VT_ADDR,	VF_KEEPNAME | VF_KEY, 
		"milter_envfrom",	VT_STRING,	VF_KEEPNAME | VF_KEY,
		"milter_envrcpt",	VT_STRING,	VF_KEEPNAME | VF_KEY,
		"created",		VT_INT,		VF_KEEPNAME,
		"updated",		VT_INT,		VF_KEEPNAME,
		"valid",		VT_INT,		VF_KEEPNAME,
		"delay",		VT_INT,		VF_KEEPNAME,
		"retries",		VT_INT,		VF_KEEPNAME,
		"visa",			VT_INT,		VF_KEEPNAME,
		"passed",		VT_INT,		VF_KEEPNAME,
		NULL);

	if (scheme == NULL)
	{
		log_die(EX_SOFTWARE, "greylist_init: vlist_scheme failed");
	}

	greylist_dbt.dbt_scheme = scheme;
	greylist_dbt.dbt_validate = dbt_common_validate;
	greylist_dbt.dbt_sql_invalid_where = DBT_COMMON_INVALID_SQL;

	/*
	 * greylist_valid need to be registered at table
	 */
	dbt_register("greylist", &greylist_dbt);

	return;
}


static int
greylist_add(var_t *attrs, greylist_t *gl)
{
	var_t *record;
	void *hostaddr;
	char *envfrom;
	char *envrcpt;
	time_t now;
	VAR_INT_T created, updated, delay, visa, valid, retries, passed;
	
	if (vtable_dereference(attrs, "milter_hostaddr", &hostaddr,
	    "milter_envfrom", &envfrom, "milter_envrcpt", &envrcpt, NULL))
	{
		log_error("greylist_add: vtable_dereference failed");
		return -1;
	}

	now = time(NULL);
	if (now == -1) {
		log_warning("greylist_add: time");
		return -1;
	}

	created = now;
	updated = now;
	delay = gl->gl_delay;
	visa = 0;
	valid = gl->gl_valid;
	retries = 1;
	passed = 0;

	record = vlist_record(greylist_dbt.dbt_scheme, hostaddr, envfrom,
	    envrcpt, &created, &updated, &valid, &delay, &retries, &visa,
	    &passed);

	if (record == NULL) {
		log_warning("greylist_add: vlist_record failed");
		return -1;
	}

	if (dbt_db_set(&greylist_dbt, record))
	{
		log_error("greylist_add: dbt_db_set failed");
		var_delete(record);
		return -1;
	}
	
	var_delete(record);

	return 0;
}

acl_action_type_t
greylist(milter_stage_t stage, char *stagename, var_t *attrs, greylist_t *gl)
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
	acl_action_type_t action = ACL_GREYLIST;

	if (dbt_db_get_from_table(&greylist_dbt, attrs, &record))
	{
		log_error("greylist: dbt_db_get_from_table failed");
		goto error;
	}

	if (record == NULL)
	{
		log_info("greylist: create new record");
		goto add;
	}

	if (vlist_dereference(record, NULL, NULL, NULL, &created, &updated,
	    &valid, &delay, &retries, &visa, &passed))
	{
		log_warning("greylist: vlist_dereference failed");
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
		action = ACL_NONE;
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
		action = ACL_NONE;
		goto update;
	}

	*retries += 1;

	log_info("greylist: remaining delay: %d seconds retries: %d",
		*created + *delay - now, *retries);


update:
	*updated = now;

	if (dbt_db_set(&greylist_dbt, record)) {
		log_warning("greylist: DBT_DB_SET failed");
		goto error;
	}

	if (record) {
		var_delete(record);
	}

	return action;

add:

	if (greylist_add(attrs, gl)) {
		log_warning("greylist: greylist_add failed");
		goto error;
	}

	if (record) {
		var_delete(record);
	}

	return action;

error:

	if (record) {
		var_delete(record);
	}

	return ACL_ERROR;
}
