#ifndef _VAR_H_
#define _VAR_H_

#include <stdarg.h>

#include "hash.h"
#include "ht.h"

#define VAR_INT_T long
#define VAR_FLOAT_T double
#define VAR_STRING_BUFFER_LEN 1024

#define VF_REF		0
#define VF_COPYNAME	1<<0
#define VF_COPYDATA	1<<1
#define VF_KEEPNAME	1<<2
#define VF_KEEPDATA	1<<3
#define VF_CREATE	1<<4

/*
 * VF_KEY is used in dbt.c to distinct between key and non-key values
 */
#define VF_KEY		1<<5

/*
 * VF_EXP_FREE is used in exp.c to mark resources that have to be freed after
 * evaluation.
 */
#define VF_EXP_FREE	1<<6


#define VF_KEEP		VF_KEEPNAME | VF_KEEPDATA
#define VF_COPY		VF_COPYNAME | VF_COPYDATA

typedef struct sockaddr_storage var_sockaddr_t;


/*
 * CAVEAT: The order of types is used for type casting in exp.c and datatype
 * selection in database drivers.
 */
typedef enum var_type { VT_NULL = 0, VT_TABLE, VT_LIST, VT_ADDR, VT_INT,
    VT_FLOAT, VT_POINTER, VT_STRING, VT_MAX = VT_STRING } var_type_t;

typedef struct var {
    var_type_t   v_type;
    char        *v_name;
    void        *v_data;
    int		 v_flags;
} var_t;


typedef struct var_compact {
	char	*vc_data;
	int	 vc_dlen;
	char	*vc_key;
	int	 vc_klen;
} var_compact_t;

#define VAR_COPY(v) var_create(v->v_type, v->v_name, v->v_data, VF_COPY)
#define VAR_MAX_TYPE(v1, v2) ((v1)->v_type > (v2)->v_type ? (v1)->v_type : (v2)->v_type)

/*
 * Prototypes
 */

void var_clear_name(var_t *v);
void var_clear(var_t *v);
void var_delete(var_t *v);
int var_data_size(var_t *v);
void var_rename(var_t *v, char *name, int flags);
int var_init(var_t *v, var_type_t type, char *name, void *data, int flags);
var_t * var_create(var_type_t type, char *name, void *data, int flags);
void * var_scan_data(var_type_t type, char *str);
var_t * var_scan_scheme(var_t *scheme, char *str);
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
int var_table_set_new(var_t *table, var_type_t type, char *name, void *data,int flags);
int var_table_setv(var_t *table, ...);
int var_table_printstr(var_t *table, char *buffer, int len, char *format);
int var_list_append(var_t *list, var_t *item);
int var_list_append_new(var_t *list, var_type_t type, char *name, void *data,int flags);
int var_table_list_append(var_t *table, char *listname, var_t *v);
int var_table_list_append_new(var_t *table, var_type_t type, char *name, void *data,int flags);
var_t * var_scheme_create(char *scheme, ...);
var_t * var_list_scheme(var_t *scheme, ...);
int var_list_dereference(var_t *list, ...);
int var_table_dereference(var_t *table, ...);
var_t * var_scheme_refcopy(var_t *scheme, ...);
void var_compact_delete(var_compact_t *vc);
var_compact_t * var_compress(var_t *v);
var_t * var_decompress(var_compact_t *vc, var_t *scheme);
var_t * var_cast_copy(var_type_t type, var_t *v);
#endif /* _VAR_H_ */
