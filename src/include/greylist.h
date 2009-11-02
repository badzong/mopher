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

void greylist_init(void);
void greylist_clear(void);
greylist_response_t greylist(var_t *attrs, greylist_t *ad);
#endif /* _GREYLIST_H_ */
