#include <stdlib.h>
#include <unistd.h>

#include "mopher.h"


acl_action_type_t
tarpit(milter_stage_t stage, char *stagename, var_t *mailspec, exp_t *exp)
{
	int delay;
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

	delay = * ((VAR_INT_T *) v->v_data);

	exp_free(v);

	ctx = vtable_get(mailspec, "milter_ctx");
	if (ctx == NULL)
	{
		log_error("tarpit: milter_ctx not set");
		return ACL_ERROR;
	}

	log_debug("tarpit: delay %d seconds", delay);

	for (;;)
	{
		nap = UTIL_MIN(delay, cf_tarpit_progress_interval);
		delay -= nap;

		/*
		 * Make sure we sleep at least nap seconds
		 */
		while ((nap = sleep(nap)));

		if (delay == 0)
		{
			break;
		}

		/*
		 * Notify MTA
		 */
		log_debug("tarpit: %d seconds remaining: report progress",
		    delay);

		if (smfi_progress(ctx) != MI_SUCCESS)
		{
			log_debug("tarpit: smfi_progress failed");
			return ACL_ERROR;
		}
	}

	return ACL_NONE;
}
