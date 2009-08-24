#ifndef _VAR_H_
#define _VAR_H_

#include "hash.h"
#include "ht.h"

#define VAR_INT_T long
#define VAR_FLOAT_T double

#define VF_REF		0
#define VF_COPYNAME	1<<0
#define VF_COPYDATA	1<<1
#define VF_KEEPNAME	1<<2
#define VF_KEEPDATA	1<<3
#define VF_KEEP		VF_KEEPNAME | VF_KEEPDATA

typedef struct sockaddr_storage var_sockaddr_t;

typedef enum var_type { VT_NULL = 0, VT_INT, VT_FLOAT, VT_STRING, VT_ADDR,
    VT_LIST } var_type_t;

typedef struct var {
    var_type_t   v_type;
    char        *v_name;
    void        *v_data;
    int		 v_flags;
} var_t;


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
hash_t var_hash(var_t * v);
int var_match(var_t * v1, var_t * v2);
ht_t * var_table_create(int buckets);
void var_table_delete(ht_t *table);
void var_table_unset(ht_t * ht, char *name);
int var_table_set(ht_t * ht, var_type_t type, char *name, void *data, int flags);
void * var_table_get(ht_t *ht, char *name);
int var_table_list_insert(ht_t * ht, var_type_t type, char *name, void *data, int flags);

#endif /* _VAR_H_ */
