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
#define MAX_BUFFER 10000000

static server_function_t server_functions[] = {
	{ "greylist_dump",	"Dump greylist tuples",		server_greylist_dump },
	{ "greylist_pass",	"Let tuple pass greylistung",	server_greylist_pass },
	{ "help",		"Print this message",		server_help },
	{ "quit",		"close connection",		server_quit },
#ifdef DEBUG
	{ "dummy",		"function dummy",		server_dummy },
#endif
	{ NULL,			NULL,				NULL },
};

static sht_t server_function_table;
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


int server_ok(int sock)
{
	int n;

	n = write(sock, "OK", 2);
	if (n == -1)
	{
		log_sys_error("server_ok: write");
		return -1;
	}	

	return 0;
}


int server_check(int sock)
{
	char ok[2];
	int n;

	n = read(sock, ok, sizeof ok);
	if (n == -1)
	{
		log_sys_error("server_check: read");
		return -1;
	}

	if (memcmp(ok, "OK", 2))
	{
		return 1;
	}

	return 0;
}


int
server_reply(int sock, char *message, ...)
{
	int len, n;
	char buffer[RECV_BUFFER];
        va_list ap;

        va_start(ap, message);
	len = vsnprintf(buffer, sizeof buffer, message, ap);
        va_end(ap);

	if (len >= sizeof buffer - 1) // Need to add \n here
	{
		log_error("server_reply: buffer exhausted");
		return -1;
	}

	// No need for \0 here.
	buffer[len] = '\n';

	n = write(sock, buffer, len + 1);
	if (n == -1)
	{
		log_sys_error("server_reply: write");
		return -1;
	}

	return n;
}

static int
server_output(int sock, char *buffer, int size)
{
	int n;

	if (server_reply(sock, "%d", size) == -1)
	{
		log_error("server_output: server_reply failed");
		return -1;
	}

	if (server_check(sock))
	{
		log_error("server_output: server_check failed");
		return -1;
	}

	n = write(sock, buffer, size);
	if (n == -1)
	{
		log_sys_error("server_output: write");
		return -1;
	}

	return n;
}


int
server_cmd(int sock, char *cmd)
{
	log_debug("server_cmd: command '%s'", cmd);
	if (server_reply(sock, cmd) == -1)
	{
		log_error("server_data_cmd: server_reply failed");
		return -1;
	}

	if (server_check(sock))
	{
		log_sys_error("server_data_cmd: server_check failed");
		return -1;
	}

	return 0;
}

int
server_data_cmd(int sock, char *cmd, char **buffer)
{
	char recv[RECV_BUFFER];
	int n;

	log_debug("server_data_cmd: command '%s'", cmd);
	if (server_reply(sock, cmd) == -1)
	{
		log_error("server_data_cmd: server_reply failed");
		return -1;
	}

	n = read(sock, recv, sizeof recv);
	if (n == -1)
	{
		log_sys_error("server_data_cmd: read");
		return -1;
	}

	n = atoi(recv);
	if (n < 0 || n > MAX_BUFFER)
	{
		log_error("server_data_cmd: bad buffer size");
		return -1;
	}

	*buffer = (char *) malloc(n + 1);
	if (*buffer == NULL)
	{
		log_sys_error("server_data_cmd: malloc");
		return -1;
	}

	if (server_ok(sock))
	{
		log_sys_error("server_data_cmd: server_ok failed");
		return -1;
	}

	n = read(sock, *buffer, n);
	if (n == -1)
	{
		log_sys_error("server_data_cmd: read");
		return -1;
	}

	(*buffer)[n] = 0;

	if (server_check(sock))
	{
		log_sys_error("server_data_cmd: server_check failed");
		return -1;
	}

	return 0;
}


int
server_help(int sock, int argc, char **argv)
{
	server_function_t *func;
	char buffer[RECV_BUFFER];

	if (strncmp(argv[0], "help", 4))
	{
		server_reply(sock, "Unknown command");
	}
	server_reply(sock, "HELP:");

	for (func = server_functions; func->sf_name; ++func)
	{
		util_concat(buffer, sizeof buffer, func->sf_name, "\t", func->sf_help, NULL);
		server_reply(sock, buffer);
	}
	
	return 1;
}


