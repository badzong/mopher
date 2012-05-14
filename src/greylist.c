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
    "greylist_updated", "greylist_expire", "greylist_connections",
    "greylist_deadline", "greylist_delay", "greylist_attempts",
    "greylist_visa", "greylist_passed",
    NULL };


int
greylist_source(char *buffer, int size, char *hostname, char *hostaddr)
{
	char *p;
	int len;

	/*
	 * Save only the domainname in the greylist tuple. Useful for
	 * greylisting mail farms.
	 */
	if (strncmp(hostname + 1, hostaddr, strlen(hostaddr)) == 0)
	{
		p = hostaddr;
	}
	else
	{
		p = strchr(hostname, '.');
		if (p == NULL)
		{
			p = hostaddr;
		}
		else
		{
			++p;
		}
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
greylist_deadline(greylist_t *gl, exp_t *deadline)
{
	gl->gl_flags |= GLF_DEADLINE;
	gl->gl_deadline = deadline;
	return gl;
}


greylist_t *
greylist_delay(greylist_t *gl, exp_t *delay)
{
	gl->gl_flags |= GLF_DELAY;
	gl->gl_delay = delay;
	return gl;
}


greylist_t *
greylist_attempts(greylist_t *gl, exp_t *attempts)
{
	gl->gl_flags |= GLF_ATTEMPTS;
	gl->gl_attempts = attempts;
	return gl;
}


greylist_t *
greylist_visa(greylist_t *gl, exp_t *visa)
{
	gl->gl_flags |= GLF_VISA;
	gl->gl_visa = visa;
	return gl;
}


greylist_t *
greylist_create(void)
{
	greylist_t *gl;

	gl = (greylist_t *) malloc(sizeof (greylist_t));
	if (gl == NULL)
	{
		log_sys_die(EX_OSERR, "greylist_create: malloc");
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
	 * greylist_listed is amibguous for multi recipient messages in stages
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
	    "\"%s\" ambiguous", *recipients, name);

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
		"greylist_expire",	VT_INT,		VF_KEEPNAME,
		"greylist_connections",	VT_INT,		VF_KEEPNAME,
		"greylist_deadline",	VT_INT,		VF_KEEPNAME,
		"greylist_delay",	VT_INT,		VF_KEEPNAME,
		"greylist_attempts",	VT_INT,		VF_KEEPNAME,
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
	 * register greylist table
	 */
	dbt_register("greylist", &greylist_dbt);

	/*
	 * Register symbols. Symbols are not cached due to abiguity.
	 */
	for (p = greylist_tuple_symbols; *p; ++p)
	{
		acl_symbol_register(*p, MS_OFF_ENVFROM, greylist_tuple_symbol,
		    AS_CACHE);
	}

	acl_symbol_register("greylist_listed", MS_OFF_ENVFROM, greylist_listed,
	    AS_NOCACHE);

	acl_symbol_register("greylist_delayed", MS_OFF_ENVFROM,
	    greylist_delayed, AS_CACHE);

	return;
}


static int
greylist_eval(exp_t *exp, var_t *mailspec, VAR_INT_T *result)
{
	var_t *v = NULL;
	VAR_INT_T *i;

	*result = 0;

	if (exp == NULL)
	{
		return 0;
	}

	v = exp_eval(exp, mailspec);
	if (v == NULL)
	{
		log_error("greylist_eval: exp_eval failed");
		goto error;
	}

	if (v->v_data == NULL)
	{
		log_error("greylist_eval: exp_eval returned no data");
		goto error;
	}

	if (v->v_type != VT_INT)
	{
		log_error("greylist_eval: bad type");
		goto error;
	}
		
	i = v->v_data;
	*result = *i;

	exp_free(v);

	return 0;


error:

	if (v)
	{
		exp_free(v);
	}

	return -1;
}


static int
greylist_properties(greylist_t *gl, var_t *mailspec, VAR_INT_T *created,
    VAR_INT_T *expire, VAR_INT_T *deadline, VAR_INT_T *delay,
    VAR_INT_T *attempts, VAR_INT_T *visa)
{
	static const greylist_flag_t flag[] = { GLF_DEADLINE, GLF_DELAY,
	    GLF_ATTEMPTS, GLF_VISA, GLF_NULL };
	exp_t *exp[] = { gl->gl_deadline, gl->gl_delay, gl->gl_attempts,
	    gl->gl_visa };
	VAR_INT_T *result[] = { deadline, delay, attempts, visa };
	int i;

	for (i = 0; flag[i]; ++i)
	{
		if ((gl->gl_flags & flag[i]) == 0)
		{
			continue;
		}

		if (greylist_eval(exp[i], mailspec, result[i]))
		{
			log_error("greylist_properties: greylist_eval failed");
			return -1;
		}
	}

	if (gl->gl_flags & GLF_DEADLINE)
	{
		*expire = *created + *deadline;
	}

	return 0;
}


static int
greylist_tuple_counted(var_t *mailspec, char *haystack, char *needle)
{
	ll_t *list;
	ll_entry_t *pos;
	var_t *match;

	list = vtable_get(mailspec, haystack);
	if (list == NULL)
	{
		goto append;
	}

	pos = LL_START(list);
	while ((match = ll_next(list, &pos)))
	{
		if (match->v_type != VT_STRING)
		{
			log_error("greylist_tuple_counted: bad type");
			return -1;
		}
		
		if (strcmp(needle, match->v_data) == 0)
		{
			return 1;
		}
	}

append:

	if (vtable_list_append_new(mailspec, VT_STRING, haystack, needle,
	    VF_KEEPNAME | VF_COPYDATA))
	{
		log_error("greylist_tuple_counted: "
		    "vtable_list_append_new failed");
		return -1;
	}

	return 0;
}


static int
greylist_add(greylist_t *gl, var_t *mailspec, char *source, char *envfrom,
    char *envrcpt, VAR_INT_T received)
{
	var_t *record;
	VAR_INT_T created = received;
	VAR_INT_T updated = received;
	VAR_INT_T expire = 0;
	VAR_INT_T connections = 1;
	VAR_INT_T deadline = 0;
	VAR_INT_T delay = 0;
	VAR_INT_T attempts = 0;
	VAR_INT_T visa = 0;
	VAR_INT_T passed = 0;
	
	if (greylist_properties(gl, mailspec, &created, &expire, &deadline,
	    &delay, &attempts, &visa))
	{
		log_error("greylist_add: greylist_properties failed");
		return -1;
	}

	/*
	 * Use default values if not set.
	 */
	if (deadline == 0)
	{
		expire = cf_greylist_deadline;
	}
	if (visa == 0)
	{
		visa = cf_greylist_visa;
	}

	expire = received + deadline;

	/*
	 * Create and store record
	 */
	record = vlist_record(greylist_dbt.dbt_scheme, source, envfrom,
	    envrcpt, &created, &updated, &expire, &connections, &deadline,
	    &delay, &attempts, &visa, &passed);

	if (record == NULL)
	{
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

	log_message(LOG_ERR, mailspec, "greylist: status=defer, delay=0/%ld, "
	    "attempts=1/%ld", delay, attempts);

	return 1;
}


static int
greylist_recipient(greylist_t * gl, VAR_INT_T *delayed, var_t *mailspec,
    char *source, char *envfrom, char *envrcpt, VAR_INT_T received)
{
	var_t *lookup = NULL, *record = NULL;
	VAR_INT_T *created;
	VAR_INT_T *updated;
	VAR_INT_T *expire;
	VAR_INT_T *connections;
	VAR_INT_T *deadline;
	VAR_INT_T *delay;
	VAR_INT_T *attempts;
	VAR_INT_T *visa;
	VAR_INT_T *passed;
	VAR_INT_T defer = 1;
	int passed_delay;

	*delayed = 0;

	/*
	 * Lookup greylist record
	 */
	lookup = vlist_record(greylist_dbt.dbt_scheme, source, envfrom,
	    envrcpt, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

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
	 * Dont't greylist if no record exists and gl is empty
	 * Empty gl means: greylist existing tuple.
	 */
	if (record == NULL && gl->gl_flags == GLF_NULL)
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
	 * Dereference record
	 */
	if (vlist_dereference(record, NULL, NULL, NULL, &created, &updated,
	    &expire, &connections, &deadline, &delay, &attempts, &visa,
	    &passed))
	{
		log_error("greylist_recipient: vlist_dereference failed");
		goto error;
	}

	log_message(LOG_DEBUG, mailspec, "greylist: status=record, "
	    "connections=%ld, deadline=%ld, delay=%ld, attempts=%ld, "
	    "visa=%ld, passed=%ld", *connections, *deadline, *delay,
	    *attempts, *visa, *passed);

	/*
	 * Record expired.
	 */
	if (*expire < received)
	{
		log_message(LOG_DEBUG, mailspec,
		    "greylist: status=expired, late=%ld",
		    received - *expire);

		var_delete(record);

		goto add;
	}

	/*
	 * Update requested properties
	 */
	if (greylist_properties(gl, mailspec, created, expire, deadline,
	    delay, attempts, visa))
	{
		log_error("greylist_recipient: greylist_properties failed");
		goto error;
	}

	/*
	 * Increment the connection counter only once per connection.
	 */
	if (!greylist_tuple_counted(mailspec, "greylist_connection_counted",
	    envrcpt))
	{
		++(*connections);
	}

	passed_delay = received - *created;

	/*
	 * Greylisting passed (delay and attempts).
	 */
	if (passed_delay > *delay && *connections > *attempts)
	{
		*expire = received + *visa;
		defer = 0;

		if (!greylist_tuple_counted(mailspec,
		    "greylist_passed_counted", envrcpt))
		{
			++(*passed);
		}

		log_message(LOG_ERR, mailspec, "greylist: status=passed, "
		    "delay=%ld/%ld, attempts=%ld/%ld, visa=%ld, passed=%ld",
		    passed_delay, *delay, *connections, *attempts, *visa,
		    *passed);

		/*
		 * Set delayed for the first message (set only here!)
		 */
		if (*passed == 1)
		{
			*delayed = passed_delay;
		}

		goto update;
	}

	/*
	 * Greylisting in action
	 */
	*expire = *created + *deadline;
	*passed = 0;

	log_message(LOG_ERR, mailspec, "greylist: status=defer, delay=%ld/%ld,"
	    " attempts=%ld/%ld", passed_delay, *delay, *connections,
	    *attempts);


update:

	*updated = received;

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

	return greylist_add(gl, mailspec, source, envfrom, envrcpt, received);


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
	VAR_INT_T *received;
	var_t *rcpt;
	int defer;
	VAR_INT_T delay = 0, max_delay = 0;


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
		defer = greylist_recipient(gl, &max_delay, mailspec, source,
		    envfrom, envrcpt, *received);
	}

	else
	{
		pos = LL_START(recipients);
		while ((rcpt = ll_next(recipients, &pos)))
		{
			/*
			 * vlist stores var_t pointers!
			 */
			envrcpt = rcpt->v_data;
			defer = greylist_recipient(gl, &delay, mailspec,
			    source, envfrom, envrcpt, *received);

			/*
			 * We are in message context! Greylist if any recipient
			 * requires to. Errors are checked below.
			 */
			if (defer)
			{
				break;
			}

			if (delay > max_delay)
			{
				max_delay = delay;
			}
		}
	}

	if (defer == -1)
	{
		log_error("greylist: greylist_recipient failed");
		return ACL_ERROR;
	}

	if (defer)
	{
		return ACL_GREYLIST;
	}

	if (max_delay == 0)
	{
		return ACL_NONE;
	}

	/*
	 * Set greylist_delayed if message just passed greylisting
	 */
	if (vtable_set_new(mailspec, VT_INT, "greylist_delayed", &max_delay,
	    VF_KEEPNAME | VF_COPYDATA))
	{
		log_error("greylist: vtable_set_new failed");
	}

	return ACL_NONE;
}
