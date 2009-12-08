#ifndef _VTABLE_H_
#define _VTABLE_H_

#include "var.h"


/*
 * Prototypes
 */

var_t * vtable_create(char *name, int flags);
var_t * vtable_lookup(var_t *table, char *name);
void * vtable_get(var_t *table, char *name);
var_t * vtable_getva(var_type_t type, var_t *table, va_list ap);
var_t * vtable_getv(var_type_t type, var_t *table, ...);
int vtable_insert(var_t *table, var_t *v);
int vtable_set(var_t *table, var_t *v);
int vtable_set_new(var_t *table, var_type_t type, char *name, void *data, int flags);
int vtable_setv(var_t *table, ...);
var_t * vtable_list_get(var_t *table, char *listname);
int vtable_list_append(var_t *table, char *listname, var_t *v);
int vtable_list_append_new(var_t *table, var_type_t type, char *name, void *data,int flags);
int vtable_dereference(var_t *table, ...);

#endif /* _VTABLE_H_ */
