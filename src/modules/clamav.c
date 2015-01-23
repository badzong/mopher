#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>

#include <mopher.h>

#define CLAMAV_CLEAN "clamav_clean"
#define CLAMAV_VIRUS "clamav_virus"
#define CLAMAV_INSTREAM "zINSTREAM\0"
#define CLAMAV_INSTREAMLEN 10
#define CLAMAV_STREAM "stream: "
#define CLAMAV_STREAMLEN 8
#define CLAMAV_OK "OK"
#define CLAMAV_OKLEN 2
#define CLAMAV_FOUND " FOUND"
#define CLAMAV_FOUNDLEN 6

#define BUFLEN 4096

static sock_rr_t clamav_srr;

int
clamav_query(milter_stage_t stage, char *name, var_t *attrs)
{
	int sock = 0;
	int n;
	char buffer[BUFLEN];
	char *message = NULL;
	VAR_INT_T *message_size;
	int32_t size;
	long zero = 0;
	char *virus = NULL;
	VAR_INT_T clean = 1;

	message_size = vtable_get(attrs, "message_size");
	if (message_size == NULL)
	{
		log_error("clamav_query: vtable_get failed");
		goto error;
	}

	/*
	 * Allocate message buffer
	 */
	message = (char *) malloc(*message_size + 1);
	if (message == NULL)
	{
		log_sys_error("clamav_query: malloc");
		goto error;
	}

	if (milter_dump_message(message, *message_size + 1, attrs) == -1)
	{
		log_error("clamav_query: milter_dump_message failed");
		goto error;
	}

	sock = sock_connect_rr(&clamav_srr);
	if (sock == -1)
	{
		log_error("clamav_query: sock_connect failed");
		goto error;
	}

	/*
	 * Write zINSTREAM
	 */
	if (write(sock, CLAMAV_INSTREAM, CLAMAV_INSTREAMLEN) == -1) {
		log_sys_error("clamav_query: write");
		goto error;
	}

	/*
	 * Write size
	 */
	size = htonl(*message_size);
	if (write(sock, &size, sizeof size) == -1) {
		log_sys_error("clamav_query: write");
		goto error;
	}
	
	/*
	 * Write message
	 */
	if (write(sock, message, *message_size) == -1) {
		log_sys_error("clamav_query: write");
		goto error;
	}

	/*
	 * Write 0. All 4 bytes are 0. No need to htonl().
	 */
	if (write(sock, &zero, sizeof zero) == -1) {
		log_sys_error("clamav_query: write");
		goto error;
	}

	/*
	 * Read response
	 */
	n = read(sock, buffer, sizeof buffer - 1);
	if (n == -1) {
		log_sys_error("clamav_query: read");
		goto error;
	}
	buffer[n] = 0;

	/*
	 * No answer.
         */
	if (n == 0)
	{
		log_error("clamav_query: no data received");
		goto error;
	}

	/*
	 * OK => No virus found
	 */
	if (strncmp(buffer + n - CLAMAV_OKLEN - 1, CLAMAV_OK, CLAMAV_OKLEN) == 0)
	{
		log_message(LOG_ERR, attrs, "clamav: clean message");
		goto exit;
	}

	/*
	 * It's not OK, so it should be FOUND
	 */
	if (strncmp(buffer + n - CLAMAV_FOUNDLEN - 1, CLAMAV_FOUND,
		CLAMAV_FOUNDLEN))
	{
		log_error("clamav_query: protocol error: unexpected: %s",
			buffer);
		goto error;
	}

	/*
	 * Virus found
	 */
	buffer[n - CLAMAV_FOUNDLEN] = 0;
	virus = buffer + CLAMAV_STREAMLEN;
	clean = 0;
	
	log_message(LOG_ERR, attrs, "clamav: virus=%s", virus);

exit:
	if (vtable_setv(attrs,
		VT_STRING, CLAMAV_CLEAN, &clean, VF_KEEPNAME | VF_COPYDATA,
		VT_STRING, CLAMAV_VIRUS, virus, VF_KEEPNAME | VF_COPYDATA,
		0))
	{
		log_error("clamav_query: vtable_setv failed");
		goto error;
	}

	close(sock);
	free(message);

	return 0;

error:
	if (message)
	{
		free(message);
	}

	if (sock > 0) {
		close(sock);
	}

	return -1;
}


int
clamav_init(void)
{
	if (sock_rr_init(&clamav_srr, "clamav_socket"))
	{
		log_die(EX_SOFTWARE, "clamav_init: sock_rr_init failed");
	}

	acl_symbol_register(CLAMAV_CLEAN, MS_EOM, clamav_query, AS_CACHE);
	acl_symbol_register(CLAMAV_VIRUS, MS_EOM, clamav_query, AS_CACHE);

	return 0;
}

void
clamav_fini(void)
{
	sock_rr_clear(&clamav_srr);

	return;
}
