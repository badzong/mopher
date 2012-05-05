#include <stdlib.h>
#include <string.h>

#include <mopher.h>


void
sht_clear(sht_t *sht)
{
	sht_record_t *sr;
	ht_pos_t pos;

	ht_start(sht->sht_ht, &pos);
	while ((sr = ht_next(sht->sht_ht, &pos)))
	{
		if (sht->sht_delete)
		{
			sht->sht_delete(sr->sr_data);
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
