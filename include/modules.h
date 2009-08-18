#ifndef _MODULES_H_
#define _MODULES_H_

/*
 * Prototypes
 */

void * modules_open(const char *path);
int modules_load(const char *path);
int modules_init(void);
void modules_clear(void);

#endif /* _MODULES_H_ */
