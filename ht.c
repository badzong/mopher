#include <sys/types.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>

#include "ht.h"

int8_t
ht_init(ht_t * ht,
	u_int32_t buckets,
	u_int32_t resize_limit,
	u_int32_t resize_upper,
	u_int32_t resize_lower,
	u_int32_t(*hash) (void *data),
	int8_t(*match) (void *data1, void *data2))
{
	if ((ht->ht_table =
	     malloc(buckets * (sizeof(ht_record_t *) + 1))) == NULL) {
		return -1;
	}

	bzero(ht->ht_table, buckets * (sizeof(ht_record_t *) + 1));

	ht->ht_buckets = buckets;
	ht->ht_resize_limit = resize_limit;
	ht->ht_resize_upper = resize_upper;
	ht->ht_resize_lower = resize_lower;
	ht->ht_hash = hash;
	ht->ht_match = match;
	ht->ht_records = 0;
	ht->ht_collisions = 0;
	ht->ht_head = 0;
	ht->ht_current_bucket = 0;
	ht->ht_current_record = NULL;

	return 0;
}

ht_t *
ht_create(u_int32_t buckets,
	  u_int32_t resize_limit,
	  u_int32_t resize_upper,
	  u_int32_t resize_lower,
	  u_int32_t(*hash) (void *data),
	  int8_t(*match) (void *data1, void *data2))
{
	ht_t *ht;

	if ((ht = (ht_t *) malloc(sizeof(ht_t))) == NULL) {
		return NULL;
	}

	if (ht_init(ht, buckets, resize_limit, resize_upper, resize_lower, hash,
		    match)) {
		return NULL;
	}

	return ht;
}

void
ht_clear(ht_t * ht, void (*destroy) (void *data))
{
	ht_record_t *record;
	ht_record_t *next;
	u_int32_t i;

	for (i = 0; i < ht->ht_buckets; ++i) {
		for (record = ht->ht_table[i]; record != NULL; record = next) {
			next = record->htr_next;
			if (destroy) {
				destroy(record->htr_data);
			}
			free(record);
		}
	}

	free(ht->ht_table);

	return;
}

void
ht_delete(ht_t * ht, void (*destroy) (void *data))
{
	ht_clear(ht, destroy);
	free(ht);
}

void *
ht_lookup(ht_t * ht, void *data)
{
	ht_record_t *record;
	u_int32_t bucket;

	bucket = ht->ht_hash(data) % ht->ht_buckets;

	if (ht->ht_table[bucket] == NULL) {
		return NULL;
	}

	for (record = ht->ht_table[bucket];
	     record != NULL; record = record->htr_next) {
		if (ht->ht_match(data, record->htr_data)) {
			return record->htr_data;
		}
	}

	return NULL;
}

void
ht_rewind(ht_t * ht)
{
	if (ht->ht_records) {
		ht->ht_current_bucket = ht->ht_head;
		ht->ht_current_record = ht->ht_table[ht->ht_head];

		return;
	}

	/*
	 * Empty table
	 */
	ht->ht_head = 0;
	ht->ht_current_bucket = 0;
	ht->ht_current_record = NULL;

	return;
}

int8_t
ht_insert(ht_t * ht, void *data)
{
	ht_record_t *record;
	u_int32_t bucket;

	if (ht_lookup(ht, data) != NULL) {
		return -1;
	}

	/*
	 * Change table size by factor 2 if threshold reached.
	 */
	if (ht->ht_buckets > ht->ht_resize_limit &&
	    ht->ht_resize_limit > 0 &&
	    ht->ht_resize_upper > 0 && ht->ht_resize_lower > 0) {
		if (HT_LOADFACTOR(ht) > ht->ht_resize_upper) {
			ht_resize(ht, ht->ht_buckets * 2);
		}
		else if (HT_LOADFACTOR(ht) < ht->ht_resize_lower) {
			ht_resize(ht, ht->ht_buckets / 2);
		}
	}

	bucket = ht->ht_hash(data) % ht->ht_buckets;

	if ((record = malloc(sizeof(ht_record_t))) == NULL) {
		return -1;
	}

	record->htr_data = data;
	record->htr_next =
	    ht->ht_table[bucket] != NULL ? ht->ht_table[bucket] : NULL;

	ht->ht_table[bucket] = record;

	if (record->htr_next != NULL) {
		++ht->ht_collisions;
	}
	++ht->ht_records;

	if (bucket < ht->ht_head || ht->ht_head == 0) {
		ht->ht_head = bucket;
	}

	/*
	 * Rewind on insert
	 */
	ht_rewind(ht);

	return 0;
}

