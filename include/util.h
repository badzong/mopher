#ifndef _UTIL_H_
#define _UTIL_H_

/*
 * Prototypes
 */

char * util_strdupenc(const char *src, const char *encaps);
struct sockaddr_storage* util_strtoaddr(const char *str);

#endif /* _UTIL_H_ */
