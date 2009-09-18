#ifndef _VAR_H_
#define _VAR_H_

#include <stdarg.h>

#include "hash.h"
#include "ht.h"

#define VAR_INT_T long
#define VAR_FLOAT_T double

#define VF_REF		0
#define VF_COPYNAME	1<<0
#define VF_COPYDATA	1<<1
#define VF_KEEPNAME	1<<2
#define VF_KEEPDATA	1<<3
#define VF_CREATE	1<<4
#define VF_KEY		1<<5
#define VF_KEEP		VF_KEEPNAME | VF_KEEPDATA

typedef struct sockaddr_storage var_sockaddr_t;

typedef enum var_type { VT_NULL = 0, VT_INT, VT_FLOAT, VT_STRING, VT_ADDR,
    VT_LIST, VT_TABLE } var_type_t;

typedef struct var {
    var_type_t   v_type;
    char        *v_name;
    void        *v_data;
    int		 v_flags;
} var_t;


typedef struct var_record {
	char	*vr_data;
	int	 vr_dlen;
	char	*vr_key;
	int	 vr_klen;
} var_record_t;

/*
 * Prototypes
 */

void var_clear(var_t * v);
void var_delete(var_t *v);
int var_init(var_t *v, var_type_t type, char *name, void *data, int flags);
var_t * var_create(var_type_t type, char *name, void *data, int flags);
int var_compare_addr(struct sockaddr_storage *ss1, struct sockaddr_storage *ss2);
int var_compare(const var_t * v1, const var_t * v2);
int var_true(const var_t * v);
int var_dump_data(var_t * v, char *buffer, int size);
int var_dump(var_t * v, char *buffer, int size);
var_t * var_table_lookup(var_t *table, char *name);
void * var_table_get(var_t * table, char *name);
var_t * var_table_getva(var_type_t type, var_t *table, va_list ap);
var_t * var_table_getv(var_type_t type, var_t *table, ...);
int var_table_insert(var_t *table, var_t *v);
int var_table_set(var_t *table, var_t *v);
int var_table_setv(var_t *table, var_type_t type, char *name, void *data, int flags);
int var_list_append(var_t *list, var_t *item);
int var_table_list_insert(var_t *table, var_type_t type, char *name, void *data, int flags);
var_record_t * var_record_pack(var_t *v);
var_t * var_record_unpack(var_record_t *vr, var_t *schema);
var_t * var_schema_create(char *name, ...);
var_t * var_record_build(var_t *schema, ...);
int var_list_dereference(var_t *list, ...);
int var_table_dereference(var_t *table, ...);
var_t * var_record_refcopy(var_t *schema, ...);
#endif /* _VAR_H_ */
