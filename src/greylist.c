#include "config.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mopher.h"

static dbt_t greylist_dbt;

greylist_t *
greylist_visa(greylist_t *gl, exp_t *visa)
{
	gl->gl_visa = visa;
	return gl;
}


greylist_t *
greylist_valid(greylist_t *gl, exp_t *valid)
{
	gl->gl_valid = valid;
	return gl;
}


greylist_t *
greylist_create(exp_t *delay)
{
	greylist_t *gl;

	gl = (greylist_t *) malloc(sizeof (greylist_t));
	if (gl == NULL)
	{
		log_die(EX_OSERR, "greylist_create: malloc");
	}

	memset(gl, 0, sizeof (greylist_t));

	gl->gl_delay = delay;

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
	VAR_INT_T i = -1;

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

	/*
	 * Register GREYLISTED
	 */
	acl_constant_register(VT_INT, "GREYLISTED", &i,
	    VF_KEEPNAME | VF_COPYDATA);

	return;
}


static int
greylist_add(var_t *mailspec, VAR_INT_T gl_delay, VAR_INT_T gl_valid,
    VAR_INT_T gl_visa)
{
	var_t *record;
	void *hostaddr;
	char *envfrom;
	char *envrcpt;
	time_t now;
	VAR_INT_T created, updated, delay, visa, valid, retries, passed;
	
	if (vtable_dereference(mailspec, "milter_hostaddr", &hostaddr,
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
	delay = gl_delay;
	visa = 0;
	valid = gl_valid;
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


static int
greylist_eval_int(exp_t *exp, var_t *mailspec, VAR_INT_T *i)
{
	var_t *result;
	VAR_INT_T *p;

	/*
	 * Set to zero in case something goes wrong
	 */
	*i = 0;

	result = exp_eval(exp, mailspec);
	if (result == NULL)
	{
		log_error("greylist_eval_int: exp_eval failed");
		return -1;
	}

	if (result->v_type != VT_INT)
	{
		log_error("greylist_eval: bad type");
		exp_free(result);
		return -1;
	}

	p = result->v_data;
	*i = *p;

	exp_free(result);

	return 0;
}


static int
greylist_eval(greylist_t *gl, var_t *mailspec, VAR_INT_T *delay,
    VAR_INT_T *valid, VAR_INT_T *visa)
{
	if (greylist_eval_int(gl->gl_delay, mailspec, delay))
	{
		log_error("greylist_eval: greylist_eval_int failed");
		return -1;
	}

	if (gl->gl_valid == NULL)
	{
		*valid = cf_greylist_valid;
	}
	else
	{
		if (greylist_eval_int(gl->gl_valid, mailspec, valid))
		{
			log_error("greylist_eval: greylist_eval_int failed");
			return -1;
		}
	}

	if (gl->gl_visa == NULL)
	{
		*visa = cf_greylist_visa;
	}
	else
	{
		if (greylist_eval_int(gl->gl_visa, mailspec, visa))
		{
			log_error("greylist_eval: greylist_eval_int failed");
			return -1;
		}
	}

	return 0;
}


acl_action_type_t
greylist(milter_stage_t stage, char *stagename, var_t *mailspec,
    greylist_t *gl)
{
	var_t *record = NULL;
	time_t now;
	VAR_INT_T *created;
	VAR_INT_T *updated;
	VAR_INT_T *delay;
	VAR_INT_T *retries;
	VAR_INT_T *visa;
	VAR_INT_T *passed;
	VAR_INT_T *valid;
	acl_action_type_t action = ACL_GREYLIST;
	VAR_INT_T gl_delay, gl_valid, gl_visa;

	/*
	 * Evaluate expressions
	 */
	if (greylist_eval(gl, mailspec, &gl_delay, &gl_valid, &gl_visa))
	{
		log_error("greylist: greylist_eval failed");
		goto error;
	}

	if (dbt_db_get_from_table(&greylist_dbt, mailspec, &record))
	{
		log_error("greylist: dbt_db_get_from_table failed");
		goto error;
	}

	/*
	 * Greylist GREYLISTED
	 */
	if (gl_delay == -1 && record == NULL)
	{
		log_debug("greylist: not greylisted yet: continue evaluation");
		return ACL_NONE;
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

	/*
	 * Greylist GREYLISTED
	 */
	if (gl_delay == -1)
	{
		gl_delay = *delay;
		gl_valid = *valid;
		gl_visa  = *visa;
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
	if (*delay < gl_delay) {
		log_info("greylist: record delay too small. Extension: %d"
			" seconds", gl_delay);
		*delay = gl_delay;
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
		    gl_visa);
		*visa = gl_visa;
		*valid = gl_visa;
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

	if (greylist_add(mailspec, gl_delay, gl_valid, gl_visa)) {
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
