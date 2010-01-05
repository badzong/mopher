#ifndef _VLIST_H_
#define _VLIST_H_

#include <var.h>

/*
 * Prototypes
 */

var_t * vlist_create(char *name, int flags);
int vlist_append(var_t *list, var_t *item);
int vlist_append_new(var_t *list, var_type_t type, char *name, void *data,int flags);
int vlist_dereference(var_t *list, ...);
var_t * vlist_scheme(char *scheme, ...);
var_t * vlist_record(var_t *scheme, ...);
void * vlist_record_get(var_t *record, char *key);
#endif /* _VLIST_H_ */
