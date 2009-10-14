#ifndef _HASH_H_
#define _HASH_H_

#include <sys/types.h>

typedef unsigned long hash_t;

hash_t hash_one_at_a_time(char *key, u_int32_t len);

#define HASH hash_one_at_a_time

#endif
