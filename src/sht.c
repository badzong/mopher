#include <stdlib.h>
#include <string.h>

#include <mopher.h>


void
sht_clear(sht_t *sht)
{
	sht_record_t *sr;
	ht_pos_t pos;

	if (sht->sht_delete != NULL)
	{
		ht_start(sht->sht_ht, &pos);
		while ((sr = ht_next(sht->sht_ht, &pos)) != NULL)
		{
			if (sr->sr_data != NULL)
			{
				sht->sht_delete(sr->sr_data);
			}
		}
	}

	ht_delete(sht->sht_ht);

	memset(sht, 0, sizeof (sht_t));

	return;
}


void
sht_delete(sht_t *sht)
{
	sht_clear(sht);
	free(sht);

	return;
}

static hash_t
sht_record_hash(sht_record_t *sr)
{
	return HASH(sr->sr_key, strlen(sr->sr_key));
}


static int
sht_record_match(sht_record_t *sr1, sht_record_t *sr2)
{
	if (strcmp(sr1->sr_key, sr2->sr_key))
	{
		return 0;
	}

	return 1;
}

static void
sht_record_delete(sht_record_t *sr)
{
	if (sr->sr_key)
	{
		free(sr->sr_key);
	}

	free(sr);

	return;
}


static sht_record_t *
sht_record_create(char *key, void *data)
{
	sht_record_t *sr = NULL;

	sr = (sht_record_t *) malloc(sizeof (sht_record_t));
	if (sr == NULL)
	{
		log_sys_error("sht_record_create: malloc");
		goto error;
	}

	memset(sr, 0, sizeof (sht_record_t));

	sr->sr_key = strdup(key);
	if (sr->sr_key == NULL)
	{
		log_sys_error("sht_record_create: strdup");
		goto error;
	}

	sr->sr_data = data;

	return sr;

error:

	if (sr)
	{
		sht_record_delete(sr);
	}

	return NULL;
}


int
sht_init(sht_t *sht, int buckets, sht_delete_t del)
{
	sht->sht_ht = ht_create(buckets, (ht_hash_t) sht_record_hash,
	    (ht_match_t) sht_record_match, (ht_delete_t) sht_record_delete);

	if (sht->sht_ht == NULL)
	{
		log_error("sht_init: ht_create failed");
		return -1;
	}

	sht->sht_delete = del;

	return 0;
}
		

sht_t *
sht_create(int buckets, sht_delete_t del)
{
	sht_t *sht = NULL;

	sht = (sht_t *) malloc(sizeof (sht_t));
	if (sht == NULL)
	{
		log_sys_error("sht_create: malloc");
		goto error;
	}

	if (sht_init(sht, buckets, del))
	{
		log_error("sht_create: sht_init failed");
		goto error;
	}

	return sht;


error:

	if (sht)
	{
		sht_delete(sht);
	}

	return NULL;
}


int
sht_insert(sht_t *sht, char *key, void *data)
{
	sht_record_t *sr = NULL;

	sr = sht_record_create(key, data);
	if (sr == NULL)
	{
		log_error("sht_insert: sht_record_create failed");
		goto error;
	}

	if (ht_insert(sht->sht_ht, sr))
	{
		log_error("sht_insert: ht_insert failed");
		goto error;
	}

	return 0;


error:

	if(sr)
	{
		sht_record_delete(sr);
	}

	return -1;
}


void *
sht_lookup(sht_t *sht, char *key)
{
	sht_record_t *sr, lookup;

	lookup.sr_key = key;
	sr = ht_lookup(sht->sht_ht, &lookup);
	if (sr == NULL)
	{
		return NULL;
	}

	return sr->sr_data;
}


void
sht_remove(sht_t *sht, char *key)
{
	sht_record_t *sr, lookup;

	lookup.sr_key = key;
	sr = ht_lookup(sht->sht_ht, &lookup);
	if (sr == NULL)
	{
		return;
	}

	if (sht->sht_delete)
	{
		sht->sht_delete(sr->sr_data);
	}

	ht_remove(sht->sht_ht, sr);

	return;
}


int
sht_replace(sht_t *sht, char *key, void *data)
{
	sht_remove(sht, key);

	return sht_insert(sht, key, data);
}


void
sht_start(sht_t *sht, ht_pos_t *pos)
{
	ht_start(sht->sht_ht, pos);

	return;
}


void *
sht_next(sht_t *sht, ht_pos_t *pos)
{
	sht_record_t *sr;

	sr = ht_next(sht->sht_ht, pos);
	if (sr == NULL)
	{
		return NULL;
	}

	return sr->sr_data;
}

#ifdef DEBUG
int
sht_test(void)
{
	sht_t *ht;
	struct sht_test {
		char *st_key;
		int   st_value;
	};
	int i;

	struct sht_test sht_tests[] = {
		{"0", 0},
		{"1", 1},
		{"2", 2},
		{"3", 3},
		{"4", 4},
		{"5", 5},
		{"6", 6},
		{"7", 7},
		{"8", 8},
		{"9", 9},
		{"foo", 10},
		{"bar", 11},
		{"foobar", 12},
		{ NULL, 0 }
	};

	struct sht_test *record;
	struct sht_test *p;

	ht = sht_create(64, free);
	
	// Test inserts
	for (record = sht_tests; record->st_key != NULL; ++record)
	{
		p = (struct sht_test *) malloc(sizeof (struct sht_test));
		if (p == NULL)
		{
			log_die(EX_SOFTWARE, "sht_test: malloc failed");
		}

		p->st_key = record->st_key;
		p->st_value = record->st_value;
		
		TEST_ASSERT(sht_insert(ht, p->st_key, p) == 0, "sht_insert failed");
	}

	// Test lookups
	for (record = sht_tests; record->st_key != NULL; ++record)
	{
		p = sht_lookup(ht, record->st_key);
		TEST_ASSERT(p != NULL, "sht_lookup failed");
		TEST_ASSERT(p->st_value == record->st_value,
			"sht_lookup didn't return the right data");
	}

	// Test replace
	for (i = 0; i < 10; ++i)
	{
		p = (struct sht_test *) malloc(sizeof (struct sht_test));
		if (p == NULL)
		{
			log_die(EX_SOFTWARE, "sht_test: malloc failed");
		}

		p->st_key = "foobar";
		p->st_value = i;
		TEST_ASSERT(sht_replace(ht, "foobar", p) == 0, "sht_replace failed");

		p = sht_lookup(ht, "foobar");
		TEST_ASSERT(p != NULL, "sht_get key not found");
		TEST_ASSERT(p->st_value == i, "sht_get value mismatch");
	}

	// Test remove
	sht_remove(ht, "foobar");
	p = sht_lookup(ht, "foobar");
	TEST_ASSERT(p == NULL, "sht_remove not successful");

	sht_delete(ht);

	return 0;
}
#endif
