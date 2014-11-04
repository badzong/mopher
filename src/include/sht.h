#ifndef _SHT_H_
#define _SHT_H_

typedef void (*sht_delete_t)(void *data);

struct sht
{
	ht_t		*sht_ht;
	sht_delete_t	 sht_delete;
};

typedef struct sht sht_t;

struct sht_record
{
	char	*sr_key;
	void	*sr_data;
};

typedef struct sht_record sht_record_t;

/*
 * Prototypes
 */

void sht_clear(sht_t *sht);
void sht_delete(sht_t *sht);
int sht_init(sht_t *sht, int buckets, sht_delete_t del);
sht_t * sht_create(int buckets, sht_delete_t del);
int sht_insert(sht_t *sht, char *key, void *data);
void * sht_lookup(sht_t *sht, char *key);
void sht_remove(sht_t *sht, char *key);
int sht_replace(sht_t *sht, char *key, void *data);
void sht_start(sht_t *sht, ht_pos_t *pos);
void * sht_next(sht_t *sht, ht_pos_t *pos);
void sht_test(int n);
#endif /* _SHT_H_ */
