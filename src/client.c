#include <config.h>

#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>

#include <mopher.h>

#define SYNC_BACKLOG 16
#define SEND_BUFFER 4096

static ll_t		*client_servers;
static char		 client_id[256];
static int		 client_running;
static ll_t		*client_queue;
static pthread_t	 client_thread;
static pthread_mutex_t	 client_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	 client_cond = PTHREAD_COND_INITIALIZER;


static void
client_delete(client_t *c)
{
	if (c->c_queue)
	{
		ll_delete(c->c_queue, (ll_delete_t) var_delete);
	}

	if (c->c_socket > 0)
	{
		close(c->c_socket);
	}

	free(c);

	return;
}


static client_t *
client_create(char *name, char *path, char *myname, char *secret)
{
	client_t	*c = NULL;
	ll_t		*q = NULL;

	c = (client_t *) malloc(sizeof (client_t));
	if (c == NULL)
	{
		log_error("client_create: malloc");
		goto error;
	}

	memset(c, 0, sizeof (client_t));

	q = ll_create();
	if (q == NULL)
	{
		log_error("client_create: ll_create failed");
		goto error;
	}
	
	c->c_server = name;
	c->c_path = path;
	c->c_myname = myname;
	c->c_secret = secret;
	c->c_socket = -1;
	c->c_queue = q;

	return c;


error:

	if (c)
	{
		client_delete(c);
	}

	if(q)
	{
		ll_delete(q, NULL);
	}

	return NULL;
}


static int
client_server_add(char *name, char *path, char *myname, char *secret)
{
	client_t *c = NULL;

	c = client_create(name, path, myname, secret);
	if (c == NULL)
	{
		log_error("client_server_add: client_create failed");
		goto error;
	}

	if (LL_INSERT(client_servers, c) == -1)
	{
		log_error("client_server_add: LL_INSERT failed");
		goto error;
	}

	return 0;

error:
	if (c)
	{
		client_delete(c);
	}

	return -1;
}


static void
client_flush(client_t *c)
{
	var_t *record;

	while((record = LL_DEQUEUE(c->c_queue)))
	{
		if (client_send(c, record))
		{
			log_error("client_flush: client_send failed");

			LL_PUSH(c->c_queue, record);

			return;
		}

		var_delete(record);
	}

	log_notice("client_flush: \"%s\" queue flushed", c->c_server);

	return;
}


static int
client_connect(client_t *c)
{
	if (c->c_socket != -1)
	{
		close(c->c_socket);
	}

	log_info("client_connect: connecting server \"%s\"", c->c_path);

	c->c_socket = sock_connect(c->c_path);
	if (c->c_socket == -1)
	{
		log_error("client_connect: connection to \"%s\" failed",
		    c->c_path);

		return -1;
	}

	if (c->c_queue->ll_size > 0)
	{
		log_warning("client_connect: \"%s\" reconnected. flushing "
		    "queue", c->c_server);

		client_flush(c);
	}

	return 0;
}

static int
client_reconnect(void)
{
	client_t *server;
	int disconnected = 0;

	for (ll_rewind(client_servers); (server = ll_next(client_servers));)
	{
		if (server->c_socket != -1)
		{
			continue;
		}

		if (client_connect(server))
		{
			++disconnected;
		}
	}

	return disconnected;
}

int
client_send(client_t *c, var_t *record)
{
	char buffer[SEND_BUFFER];
	int len;

	if (c->c_socket == -1)
	{
		client_connect(c);
	}

	if (c->c_socket == -1)
	{
		return -1;
	}

	len = var_dump(record, buffer, sizeof buffer - 1);
	if (len == -1)
	{
		log_error("client_send: var_dump failed");
		return -1;
	}

	buffer[len] = '\n';
	buffer[++len] = 0;

	if (write(c->c_socket, buffer, len) < len)
	{
		log_error("client_send: write");

		close(c->c_socket);
		c->c_socket = -1;

		return -1;
	}

	return 0;
}


static void
client_enqueue(client_t *c, var_t *record)
{
	var_t *copy;

	copy = VAR_COPY(record);
	if (copy == NULL)
	{
		log_error("client_enqueue: VAR_COPY failed: record lost");
		return;
	}

	if (LL_ENQUEUE(c->c_queue, copy) == -1)
	{
		log_error("client_enqueue: LL_ENQUEUE failed: record lost");
		var_delete(copy);
	}

	return;
}


static int
client_update(void)
{
	client_t *server;
	var_t *record;
	int incomplete = 0;

	while ((record = LL_DEQUEUE(client_queue)))
	{
		ll_rewind(client_servers);
		while ((server = ll_next(client_servers)))
		{
			if (client_send(server, record) == 0)
			{
				continue;
			}

			log_error("client_update: client_send failed");

			++incomplete;

			client_enqueue(server, record);
		}

		var_delete(record);
	}

	return incomplete;
}


