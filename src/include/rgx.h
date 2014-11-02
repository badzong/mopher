#ifndef _RGX_H_
#define _RGX_H_

#include <regex.h>

void rgx_delete(regex_t *rgx);
regex_t * rgx_create(char *pattern_enc);
int rgx_match(regex_t *rgx, char *str);

#endif
