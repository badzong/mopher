#ifndef _GREYLIST_H_
#define _GREYLIST_H_

#include "exp.h"

enum greylist_response
{
	GL_ERROR	= -1,
	GL_NULL		=  0,
	GL_PASS,
	GL_DELAY
};

typedef enum greylist_response greylist_response_t;

struct greylist {
	exp_t *gl_delay;
	exp_t *gl_visa;
	exp_t *gl_valid;
};

typedef struct greylist greylist_t;
	
/*
 * Prototypes
 */

greylist_t * greylist_visa(greylist_t *gl, exp_t *visa);
greylist_t * greylist_valid(greylist_t *gl, exp_t *valid);
greylist_t * greylist_delay(greylist_t *gl, exp_t *delay);
greylist_t * greylist_create(void);
void greylist_delete(greylist_t *gl);
void greylist_init(void);
acl_action_type_t greylist(milter_stage_t stage, char *stagename, var_t *mailspec, void *data);
#endif /* _GREYLIST_H_ */
