#ifndef _VAR_H_
#define _VAR_H_

#include "hash.h"
#include "ht.h"

#define VAR_INT_T long
#define VAR_FLOAT_T double

typedef struct sockaddr_storage var_sockaddr_t;

typedef enum var_type { VT_NULL = 0, VT_INT, VT_FLOAT, VT_STRING, VT_ADDR,
    VT_LIST } var_type_t;

typedef struct var {
    var_type_t   v_type;
    char        *v_name;
    void        *v_data;
} var_t;


/*
 * Prototypes
 */

void var_clear_data(var_t *v);
void var_clear(var_t *v);
int var_set_reference(var_t *v, var_type_t type, void *data);
int var_set_copy(var_t *v, var_type_t type, const void *data);
int var_init_copy(var_t *v, var_type_t type, const char *name, const void *data);
int var_init_reference(var_t *v, var_type_t type, const char *name, void *data);
var_t * var_create_copy(var_type_t type, const char *name, const void *data);
var_t * var_create_reference(var_type_t type, const char *name, void *data);
void var_delete(var_t *v);
var_t * var_strtoi(const char *name, const char *str);
var_t * var_strtof(const char *name, const char *str);
int var_string_rencaps(const char *src, char **dst, const char *encaps);
var_t * var_strtostr(const char *name, const char *str);
var_t * var_strtoa(const char *name, const char *str);
int var_compare_addr(struct sockaddr_storage *ss1, struct sockaddr_storage *ss2);
int var_compare(const var_t *v1, const var_t *v2);
int var_dump_data(var_t *v, char *buffer, int size);
int var_dump(var_t *v, char *buffer, int size);
hash_t var_hash(var_t *v);
int var_match(var_t *v1, var_t *v2);
int var_table_unset(ht_t *ht, const char *name);
int var_table_save(ht_t *ht, var_type_t type, const char *name, const void *data);
int var_table_list_insert(ht_t *ht, var_type_t type, char *name, void *data);

#endif /* _VAR_H_ */
