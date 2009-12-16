#include "config.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mopher.h"

static dbt_t greylist_dbt;

/*
 * Greylist symbols and db translation
 */
static char *greylist_tuple_symbols[] = { "greylist_created", "greylist_updated",
    "greylist_valid", "greylist_delay", "greylist_retries", "greylist_visa",
    "greylist_passed", NULL };

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


static int
greylist_delayed(milter_stage_t stage, char *name, var_t *mailspec)
{
	/*
	 * greylist_delayed is set by greylist. if greylist() isn't called,
	 * greylist_delayed is null.
	 */

	if (vtable_set_new(mailspec, VT_INT, name, NULL, VF_COPYNAME))
	{
		log_error("greylist_delayed: vtable_set_new failed");
		return -1;
	}

	return 0;
}


static int
greylist_listed(milter_stage_t stage, char *name, var_t *mailspec)
{
	var_t *record;
	VAR_INT_T i = 0, *p = &i;
	VAR_INT_T *recipients;

	/*
	 * greylist_listed is amibuous for multi recipient messages in stages
	 * other than MS_ENVRCPT.
	 */
	if (stage == MS_ENVFROM)
	{
		goto load;
	}

	recipients = vtable_get(mailspec, "milter_recipients");
	if (recipients == NULL)
	{
		log_error("greylist_listed: vtable_get failed");
		return -1;
	}

	if (*recipients == 1)
	{
		goto load;
	}

	/*
	 * In a multi recipient message the greylist symbols are set to zero
	 */
	log_error("greylist_listed: message has %ld recipients: symbol "
	    "\"%s\" abiguous", *recipients, name);

	p = NULL;

	goto exit;
		

load:

	if (dbt_db_get_from_table(&greylist_dbt, mailspec, &record))
	{
		log_error("greylist_listed_delayed: dbt_db_get_from_table "
		    "failed");
		return -1;
	}

	if (record)
	{
		i = 1;
		var_delete(record);
	}

exit:

	if (vtable_set_new(mailspec, VT_INT, name, p, VF_COPY))
	{
		log_error("greylist_listed: vtable_set_new failed");
		return -1;
	}

	return 0;
}


static int
greylist_tuple_symbol(milter_stage_t stage, char *name, var_t *mailspec)
{
	VAR_INT_T *recipients;

	if (stage == MS_ENVRCPT)
	{
		goto load;
	}

	recipients = vtable_get(mailspec, "milter_recipients");
	if (recipients == NULL)
	{
		log_error("greylist_tuple_symbol: vtable_get failed");
		return -1;
	}

	if (*recipients == 1)
	{
		goto load;
	}

	/*
	 * In a multi recipient message the greylist symbols are set to zero
	 */
	log_error("greylist_tuple_load: message has %ld recipients: symbol "
	    "\"%s\" abiguous", *recipients, name);

	if (vtable_set_new(mailspec, VT_INT, name, NULL, VF_COPYNAME))
	{
		log_error("greylist_tuple_symbol: vtable_set_new failed");
		return -1;
	}

	return 0;

load:
	/*
	 * Load greylist tuple if we are in envrcpt or if the massage has only
	 * 1 recipient.
	 */
	if (dbt_db_load_into_table(&greylist_dbt, mailspec))
	{
		log_error("greylist_tuple_symbol: dbt_db_load_into_table "
		    "failed");

		return -1;
	}

	return 0;
}