void
ht_remove(ht_t * ht, void *data)
{
	ht_record_t *record;
	ht_record_t **prev;
	u_int32_t bucket;

	bucket = ht->ht_hash(data) % ht->ht_buckets;

	if (ht->ht_table[bucket] == NULL) {
		return;
	}

	for (record = ht->ht_table[bucket], prev = &ht->ht_table[bucket];
	     record != NULL;
	     prev = &(*prev)->htr_next, record = record->htr_next) {
		if (ht->ht_match(data, record->htr_data)) {
			break;
		}
	}

	/*
	 * Not found
	 */
	if (record == NULL) {
		return;
	}

	/*
	 * If we remove the current_record advance
	 */
	if (record == ht->ht_current_record) {
		ht_next(ht);
	}

	*prev = record->htr_next;
	free(record);

	if (ht->ht_table[bucket] != NULL) {
		--ht->ht_collisions;
	}
	--ht->ht_records;

	/*
	 * Rewind on insert and remove
	 */
	ht_rewind(ht);

	return;
}

void
ht_dump(ht_t * ht, void (*print_data) (void *data))
{
	ht_record_t *record;
	u_int32_t i;

	printf("HASHTABLE DUMP\n");
	printf("%4lu buckets\n", (unsigned long) ht->ht_buckets);
	printf("%4lu records\n", (unsigned long) ht->ht_records);
	printf("%4lu collisions\n", (unsigned long) ht->ht_collisions);
	printf("\nTABLE RECORDS\n");

	for (i = 0; i < ht->ht_buckets; ++i) {
		printf("%4lu %p", (unsigned long) i, ht->ht_table[i]);
		for (record = ht->ht_table[i];
		     record != NULL; record = record->htr_next) {
			printf(" [");
			print_data(record->htr_data);
			printf("] >> %p", record->htr_next);
		}
		printf("\n");
	}

	printf("\n");

	return;
}

int8_t
ht_resize(ht_t * ht, u_int32_t buckets)
{
	ht_t new;
	ht_record_t *record;
	u_int32_t i;

	if (ht_init(&new, buckets, ht->ht_resize_limit, ht->ht_resize_upper,
		    ht->ht_resize_lower, ht->ht_hash, ht->ht_match)) {
		return -1;
	}

	for (i = 0; i < ht->ht_buckets; ++i) {
		for (record = ht->ht_table[i];
		     record != NULL; record = record->htr_next) {
			if (ht_insert(&new, record->htr_data)) {
				return -1;
			}
		}
	}

	ht_clear(ht, NULL);

	memcpy(ht, &new, sizeof(ht_t));

	return 0;
}

int8_t
ht_walk(ht_t * ht, int (*callback) (void *data))
{
	ht_record_t *record;
	u_int32_t i;
	int8_t r;

	for (i = 0; i < ht->ht_buckets; ++i) {
		for (record = ht->ht_table[i];
		     record != NULL; record = record->htr_next) {
			if ((r = callback(record->htr_data))) {
				return r;
			}
		}
	}

	return 0;
}

void *
ht_next(ht_t * ht)
{
	ht_record_t *record;
	u_int32_t i;

	/*
	 * End of table
	 */
	if (ht->ht_current_record == NULL) {
		return NULL;
	}

	/*
	 * Save record
	 */
	record = ht->ht_current_record;

	/*
	 * Next record in current chain
	 */
	if (record->htr_next) {
		ht->ht_current_record = record->htr_next;
		return record->htr_data;
	}

	/*
	 * Next bucket
	 */
	for (i = ht->ht_current_bucket + 1;
	     i < ht->ht_buckets && ht->ht_table[i] == NULL; ++i);

	if (ht->ht_table[i]) {
		ht->ht_current_bucket = i;
		ht->ht_current_record = ht->ht_table[i];
		return record->htr_data;
	}

	/*
	 * Last record
	 */
	ht->ht_current_bucket = 0;
	ht->ht_current_record = NULL;

	return record->htr_data;
}
