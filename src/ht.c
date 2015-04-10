#include <config.h>

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ht.h>
#include <hash.h>
#include <log.h>

/*
 * Debug if collisions reach 20%
 */
#define COLLISION_ALERT 20

/*
 * Double hashtable size if load exceeds 50%
 */
#define MAX_LOAD 50

int
ht_init(ht_t *ht, hash_t buckets, ht_hash_t hash, ht_match_t match,
	ht_delete_t delete)
{
	if((ht->ht_table = malloc(buckets * sizeof(ht_record_t *))) == NULL) {
		log_sys_warning("ht_init: malloc");
		return -1;
	}

	bzero(ht->ht_table, buckets * sizeof(ht_record_t *));

	ht->ht_buckets = buckets;
	ht->ht_hash = hash;
	ht->ht_match = match;
	ht->ht_delete = delete;
	ht->ht_records = 0;
	ht->ht_collisions = 0;
	ht->ht_head = 0;

	return 0;
}


ht_t *
ht_create(hash_t buckets, ht_hash_t hash, ht_match_t match, ht_delete_t delete)
{
	ht_t *ht;

	if((ht = (ht_t *) malloc(sizeof(ht_t))) == NULL) {
		log_sys_warning("ht_create: malloc");
		return NULL;
	}

	if(ht_init(ht, buckets, hash, match, delete)) {
		log_warning("ht_create: ht_init failed");
		return NULL;
	}

	return ht;
}


void
ht_clear(ht_t *ht)
{
	ht_record_t *record;
	ht_record_t *next;
	hash_t i;

	for(i = 0; i < ht->ht_buckets; ++i) {
		for(record = ht->ht_table[i]; record != NULL; record = next) {
			next = record->htr_next;
			if(ht->ht_delete) {
				ht->ht_delete(record->htr_data);
			}
			free(record);
		}
	}

	free(ht->ht_table);

	return;
}


void
ht_delete(ht_t *ht)
{
	ht_clear(ht);
	free(ht);

	return;
}


void *
ht_lookup(ht_t *ht, void *data)
{
	ht_record_t *record;
	hash_t bucket;

	bucket = ht->ht_hash(data) % ht->ht_buckets;

	if(ht->ht_table[bucket] == NULL) {
		return NULL;
	}

	for(record = ht->ht_table[bucket];
		record != NULL;
		record = record->htr_next)
	{
		if(ht->ht_match(data, record->htr_data)) {
			return record->htr_data;
		}
	}

	return NULL;
}


void
ht_start(ht_t *ht, ht_pos_t *pos)
{
	if(ht->ht_records)
	{
		pos->htp_bucket = ht->ht_head;
		pos->htp_record = ht->ht_table[ht->ht_head];
	}
	else
	{
		pos->htp_bucket = 0;
		pos->htp_record = NULL;
	}

	return;
}

int8_t
ht_resize(ht_t *ht)
{
	ht_t new;
	ht_record_t *record;
	uint32_t i, buckets;
	
	buckets = ht->ht_buckets * 2;

	log_error("ht_resize: load exceeded %.1f %% resize from %d to %d",
	    HT_LOADFACTOR(ht), ht->ht_buckets, buckets);

	/*
	 * Create the new hash table. ht_delete is set to NULL to prevent data
	 * loss in case of an error.
	 */
	if (ht_init(&new, buckets, ht->ht_hash, ht->ht_match, NULL))
	{
		log_error("ht_resize: ht_init failed");
		return -1;
	}

	for(i = 0; i < ht->ht_buckets; ++i)
	{
		for (record = ht->ht_table[i];
		    record != NULL;
		    record = record->htr_next)
		{
			if(ht_insert(&new, record->htr_data))
			{
				log_error("ht_resize: ht_insert failed");
				ht_clear(&new);
				return -1;
			}
		}
	}

	/*
	 * Swap ht_delete
	 */
	new.ht_delete = ht->ht_delete;
	ht->ht_delete = NULL;

	ht_clear(ht);

	memcpy(ht, &new, sizeof(ht_t));

	log_info("ht_resize: successful, records=%d (%.1f%%), collisions=%d "
	    "(%.1f%%)", ht->ht_records, HT_LOADFACTOR(ht), ht->ht_collisions,
	    HT_COLLFACTOR(ht));

	return 0;
}

