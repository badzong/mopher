#ifndef _GREYLIST_H_
#define _GREYLIST_H_

typedef enum greylist_response { GL_ERROR = -1, GL_NULL = 0, GL_PASS,
	GL_DELAY } greylist_response_t;

/*
 * Prototypes
 */

void greylist_init(void);
greylist_response_t greylist(var_t *attrs, acl_delay_t *ad);

#endif /* _GREYLIST_H_ */
