#include <stdlib.h>
#include <unistd.h>


#include "mopher.h"

tarpit_t *
tarpit_create(void)
{
	tarpit_t *tp;

	tp = (tarpit_t *) malloc(sizeof (tarpit_t));
	if (tp == NULL)
	{
		log_die(EX_OSERR, "tarpit_create: malloc");
	}

	tp->tp_delay = cf_tarpit_delay;

	return tp;
}


tarpit_t *
tarpit_delay(tarpit_t *tp, int delay)
{
	tp->tp_delay = delay;

	return tp;
}


acl_action_type_t
tarpit(var_t *mailspec, tarpit_t *tp)
{
	log_debug("tarpit: delay %d seconds", tp->tp_delay);

	sleep(tp->tp_delay);

	return ACL_NONE;
}