int
server_greylist_dump(int sock, int argc, char **argv)
{
	char *dump = NULL;
	int len;
	int r = -1;

	len = greylist_dump(&dump);

	log_debug("server_greylist_dump: greylist size: %d bytes", len);

	switch(len)
	{
	case 0:
		server_reply(sock, "greylist empty");
		return 1;
	case -1:
		log_error("server_greylist_dump: greylist_dump failed");
		goto error;
	default:
		break;
	}

	if(server_output(sock, dump, len) == -1)
	{
		log_sys_error("server_greylist_dump: write");
	}
	else
	{
		r = 1; //OK
	}

error:
	if (dump)
	{
		free(dump);
	}
	
	return r;
}


int
server_greylist_pass(int sock, int argc, char **argv)
{
	if (argc != 4)
	{
		server_reply(sock, "Usage: %s source from rcpt", argv[0]);
		return -1;
	}

	switch(greylist_pass(argv[1], argv[2], argv[3]))
	{
	case -1:
		log_error("server_greylist_pass: greylist_pass failed");
		return -1;
	case 0:
		server_reply(sock, "Not found");
	default:
		break;
	}

	return 1;
}


int
server_quit(int sock, int argc, char **argv)
{
	return 0;
}


int
server_dummy(int sock, int argc, char **argv)
{
	return 1;
}


static int
server_exec_cmd(int sock, char *cmd)
{
	int argc = 0;
	char *argv[MAXARGS];
	char *save, *p, *nil;
	server_function_t *sf;
	int r;

	for (nil = cmd; (p = strtok_r(nil, " ", &save)) && argc < MAXARGS; nil = NULL, ++argc)
	{
		argv[argc] = p;
	}

	if (argc == MAXARGS)
	{
		server_reply(sock, "Too many arguments");
		log_error("server_exec_cmd: Too many arguments");
		return -1;
	}

	sf = sht_lookup(&server_function_table, argv[0]);
	if (sf == NULL)
	{
		sf = sht_lookup(&server_function_table, "help");
		if (sf == NULL)
		{
			log_die(EX_SOFTWARE, "server_exec_cmd: help not found. This is impossible hence fatal.");
		}
	}

	r = sf->sf_callback(sock, argc, argv);
	switch (r)
	{
	case 0:
		server_reply(sock, "CLOSE");
		break;
	case -1:
		log_error("server_exec_cmd: %s failed", argv[0]);
		server_reply(sock, "ERROR");
		break;
	default:
		server_ok(sock);
		break;
	}

	return r;
}

static int
server_request(int sock)
{
	char cmd_buffer[RECV_BUFFER];
	int len;
	int r;

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
	 * Strip trailing newlines
	 */
	while (cmd_buffer[len - 1] == '\n')
	{
		cmd_buffer[--len] = 0;
	}

	/*
	 * No command specified
	 */
	if (!len)
	{
		return 0;
	}

	r = server_exec_cmd(sock, cmd_buffer);
	if (r == -1)
	{
		log_error("server_request: server_exec_cmd failed");
	}

	return r;
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
	server_socket = sock_listen(cf_control_socket, BACKLOG);
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
			switch(server_request(server_clients[i]))
			{
			case -1:
				log_error("server_main: server_request failed");
			case 0:
				break;
			default:
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
	if (!cf_control_socket)
	{
		log_debug("server_init: server_socket is empty: exit");
		return 0;
	}

	/*
	 * Load function table
	 */
	if (sht_init(&server_function_table, FUNC_BUCKETS, NULL))
	{
		log_die(EX_SOFTWARE, "server_init: sht_init failed");
	}

	for (func = server_functions; func->sf_name; ++func)
	{
		if (sht_insert(&server_function_table, func->sf_name, func))
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
	if (!cf_control_socket)
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

	sht_clear(&server_function_table);

	return;
}
