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
#include <fcntl.h>
#include <errno.h>

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

int
sock_connect_timeout(int fd, struct sockaddr *sa, socklen_t len, int timeout)
{
	int arg, opt;
	socklen_t optlen;
	fd_set fdset; 
	struct timeval tv; 
	int success = -1;
	int deadline = time(NULL) + timeout;
	
	// Get Flags
	arg = fcntl(fd, F_GETFL, NULL);
	if (arg == -1)
	{
		log_sys_error("sock_connect_timeout: fcntl");
		return -1;
	}

	// Add O_NONBLOCK
	if (fcntl(fd, F_SETFL, arg | O_NONBLOCK) == -1)
	{ 
		log_sys_error("sock_connect_timeout: fcntl");
		return -1;
	}

	success = connect(fd, sa, len);
	if (success == -1)
	{
		if (errno != EINPROGRESS)
		{
			log_sys_error("sock_connect_timeout: connect");
			return -1;
		}
	}

	while (success == -1)
	{ 
		// Recalculate temiout in case of EINTR
		tv.tv_sec = deadline - time(NULL); 
		tv.tv_usec = 0; 

		FD_ZERO(&fdset); 
		FD_SET(fd, &fdset); 

		switch(select(fd + 1, NULL, &fdset, NULL, &tv))
		{
		case -1:
			if (errno == EINTR)
			{
				continue;
			}
			log_sys_error("sock_connect_timeout: select");
			return -1;

		// Time exceeded
		case 0:
			return -1;

		default:
			success = 0;
		}
	}

	// Socket connected
	optlen = sizeof opt;
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *) &opt, &optlen) == -1)
	{ 
		log_sys_error("sock_connect_timeout: getsockopt");
		return -1;
	} 
	if (opt)
	{
		log_sys_error("sock_connect_timeout: pending error");
		return -1;
	}

	// Get Flags 
	arg = fcntl(fd, F_GETFL, NULL);
	if (arg == -1)
	{
		log_sys_error("sock_connect_timeout: fcntl");
		return -1;
	}

	// Remove O_NONBLOCK
	if (fcntl(fd, F_SETFL, arg & (~O_NONBLOCK)) == -1)
	{ 
		log_sys_error("sock_connect_timeout: fcntl");
		return -1;
	}

	return 0;
}


static int
sock_unix_connect(char *path, int timeout)
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

	if(sock_connect_timeout(fd, (struct sockaddr *) &sa, sizeof sa, timeout))
	{
		log_error("sock_connect_unix: %s: sock_connect_timeout failed",
			path);
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
sock_inet_connect(char *host, char *port, int timeout)
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

		if(sock_connect_timeout(fd, res->ai_addr, res->ai_addrlen, timeout) == 0) {
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
		return sock_unix_connect(uri + 5, cf_connect_timeout);
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

	r = sock_inet_connect(host, port, cf_connect_timeout);

	free(port);

	return r;
}
