#ifndef _LL_H_
#define _LL_H_

/*
 * Includes
 */

#include <sys/types.h>

/*
 * Types
 */

typedef struct ll_entry {
	void			*lle_data;
	struct ll_entry	*lle_next;
} ll_entry_t;

typedef struct ll {
	u_int32_t	ll_size;
	ll_entry_t	*ll_head;
	ll_entry_t	*ll_tail;
} ll_t;

typedef void (*ll_delete_t)(void *data);

/*
 * Prototypes
 */

void ll_init(ll_t * ll);
ll_t * ll_create();
void ll_walk(ll_t * ll, void (*callback) (void *item, void *item_data), void *data);
void ll_clear(ll_t * ll, void (*destroy) (void *data));
void ll_delete(ll_t * ll, void (*destroy) (void *data));
int32_t ll_insert_head(ll_t * ll, void *data);
int32_t ll_insert_tail(ll_t * ll, void *data);
void * ll_remove_head(ll_t * ll);
void * ll_next(ll_t * ll, ll_entry_t **position);
void ll_test(int n);

/*
 * Macros
 */
#define LL_SIZE(ll)	((ll)->ll_size)
#define LL_HEAD(ll)	((ll)->ll_head->lle_data)
#define LL_INSERT	ll_insert_tail
#define	LL_ENQUEUE	ll_insert_tail
#define LL_DEQUEUE	ll_remove_head
#define LL_PUSH		ll_insert_head
#define LL_POP		ll_remove_head
#define LL_START(ll)	((ll)->ll_head)

#endif /* _LL_H_ */