static void *
client_main(void *arg)
{
	int		r, incomplete = 0;
	struct timespec	ts;

	/*
	 * Client thread is running
	 */
	client_running = 1;

	/*
	 * Get current time
	 */
	if (util_now(&ts))
	{
		log_error("client_main: util_now failed");
		goto error;
	}

	log_debug("client_main: client thread running");

	if (pthread_mutex_lock(&client_mutex))
	{
		log_error("client_main: pthread_mutex_lock");
		goto error;
	}

	while (client_running)
	{
		r = pthread_cond_timedwait(&client_cond, &client_mutex, &ts);

		/*
		 * Signaled wakeup
		 */
		if (r == 0)
		{
			log_debug("client_main: start synchronisation");
			incomplete = client_update();

			continue;
		}

		/*
		 * Error
		 */
		else if (r != ETIMEDOUT)
		{
			log_error("client_main: pthread_cond_wait");
			break;
		}

		/*
		 * Timeout
		 */
		if (incomplete)
		{
			incomplete = client_reconnect();
		}

		ts.tv_sec += cf_client_retry_interval;
	}

	log_debug("client_main: shutdown");

error:
	client_running = 0;

	if (pthread_mutex_unlock(&client_mutex))
	{
		log_error("client_main: pthread_mutex_unlock");
	}

	pthread_exit(NULL);

	return NULL;
}

int
client_sync(dbt_t *dbt, var_t *record)
{
	var_t	*copy;

	/*
	 * No one to sync with
	 */
	if (client_servers->ll_size == 0) 
	{
		return 0;
	}

	if (client_running == 0)
	{
		log_error("client_sync: client thread has terminated");
		return -1;
	}

	copy = VAR_COPY(record);
	if (copy == NULL)
	{
		log_error("client_sync: VAR_COPY failed");
		return -1;
	}

	if (pthread_mutex_lock(&client_mutex))
	{
		log_error("client_sync: pthread_mutex_lock");
		return -1;
	}

	if (pthread_cond_signal(&client_cond))
	{
		log_error("client_sync: pthread_cond_signal");
		return -1;
	}

	LL_ENQUEUE(client_queue, copy);

	if (pthread_mutex_unlock(&client_mutex))
	{
		log_error("client_sync: pthread_mutex_unlock");
	}

	return 0;
}


int
client_init()
{
	var_t *servers;
	ht_t *server_table;
	var_t *server;
	char *name;
	char *path;
	char *secret;
	char *myname;
	int len;

	client_servers = ll_create();
	if (client_servers == NULL)
	{
		log_error("client_init: ll_create failed");
		return -1;
	}

	/*
	 * Create server list from config
	 */
	servers = cf_get(VT_TABLE, "server", NULL);
	if (servers == NULL)
	{
		log_info("client_init: no servers to sync with");
		return 0;
	}

	server_table = servers->v_data;

	for (ht_rewind(server_table); (server = ht_next(server_table));)
	{
		name = server->v_name;
		path = vtable_get(server, "socket");
		secret = vtable_get(server, "secret");
		myname = vtable_get(server, "myname");

		if (myname == NULL)
		{
			myname = cf_hostname;
		}

		if (client_server_add(name, path, myname, secret))
		{
			log_error("client_init: client_server_add failed");
			return -1;
		}
	}

	/*
	 * Set client id
	 */
	len = snprintf(client_id, sizeof client_id, "%s:%d", cf_hostname,
	    getpid());

	if (len >= sizeof client_id)
	{
		log_error("client_init: buffer exhausted");
		return -1;
	}

	log_debug("client_init: my id is \"%s\"", client_id);

	/*
	 * Create update queue
	 */
	client_queue = ll_create();
	if (client_queue == NULL)
	{
		log_error("client_init: ll_create failed");
		return -1;
	}

	/*
	 * Start thread
	 */
	if (util_thread_create(&client_thread, client_main))
	{
		log_error("client_init: util_thread_create failed");
		return -1;
	}

	return 0;
}


void
client_clear(void)
{
	if (client_servers->ll_size == 0) 
	{
		ll_delete(client_servers, (ll_delete_t) client_delete);
		return;
	}

	if (pthread_mutex_lock(&client_mutex))
	{
		log_error("client_clear: pthread_mutex_lock");
	}

	client_running = 0;

	if (pthread_cond_signal(&client_cond))
	{
		log_error("client_clear: pthread_cond_signal");
	}

	if (pthread_mutex_unlock(&client_mutex))
	{
		log_error("client_clear: pthread_mutex_unlock");
	}

	if (pthread_join(client_thread, NULL))
	{
		log_error("client_clear: pthread_mutex_join");
	}

	ll_delete(client_queue, (ll_delete_t) var_delete);
	ll_delete(client_servers, (ll_delete_t) client_delete);

	return;
}
