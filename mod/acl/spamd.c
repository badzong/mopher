#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#include "lib.h"
#include "milter.h"

#define BUFLEN 1024

#define SPAMD_SPAMD "SPAMD/"
#define SPAMD_SPAMDLEN 6
#define SPAMD_VERSION "1.1"
#define SPAMD_VERSIONLEN 3
#define SPAMD_EX_OK "EX_OK"
#define SPAMD_EX_OKLEN 5
#define SPAMD_SPAM "Spam: "
#define SPAMD_SPAMLEN 6
#define SPAMD_TRUE "True ; "
#define SPAMD_TRUELEN 7
#define SPAMD_FALSE "False ; "
#define SPAMD_FALSELEN 8


static char *spamd_symbols[] = { "spamd_status", "spamd_score",
	"spamd_symbols", NULL};

static int
spamd_header(var_t *attrs, char *header, int len)
{
	char *hostname;
	char *addrstr;
	char *helo;
	char *envfrom;
	VAR_INT_T *recipients;
	char *envrcpt;
	char *queueid;
	time_t t;
	char timestamp[BUFLEN];
	struct tm tm;

	if (var_table_dereference(attrs, "milter_received", &t,
		"milter_hostname", &hostname, "milter_helo", &helo,
		"milter_envfrom", &envfrom, "milter_envrcpt", &envrcpt,
		"milter_recipients", &recipients, "milter_queueid", &queueid,
		"milter_addrstr", &addrstr, NULL));
	{
		log_error("spamd_header: var_table_dereference failed");
		goto error;
	}

	if (gmtime_r(&t, &tm) == NULL) {
		log_error("spamd_header: gmtime failed");
		return -1;
	}

	if (strftime(timestamp, sizeof(timestamp),
		"%a, %d %b %Y %H:%M:%S +0000", &tm) == 0)
	{
		log_error("spamd_header: strftime failed");
		timestamp[0] = '\0';
	}

	if(mp->mp_recipients == 1) {
		snprintf(header, len, "Received: from %s (%s [%s])\r\n"
			"\tby %s (envelope-sender <%s>) (%s) with SMTP id %s\r"
			"\n\tfor <%s>; %s\r\n", helo, hostname, address,
			cf_hostname, envfrom, BINNAME, queueid, envrcpt,
			timestamp);
	} else {
		snprintf(header, len, "Received: from %s (%s [%s])\r\n"
			"\tby %s (envelope-sender <%s>) (%s) with SMTP id %s;"
			"\r\n\t%s\r\n", helo, hostname, address, cf_hostname,
			envfrom, BINNAME, queueid, timestamp);
	}

	return 0;
}


int
spamd_query(var_t *attrs)
{
	int sock;
	int n;
	char header[BUFLEN];
	char buffer[BUFLEN];
	char *p, *q;
	VAR_INT_T size;

	if (var_table_dereference(attrs, "milter_size", &size, NULL));
	{
		log_error("spamd_header: var_table_dereference failed");
		goto error;
	}

	if(spamd_header(mp, header, sizeof(header))) {
		log_error("spamd_query: spamd_header failed");
		return -1;
	}

	snprintf(buffer, sizeof(buffer), "SYMBOLS SPAMC/1.2\r\n"
		"Content-length: %d\r\n\r\n", size + strlen(header));

	/*
	 * HERE!
	 */
	if((sock = socket_connect(config("spamd_socket"))) == -1) {
		log_error("spamd_query: socket_connect failed");
		return -1;
	}
	 
	 /*
	  * Write spamassassin request
	  */
	if(write(sock, buffer, strlen(buffer)) == -1) {
		log_error("spamd_query: write");
		return -1;
	}

	/*
	 * Write header
	 */
	if(write(sock, header, strlen(header)) == -1) {
		log_error("spamd_query: write");
		return -1;
	}

	/*
	 * Write message
	 */
	if(write(sock, mp->mp_message, mp->mp_size) == -1) {
		log_error("spamd_query: write");
		return -1;
	}

	/*
	 * Read response
	 */
	if((n = read(sock, buffer, sizeof buffer - 1)) == -1) {
		log_error("spamd_query: read");
		return -1;
	}
	buffer[n] = 0;

	/*
	 * Parse response
	 */
	p = buffer;
	if(strncmp(p, SPAMD_SPAMD, SPAMD_SPAMDLEN)) {
		log_error("spamd_query: protocol error");
		return -1;
	}

	p += SPAMD_SPAMDLEN;
	if (strncmp(p, SPAMD_VERSION, SPAMD_VERSIONLEN)) {
		log_notice("spamd_query: protocol version mismtach");
	}

	p += SPAMD_VERSIONLEN;
	if((p = strstr(p, SPAMD_EX_OK)) == NULL) {
		log_error("spamd_query: spamd returned non EX_OK");
		return -1;
	}

	/*
	 * Spamd returns 2 lines. Read 2nd line if neccessary.
	 */
	p += SPAMD_EX_OKLEN;
	if(strlen(p) <= 2) {  /* '\r\n' */
		if((n = read(sock, buffer, sizeof buffer)) == -1) {
			log_error("spamd_query: read");
			return -1;
		}
		buffer[n] = 0;
		p = buffer;
	}
	else {
		p += 2;  /* '\r\n' */
	}

	/*
	 * Parse results
	 */
	if(strncmp(p, SPAMD_SPAM, SPAMD_SPAMLEN)) {
		log_error("spamd_query: protocol error");
		return -1;
	}

	p += SPAMD_SPAMLEN;
	if(!strncmp(p, SPAMD_TRUE, SPAMD_TRUELEN)) {
		map_set(&mp->mp_info, "spamd_status", "spam");
		p += SPAMD_TRUELEN;
	}
	else if(!strncmp(p, SPAMD_FALSE, SPAMD_FALSELEN)) {
		map_set(&mp->mp_info, "spamd_status", "ham");
		p += SPAMD_FALSELEN;
	}
	else
	{
		log_error("spamd_query: protocol error");
		return -1;
	}

	/*
	 * Cut score.
	 */
	if((q = strchr(p, ' ')) == NULL) {
		log_error("spamd_query: protocol error");
		return -1;
	}
	*q++ = 0;

	map_set(&mp->mp_info, "spamd_score", p);

	/*
	 * Set SYMBOLS
	 */
	if((p = strchr(q, '\r')) == NULL) {
		log_error("spamd_query: protocol error");
		return -1;
	}
	p += 4; /* \r\n\r\n */
	if((q = strchr(p, '\r')) == NULL) {
		log_error("spamd_query: protocol error");
		return -1;
	}
	*q = 0;

	map_set_str(&mp->mp_info, "spamd_symbols", p, ',');

	close(sock);

	return 0;
}


int
init(void)
{
	char **p;
	int r;

	for (p = spamd_symbols; *p; ++p) {
		if (!acl_symbol_register(AS_CALLBACK, *p, spamd)) {
			continue;
		}

		log_error("spamd: init: acl_symbol_register failed");
		return -1;
	}
}
