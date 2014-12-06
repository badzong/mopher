#include <stdlib.h>
#include <unistd.h>

#include <mopher.h>

#define TARPIT_SYMBOL "tarpit_delayed"

acl_action_type_t
tarpit(milter_stage_t stage, char *stagename, var_t *mailspec, void *data)
{
	exp_t *exp = data;
	VAR_INT_T delay;
	VAR_INT_T remaining;
	VAR_INT_T *stored_delay;
	int nap;
	var_t *v;

	SMFICTX *ctx;

	v = exp_eval(exp, mailspec);
	if (v == NULL)
	{
		log_error("tarpit: exp_eval failed");
		return ACL_ERROR;
	}

	if (v->v_type != VT_INT)
	{
		log_error("tarpit: bad delay type. Need integer.");
		return ACL_ERROR;
	}

	remaining = delay = * ((VAR_INT_T *) v->v_data);

	exp_free(v);

	if (delay <= 0)
	{
		log_debug("tarpit: delay=%d", delay);
		return ACL_NONE;
	}

	ctx = vtable_get(mailspec, "milter_ctx");
	if (ctx == NULL)
	{
		log_error("tarpit: milter_ctx not set");
		return ACL_ERROR;
	}

	log_message(LOG_ERR, mailspec, "tarpit: delay=%d", delay);

	for (;;)
	{
		nap = UTIL_MIN(remaining, cf_tarpit_progress_interval);
		remaining -= nap;

		/*
		 * Make sure we sleep at least nap seconds
		 */
		while ((nap = sleep(nap)));

		if (remaining <= 0)
		{
			break;
		}

		/*
		 * Notify MTA
		 */
		log_debug("tarpit: %d seconds remaining: report progress",
		    remaining);

		/*
		 * Happens if connection is dropped
		 */
		if (smfi_progress(ctx) != MI_SUCCESS)
		{
			log_message(LOG_ERR, mailspec,
				"tarpit: connection aborted");

			/*
			 * Do not continue ACL evaluation
			 */
			return ACL_TEMPFAIL;
		}
	}

	stored_delay = vtable_get(mailspec, TARPIT_SYMBOL);
	if (stored_delay == NULL)
	{
		if (vtable_set_new(mailspec, VT_INT, TARPIT_SYMBOL, &delay,
		    VF_KEEPNAME | VF_COPYDATA))
		{
			log_error("tarpit: vtable_set_new failed");
			return ACL_ERROR;
		}
	}
	else
	{
		*stored_delay += delay;
	}

	return ACL_NONE;
}


static int
tarpit_delay(milter_stage_t stage, char *name, var_t *mailspec)
{
	VAR_INT_T zero = 0;

	/*
	 * This function is only called, when tarpit_delay is not set (tarpit
	 * was never called)
	 */

	if (vtable_set_new(mailspec, VT_INT, TARPIT_SYMBOL, &zero,
	    VF_KEEPNAME | VF_COPYDATA))
	{
		log_error("tarpit_delay: vtable_set_new failed");
		return -1;
	}

	return 0;
}

void
tarpit_init(void)
{
	/*
	 * tarpit delay is changed each time tarpit is called -> AS_CACHE.
	 */
	acl_symbol_register(TARPIT_SYMBOL, MS_ANY, tarpit_delay, AS_CACHE);

	return;
}
