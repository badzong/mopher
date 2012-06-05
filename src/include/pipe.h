#ifndef _PIPE_H_
#define _PIPE_H_

/*
 * Prototypes
 */

acl_action_type_t pipe_action(milter_stage_t stage, char *stagename, var_t *mailspec, void *data);

#endif /* _PIPE_H_ */
