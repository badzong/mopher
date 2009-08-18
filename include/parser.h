#include <sys/types.h>
#include <stdio.h>
/*
 * Prototypes
 */

int8_t parser(char *path, FILE **input, int (*parser)(void));
