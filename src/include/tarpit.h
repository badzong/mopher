#ifndef _TARPIT_H_
#define _TARPIT_H_

/*
 * Prototypes
 */

acl_action_type_t tarpit(milter_stage_t stage, char *stagename, var_t *mailspec, void *data, int depth);
void tarpit_init(void);
#endif /* _TARPIT_H_ */
