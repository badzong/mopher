#include <config.h>

#include <sys/types.h>

#include <hash.h>

hash_t
hash_one_at_a_time(char *key, u_int32_t len)
{
	u_int32_t hash;
	u_int32_t i;

	for (hash = 0, i = 0; i < len; ++i) {
		hash += key[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	return hash;
}

hash_t
hash_djb(void *key, u_int32_t len)
{
	unsigned char *p = key;
	unsigned long h = 0;
	unsigned long i;

	for (i = 0; i < len; i++)
		h = 33 * h + p[i];

	return h;
}

hash_t
hash_test(void *key, u_int32_t len)
{
	unsigned char *p = key;
	unsigned long h = 0;
	unsigned long i;

	for (i = 0; i < len; i++)
		h += p[i];

	return h;
}

hash_t
hash_chain(void *key, u_int32_t len)
{
	return hash_test(key, len) % 5;
}
