#ifndef _GREYLIST_H_
#define _GREYLIST_H_

#include <exp.h>

enum greylist_response
{
	GL_ERROR	= -1,
	GL_NULL		=  0,
	GL_PASS,
	GL_DELAY
};
typedef enum greylist_response greylist_response_t;

enum greylist_flag
{
	GLF_NULL	= 0,
	GLF_DEADLINE	= 1<<0,
	GLF_DELAY	= 1<<1,
	GLF_ATTEMPTS	= 1<<2,
	GLF_VISA	= 1<<3
};
typedef enum greylist_flag greylist_flag_t;

struct greylist {
	greylist_flag_t	 gl_flags;
	exp_t		*gl_deadline;
	exp_t		*gl_delay;
	exp_t		*gl_attempts;
	exp_t		*gl_visa;
};
typedef struct greylist greylist_t;


/*
 * Prototypes
 */

greylist_t * greylist_deadline(greylist_t *gl, exp_t *deadline);
greylist_t * greylist_delay(greylist_t *gl, exp_t *delay);
greylist_t * greylist_attempts(greylist_t *gl, exp_t *attempts);
greylist_t * greylist_visa(greylist_t *gl, exp_t *visa);
greylist_t * greylist_create(void);
void greylist_delete(greylist_t *gl);
void greylist_init(void);
acl_action_type_t greylist(milter_stage_t stage, char *stagename, var_t *mailspec, void *data, int depth);
int greylist_pass(char *source, char *envfrom, char *envrcpt);
int greylist_dump_record(dbt_t *dbt, var_t *record);
int greylist_dump(char **dump);

#endif /* _GREYLIST_H_ */
