#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <mopher.h>


#define BUFSIZE 10000

static acl_action_type_t
pipe_socket(char *dest, char *message, int size)
{
	int sock;
	int n;
	
	sock = sock_connect(dest);
	if (sock == -1)
	{
		log_error("pipe_socket: sock_connect failed");
		return ACL_ERROR;
	}

	n = write(sock, message, size);
	if (n == -1)
	{
		log_sys_error("pipe_socket: write");
		return ACL_ERROR;
	}

	close(sock);

	return ACL_NONE;
}

static acl_action_type_t
pipe_exec(char *dest, char *message, int size)
{
	FILE *fp;
	int status;


	fp = popen(dest, "w");
	if (fp == NULL)
	{
		log_sys_error("pipe_exec: popen");
		return ACL_ERROR;
	}

	if (fwrite(message, size, 1, fp) != 1)
	{
		log_sys_error("pipe_exec: fwrite");
		pclose(fp);
		return ACL_ERROR;
	}

	status = pclose(fp);
	if (status)
	{
		log_error("pipe_exec: command failed");
		return ACL_ERROR;
	}

	return ACL_NONE;
}


acl_action_type_t
pipe_action(milter_stage_t stage, char *stagename, var_t *mailspec, void *data,
	int depth)
{
	exp_t *exp = data;
	var_t *v;
	char *dest;
	char *message;
	int size;
	acl_action_type_t a;
	
	v = exp_eval(exp, mailspec);
	if (v == NULL)
	{
		log_error("pipe_exec: exp_eval failed");
		return ACL_ERROR;
	}

	if (v->v_type != VT_STRING)
	{
		log_error("pipe_exec: bad expression in mail.acl");
		return ACL_ERROR;
	}

	dest = v->v_data;
	log_message(LOG_ERR, mailspec, "pipe_exec: %s", dest);

	size = milter_message(mailspec, &message);
	if (size == -1)
	{
		log_error("pipe_exec: milter_message failed");
		return ACL_ERROR;
	}

	if (strncmp(dest, "inet:", 5) == 0 || strncmp(dest, "unix:", 5) == 0)
	{
		a = pipe_socket(dest, message, size);
	}
	else
	{
		a = pipe_exec(dest, message, size);
	}

	free(message);

	return a;
}
