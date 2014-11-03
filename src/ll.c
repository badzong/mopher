#include <config.h>

#include <stdlib.h>

#include <ll.h>
#include <test.h>

void
ll_init(ll_t * ll)
{
	ll->ll_size = 0;
	ll->ll_head = NULL;
	ll->ll_tail = NULL;
}

ll_t *
ll_create()
{
	ll_t *ll;

	if ((ll = (ll_t *) malloc(sizeof(ll_t))) == NULL)
	 	return NULL;

	ll_init(ll);

	return ll;
}

void
ll_walk(ll_t * ll, void (*callback) (void *data))
{
	ll_entry_t *entry;

	for (entry = ll->ll_head; entry; entry = entry->lle_next) {
		callback(entry->lle_data);
	}

	return;
}

void
ll_clear(ll_t * ll, void (*destroy) (void *data))
{
	ll_entry_t *entry;
	ll_entry_t *next;

	if (destroy != NULL) {
		ll_walk(ll, destroy);
	}

	for (entry = ll->ll_head; entry; entry = next) {
		next = entry->lle_next;
		free(entry);
	}

	/*
	 * Reset to zero
	 */
	ll_init(ll);

	return;
}

void
ll_delete(ll_t * ll, void (*destroy) (void *data))
{
	ll_clear(ll, destroy);
	free(ll);
}

int32_t
ll_insert_head(ll_t * ll, void *data)
{
	ll_entry_t *entry;

	if ((entry = malloc(sizeof(ll_entry_t))) == NULL) {
		return -1;
	}

	entry->lle_data = data;
	entry->lle_next = ll->ll_head;

	ll->ll_head = entry;

	if (ll->ll_tail == NULL) {
		ll->ll_tail = ll->ll_head;
	}

	++ll->ll_size;

	return ll->ll_size;
}

int32_t
ll_insert_tail(ll_t * ll, void *data)
{
	ll_entry_t *entry;

	if ((entry = malloc(sizeof(ll_entry_t))) == NULL) {
		return -1;
	}

	entry->lle_data = data;
	entry->lle_next = NULL;

	if (ll->ll_tail != NULL) {
		ll->ll_tail->lle_next = entry;
	}

	ll->ll_tail = entry;

	if (ll->ll_head == NULL) {
		ll->ll_head = ll->ll_tail;
	}

	++ll->ll_size;

	return ll->ll_size;
}

void *
ll_remove_head(ll_t * ll)
{
	ll_entry_t *entry;
	void *data;

	if (ll->ll_size == 0) {
		return NULL;
	}

	entry = ll->ll_head;

	ll->ll_head = ll->ll_head->lle_next;

	if (ll->ll_head == NULL) {
		ll->ll_tail = NULL;
	}

	data = entry->lle_data;
	free(entry);

	--ll->ll_size;

	return data;
}


void *
ll_next(ll_t * ll, ll_entry_t **position)
{
	ll_entry_t *entry;

	/*
	 * List end
	 */
	if (*position == NULL) {
		return NULL;
	}

	entry = *position;

	*position = (*position)->lle_next;

	return entry->lle_data;
}

#ifdef DEBUG

static int test_array[] = {1,2,3,4,5,6,7,8,9,10,0};
static int test_i;

static void
ll_test_callback(int *p)
{
	++test_i;
	TEST_ASSERT(*p == test_i, "Wrong value: expect %d, got %d", test_i, *p);
	return;
}

void
ll_test(void)
{
	ll_t ll;
	ll_t *pll;
	int i = 0;
	int *p;

	pll = &ll;
	ll_init(pll);
	for (p = test_array; *p; ++p, ++i)
	{
		TEST_ASSERT(ll_insert_tail(pll, p) > 0, "LL_INSERT failed");
	}
	ll_walk(pll, (void *) ll_test_callback);

	while(i--)
	{
		TEST_ASSERT(ll_remove_head(pll) != NULL, "LL_REMOVE failed");
	}
	TEST_ASSERT(pll->ll_size == 0, "list has size %d. should be empty", pll->ll_size);

	i = test_i = 0;
	pll = ll_create();
	TEST_ASSERT(pll != NULL, "ll_create failed");
	for (p = test_array; *p; ++p, ++i)
	{
		TEST_ASSERT(ll_insert_tail(pll, p) > 0, "LL_INSERT failed");
	}
	ll_walk(pll, (void *) ll_test_callback);

	while(i--)
	{
		TEST_ASSERT(ll_remove_head(pll) != NULL, "LL_REMOVE failed");
	}
	TEST_ASSERT(pll->ll_size == 0, "list has size %d. should be empty", pll->ll_size);
	ll_delete(pll, NULL);
}

#endif
