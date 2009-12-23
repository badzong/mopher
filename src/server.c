#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#include "mopher.h"

#define MAX_CLIENTS 10
#define BACKLOG 16
#define RECV_BUFFER 4096

static int		server_socket;
static int		server_clients[MAX_CLIENTS + 1];
static int		server_running = 1;
static pthread_t	server_thread;
static pthread_attr_t	server_attr;


static void
server_usr2(int sig)
{
	log_debug("server_usr2: caught signal %d", sig);

	return;
}


static int
server_update(int socket)
{
	dbt_t *dbt;
	char buffer[RECV_BUFFER];
	int len;
	char *p;
	char *name = NULL;
	var_t *record = NULL;

	len = read(socket, buffer, sizeof buffer);
	if (len == -1)
	{
		log_error("server_update: read");
		goto error;
	}

	if (len == 0)
	{
		log_error("server_update: connection closed");
		return 0;
	}

	/*
	 * Cut trailing newline
	 */
	buffer[--len] = 0;

	p = strchr(buffer, '=');
	if (p == NULL)
	{
		log_error("server_update: bad string received");
		goto error;
	}

	len = p - buffer;
	
	name = strndup(buffer, len);
	if (name == NULL)
	{
		log_error("server_update: strndup");
		goto error;
	}

	dbt = dbt_lookup(name);
	if (dbt == NULL)
	{
		log_error("server_update: bad table \"%s\"", name);
		goto error;
	}

	record = var_scan_scheme(dbt->dbt_scheme, buffer);
	if (record == NULL)
	{
		log_error("server_update: var_scan_scheme failed");
		goto error;
	}

	if (dbt_db_set(dbt, record))
	{
		log_error("server_update: dbt_db_set failed");
		goto error;
	}

	free(name);
	var_delete(record);

	return len;


error:
	if (name)
	{
		free(name);
	}

	if (record)
	{
		var_delete(record);
	}

	return -1;
}


static void *
server_main(void *arg)
{
	struct sockaddr_storage caddr;
	unsigned int len;
	int maxfd;
	int i;
	int ready;
	fd_set master;
	fd_set rs;

	/*
	 * Use SIGUSR2 to get out of select
	 */
	if (util_signal(SIGUSR2, server_usr2))
	{
		log_die(EX_SOFTWARE, "server_main: util_signal failed");
	}

	/*
	 * Create server socket
	 */
	server_socket = sock_listen(cf_server_socket, BACKLOG);
	if (server_socket == -1)
	{
		log_die(EX_SOFTWARE, "server_init: sock_listen failed");
	}

	/*
	 * Initialize client sockets
	 */
	for (i = 0; i < MAX_CLIENTS; server_clients[i++] = -1);
	server_clients[MAX_CLIENTS] = 0;

	/*
	 * Prepare master set and max fd
	 */
	FD_ZERO(&master);
	FD_SET(server_socket, &master);
	maxfd = server_socket;

	/*
	 * Main programm loop
	 */
	while(server_running)
	{
		rs = master; /* Structure assignment */

		if((ready = select(maxfd + 1, &rs, NULL, NULL, NULL)) == -1)
		{
			if(errno == EINTR)
			{
				continue;
			}

			log_error("server: select");
		}

		/*
		 * New client connection
		 */
		if(FD_ISSET(server_socket, &rs))
		{
			/*
			 * Get a free client slot
			 */
			for(i = 0; server_clients[i] > 0; ++i);

			if(server_clients[i] == 0)
			{
				log_error("server: client slots depleted");

				/*
				 * Disable server_socket in master set and queue new connections
				 */
				FD_CLR(server_socket, &master);

				goto server_slots_depleted;
			}

			len = sizeof caddr;

			server_clients[i] = accept(server_socket,
			    (struct sockaddr*) &caddr, &len);

			if(server_clients[i] == -1)
			{
				log_error("server: accept");
			}

			else
			{
				/*
			 	 * Add client to the master set
			 	 */
				FD_SET(server_clients[i], &master);
				if(server_clients[i] > maxfd) {
					maxfd = server_clients[i];
				}
			}

server_slots_depleted:

			if(--ready == 0) {
				continue;
			}
		}

		for(i = 0; server_clients[i] && ready; ++i) {
			if(server_clients[i] == -1) {
				continue;
			}

			if(!FD_ISSET(server_clients[i], &rs)) {
				continue;
			}

			--ready;

			/*
			 * Handle request
			 */
			len = server_update(server_clients[i]);
			if (len == -1)
			{
				log_error("server_main: server_update failed");
			}

			if (len)
			{
				continue;
			}

			/*
			 * Close client socket
			 */
			FD_CLR(server_clients[i], &master);
			close(server_clients[i]);
			server_clients[i] = -1;

			if(server_clients[i] == maxfd)
			{
				maxfd = 0; /* new max fd calculated below */
			}

			/*
			 * Slot available. Accept new connections if disabled
			 */
			if(!FD_ISSET(server_socket, &master))
			{
				FD_SET(server_socket, &master);
			}
		}

		if(maxfd)
		{
			continue;
		}

		/*
		 * Calculate new max fd
		 */
		for(i = 0; server_clients[i]; ++i)
		{
			if(server_clients[i] > maxfd)
			{
				maxfd = server_clients[i];
			}
		}
	}

	log_debug("server_main: shutdown");

	/*
	 * Close all sockets
	 */
	close(server_socket);

	for(i = 0; server_clients[i] > 0; ++i)
	{
		if (server_clients[i] > 0)
		{
			close(server_clients[i]);
		}
	}

	pthread_exit(NULL);

	return NULL;
}


int
server_init()
{
	/*
	 * Start server thread
	 */
	if (util_thread_create(&server_thread, &server_attr, server_main))
	{
		log_error("server_init: util_thread_create failed");
		return -1;
	}

	return 0;
}


void
server_clear(void)
{
	server_running = 0;

	if (pthread_kill(server_thread, SIGUSR2))
	{
		log_error("server_clear: pthread_kill");
	}

	util_thread_join(server_thread);

	return;
}
