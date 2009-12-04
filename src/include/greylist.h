#ifndef _GREYLIST_H_
#define _GREYLIST_H_

enum greylist_response
{
	GL_ERROR	= -1,
	GL_NULL		=  0,
	GL_PASS,
	GL_DELAY
};

typedef enum greylist_response greylist_response_t;

struct greylist {
	int gl_delay;
	int gl_visa;
	int gl_valid;
};

typedef struct greylist greylist_t;
	
/*
 * Prototypes
 */

greylist_t * greylist_delay(greylist_t *gl, int delay);
greylist_t * greylist_visa(greylist_t *gl, int visa);
greylist_t * greylist_valid(greylist_t *gl, int valid);
greylist_t * greylist_create(void);
void greylist_delete(greylist_t *gl);
void greylist_init(void);
void greylist_clear(void);
greylist_response_t greylist(var_t *attrs, greylist_t *gl);
#endif /* _GREYLIST_H_ */
