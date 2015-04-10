#ifndef _HASH_H_
#define _HASH_H_

#include <stdint.h>

typedef unsigned long hash_t;

hash_t hash_one_at_a_time(char *key, uint32_t len);
hash_t hash_djb(void *key, uint32_t len);
hash_t hash_test(void *key, uint32_t len);
hash_t hash_chain(void *key, uint32_t len);

#define HASH hash_one_at_a_time
//#define HASH hash_djb
//#define HASH hash_test
//#define HASH hash_chain

#endif