void
greylist_init(void)
{
	var_t *scheme;
	VAR_INT_T i = -1;
	char **p;

	scheme = vlist_scheme("greylist",
		"milter_hostaddr",	VT_ADDR,	VF_KEEPNAME | VF_KEY, 
		"milter_envfrom",	VT_STRING,	VF_KEEPNAME | VF_KEY,
		"milter_envrcpt",	VT_STRING,	VF_KEEPNAME | VF_KEY,
		"greylist_created",	VT_INT,		VF_KEEPNAME,
		"greylist_updated",	VT_INT,		VF_KEEPNAME,
		"greylist_valid",	VT_INT,		VF_KEEPNAME,
		"greylist_delay",	VT_INT,		VF_KEEPNAME,
		"greylist_retries",	VT_INT,		VF_KEEPNAME,
		"greylist_visa",	VT_INT,		VF_KEEPNAME,
		"greylist_passed",	VT_INT,		VF_KEEPNAME,
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

	/*
	 * Register symbols. Symbols are not cached due to abiguity.
	 */
	for (p = greylist_tuple_symbols; *p; ++p)
	{
		acl_symbol_register(*p, MS_OFF_ENVFROM, greylist_tuple_symbol,
		    AS_NOCACHE);
	}

	acl_symbol_register("greylist_listed", MS_OFF_ENVFROM, greylist_listed,
	    AS_NOCACHE);

	acl_symbol_register("greylist_delayed", MS_OFF_ENVFROM,
	    greylist_delayed, AS_CACHE);

	return;
}


static int
greylist_add(struct sockaddr_storage *hostaddr, char *envfrom, char *envrcpt,
    VAR_INT_T received, VAR_INT_T delay, VAR_INT_T valid, VAR_INT_T visa)
{
	var_t *record;
	VAR_INT_T retries = 1;
	VAR_INT_T passed = 0;
	
	record = vlist_record(greylist_dbt.dbt_scheme, hostaddr, envfrom,
	    envrcpt, &received, &received, &valid, &delay, &retries, &visa,
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

	log_info("greylist_add: from: %s to: %s: delay: %d", envfrom, envrcpt,
	    delay);

	return 1;
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


static int
greylist_recipient(VAR_INT_T *delayed, struct sockaddr_storage *hostaddr,
    char *envfrom, char *envrcpt, VAR_INT_T received, VAR_INT_T delay,
    VAR_INT_T valid, VAR_INT_T visa)
{
	var_t *lookup = NULL, *record = NULL;
	VAR_INT_T *rec_created;
	VAR_INT_T *rec_updated;
	VAR_INT_T *rec_delay;
	VAR_INT_T *rec_retries;
	VAR_INT_T *rec_visa;
	VAR_INT_T *rec_passed;
	VAR_INT_T *rec_valid;
	int defer;

	/*
	 * Set delayed to zero
	 */
	*delayed = 0;

	/*
	 * Lookup greylist record
	 */
	lookup = vlist_record(greylist_dbt.dbt_scheme, hostaddr, envfrom,
	    envrcpt, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

	if (lookup == NULL)
	{
		log_error("greylist_recipient: vlist_record failed");
		goto error;
	}

	if (dbt_db_get(&greylist_dbt, lookup, &record))
	{
		log_error("greylist_recipient: dbt_db_get failed");
		goto error;
	}

	var_delete(lookup);
	lookup = NULL;

	/*
	 * Dont't greylist if no record exists (GREYLISTED)
	 */
	if (record == NULL && delay == -1)
	{
		return 0;
	}

	/*
	 * No record found: create new record
	 */
	if (record == NULL)
	{
		goto add;
	}

	/*
	 * Get record properties
	 */
	if (vlist_dereference(record, NULL, NULL, NULL, &rec_created,
	    &rec_updated, &rec_valid, &rec_delay, &rec_retries, &rec_visa,
	    &rec_passed))
	{
		log_error("greylist_recipient: vlist_dereference failed");
		goto error;
	}

	/*
	 * Greylist if record exists
	 */
	if (delay == -1)
	{
		delay = *rec_delay;
		valid = *rec_valid;
		visa  = *rec_visa;
	}

	/*
	 * Record expired.
	 */
	if (*rec_updated + *rec_valid < received)
	{
		log_info("greylist: from: %s to: %s: record expired %d seconds "
		    "ago", envfrom, envrcpt,
		    received - *rec_created - *rec_valid);

		goto add;
	}

	/*
	 * Delay smaller than requested.
	 */
	if (*rec_delay < delay)
	{
		log_info("greylist: from: %s to: %s: record delay too small: "
		    "extended to %d seconds", envfrom, envrcpt, delay);

		*rec_delay = delay;
		*rec_passed = 0;
		defer = 1;

		goto update;
	}

	/*
	 * Valid visa
	 */
	if (*rec_passed > 0)
	{
		log_info("greylist: from: %s to: %s: valid visa found. "
		    "expiry: %d seconds", envfrom, envrcpt,
		    *rec_updated + *rec_valid - received);

		*rec_passed += 1;
		defer = 0;

		goto update;
	}

	/*
	 * Delay passed
	 */
	if (*rec_created + *rec_delay < received)
	{
		log_info("greylist: from: %s to: %s: delay passed. create "
		    "visa for %d seconds", envfrom, envrcpt, visa);

		*rec_visa = visa;
		*rec_valid = visa;
		*rec_passed = 1;
		defer = 0;

		/*
		 * Set delayed
		 */
		*delayed = received - *rec_created;

		goto update;
	}

	/*
	 * Greylisting in action
	 */
	*rec_retries += 1;
	defer = 1;

	log_info("greylist: from: %s to: %s: remaining delay: %d seconds "
	    "retries: %d", envfrom, envrcpt,
	    *rec_created + *rec_delay - received, *rec_retries);


update:
	*rec_updated = received;

	if (dbt_db_set(&greylist_dbt, record))
	{
		log_error("greylist: DBT_DB_SET failed");
		goto error;
	}

	if (record)
	{
		var_delete(record);
	}

	return defer;

add:
	if (record) {
		var_delete(record);
	}

	return greylist_add(hostaddr, envfrom, envrcpt, received,
	    delay, valid, visa);

error:

	if (lookup)
	{
		var_delete(lookup);
	}

	if (record)
	{
		var_delete(record);
	}

	return -1;
}


acl_action_type_t
greylist(milter_stage_t stage, char *stagename, var_t *mailspec, void *data)
{
	greylist_t *gl = data;
	struct sockaddr_storage *hostaddr;
	char *envfrom;
	char *envrcpt;
	ll_t *recipients;
	var_t *vrcpt;
	VAR_INT_T *received;
	VAR_INT_T delay;
	VAR_INT_T valid;
	VAR_INT_T visa;
	VAR_INT_T delayed;
	VAR_INT_T min_delayed = 0;
	int defer = 0, r;

	/*
	 * Evaluate expressions
	 */
	if (greylist_eval(gl, mailspec, &delay, &valid, &visa))
	{
		log_error("greylist: greylist_eval failed");
		return ACL_ERROR;
	}

	/*
	 * Get hostaddr, enfrom, envrcpt, recipients and received
	 */
	if (vtable_dereference(mailspec, "milter_hostaddr", &hostaddr,
	    "milter_envfrom", &envfrom, "milter_envrcpt", &envrcpt,
	    "milter_recipient_list", &recipients, "milter_received", &received,
	    NULL))
	{
		log_error("greylist: vtable_dereference failed");
		return ACL_ERROR;
	}

	/*
	 * Check stage: In envrcpt we only need to check envrcpt; in all other
	 * stages we need to check all recipients.
	 */
	if (stage == MS_ENVRCPT)
	{
		defer = greylist_recipient(&min_delayed, hostaddr, envfrom,
		    envrcpt, *received, delay, valid, visa);
	}
	else
	{
		ll_rewind(recipients);
		while ((vrcpt = ll_next(recipients)))
		{
			/*
			 * vlist stores var_t pointers!
			 */
			envrcpt = vrcpt->v_data;

			r = greylist_recipient(&delayed, hostaddr, envfrom,
			    envrcpt, *received, delay, valid, visa);

			if (r == -1)
			{
				break;
			}

			if (delayed && min_delayed == 0)
			{
				min_delayed = delayed;
			}
			else if (delayed && delayed < min_delayed)
			{
				min_delayed = delayed;
			}

			defer += r;
		}
	}

	if (defer == -1)
	{
		log_error("greylist: greylist_recipient failed");
		return ACL_ERROR;
	}

	if (defer)
	{
		log_debug("greylist: %d recipients need greylisting", defer);
		return ACL_GREYLIST;
	}

	/*
	 * Set greylist delayed
	 */
	if (min_delayed)
	{
		if (vtable_set_new(mailspec, VT_INT, "greylist_delayed",
		    &min_delayed, VF_KEEPNAME | VF_COPYDATA))
		{
			log_error("greylist: vtables_set_new failed");
			return -1;
		}
	}

	return ACL_NONE;
}
