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
var_t * vlist_record_from_table(var_t *scheme, var_t *table);
void * vlist_record_lookup(var_t *record, char *key);
void * vlist_record_get(var_t *record, char *key);
int vlist_record_keys_missing(var_t *record, var_t *table);
#endif /* _VLIST_H_ */
