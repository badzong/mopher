#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <mopher.h>

#define BUFLEN 4096

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


static const char spamd_single[] = "Received: from %s (%s)\r\n\tby %s "
    "(envelope-sender <%s>) (%s) with SMTP id %s\r\n\tfor <%s>; %s\r\n";

static const char spamd_multi[] = "Received: from %s (%s)\r\n\tby %s "
    "(envelope-sender <%s>) (%s) with SMTP id %s;\r\n\t%s%s\r\n";

static char *spamd_symbols[] = { "spamd_spam", "spamd_score",
	"spamd_symbols", NULL};

static sock_rr_t spamd_srr;

static void
spamd_printable_buffer(char *buffer, int len)
{
	int i;

	/*
	 * we use len as index. So the last char is buffer[len - 1]
	 */
	--len;

	for (i = 0; i < len; ++i)
	{
		if (buffer[i] < 33 || buffer[i] > 126)
		{
			buffer[i] = '.';
		}
	}

	buffer[len] = 0;

	return;
}

static int
spamd_rdns_none(char *hostname, char *hostaddr)
{
	char *clean_hostname;
	int r = 0;

	clean_hostname = util_strdupenc(hostname, "[]");
	if (clean_hostname == NULL)
	{
		/*
                 * If we just ran out of memory. Don't care.
                 */
		return 0;
	}

	if (strcmp(clean_hostname, hostaddr) == 0)
	{
		r = 1;
	}

	free(clean_hostname);
	return r;
}

static int
spamd_header(var_t *attrs, char *header, int len)
{
	char *hostname;
	char *hostaddr_str;
	char *helo;
	char *envfrom;
	VAR_INT_T recipients;
	char *envrcpt;
	char *queue_id;
	time_t t;
	char timestamp[BUFLEN];
	char host[BUFLEN];
	struct tm tm;
	int r;
	const char *fmt;
	

	r = acl_symbol_dereference(attrs, "received", &t, "hostname",
		&hostname, "helo", &helo, "envfrom_addr", &envfrom,
		"envrcpt_addr", &envrcpt, "recipients", &recipients, "queue_id",
		&queue_id, "hostaddr_str", &hostaddr_str, NULL);
	if (r)
	{
		log_error("spamd_header: acl_symbol_dereference failed");
		return -1;
	}

	t = time(NULL);
	if (t == -1)
	{
		log_sys_error("spamd_header: time failed");
		return -1;
	}

	if (gmtime_r(&t, &tm) == NULL) {
		log_sys_error("spamd_header: gmtime failed");
		return -1;
	}

	if (strftime(timestamp, sizeof(timestamp),
	    "%a, %d %b %Y %H:%M:%S +0000", &tm) == 0)
	{
		log_sys_error("spamd_header: strftime failed");
		timestamp[0] = '\0';
	}

	if(recipients == 1)
	{
		fmt = spamd_single;
	}
	else
	{
		fmt = spamd_multi;
		envrcpt = "";
	}

	/*
 	 * Handle RDNS None
 	 */
	if (spamd_rdns_none(hostname, hostaddr_str))
	{
		r = snprintf(host, sizeof(host), "[%s]", hostaddr_str);
	}
	else
	{
		r = snprintf(host, sizeof(host), "%s [%s]", hostname,
			hostaddr_str);
	}

	if (r >= sizeof(host))
	{
		log_error("spamd_header: buffer exhausted");
		return -1;
	}

	len = snprintf(header, len, fmt, helo, host, cf_hostname,
		envfrom, BINNAME, queue_id, envrcpt, timestamp);

	return len;
}


