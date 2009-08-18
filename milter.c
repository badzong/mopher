#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <libmilter/mfapi.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "milter.h"
// #include "access.h"
#include "log.h"
#include "cf.h"
#include "acl.h"

#define LOGNAME "milter"
#define TIMELEN 16
#define INTLEN 10
#define BUCKETS 256

#define STAGE_CONNECT	"connect"
#define STAGE_HELO	"helo"
#define STAGE_ENVFROM	"envfrom"
#define STAGE_ENVRCPT	"envrcpt"
#define STAGE_HEADER	"header"
#define STAGE_EOH	"eoh"
#define STAGE_BODY	"body"
#define STAGE_EOM	"eom"

static sfsistat
milter_acl(char *stage, milter_priv_t * mp)
{
	switch (acl(stage, mp->mp_table)) {

	case AA_PASS:
		return SMFIS_ACCEPT;

	case AA_BLOCK:
		return SMFIS_REJECT;

	case AA_ERROR:
	case AA_DELAY:
		return SMFIS_TEMPFAIL;

	case AA_DISCARD:
		return SMFIS_DISCARD;

	case AA_CONTINUE:
		return SMFIS_CONTINUE;

	default:
		log_error("milter_acl: acl returned unknown action");
	}

	return SMFIS_TEMPFAIL;
}

static void
milter_priv_delete(milter_priv_t * mp)
{
	if (mp->mp_table) {
		ht_delete(mp->mp_table, (void *) var_delete);
	}

	if (mp->mp_message) {
		free(mp->mp_message);
	}

	free(mp);

	return;
}

static milter_priv_t *
milter_priv_create(void)
{
	milter_priv_t *mp = NULL;

	if ((mp = malloc(sizeof(milter_priv_t))) == NULL) {
		log_error("milter_priv_create: malloc");
		goto error;
	}

	memset(mp, 0, sizeof(milter_priv_t));

	if ((mp->mp_table =
	     HT_CREATE_STATIC(BUCKETS, (void *) var_hash,
			      (void *) var_match)) == NULL) {
		log_error("milter_priv_create: HT_CREATE_STATIC failed");
		goto error;
	}

	return mp;

error:

	if (mp) {
		milter_priv_delete(mp);
	}

	return NULL;
}

static sfsistat
milter_connect(SMFICTX * ctx, char *hostname, _SOCK_ADDR * hostaddr)
{
	milter_priv_t *mp;
	time_t now;

	if ((mp = milter_priv_create()) == NULL) {
		log_error("milter_connect: milter_priv_create failed");
		return SMFIS_TEMPFAIL;
	}

	smfi_setpriv(ctx, mp);

	if (var_table_save
	    (mp->mp_table, VT_STRING, "milter_stage", STAGE_CONNECT)) {
		log_error("milter_connect: var_table_save failed");
		return SMFIS_TEMPFAIL;
	}

	if ((now = time(NULL)) == -1) {
		log_error("milter_connect: time");
		return SMFIS_TEMPFAIL;
	}

	if (var_table_save(mp->mp_table, VT_INT, "milter_received", &now)) {
		log_error("milter_conect: var_table_save failed");
		return SMFIS_TEMPFAIL;
	}

	if (var_table_save
	    (mp->mp_table, VT_STRING, "milter_hostname", hostname)) {
		log_error("milter_conect: var_table_save failed");
		return SMFIS_TEMPFAIL;
	}

	if (var_table_save(mp->mp_table, VT_ADDR, "milter_hostaddr", hostaddr)) {
		log_error("milter_conect: var_table_save failed");
		return SMFIS_TEMPFAIL;
	}

	// log_debug("milter_connect: connection from: %s
	// (%s)", hostname, addr);

	return milter_acl(STAGE_CONNECT, mp);
}