int8_t
ht_insert(ht_t *ht, void *data)
{
	ht_record_t *record;
	hash_t bucket;
	float cf;

	if(ht_lookup(ht, data) != NULL) {
		log_debug("ht_insert: duplicate entry");
		return -1;
	}

	bucket = ht->ht_hash(data) % ht->ht_buckets;

	if((record = malloc(sizeof (ht_record_t))) == NULL) {
		log_sys_warning("ht_insert: malloc");
		return -1;
	}

	record->htr_data = data;
	record->htr_next =
		ht->ht_table[bucket] != NULL ? ht->ht_table[bucket] : NULL;

	if(ht->ht_table[bucket] != NULL) {
		++ht->ht_collisions;
	}


	if(bucket < ht->ht_head || ht->ht_records == 0) {
		ht->ht_head = bucket;
	}

	ht->ht_table[bucket] = record;
	++ht->ht_records;

	cf = HT_COLLFACTOR(ht);
	if (cf > COLLISION_ALERT)
	{
		/*
		 * FIXME: format is evaluated twice: %%%%
		 */
		log_debug("ht_insert: collisions %.1f%%%%", cf);
	}

	if (HT_LOADFACTOR(ht) > MAX_LOAD)
	{
		if (ht_resize(ht))
		{
			log_error("ht_insert: ht_resize failed");
		}
	}

	return 0;
}


void
ht_remove(ht_t *ht, void *data)
{
	ht_record_t *record;
	ht_record_t **prev;
	hash_t bucket;
	int collision = 0;

	bucket = ht->ht_hash(data) % ht->ht_buckets;

	if(ht->ht_table[bucket] == NULL) {
		log_debug("ht_remove: record not found");
		return;
	}
	
	record = ht->ht_table[bucket];
	prev = &ht->ht_table[bucket];

	if (record->htr_next != NULL)
	{
		collision = 1;
	}

	while (record != NULL)
	{
		if(ht->ht_match(data, record->htr_data)) {
			break;
		}

		record = record->htr_next;
		prev = &(*prev)->htr_next;
	}

	/*
	 * Not found
	 */
	if(record == NULL) {
		log_debug("ht_remove: record not found");
		return;
	}
	
	if(ht->ht_delete) {
		ht->ht_delete(record->htr_data);
	}

	*prev = record->htr_next;
	free(record);

	if(ht->ht_table[bucket] != NULL) {
		--ht->ht_collisions;
	}
	--ht->ht_records;

	/*
	 * Adjust collision counter
	 */
	if (collision)
	{
		--ht->ht_collisions;
	}

	return;
}


void
ht_dump(ht_t *ht, void (*print_data)(void *data))
{
	ht_record_t *record;
	hash_t i;

	printf("HASHTABLE DUMP\n");
	printf("%4lu buckets\n", (unsigned long) ht->ht_buckets);
	printf("%4lu records\n", (unsigned long) ht->ht_records);
	printf("%4lu collisions\n", (unsigned long) ht->ht_collisions);
	printf("\nTABLE RECORDS\n");

	for(i = 0; i < ht->ht_buckets; ++i) {
		printf("%4lu %p", (unsigned long) i, ht->ht_table[i]);
		for(record = ht->ht_table[i];
			record != NULL;
			record = record->htr_next)
		{
			printf(" [");
			print_data(record->htr_data);
			printf("] >> %p", record->htr_next);
		}
		printf("\n");
	}

	printf("\n");

	return;
}


int
ht_walk(ht_t *ht, int (*callback)(void *data))
{
	ht_record_t *record;
	hash_t i;
	int r;

	for(i = 0; i < ht->ht_buckets; ++i) {
		for(record = ht->ht_table[i];
			record != NULL;
			record = record->htr_next)
		{
			if((r = callback(record->htr_data))) {
				log_debug("ht_walk: callback returned %d", r);
				return r;
			}
		}
	}

	return 0;
}


void *
ht_next(ht_t *ht, ht_pos_t *pos)
{
	ht_record_t *record;
	hash_t i;

	/*
	 * End of table
	 */
	if(pos->htp_record == NULL)
	{
		return NULL;
	}

	/*
	 * Save record
	 */
	record = pos->htp_record;


	/*
	 * Next record in current chain
	 */
	if(record->htr_next)
	{
		pos->htp_record = record->htr_next;

		return record->htr_data;
	}

	/*
	 * Next bucket
	 */
	for(i = pos->htp_bucket + 1;
	    i < ht->ht_buckets && ht->ht_table[i] == NULL;
	    ++i);

	/*
	 * Last record
	 */
	if(i == ht->ht_buckets)
	{
		pos->htp_bucket = 0;
		pos->htp_record = NULL;

		return record->htr_data;
	}

	pos->htp_bucket = i;
	pos->htp_record = ht->ht_table[i];

	return record->htr_data;
}
