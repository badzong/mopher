#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#include <mopher.h>

#define MAX_CLIENTS 10
#define BACKLOG 16
#define RECV_BUFFER 4096
#define MAXARGS 16
#define FUNC_BUCKETS 64

static server_function_t server_functions[] = {
	{ "greylist",	"Dump greylist tuples",		server_greylist_dump },
	{ "pass",	"Let tuple pass greylistung",	server_greylist_pass },
	{ "help",	"Print this dialog",		server_help },
#ifdef DEBUG
	{ "echo",	"Echo input for debugging",	server_echo },
#endif
	{ NULL,		NULL,				NULL },
};

static sht_t *server_function_table;
static int server_socket;
static int server_clients[MAX_CLIENTS + 1];
static int server_running;
static pthread_t server_thread;


static void
server_usr2(int sig)
{
	log_debug("server_usr2: caught signal %d", sig);

	return;
}


static int
server_reply(int sock, char *message)
{
	int len, n;
	char buffer[RECV_BUFFER];

	len = util_concat(buffer, sizeof buffer, message, "\n", NULL);
	if (len == -1)
	{
		log_error("server_reply: util_concat failed");
		return -1;
	}

	n = write(sock, buffer, len);
	if (n == -1)
	{
		log_sys_error("server_reply: write");
		return -1;
	}

	return n;
}


int
server_help(int sock, int argc, char **argv)
{
	server_function_t *func;
	char buffer[RECV_BUFFER];

	for (func = server_functions; func->sf_name; ++func)
	{
		util_concat(buffer, sizeof buffer, func->sf_name, "\t", func->sf_help, NULL);
		server_reply(sock, buffer);
	}
	
	return 0;
}


int
server_greylist_dump(int sock, int argc, char **argv)
{
	return 0;
}


int
server_greylist_pass(int sock, int argc, char **argv)
{
	return 0;
}


int
server_echo(int sock, int argc, char **argv)
{
	return 0;
}


static int
server_exec_cmd(int sock, char *cmd)
{
	int argc = 0;
	char *argv[MAXARGS];
	char *save, *p, *nil;
	server_function_t *sf;

	for (nil = cmd; (p = strtok_r(nil, " ", &save)) && argc < MAXARGS; nil = NULL, ++argc)
	{
		argv[argc] = p;
	}

	if (argc == MAXARGS)
	{
		server_reply(sock, "ERROR: Too many arguments");
		log_error("server_exec_cmd: Too many arguments");
		return -1;
	}

	sf = sht_lookup(server_function_table, argv[0]);
	if (sf == NULL)
	{
		sf = sht_lookup(server_function_table, "help");
		if (sf == NULL)
		{
			log_die(EX_SOFTWARE, "server_exec_cmd: help not found. This is impossible hence fatal.");
		}
	}

	return sf->sf_callback(sock, argc, argv);
}

static int
server_request(int sock)
{
	char cmd_buffer[RECV_BUFFER];
	int len;

	len = read(sock, cmd_buffer, sizeof cmd_buffer);
	if (len == -1)
	{
		log_sys_error("server_request: read");
		return -1;
	}

	if (len == sizeof cmd_buffer)
	{
		log_sys_error("server_request: buffer exhausted");
		return -1;
	}

	cmd_buffer[len] = 0;

	/*
	 * Connection closed
	 */
	if (!len)
	{
		return 0;
	}

	if(server_exec_cmd(sock, cmd_buffer))
	{
		log_error("server_request: server_exec_cmd failed");
		return -1;
	}

	return len;
}

static int
server_update(int sock)
{
	dbt_t *dbt;
	char buffer[RECV_BUFFER];
	int len;
	char *p;
	char *name = NULL;
	var_t *record = NULL;

	len = read(sock, buffer, sizeof buffer);
	if (len == -1)
	{
		log_sys_error("server_update: read");
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
		log_sys_error("server_update: strndup");
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
	char *client_addr;

	/*
	 * Server is running
	 */
	server_running = 1;

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

			log_sys_error("server_main: select");
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
				log_error("server_main: client slots depleted");

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
				log_sys_error("server_main: accept");
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

			client_addr = util_addrtostr(&caddr);
			if (client_addr)
			{
				log_error("server_main: new client connection from %s", client_addr);
				free(client_addr);
			}
			else
			{
				log_error("server_main: util_addrtostr failed");
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
			len = server_request(server_clients[i]);
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
	server_function_t *func;

	/*
	 * Don't start the server if server_socket is empty
	 */
	if (!cf_server_socket)
	{
		log_debug("server_init: server_socket is empty: exit");
		return 0;
	}

	/*
	 * Load function table
	 */
	server_function_table = sht_create(FUNC_BUCKETS, NULL);
	if (server_function_table == NULL)
	{
		log_die(EX_SOFTWARE, "server_init: sht_create failed");
	}

	for (func = server_functions; func->sf_name; ++func)
	{
		if (sht_insert(server_function_table, func->sf_name, func))
		{
			log_die(EX_SOFTWARE, "server_init: sht_insert failed");
		}
	}
	
	/*
	 * Start server thread
	 */
	if (util_thread_create(&server_thread, server_main, NULL))
	{
		log_error("server_init: util_thread_create failed");
		return -1;
	}

	return 0;
}


void
server_clear(void)
{
	if (!cf_server_socket)
	{
		return;
	}

	server_running = 0;

	if (pthread_kill(server_thread, SIGUSR2))
	{
		log_sys_error("server_clear: pthread_kill");
	}

	util_thread_join(server_thread);

	/*
	 * Remove SIGUSR2 handler (to avoid an error on reload)
	 */
	if (util_signal(SIGUSR2, SIG_DFL))
	{
		log_error("server_main: util_signal failed");
	}

	sht_clear(server_function_table);

	return;
}