static sfsistat
milter_helo(SMFICTX * ctx, char *helostr)
{
	milter_priv_t *mp;

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (var_table_save(mp->mp_table, VT_STRING, "milter_stage", STAGE_HELO)) {
		log_error("milter_helo: var_table_save failed");
		return SMFIS_TEMPFAIL;
	}

	if (var_table_save(mp->mp_table, VT_STRING, "milter_helo", helostr)) {
		log_error("milter_helo: var_table_save failed");
		return SMFIS_TEMPFAIL;
	}

	log_debug("milter_helo: helo hostname: %s", helostr);

	return milter_acl(STAGE_HELO, mp);
}

static sfsistat
milter_envfrom(SMFICTX * ctx, char **argv)
{
	milter_priv_t *mp;
	char *envfrom = NULL;

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (var_table_save
	    (mp->mp_table, VT_STRING, "milter_stage", STAGE_ENVFROM)) {
		log_error("milter_envfrom: var_table_save failed");
		goto error;
	}

	switch (var_string_rencaps(argv[0], &envfrom, "<>")) {
	case 0:
		envfrom = argv[0];
		break;

	case -1:
		log_error("milter_envfrom: var_string_rencaps failed");
		goto error;
	}

	log_debug("milter_envfrom: envelope from: %s", envfrom);

	if (var_table_save(mp->mp_table, VT_STRING, "milter_envfrom", envfrom)) {
		log_error("milter_envfrom: var_table_save failed");
		goto error;
	}

	if (envfrom != argv[0]) {
		free(envfrom);
	}

	return milter_acl(STAGE_ENVFROM, mp);

error:

	if (envfrom != argv[0]) {
		free(envfrom);
	}

	return SMFIS_TEMPFAIL;
}

static sfsistat
milter_envrcpt(SMFICTX * ctx, char **argv)
{
	milter_priv_t *mp;
	char *envrcpt = NULL;

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (var_table_save
	    (mp->mp_table, VT_STRING, "milter_stage", STAGE_ENVRCPT)) {
		log_error("milter_envrcpt: var_table_save failed");
		goto error;
	}

	switch (var_string_rencaps(argv[0], &envrcpt, "<>")) {
	case 0:
		envrcpt = argv[0];
		break;

	case -1:
		log_error("milter_envrcpt: var_string_rencaps failed");
		goto error;
	}

	if (var_table_save(mp->mp_table, VT_STRING, "milter_envrcpt", envrcpt)) {
		log_error("milter_envrcpt: var_table_save failed");
		goto error;
	}

	if (var_table_list_insert
	    (mp->mp_table, VT_STRING, "milter_recipient_list", envrcpt)) {
		log_error("milter_envrcpt: var_table_list_insert failed");
		goto error;
	}

	++mp->mp_recipients;

	log_debug("milter_envrcpt: envelope recipient: %s", envrcpt);

	if (envrcpt != argv[0]) {
		free(envrcpt);
	}

	return milter_acl(STAGE_ENVRCPT, mp);

error:
	if (envrcpt != argv[0]) {
		free(envrcpt);
	}

	return SMFIS_TEMPFAIL;
}

static sfsistat
milter_header(SMFICTX * ctx, char *headerf, char *headerv)
{
	milter_priv_t *mp;
	int32_t len;

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (var_table_save
	    (mp->mp_table, VT_STRING, "milter_stage", STAGE_HEADER)) {
		log_error("milter_header: var_table_save failed");
		return SMFIS_TEMPFAIL;
	}

	/*
	 * headerf: headerv\r\n
	 */
	len = strlen(headerf) + strlen(headerv) + 5;

	if ((mp->mp_message =
	     (char *) realloc(mp->mp_message, mp->mp_size + len))
	    == NULL) {
		log_error("milter_header: realloc");
		return SMFIS_TEMPFAIL;
	}

	snprintf(mp->mp_message + mp->mp_size, len,
		 "%s: %s\r\n", headerf, headerv);

	/*
	 * Don't count \0
	 */
	mp->mp_size += len - 1;

	return milter_acl(STAGE_HEADER, mp);
}

