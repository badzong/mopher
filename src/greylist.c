#include <config.h>

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mopher.h>

static dbt_t greylist_dbt;

/*
 * Greylist symbols and db translation
 */
static char *greylist_tuple_symbols[] = { "greylist_created",
    "greylist_updated", "greylist_valid", "greylist_delay", "greylist_retries",
    "greylist_visa", "greylist_passed", NULL };


int
greylist_source(char *buffer, int size, char *hostname, char *hostaddr)
{
	char *p;
	int len;

	/*
	 * Save only the domainname in the greylist tuple. Useful for
	 * greylisting mail farms.
	 */
	p = strchr(hostname, '.');
	if (p == NULL)
	{
		p = hostaddr;
	}
	else
	{
		++p;
	}

	len = strlen(p);
	if (len >= size)
	{
		log_error("greylist_hostid: buffer exhausted");
		return -1;
	}

	strcpy(buffer, p);

	return len;
}


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
greylist_delay(greylist_t *gl, exp_t *delay)
{
	gl->gl_delay = delay;
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

	memset(gl, 0, sizeof (greylist_t));

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
	char **p;

	scheme = vlist_scheme("greylist",
		"milter_greylist_src",	VT_STRING,	VF_KEEPNAME | VF_KEY, 
		"milter_envfrom_addr",	VT_STRING,	VF_KEEPNAME | VF_KEY,
		"milter_envrcpt_addr",	VT_STRING,	VF_KEEPNAME | VF_KEY,
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
greylist_add(char *source, char *envfrom, char *envrcpt, VAR_INT_T received,
    VAR_INT_T delay, VAR_INT_T valid, VAR_INT_T visa)
{
	var_t *record;
	VAR_INT_T retries = 0;
	VAR_INT_T passed = 0;
	
	/*
	 * Use default values if not set.
	 */
	if (delay == 0)
	{
		delay = cf_greylist_delay;
	}
	if (valid == 0)
	{
		valid = cf_greylist_valid;
	}
	if (visa == 0)
	{
		visa = cf_greylist_visa;
	}

	/*
	 * Create and store record
	 */
	record = vlist_record(greylist_dbt.dbt_scheme, source, envfrom,
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

	return 1;
}


static int
greylist_eval(greylist_t *gl, var_t *mailspec, VAR_INT_T *delay,
    VAR_INT_T *valid, VAR_INT_T *visa)
{
	/*
	 * Arrays are used to loop over delay, valid and visa
	 */
	VAR_INT_T *ip[] = { delay, valid, visa, NULL };
	exp_t *ep[] = { gl->gl_delay, gl->gl_valid, gl->gl_visa, NULL };
	VAR_INT_T **i;
	exp_t **e;
	var_t *v;

	/*
	 * Set all values to zero
	 */
	*delay = *valid = *visa = 0;

	for (i = ip, e = ep; *i; ++i, ++e)
	{
		/*
		 * No expression set (*i = 0)
		 */
		if (*e == NULL)
		{
			continue;
		}

		v = exp_eval(*e, mailspec);
		if (v == NULL)
		{
			log_error("grelist_eval: exp_eval failed");
			return -1;
		}

		/*
		 * Dereferencing double pointer
		 */
		**i = var_intval(v);

		exp_free(v);
	}

	return 0;
}


static int
greylist_recipient(var_t *mailspec, VAR_INT_T *delayed, char *source,
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
	lookup = vlist_record(greylist_dbt.dbt_scheme, source, envfrom,
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
	 * Dont't greylist if no record exists and no delay was specified
	 */
	if (record == NULL && delay == 0)
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
	 * Record expired. After the greylist period, valid becomes visa!
	 */
	if (*rec_updated + *rec_valid < received)
	{
		log_message(LOG_DEBUG, mailspec,
		    "greylist: status=expired, expiry=%d seconds ago",
		    received - *rec_created - *rec_valid);

		goto add;
	}

	/*
	 * Update requested properties
	 */
	if (delay && *rec_delay != delay)
	{
		log_message(LOG_ERR, mailspec, "greylist: status=subst, orig "
		    "delay=%d seconds, new delay=%d", *rec_delay, delay);

		*rec_delay = delay;
	}
	if (valid && *rec_valid != valid)
	{
		log_message(LOG_ERR, mailspec, "greylist: status=subst, orig "
		    "valid=%d seconds, new valid=%d seconds", *rec_valid,
		    valid);

		*rec_valid = valid;
	}
	if (visa && *rec_visa != visa)
	{
		log_message(LOG_ERR, mailspec, "greylist: status=subst, orig "
		    "visa=%d seconds, new visa=%d seconds", *rec_visa, visa);

		*rec_visa = visa;
	}

	/*
	 * Delay passed
	 */
	if (*rec_created + *rec_delay < received)
	{
		*rec_valid = *rec_visa;
		*rec_passed += 1;
		defer = 0;

		/*
		 * Set delayed for the first message (set only here!)
		 */
		if (*rec_passed == 1)
		{
			*delayed = received - *rec_created;
		}

		log_message(LOG_ERR, mailspec, "greylist: status=passed, "
		    "delay=%d(%d) seconds, visa=%d seconds", 
		    received - *rec_created, *rec_delay, *rec_visa);

		goto update;
	}

	/*
	 * Greylisting in action
	 */
	*rec_retries += 1;
	defer = 1;

	log_message(LOG_ERR, mailspec, "greylist: status=defer delay=%d "
	    "seconds, remaining=%d seconds, retries=%d",  *rec_delay,
	    *rec_created + *rec_delay - received, *rec_retries);


update:
	*rec_updated = received;

	if (dbt_db_set(&greylist_dbt, record))
	{
		log_error("greylist: dbt_db_set failed");
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

	log_message(LOG_ERR, mailspec, "greylist: status=defer, delay=%d "
	    "seconds", delay);

	return greylist_add(source, envfrom, envrcpt, received,
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
	char *source;
	char *envfrom;
	char *envrcpt;
	ll_t *recipients;
	ll_entry_t *pos;
	var_t *vrcpt;
	VAR_INT_T *received;
	VAR_INT_T delay;
	VAR_INT_T valid;
	VAR_INT_T visa;
	VAR_INT_T delayed;
	VAR_INT_T max_delay = 0;
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
	 * Get source, enfrom, envrcpt, recipients and received
	 */
	if (vtable_dereference(mailspec, "milter_greylist_src", &source,
	    "milter_envfrom_addr", &envfrom, "milter_envrcpt_addr", &envrcpt,
	    "milter_recipient_list", &recipients, "milter_received", &received,
	    NULL) < 4)
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
		defer = greylist_recipient(mailspec, &max_delay, source,
		    envfrom, envrcpt, *received, delay, valid, visa);
	}
	else
	{
		pos = LL_START(recipients);
		while ((vrcpt = ll_next(recipients, &pos)))
		{
			/*
			 * vlist stores var_t pointers!
			 */
			envrcpt = vrcpt->v_data;

			r = greylist_recipient(mailspec, &delayed, source,
			    envfrom, envrcpt, *received, delay, valid, visa);

			if (r == -1)
			{
				break;
			}

			if (delayed > max_delay)
			{
				max_delay = delayed;
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
		log_message(LOG_DEBUG, mailspec, "greylist: delayed "
		    "recipients=%d", defer);

		return ACL_GREYLIST;
	}

	/*
	 * Set greylist delayed
	 */
	if (max_delay)
	{
		if (vtable_set_new(mailspec, VT_INT, "greylist_delayed",
		    &max_delay, VF_KEEPNAME | VF_COPYDATA))
		{
			log_error("greylist: vtables_set_new failed");
			return -1;
		}
	}

	return ACL_NONE;
}
