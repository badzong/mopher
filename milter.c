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
#include "util.h"
#include "table.h"

#define MSN_CONNECT	"connect"
#define MSN_UNKNOWN	"unknown"
#define MSN_HELO	"helo"
#define MSN_ENVFROM	"envfrom"
#define MSN_ENVRCPT	"envrcpt"
#define MSN_DATA	"data"
#define MSN_HEADER	"header"
#define MSN_EOH		"eoh"
#define MSN_BODY	"body"
#define MSN_EOM		"eom"


int milter_running = 1;


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
		var_delete(mp->mp_table);
	}

	if (mp->mp_header) {
		free(mp->mp_header);
	}

	if (mp->mp_body) {
		free(mp->mp_body);
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

	if ((mp->mp_table = var_create(VT_TABLE, "mp_table", NULL,
		VF_CREATE | VF_KEEPNAME)) == NULL) {
		log_error("milter_priv_create: var_create failed");
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
	VAR_INT_T now;
	VAR_INT_T stage = MS_CONNECT;
	char *version;
	struct sockaddr_storage *ha_clean;
	char *addrstr;

	if ((mp = milter_priv_create()) == NULL) {
		log_error("milter_connect: milter_priv_create failed");
		return SMFIS_TEMPFAIL;
	}

	smfi_setpriv(ctx, mp);

	if ((now = (VAR_INT_T) time(NULL)) == -1) {
		log_error("milter_connect: time");
		return SMFIS_TEMPFAIL;
	}

	version = smfi_getsymval(ctx, "v");
	if (version == NULL)
	{
		log_error("milter_connect: smfi_getsymval failed");
		return SMFIS_TEMPFAIL;
	}

	ha_clean = util_hostaddr((struct sockaddr_storage *) hostaddr);
	if (ha_clean == NULL) {
		log_error("milter_conect: var_addr_clean failed");
		return SMFIS_TEMPFAIL;
	}

	addrstr = util_addrtostr(ha_clean);
	if (addrstr == NULL) {
		log_error("milter_conect: util_addrtostr failed");
		return SMFIS_TEMPFAIL;
	}

	if (var_table_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEP,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_CONNECT,
		VF_KEEPNAME | VF_KEEPDATA,
	    VT_INT, "milter_received", &now, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_version", version, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_hostname", hostname, VF_KEEPNAME | VF_COPYDATA,
	    VT_ADDR, "milter_hostaddr", ha_clean, VF_KEEPNAME,
	    VT_STRING, "milter_addrstr", addrstr, VF_KEEPNAME,
	    VT_NULL))
	{
		log_error("milter_connect: var_table_setv failed");
		return SMFIS_TEMPFAIL;
	}

	log_debug("milter_connect: connection from: %s (%s)", hostname,
		addrstr);

	return milter_acl(MSN_CONNECT, mp);
}

static sfsistat
milter_unknown(SMFICTX * ctx, const char *cmd)
{
	milter_priv_t *mp;
	VAR_INT_T stage = MS_UNKNOWN;

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (var_table_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEP,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_UNKNOWN, VF_KEEP,
	    VT_STRING, "milter_unknown_command", cmd,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_NULL))
	{
		log_error("milter_unknown: var_table_setv failed");
		return SMFIS_TEMPFAIL;
	}

	log_debug("milter_unknown: unknown command: \"%s\"", cmd);

	return milter_acl(MSN_UNKNOWN, mp);
}

static sfsistat
milter_helo(SMFICTX * ctx, char *helostr)
{
	milter_priv_t *mp;
	VAR_INT_T stage = MS_HELO;

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (var_table_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEP,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_HELO, VF_KEEP,
	    VT_STRING, "milter_helo", helostr, VF_KEEPNAME | VF_COPYDATA,
	    VT_NULL))
	{
		log_error("milter_helo: var_table_setv failed");
		return SMFIS_TEMPFAIL;
	}

	log_debug("milter_helo: helo hostname: %s", helostr);

	return milter_acl(MSN_HELO, mp);
}

static sfsistat
milter_envfrom(SMFICTX * ctx, char **argv)
{
	milter_priv_t *mp;
	VAR_INT_T stage = MS_ENVFROM;
	char *envfrom;

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if ((envfrom = util_strdupenc(argv[0], "<>")) == NULL) {
		log_error("milter_envfrom: util_strdupenc failed");
		return SMFIS_TEMPFAIL;
	}

	if (var_table_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEPNAME | VF_KEEPDATA,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_ENVFROM, VF_KEEP,
	    VT_STRING, "milter_envfrom", envfrom, VF_KEEPNAME,
	    VT_NULL))
	{
		log_error("milter_envfrom: var_table_setv failed");
		return SMFIS_TEMPFAIL;
	}

	log_debug("milter_envfrom: envelope from: %s", envfrom);

	return milter_acl(MSN_ENVFROM, mp);
}

