#include <malloc.h>
#include <string.h>
#include <arpa/inet.h>

#include "log.h"


char *
util_strdupenc(const char *src, const char *encaps)
{
	char *dup;
	int len;

	len = strlen(src);

	/*
	 * No encapsulation found.
	 */
	if (!(src[0] == encaps[0] && src[len - 1] == encaps[1])) {
		return strdup(src);
	}

	if ((dup = strdup(src + 1)) == NULL) {
		log_warning("util_strdupenc: strdup");
		return NULL;
	}

	dup[len - 2] = 0;

	return dup;
}

struct sockaddr_storage*
util_strtoaddr(const char *str)
{
	struct sockaddr_storage *ss;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;

	if((ss = (struct sockaddr_storage *)
		malloc(sizeof(struct sockaddr_storage))) == NULL) {
		return NULL;
	}

	sin = (struct sockaddr_in *) ss;
	sin6 = (struct sockaddr_in6 *) ss;

	if (inet_pton(AF_INET, str, &sin->sin_addr) == 1) {
		ss->ss_family = AF_INET;
	}
	else if (inet_pton(AF_INET6, str, &sin6->sin6_addr) == 1) {
		ss->ss_family = AF_INET6;
	}
	else {
		return NULL;
	}

	return ss;
}

