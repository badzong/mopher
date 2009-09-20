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
struct sockaddr_storage * util_hostaddr(struct sockaddr_storage *ss);
int util_addrcmp(struct sockaddr_storage *ss1, struct sockaddr_storage *ss2);

#endif /* _UTIL_H_ */
