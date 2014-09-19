#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netdb.h>

#include <log.h>

static int
sock_unix_listen(char *path, int backlog)
{
	int fd = 0;
	struct sockaddr_un sa;

	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_LOCAL;
	strncpy(sa.sun_path, path, sizeof(sa.sun_path));

	fd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if(fd == -1) {
		log_sys_error("sock_unix_listen: socket");
		goto error;
	}

	if(bind(fd, (struct sockaddr*) &sa, sizeof sa) == -1) {
		log_sys_error("sock_unix_listen: bind: %s", path);
		goto error;
	}

	if(listen(fd, backlog) == -1) {
		log_sys_error("sock_unix_listen: listen: %s", path);
		goto error;
	}

	if (util_chmod(path, cf_control_socket_permissions))
	{
		log_die(EX_SOFTWARE, "sock_unix_listen: util_chmod failed");
	}

	return fd;


error:

	if (fd > 0) {
		close(fd);
	}

	return -1;

}


void
sock_unix_unlink(char *uri)
{
	if(strncmp(uri, "unix:", 5)) {
		log_error("sock_unix_unlink: bad socket name \"%s\"", uri + 5);
		return;
	}

	if(unlink(uri + 5)) {
		log_sys_error("sock_unix_unlink: unlink %s", uri + 5);
	}

	log_debug("sock_unix_unlink: %s removed", uri + 5);

	return;
}


static int
sock_unix_connect(char *path)
{
	int fd;
	struct sockaddr_un sa;

	fd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (fd == -1) {
		log_sys_error("sock_unix_connect: socket: \"%s\"", path);
		return -1;
	}

	memset(&sa, 0, sizeof sa);

	sa.sun_family = AF_LOCAL;
	strcpy(sa.sun_path, path);

	if(connect(fd, (struct sockaddr *) &sa, sizeof sa) == -1) {
		log_sys_error("sock_connect_unix: connect: \"%s\"", path);
		return -1;
	}

	return fd;
}


static int
sock_inet_listen(char *bindaddr, char *port, int backlog)
{
	struct addrinfo hints;
	struct addrinfo *res;
	struct addrinfo *next;
	int e;
	int fd = -1;
	int opt = 1;

	memset(&hints, 0, sizeof(hints));

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	e = getaddrinfo(bindaddr, port, &hints, &res);
	if (e) {
		log_sys_error("sock_inet_listen: getaddrinfo: %s",
			gai_strerror(e));
		return -1;
	}

	for(next = res; next != NULL; next = next->ai_next) {
		fd = socket(next->ai_family, next->ai_socktype,
			next->ai_protocol);

		if(fd == -1) {
			continue;
		}

		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt))
		{
			log_sys_error("sock_inet_listen: setsockopt");
		}

		if(bind(fd, next->ai_addr, next->ai_addrlen) == 0) {
			break;
		}

		close(fd);
		fd = -1;
	}

	freeaddrinfo(res);

	if (fd == -1) {
		log_die(EX_OSERR, "sock_inet_listen: can't bind to %s:%s",
			bindaddr ? bindaddr : "*", port);
	}

	if(listen(fd, backlog) == -1) {
		log_sys_die(EX_OSERR, "sock_inet_listen: listen \"%d\"", port);
	}

	return fd;
}


static int
sock_inet_connect(char *host, char *port)
{
	struct addrinfo *res, *ai;
	struct addrinfo hints;
	int e;
	int fd = -1;


	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;

	if((e = getaddrinfo(host, port, &hints, &ai))) {
		log_sys_error("sock_inet_connect: getaddrinfo %s:%s: %s",
			host, port, gai_strerror(e));
		return -1;
	}

	for(res = ai;res != NULL; res = res->ai_next) {
		fd = socket(res->ai_family, res->ai_socktype,
			res->ai_protocol);

		if(fd == -1) {
			continue;
		}

		if(connect(fd, res->ai_addr, res->ai_addrlen) == 0) {
			break;
		}

		close(fd);
		fd = -1;
	}

	freeaddrinfo(ai);
	if (fd == -1) {
		log_error("sock_inet_connect: connection to %s:%s failed", host,
			port);
		return -1;
	}

	return fd;
}


int
sock_listen(char *uri, int backlog)
{
	char *bindaddr;
	char *port;

	if(strncmp(uri, "unix:", 5) == 0) {
		return sock_unix_listen(uri + 5, backlog);
	}
	
	if(strncmp(uri, "inet:", 5) == 0) {
		if((bindaddr = strrchr(uri + 5, '@'))) {
			port = uri + 5;
			*bindaddr++ = 0;
		}
		else {
			bindaddr = NULL;
			port = uri + 5;
		}

		return sock_inet_listen(bindaddr, port, backlog);
	}

	log_error("sock_listen: bad socket string \"%s\"", uri);

	return -1;
}


int
sock_connect(char *uri)
{
	char *host;
	char *port;
	int r;

	/*
	 * UNIX domain sockets
	 */
	if(strncmp(uri, "unix:", 5) == 0) {
		return sock_unix_connect(uri + 5);
	}
	
	if(strncmp(uri, "inet:", 5) != 0) {
		log_error("sock_connect: bad socket uri '%s'", uri);
		return -1;
	}

	/*
	 * TCP sockets
	 */
	if((port = strdup(uri + 5)) == NULL) {
		log_sys_error("sock_connect: strdup");
		return -1;
	}
	
	if((host = strrchr(port, '@')) == NULL) {
		log_error("sock_connect: bad socket string \"%s\"", port);
		return -1;
	}
	*host++ = 0;

	r = sock_inet_connect(host, port);

	free(port);

	return r;
}
