#ifndef _TARPIT_H_
#define _TARPIT_H_

struct tarpit
{
	int tp_delay;
};

typedef struct tarpit tarpit_t;

/*
 * Prototypes
 */

tarpit_t * tarpit_create(void);
tarpit_t * tarpit_delay(tarpit_t *tp, int delay);
acl_action_type_t tarpit(var_t *mailspec, tarpit_t *tp);
#endif /* _TARPIT_H_ */
