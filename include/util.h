#ifndef _UTIL_H_
#define _UTIL_H_

/*
 * Prototypes
 */

int util_parser(char *path, FILE ** input, int (*parser) (void));
char * util_strdupenc(const char *src, const char *encaps);
struct sockaddr_storage* util_strtoaddr(const char *str);
int util_file_exists(char *path);
int util_file(char *path, char **buffer);

#endif /* _UTIL_H_ */