static sfsistat
milter_eoh(SMFICTX * ctx)
{
	milter_priv_t *mp;
	int32_t len;

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (var_table_save(mp->mp_table, VT_STRING, "milter_stage", STAGE_EOH)) {
		log_error("milter_eoh: var_table_save failed");
		return SMFIS_TEMPFAIL;
	}

	/*
	 * blank line between header and body
	 */
	len = 3;

	if ((mp->mp_message =
	     (char *) realloc(mp->mp_message, mp->mp_size + len))
	    == NULL) {
		log_error("milter_eoh: realloc");
		return SMFIS_TEMPFAIL;
	}

	strncpy(mp->mp_message + mp->mp_size, "\r\n", len);

	mp->mp_size += len - 1;

	return milter_acl(STAGE_EOH, mp);
}

static sfsistat
milter_body(SMFICTX * ctx, unsigned char *body, size_t len)
{
	milter_priv_t *mp;

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (var_table_save(mp->mp_table, VT_STRING, "milter_stage", STAGE_BODY)) {
		log_error("milter_body: var_table_save failed");
		return SMFIS_TEMPFAIL;
	}

	if ((mp->mp_message = (char *) realloc(mp->mp_message,
					       mp->mp_size +
					       len + 1)) == NULL) {
		log_error("milter_body: realloc");
		return SMFIS_TEMPFAIL;
	}

	memcpy(mp->mp_message + mp->mp_size, body, len);

	mp->mp_size += len;
	mp->mp_message[mp->mp_size] = 0;

	return milter_acl(STAGE_BODY, mp);
}

static sfsistat
milter_eom(SMFICTX * ctx)
{
	milter_priv_t *mp;

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (var_table_save(mp->mp_table, VT_STRING, "milter_stage", STAGE_EOM)) {
		log_error("milter_eom: var_table_save failed");
		return SMFIS_TEMPFAIL;
	}

	if (var_table_save(mp->mp_table, VT_INT, "milter_size", &mp->mp_size)) {
		log_error("milter_eom: var_table_save failed");
		return SMFIS_TEMPFAIL;
	}

	if (var_table_save
	    (mp->mp_table, VT_INT, "milter_recipients", &mp->mp_recipients)) {
		log_error("milter_eom: var_table_save failed");
		return SMFIS_TEMPFAIL;
	}

	log_debug("milter_eom: message size: %d", mp->mp_size);

	return milter_acl(STAGE_EOM, mp);
}

static sfsistat
milter_close(SMFICTX * ctx)
{
	milter_priv_t *mp;

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (mp == NULL) {
		return SMFIS_CONTINUE;
	}

	milter_priv_delete(mp);

	smfi_setpriv(ctx, NULL);

	log_debug("milter_close: connection closed");

	return SMFIS_CONTINUE;
}

int8_t
milter(void)
{
	int8_t r;
	struct smfiDesc smfid;

	bzero(&smfid, sizeof(struct smfiDesc));

	smfid.xxfi_name = __FILE__;
	smfid.xxfi_version = SMFI_VERSION;
	smfid.xxfi_flags = SMFIF_ADDHDRS;
	smfid.xxfi_connect = milter_connect;
	smfid.xxfi_helo = milter_helo;
	smfid.xxfi_envfrom = milter_envfrom;
	smfid.xxfi_envrcpt = milter_envrcpt;
	smfid.xxfi_header = milter_header;
	smfid.xxfi_eoh = milter_eoh;
	smfid.xxfi_body = milter_body;
	smfid.xxfi_eom = milter_eom;
	smfid.xxfi_close = milter_close;

	if (smfi_setconn(cf_milter_socket) == MI_FAILURE) {
		log_die(EX_SOFTWARE, "milter: smfi_setconn failed");
	}

	smfi_settimeout(cf_milter_socket_timeout);

	if (smfi_register(smfid) == MI_FAILURE) {
		log_die(EX_SOFTWARE, "milter: smfi_register failed");
	}

	acl_init();

	r = smfi_main();

	acl_clear();

	return r;
}
