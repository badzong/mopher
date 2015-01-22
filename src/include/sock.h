#ifndef _SOCK_H_
#define _SOCK_H_

/*
 * Prototypes
 */

typedef struct sock_rr {
	pthread_mutex_t  sr_mutex;
	ll_t		 sr_list;
	ll_entry_t	*sr_pos;
} sock_rr_t;

void sock_unix_unlink(char *uri);
int sock_listen(char *uri, int backlog);
int sock_connect(char *uri);
int sock_connect_rr(sock_rr_t *sr);
void sock_rr_clear(sock_rr_t *sr);
int sock_rr_init(sock_rr_t *sr, char *confkey);

#endif /* _SOCK_H_ */
