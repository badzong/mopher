#ifndef _CLIENT_H_
#define _CLIENT_H_

typedef struct client {
	char	*c_server;
	char	*c_path;
	char	*c_myname;
	char	*c_secret;
	int	 c_socket;
	ll_t	*c_queue;
} client_t;
	
/*
 * Prototypes
 */

int client_send(client_t *c, var_t *record);
int client_sync(dbt_t *dbt, var_t *record);
int client_init();
void client_clear(void);
#endif /* _CLIENT_H_ */
