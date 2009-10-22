#ifndef _MODULES_H_
#define _MODULES_H_

/*
 * Prototypes
 */

void * module_open(const char *path);
int module_load(const char *path);
int module_init(void);
void module_clear(void);

#endif /* _MODULES_H_ */