int
spamd_query(milter_stage_t stage, char *name, var_t *attrs)
{
	int sock = 0;
	var_t *symbols = NULL;
	int n;
	char recv_header[BUFLEN];
	char buffer[BUFLEN];
	char *p, *q;
	char *message = NULL;
	VAR_INT_T spam;
	VAR_FLOAT_T score;
	VAR_INT_T *message_size;
	long header_size, size;

	/*
         * Build received header
         */
	header_size = spamd_header(attrs, recv_header, sizeof recv_header);
	if (header_size == -1)
	{
		log_error("spamd_query: spamd_header failed");
		goto error;
	}

	log_debug("spamd_query: received header:\n%s", recv_header);

	/*
         * Get message size
         */
	message_size = vtable_get(attrs, "message_size");
	if (message_size == NULL)
	{
		log_error("spamd_query: vtable_get failed");
		goto error;
	}

	size = header_size + *message_size;

	/*
	 * Allocate message buffer
	 */
	message = (char *) malloc(size + 1);
	if (message == NULL)
	{
		log_sys_error("spamd_query: malloc");
		goto error;
	}

	/*
	 * Dump message
	 */
	memcpy(message, recv_header, header_size);
	if (milter_dump_message(message + header_size, size - header_size + 1,
	    attrs) == -1)
	{
		log_error("spamd_query: milter_dump_message failed");
		goto error;
	}

	snprintf(buffer, sizeof(buffer), "SYMBOLS SPAMC/1.2\r\n"
	    "Content-length: %ld\r\n\r\n", size);

	sock = sock_connect_rr(&spamd_srr);
	if (sock == -1)
	{
		log_error("spamd_query: sock_connect failed");
		goto error;
	}
	 
	 /*
	  * Write spamassassin request
	  */
	if (write(sock, buffer, strlen(buffer)) == -1) {
		log_sys_error("spamd_query: write");
		goto error;
	}

	/*
	 * Write message
	 */
	if (write(sock, message, size) == -1) {
		log_sys_error("spamd_query: write");
		goto error;
	}

	/*
	 * Read response
	 */
	n = read(sock, buffer, sizeof buffer - 1);
	if (n == -1) {
		log_sys_error("spamd_query: read");
		goto error;
	}
	buffer[n] = 0;

	/*
	 * No answer. Seen when spamd ran out of mem.
         */
	if (n == 0)
	{
		log_error("spamd_query: no data received");
		goto error;
	}

	/*
	 * Parse response
	 */
	p = buffer;
	if (strncmp(p, SPAMD_SPAMD, SPAMD_SPAMDLEN)) {
		spamd_printable_buffer(p, SPAMD_SPAMDLEN + 1);
		log_error("spamd_query: protocol error: expected='%s' "
			"received='%s'", SPAMD_SPAMD, p);
		goto error;
	}

	p += SPAMD_SPAMDLEN;
	if (strncmp(p, SPAMD_VERSION, SPAMD_VERSIONLEN)) {
		spamd_printable_buffer(p, SPAMD_VERSIONLEN + 1);
		log_notice("spamd_query: protocol version mismtach: "
			"expected='%s' received='%s'", SPAMD_VERSION, p);
	}

	p += SPAMD_VERSIONLEN;
	p = strstr(p, SPAMD_EX_OK);
	if(p == NULL) {
		log_error("spamd_query: spamd returned non EX_OK");
		goto error;
	}

	/*
	 * Spamd returns 2 lines. Read 2nd line if neccessary.
	 */
	p += SPAMD_EX_OKLEN;
	if(strlen(p) <= 2) {  /* '\r\n' */

		n = read(sock, buffer, sizeof(buffer));
		if (n == -1) {
			log_sys_error("spamd_query: read");
			goto error;
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
		spamd_printable_buffer(p, SPAMD_SPAMLEN + 1);
		log_error("spamd_query: protocol error: expected='%s' "
			"received='%s'", SPAMD_SPAM, p);
		goto error;
	}

	p += SPAMD_SPAMLEN;
	if(!strncmp(p, SPAMD_TRUE, SPAMD_TRUELEN)) {
		spam = 1;
		p += SPAMD_TRUELEN;
	}
	else if(!strncmp(p, SPAMD_FALSE, SPAMD_FALSELEN)) {
		spam = 0;
		p += SPAMD_FALSELEN;
	}
	else
	{
		spamd_printable_buffer(p, (SPAMD_TRUELEN > SPAMD_FALSELEN ?
			SPAMD_TRUELEN: SPAMD_FALSELEN) + 1);
		log_error("spamd_query: protocol error: expected: '%s|%s' "
			"received='%s'", SPAMD_TRUE, SPAMD_FALSE, p);
		goto error;
	}

	/*
	 * Cut score.
	 */
	q = strchr(p, ' ');
	if (q == NULL) {
		log_error("spamd_query: protocol error: couldn't find the "
			"next space character");
		goto error;
	}
	*q++ = 0;

	score = (VAR_FLOAT_T) strtod(p, NULL);

	/*
	 * Set SYMBOLS
	 */
	p = strchr(q, '\r');
	if (p == NULL) {
		log_error("spamd_query: protocol error: couldn't find the "
			"start of the spamd symbols");
		goto error;
	}
	p += 4; /* \r\n\r\n */

	q = strchr(p, '\r');
	if (q == NULL) {
		log_error("spamd_query: protocol error: couldn't find the "
			"end of the spamd symbols");
		goto error;
	}
	*q = 0;

	log_message(LOG_ERR, attrs, "spamd: spam=%d score=%.1f symbols=%s",
	    spam, score, p);

	symbols = var_create(VT_LIST, "spamd_symbols", NULL,
		VF_KEEPNAME | VF_CREATE);
	if (symbols == NULL) {
		log_error("spamd_query: var_create failed");
		goto error;
	}

	do {
		q = strchr(p, ',');
		if (q) {
			*q = 0;
		}

		if (vlist_append_new(symbols, VT_STRING, NULL, p,
			VF_COPYDATA) == -1) {
			log_error("spamd_query: vlist_append failed");
			goto error;
		}

		p = q + 1;
	} while (q);

	if (vtable_setv(attrs, VT_INT, "spamd_spam", &spam,
		VF_KEEPNAME | VF_COPYDATA, VT_FLOAT, "spamd_score", &score,
		VF_KEEPNAME | VF_COPYDATA, VT_NULL)) {
		log_error("spamd_query: vtable_setv failed");
		goto error;
	}

	if (vtable_set(attrs, symbols)) {
		log_error("spamd_query: vtable_set failed");
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

	if (symbols) {
		var_delete(symbols);
	}

	return -1;
}


int
spamd_init(void)
{
	char **p;
	
	if (sock_rr_init(&spamd_srr, "spamd_socket"))
	{
		log_die(EX_SOFTWARE, "spamd_init: sock_rr_init failed");
	}

	for (p = spamd_symbols; *p; ++p) {
		acl_symbol_register(*p, MS_OFF_EOM, spamd_query, AS_CACHE);
	}

	return 0;
}

void
spamd_fini(void)
{
	sock_rr_clear(&spamd_srr);

	return;
}