static sfsistat
milter_envrcpt(SMFICTX * ctx, char **argv)
{
	milter_priv_t *mp;
	VAR_INT_T stage = MS_ENVRCPT;
	char *envrcpt = NULL;

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if ((envrcpt = util_strdupenc(argv[0], "<>")) == NULL) {
		log_error("milter_envrcpt: util_strdupenc failed");
		return SMFIS_TEMPFAIL;
	}

	++mp->mp_recipients;

	/*
	 * Append recipient to recipient_list
	 */
	if (var_table_list_append
	    (mp->mp_table, VT_STRING, "milter_recipient_list", envrcpt,
	     VF_KEEP)) {
		log_error("milter_envrcpt: var_table_list_append failed");
		return SMFIS_TEMPFAIL;
	}

	if (var_table_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEPNAME | VF_KEEPDATA,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_ENVRCPT, VF_KEEP,
	    VT_STRING, "milter_envrcpt", envrcpt, VF_KEEPNAME,
	    VT_NULL))
	{
		log_error("milter_envrcpt: var_table_setv failed");
		return SMFIS_TEMPFAIL;
	}

	log_debug("milter_envrcpt: envelope recipient: %s", envrcpt);

	return milter_acl(MSN_ENVRCPT, mp);
}

static sfsistat
milter_data(SMFICTX * ctx)
{
	milter_priv_t *mp;
	VAR_INT_T stage = MS_DATA;

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (var_table_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEP,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_DATA, VF_KEEP,
	    VT_INT, "milter_recipients", &mp->mp_recipients,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_NULL))
	{
		log_error("milter_data: var_table_setv failed");
		return SMFIS_TEMPFAIL;
	}

	return milter_acl(MSN_DATA, mp);
}
static sfsistat
milter_header(SMFICTX * ctx, char *headerf, char *headerv)
{
	milter_priv_t *mp;
	VAR_INT_T stage = MS_HEADER;
	int32_t len;

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	/*
	 * headerf: headerv\r\n
	 */
	len = strlen(headerf) + strlen(headerv) + 5;

	if ((mp->mp_header = (char *) realloc(mp->mp_header,
	    mp->mp_headerlen + len)) == NULL)
	{
		log_error("milter_header: realloc");
		return SMFIS_TEMPFAIL;
	}

	snprintf(mp->mp_header + mp->mp_headerlen, len, "%s: %s\r\n", headerf,
		headerv);

	/*
	 * Don't count \0
	 */
	mp->mp_headerlen += len - 1;

	if (var_table_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEPNAME | VF_KEEPDATA,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_HEADER, VF_KEEP,
	    VT_STRING, "milter_header_name", headerf,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_header_value", headerv,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_NULL))
	{
		log_error("milter_envrcpt: var_table_setv failed");
		return SMFIS_TEMPFAIL;
	}

	return milter_acl(MSN_HEADER, mp);
}

static sfsistat
milter_eoh(SMFICTX * ctx)
{
	milter_priv_t *mp;
	VAR_INT_T stage = MS_EOH;

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (var_table_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEPNAME | VF_KEEPDATA,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_EOH, VF_KEEP,
	    VT_STRING, "milter_header", mp->mp_header, VF_KEEP,
	    VT_INT, "milter_header_size", &mp->mp_headerlen,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_NULL))
	{
		log_error("milter_eoh: var_table_setv failed");
		return SMFIS_TEMPFAIL;
	}

	log_debug("milter_eom: header size: %d", mp->mp_headerlen);

	return milter_acl(MSN_EOH, mp);
}

static sfsistat
milter_body(SMFICTX * ctx, unsigned char *body, size_t len)
{
	milter_priv_t *mp;
	VAR_INT_T stage = MS_BODY;

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if ((mp->mp_body = (char *) realloc(mp->mp_body,
	    mp->mp_bodylen + len + 1)) == NULL)
	{
		log_error("milter_body: realloc");
		return SMFIS_TEMPFAIL;
	}

	memcpy(mp->mp_body + mp->mp_bodylen, body, len);

	mp->mp_bodylen += len;
	mp->mp_body[mp->mp_bodylen] = 0;

	if (var_table_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEPNAME | VF_KEEPDATA,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_BODY, VF_KEEP,
	    VT_NULL))
	{
		log_error("milter_body: var_table_setv failed");
		return SMFIS_TEMPFAIL;
	}

	return milter_acl(MSN_BODY, mp);
}

static sfsistat
milter_eom(SMFICTX * ctx)
{
	milter_priv_t *mp;
	VAR_INT_T stage = MS_EOM;

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (var_table_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEPNAME | VF_KEEPDATA,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_EOM, VF_KEEP,
	    VT_INT, "milter_body_size", &mp->mp_bodylen,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_body", &mp->mp_body, VF_KEEP,
	    VT_NULL))
	{
		log_error("milter_body: var_table_setv failed");
		return SMFIS_TEMPFAIL;
	}

	log_debug("milter_eom: body size: %d", mp->mp_bodylen);

	return milter_acl(MSN_EOM, mp);
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

	/*
	 * Use this thread do cleanup the tables.
	 */
	table_janitor(0);

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
	smfid.xxfi_unknown = milter_unknown;
	smfid.xxfi_helo = milter_helo;
	smfid.xxfi_envfrom = milter_envfrom;
	smfid.xxfi_envrcpt = milter_envrcpt;
	smfid.xxfi_data = milter_data;
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

	milter_running = 0;

	acl_clear();

	return r;
}
