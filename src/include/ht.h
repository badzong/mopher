#ifndef _HT_H_
#define _HT_H_

/*
 * Includes
 */
#include <sys/types.h>
#include <hash.h>

/*
 * Types
 */

typedef hash_t (*ht_hash_t)(void *data);
typedef int (*ht_match_t)(void *data1, void *data2);
typedef void (*ht_delete_t)(void *data);

typedef struct ht_record {
	void			*htr_data;
	struct ht_record	*htr_next;
} ht_record_t;

typedef struct ht {
	ht_record_t		**ht_table;
	hash_t			  ht_buckets;
	int			  ht_records;
	int		 	  ht_collisions;
	int		 	  ht_resize_lower;
	ht_hash_t		  ht_hash;
	ht_match_t		  ht_match;
	ht_delete_t		  ht_delete;
	int			  ht_head;
	int			  ht_current_bucket;
	ht_record_t		 *ht_current_record;
} ht_t;

/*
 * Prototypes
 */

int ht_init(ht_t * ht,hash_t buckets, ht_hash_t hash, ht_match_t match, ht_delete_t delete);
ht_t * ht_create(hash_t buckets, ht_hash_t hash, ht_match_t match, ht_delete_t delete);
void ht_clear(ht_t * ht);
void ht_delete(ht_t * ht);
void * ht_lookup(ht_t * ht, void *data);
void ht_rewind(ht_t * ht);
int8_t ht_insert(ht_t * ht, void *data);
void ht_remove(ht_t * ht, void *data);
void ht_dump(ht_t * ht, void (*print_data) (void *data));
int ht_walk(ht_t * ht, int (*callback) (void *data));
void * ht_next(ht_t * ht);

/*
 * Macros
 */
#define HT_RECORDS(ht)		((ht)->ht_records)
#define HT_COLLISIONS(ht)	((ht)->ht_collisions)

/*
 * Calculate percental load/collision factors
 */
#define HT_LOADFACTOR(ht)	((float) 100 / (float) (ht)->ht_buckets * (float) (ht)->ht_records)
#define HT_COLLFACTOR(ht)	((float) 100 / (float) (ht)->ht_records * (float) (ht)->ht_collisions)

#endif /* _HT_H_ */
