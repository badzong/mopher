#ifndef _HT_H_
#define _HT_H_

/*
 * Includes
 */
#include <sys/types.h>

/*
 * Types
 */

typedef struct ht_record {
	void				*htr_data;
	struct ht_record	*htr_next;
} ht_record_t;

typedef struct ht {
	ht_record_t		**ht_table;
	u_int32_t		  ht_buckets;
	u_int32_t		  ht_records;
	u_int32_t	 	  ht_collisions;
	u_int32_t	 	  ht_resize_limit;
	u_int32_t	 	  ht_resize_upper;
	u_int32_t	 	  ht_resize_lower;
	u_int32_t		(*ht_hash)(void *data);
	int8_t			(*ht_match)(void *data1, void *data2);
	u_int32_t		  ht_head;
	u_int32_t		  ht_current_bucket;
	ht_record_t		 *ht_current_record;
} ht_t;

/*
 * Prototypes
 */

int8_t ht_init(ht_t *ht, u_int32_t buckets, u_int32_t resize_limit, u_int32_t resize_upper, u_int32_t resize_lower, u_int32_t (*hash)(void *data),int8_t (*match)(void *data1, void *data2));
ht_t * ht_create(u_int32_t buckets, u_int32_t resize_limit, u_int32_t resize_upper, u_int32_t resize_lower, u_int32_t (*hash)(void *data),int8_t (*match)(void *data1, void *data2));
void ht_clear(ht_t *ht, void (*destroy)(void *data));
void ht_delete(ht_t *ht, void (*destroy)(void *data));
void * ht_lookup(ht_t *ht, void *data);
void ht_rewind(ht_t *ht);
int8_t ht_insert(ht_t *ht, void *data);
void ht_remove(ht_t *ht, void *data);
void ht_dump(ht_t *ht, void (*print_data)(void *data));
int8_t ht_resize(ht_t *ht, u_int32_t buckets);
int8_t ht_walk(ht_t *ht, int (*callback)(void *data));
void * ht_next(ht_t *ht);


/*
 * Macros
 */
#define HT_INIT_STATIC(ht, buckets, hash, match) ht_init(ht, buckets, 0, 0, 0, hash, match)
#define HT_CREATE_STATIC(buckets, hash, match) ht_create(buckets, 0, 0, 0, hash, match)
#define HT_RECORDS(ht)		((ht)->ht_records)
#define HT_COLLISIONS(ht)	((ht)->ht_collisions)

/*
 * Calculate percental load/collision factors
 */
#define HT_LOADFACTOR(ht)	((float) 100 / (float) (ht)->ht_buckets * (float) (ht)->ht_records)
#define HT_COLLFACTOR(ht)	((float) 100 / (float) (ht)->ht_records * (float) (ht)->ht_collisions)

#endif /* _HT_H_ */
