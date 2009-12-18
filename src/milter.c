#include "config.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libmilter/mfapi.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "mopher.h"

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

#define BUCKETS 64

#define POSTFIX "Postfix"
#define POSTFIX_LEN 7

#define SENDMAIL "Sendmail"
#define SENDMAIL_LEN 8


/*
 * Symbols without callback
 */
static milter_symbol_t milter_symbols[] = {
	{ "milter_ctx", MS_ANY },
	{ "milter_id", MS_ANY },
	{ "milter_mta_version", MS_ANY },
	{ "milter_stage", MS_ANY },
	{ "milter_stagename", MS_ANY }, 
	{ "milter_unknown_command", MS_UNKNOWN },
	{ "milter_received", MS_ANY },
	{ "milter_hostaddr", MS_ANY },
	{ "milter_addrstr", MS_ANY },
	{ "milter_hostname", MS_ANY },
	{ "milter_helo", MS_OFF_HELO },
	{ "milter_envfrom", MS_OFF_ENVFROM },
	{ "milter_envrcpt", MS_OFF_ENVRCPT },
	{ "milter_recipients", MS_OFF_ENVRCPT },
	{ "milter_recipient_list", MS_OFF_DATA },
	{ "milter_header_name", MS_HEADER },
	{ "milter_header_value", MS_HEADER },
	{ "milter_header", MS_OFF_EOH },
	{ "milter_header_size", MS_OFF_EOH },
	{ "milter_body", MS_EOM },
	{ "milter_body_size", MS_EOM },
	{ NULL, 0 }
};


/*
 * List of all available macros Postfix and Sendmail
 */
static char *milter_macros[] = {
	"milter_queueid", "milter_myhostname", "milter_client",
	"milter_auth_athen", "milter_auth_author", "milter_auth_type",
	"milter_client_addr", "milter_client_connections",
	"milter_client_name", "milter_client_port", "milter_client_ptr",
	"milter_cert_issuer", "milter_cert_subject", "milter_cipher_bits",
	"milter_cipher", "milter_daemon_name", "milter_mail_addr",
	"milter_mail_host", "milter_mail_mailer", "milter_rcpt_addr",
	"milter_rcpt_host", "milter_rcpt_mailer", "milter_tls_version",
	NULL
};


/*
 * http://www.postfix.org/MILTER_README.html (2009-09-26)
 */
static milter_macro_t milter_postfix_macros[] = {
	{ "milter_queueid", "i", MS_OFF_EOH },
	{ "milter_myhostname", "j", MS_ANY },
	{ "milter_client", "_", MS_ANY },
	{ "milter_auth_athen", "{auth_authen}", MS_OFF_ENVFROM },
	{ "milter_auth_author", "{auth_author}", MS_OFF_ENVFROM },
	{ "milter_auth_type", "{auth_type}", MS_OFF_ENVFROM },
	{ "milter_client_addr", "{client_addr}", MS_ANY },
	{ "milter_client_connections", "{client_connections}", MS_CONNECT },
	{ "milter_client_name", "{client_name}", MS_ANY },
	{ "milter_client_port", "{client_port}", MS_ANY },
	{ "milter_client_ptr", "{client_ptr}", MS_ANY },
	{ "milter_cert_issuer", "{cert_issuer}", MS_OFF_HELO },
	{ "milter_cert_subject", "{cert_subject}", MS_OFF_HELO },
	{ "milter_cipher_bits", "{cipher_bits}", MS_OFF_HELO },
	{ "milter_cipher", "{cipher}", MS_OFF_HELO },
	{ "milter_daemon_name", "{daemon_name}", MS_ANY },
	{ "milter_mail_addr", "{mail_addr}", MS_OFF_DATA },
	{ "milter_mail_host", "{mail_host}", MS_OFF_DATA },
	{ "milter_mail_mailer", "{mail_mailer}", MS_OFF_DATA },
	{ "milter_rcpt_addr", "{rcpt_addr}", MS_ENVRCPT },
	{ "milter_rcpt_host", "{rcpt_host}", MS_ENVRCPT },
	{ "milter_rcpt_mailer", "{rcpt_mailer}", MS_ENVRCPT },
	{ "milter_tls_version", "{tls_version}", MS_OFF_HELO },
	{ NULL, NULL, 0 }
};


/*
 * FIXME: This is a copy of milter_postfix_macros. NEED SENDMAIL!
 */
static milter_macro_t milter_sendmail_macros[] = {
	{ "milter_queueid", "i", MS_OFF_EOH },
	{ "milter_myhostname", "j", MS_ANY },
	{ "milter_client", "_", MS_ANY },
	{ "milter_auth_athen", "{auth_authen}", MS_OFF_ENVFROM },
	{ "milter_auth_author", "{auth_author}", MS_OFF_ENVFROM },
	{ "milter_auth_type", "{auth_type}", MS_OFF_ENVFROM },
	{ "milter_client_addr", "{client_addr}", MS_ANY },
	{ "milter_client_connections", "{client_connections}", MS_CONNECT },
	{ "milter_client_name", "{client_name}", MS_ANY },
	{ "milter_client_port", "{client_port}", MS_ANY },
	{ "milter_client_ptr", "{client_ptr}", MS_ANY },
	{ "milter_cert_issuer", "{cert_issuer}", MS_OFF_HELO },
	{ "milter_cert_subject", "{cert_subject}", MS_OFF_HELO },
	{ "milter_cipher_bits", "{cipher_bits}", MS_OFF_HELO },
	{ "milter_cipher", "{cipher}", MS_OFF_HELO },
	{ "milter_daemon_name", "{daemon_name}", MS_ANY },
	{ "milter_mail_addr", "{mail_addr}", MS_OFF_DATA },
	{ "milter_mail_host", "{mail_host}", MS_OFF_DATA },
	{ "milter_mail_mailer", "{mail_mailer}", MS_OFF_DATA },
	{ "milter_rcpt_addr", "{rcpt_addr}", MS_ENVRCPT },
	{ "milter_rcpt_host", "{rcpt_host}", MS_ENVRCPT },
	{ "milter_rcpt_mailer", "{rcpt_mailer}", MS_ENVRCPT },
	{ "milter_tls_version", "{tls_version}", MS_OFF_HELO },
	{ NULL, NULL, 0 }
};


/*
 * Macros are loaded into hash tables
 */
static sht_t *milter_postfix_macro_table;
static sht_t *milter_sendmail_macro_table;

/*
 * Rwlock for reloading
 */
static pthread_rwlock_t milter_reload_lock = PTHREAD_RWLOCK_INITIALIZER;

/*
 * Milter id
 */
static VAR_INT_T milter_id;
static pthread_mutex_t milter_id_mutex = PTHREAD_MUTEX_INITIALIZER;

int milter_running = 1;


static int
milter_get_id(void)
{
	VAR_INT_T id;

	if (pthread_mutex_lock(&milter_id_mutex))
	{
		log_error("milter_id: pthread_mutex_lock");
		return -1;
	}

	id = ++milter_id;

	if (pthread_mutex_unlock(&milter_id_mutex))
	{
		log_error("milter_id: pthread_mutex_unlock");
	}

	return id;
}


static int
milter_macro_lookup(milter_stage_t stage, char *name, var_t *attrs)
{
	sht_t *macro_table;
	char *version;
	milter_macro_t *mm;
	char *value;
	SMFICTX *ctx;
	char *stagename;
	int r;

	r = acl_symbol_dereference(attrs, "milter_ctx", &ctx,
		"milter_mta_version", &version, "milter_stagename", &stagename,
		NULL);
	if (r)
	{
		log_error("milter_macro_lookup: acl_symbol_dereference "
		    "failed");
		return -1;
	}

	if (strncmp(version, POSTFIX, POSTFIX_LEN) == 0) {
		macro_table = milter_postfix_macro_table;
	}
	else if (strncmp(version, SENDMAIL, SENDMAIL_LEN) == 0) {
		macro_table = milter_postfix_macro_table;
	}
	else {
		log_error("milter_macro_lookup: unkown MTA \"%s\"",
			version);
		return -1;
	}

	mm = sht_lookup(macro_table, name);
	if (mm == NULL) {
		log_error("milter_macro_lookup: \"%s\" has no macro for \"%s\"",
			version, name);
		return -1;
	}

	if ((stage & mm->mm_stage) == 0) {
		log_error("milter_macro_lookup: \"%s (%s)\" not set at \"%s\""
			" stage (%s)", mm->mm_macro, name, stagename, version);
		return -1;
	}

	value = smfi_getsymval(ctx, mm->mm_macro);
	if (value == NULL)
	{
		log_error("milter_macro_lookup: smfi_getsymval failed for"
			" \"%s\"", mm->mm_macro);
		return SMFIS_TEMPFAIL;
	}

	if (vtable_set_new(attrs, VT_STRING, mm->mm_name, value,
	    VF_KEEPNAME | VF_COPYDATA))
	{
		log_error("milter_macro_lookup: vtable_set failed");
		return -1;
	}

	return 0;
}


static sfsistat
milter_acl(milter_stage_t stage, char *stagename, milter_priv_t * mp)
{
	/*
	 * Translate acl_action_type into sfsistat
	 */
	switch (acl(stage, stagename, mp->mp_table))
	{
	case ACL_CONTINUE:
		return SMFIS_CONTINUE;

	case ACL_REJECT:
		return SMFIS_REJECT;

	case ACL_DISCARD:
		return SMFIS_DISCARD;

	case ACL_ACCEPT:
		return SMFIS_ACCEPT;

	case ACL_TEMPFAIL:
	case ACL_GREYLIST:
		return SMFIS_TEMPFAIL;

	case ACL_ERROR:
		log_error("milter_acl: acl failed in %s stage", stagename);
		break;

	default:
		log_error("milter_acl: acl returned bad action type in %s "
		    "stage", stagename);
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

	mp = (milter_priv_t *) malloc(sizeof (milter_priv_t));
	if (mp == NULL)
	{
		log_error("milter_priv_create: malloc");
		goto error;
	}

	memset(mp, 0, sizeof(milter_priv_t));

	mp->mp_table = vtable_create("mp_table", VF_KEEPNAME);
	if (mp->mp_table == NULL)
	{
		log_error("milter_priv_create: vtable_create failed");
		goto error;
	}

	return mp;

error:

	if (mp)
	{
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
	char *mta_version;
	struct sockaddr_storage *ha_clean;
	char *addrstr;
	VAR_INT_T id;
	sfsistat r;

	if (pthread_rwlock_rdlock(&milter_reload_lock))
	{
		log_error("milter_connect: pthread_rwlock_rdlock");
		return SMFIS_TEMPFAIL;
	}

	if ((mp = milter_priv_create()) == NULL) {
		log_error("milter_connect: milter_priv_create failed");
		return SMFIS_TEMPFAIL;
	}

	smfi_setpriv(ctx, mp);

	if ((id = milter_get_id()) == -1)
	{
		log_error("milter_connect: milter_id failed");
		return SMFIS_TEMPFAIL;
	}

	if ((now = (VAR_INT_T) time(NULL)) == -1) {
		log_error("milter_connect: time");
		return SMFIS_TEMPFAIL;
	}

	mta_version = smfi_getsymval(ctx, "v");
	if (mta_version == NULL)
	{
		log_error("milter_connect: smfi_getsymval for \"v\" failed");
		return SMFIS_TEMPFAIL;
	}

	ha_clean = util_hostaddr((struct sockaddr_storage *) hostaddr);
	if (ha_clean == NULL) {
		log_error("milter_conect: util_hostaddr failed");
		return SMFIS_TEMPFAIL;
	}

	addrstr = util_addrtostr(ha_clean);
	if (addrstr == NULL) {
		log_error("milter_conect: util_addrtostr failed");
		return SMFIS_TEMPFAIL;
	}

	if (vtable_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEP,
	    VT_INT, "milter_id", &id, VF_KEEPNAME | VF_COPYDATA,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_CONNECT,
		VF_KEEPNAME | VF_KEEPDATA,
	    VT_INT, "milter_received", &now, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_mta_version", mta_version,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_hostname", hostname, VF_KEEPNAME | VF_COPYDATA,
	    VT_ADDR, "milter_hostaddr", ha_clean, VF_KEEPNAME,
	    VT_STRING, "milter_addrstr", addrstr, VF_KEEPNAME,
	    VT_NULL))
	{
		log_error("milter_connect: vtable_setv failed");
		return SMFIS_TEMPFAIL;
	}

	log_debug("milter_connect: connection from: %s (%s)", hostname,
		addrstr);

	r = milter_acl(MS_CONNECT, MSN_CONNECT, mp);

	if (pthread_rwlock_unlock(&milter_reload_lock))
	{
		log_error("milter_connect: pthread_rwlock_unlock");
	}

	return r;

}

static sfsistat
milter_unknown(SMFICTX * ctx, const char *cmd)
{
	milter_priv_t *mp;
	VAR_INT_T stage = MS_UNKNOWN;
	sfsistat r;

	if (pthread_rwlock_rdlock(&milter_reload_lock))
	{
		log_error("milter_unknwon: pthread_rwlock_rdlock");
		return SMFIS_TEMPFAIL;
	}

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (vtable_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEP,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_UNKNOWN, VF_KEEP,
	    VT_STRING, "milter_unknown_command", cmd,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_NULL))
	{
		log_error("milter_unknown: vtable_setv failed");
		return SMFIS_TEMPFAIL;
	}

	log_debug("milter_unknown: unknown command: \"%s\"", cmd);

	r = milter_acl(MS_UNKNOWN, MSN_UNKNOWN, mp);

	if (pthread_rwlock_unlock(&milter_reload_lock))
	{
		log_error("milter_unknwon: pthread_rwlock_unlock");
	}

	return r;
}

static sfsistat
milter_helo(SMFICTX * ctx, char *helostr)
{
	milter_priv_t *mp;
	VAR_INT_T stage = MS_HELO;
	sfsistat r;

	if (pthread_rwlock_rdlock(&milter_reload_lock))
	{
		log_error("milter_helo: pthread_rwlock_rdlock");
		return SMFIS_TEMPFAIL;
	}

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (vtable_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEP,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_HELO, VF_KEEP,
	    VT_STRING, "milter_helo", helostr, VF_KEEPNAME | VF_COPYDATA,
	    VT_NULL))
	{
		log_error("milter_helo: vtable_setv failed");
		return SMFIS_TEMPFAIL;
	}

	log_debug("milter_helo: helo hostname: %s", helostr);

	r = milter_acl(MS_HELO, MSN_HELO, mp);

	if (pthread_rwlock_unlock(&milter_reload_lock))
	{
		log_error("milter_helo: pthread_rwlock_unlock");
	}

	return r;
}

static sfsistat
milter_envfrom(SMFICTX * ctx, char **argv)
{
	milter_priv_t *mp;
	VAR_INT_T stage = MS_ENVFROM;
	sfsistat r;

	if (pthread_rwlock_rdlock(&milter_reload_lock))
	{
		log_error("milter_envfrom: pthread_rwlock_rdlock");
		return SMFIS_TEMPFAIL;
	}

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (vtable_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEP,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_ENVFROM, VF_KEEP,
	    VT_STRING, "milter_envfrom", argv[0], VF_KEEPNAME | VF_COPYDATA,
	    VT_NULL))
	{
		log_error("milter_envfrom: vtable_setv failed");
		return SMFIS_TEMPFAIL;
	}

	log_debug("milter_envfrom: envelope from: %s", argv[0]);

	r = milter_acl(MS_ENVFROM, MSN_ENVFROM, mp);

	if (pthread_rwlock_unlock(&milter_reload_lock))
	{
		log_error("milter_envfrom: pthread_rwlock_unlock");
	}

	return r;
}

static sfsistat
milter_envrcpt(SMFICTX * ctx, char **argv)
{
	milter_priv_t *mp;
	VAR_INT_T stage = MS_ENVRCPT;
	sfsistat r;

	if (pthread_rwlock_rdlock(&milter_reload_lock))
	{
		log_error("milter_envrpt: pthread_rwlock_rdlock");
		return SMFIS_TEMPFAIL;
	}

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	++mp->mp_recipients;

	if (vtable_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEP,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_ENVRCPT, VF_KEEP,
	    VT_STRING, "milter_envrcpt", argv[0], VF_KEEPNAME | VF_COPYDATA,
	    VT_INT, "milter_recipients", &mp->mp_recipients,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_NULL))
	{
		log_error("milter_envrcpt: vtable_setv failed");
		return SMFIS_TEMPFAIL;
	}

	log_debug("milter_envrcpt: envelope recipient: %s", argv[0]);

	r = milter_acl(MS_ENVRCPT, MSN_ENVRCPT, mp);

	/*
	 * Add accepted recipients to recipient_list
	 */
	if (r == SMFIS_CONTINUE || r == SMFIS_ACCEPT)
	{
		if (vtable_list_append_new(mp->mp_table, VT_STRING,
		    "milter_recipient_list", argv[0],
		    VF_KEEPNAME | VF_COPYDATA))
		{
			log_error(
			    "milter_envrcpt: vtable_list_append_new failed");
			return SMFIS_TEMPFAIL;
		}
	}

	if (pthread_rwlock_unlock(&milter_reload_lock))
	{
		log_error("milter_envrcpt: pthread_rwlock_unlock");
	}

	return r;
}

static sfsistat
milter_data(SMFICTX * ctx)
{
	milter_priv_t *mp;
	VAR_INT_T stage = MS_DATA;
	sfsistat r;

	if (pthread_rwlock_rdlock(&milter_reload_lock))
	{
		log_error("milter_data: pthread_rwlock_rdlock");
		return SMFIS_TEMPFAIL;
	}

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (vtable_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEP,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_DATA, VF_KEEP,
	    VT_INT, "milter_recipients", &mp->mp_recipients,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_NULL))
	{
		log_error("milter_data: vtable_setv failed");
		return SMFIS_TEMPFAIL;
	}

	r = milter_acl(MS_DATA, MSN_DATA, mp);

	if (pthread_rwlock_unlock(&milter_reload_lock))
	{
		log_error("milter_data: pthread_rwlock_unlock");
	}

	return r;
}


static sfsistat
milter_header(SMFICTX * ctx, char *headerf, char *headerv)
{
	milter_priv_t *mp;
	VAR_INT_T stage = MS_HEADER;
	int32_t len;
	sfsistat r;

	if (pthread_rwlock_rdlock(&milter_reload_lock))
	{
		log_error("milter_header: pthread_rwlock_rdlock");
		return SMFIS_TEMPFAIL;
	}

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

	if (vtable_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEP,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_HEADER, VF_KEEP,
	    VT_STRING, "milter_header_name", headerf,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_header_value", headerv,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_NULL))
	{
		log_error("milter_envrcpt: vtable_setv failed");
		return SMFIS_TEMPFAIL;
	}

	r = milter_acl(MS_HEADER, MSN_HEADER, mp);

	if (pthread_rwlock_unlock(&milter_reload_lock))
	{
		log_error("milter_header: pthread_rwlock_unlock");
	}

	return r;
}

static sfsistat
milter_eoh(SMFICTX * ctx)
{
	milter_priv_t *mp;
	VAR_INT_T stage = MS_EOH;
	sfsistat r;

	if (pthread_rwlock_rdlock(&milter_reload_lock))
	{
		log_error("milter_eoh: pthread_rwlock_rdlock");
		return SMFIS_TEMPFAIL;
	}

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (vtable_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEP,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_EOH, VF_KEEP,
	    VT_STRING, "milter_header", mp->mp_header, VF_KEEP,
	    VT_INT, "milter_header_size", &mp->mp_headerlen,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_NULL))
	{
		log_error("milter_eoh: vtable_setv failed");
		return SMFIS_TEMPFAIL;
	}

	log_debug("milter_eom: header size: %d", mp->mp_headerlen);

	r = milter_acl(MS_EOH, MSN_EOH, mp);

	if (pthread_rwlock_unlock(&milter_reload_lock))
	{
		log_error("milter_eoh: pthread_rwlock_unlock");
	}

	return r;
}

static sfsistat
milter_body(SMFICTX * ctx, unsigned char *body, size_t len)
{
	milter_priv_t *mp;
	VAR_INT_T stage = MS_BODY;
	sfsistat r;

	if (pthread_rwlock_rdlock(&milter_reload_lock))
	{
		log_error("milter_body: pthread_rwlock_rdlock");
		return SMFIS_TEMPFAIL;
	}

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

	if (vtable_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEP,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_BODY, VF_KEEP,
	    VT_NULL))
	{
		log_error("milter_body: vtable_setv failed");
		return SMFIS_TEMPFAIL;
	}

	r = milter_acl(MS_BODY, MSN_BODY, mp);

	if (pthread_rwlock_unlock(&milter_reload_lock))
	{
		log_error("milter_body: pthread_rwlock_unlock");
	}

	return r;
}

static sfsistat
milter_eom(SMFICTX * ctx)
{
	milter_priv_t *mp;
	VAR_INT_T stage = MS_EOM;
	sfsistat r;

	if (pthread_rwlock_rdlock(&milter_reload_lock))
	{
		log_error("milter_eom: pthread_rwlock_rdlock");
		return SMFIS_TEMPFAIL;
	}

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (vtable_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEP,
	    VT_INT, "milter_stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_stagename", MSN_EOM, VF_KEEP,
	    VT_INT, "milter_body_size", &mp->mp_bodylen,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "milter_body", mp->mp_body, VF_KEEP,
	    VT_NULL))
	{
		log_error("milter_body: vtable_setv failed");
		return SMFIS_TEMPFAIL;
	}

	log_debug("milter_eom: body size: %d", mp->mp_bodylen);

	r = milter_acl(MS_EOM, MSN_EOM, mp);

	if (pthread_rwlock_unlock(&milter_reload_lock))
	{
		log_error("milter_eom: pthread_rwlock_unlock");
	}

	return r;
}


static sfsistat
milter_close(SMFICTX * ctx)
{
	milter_priv_t *mp;

	if (pthread_rwlock_rdlock(&milter_reload_lock))
	{
		log_error("milter_close: pthread_rwlock_rdlock");
		return SMFIS_TEMPFAIL;
	}

	mp = ((milter_priv_t *) smfi_getpriv(ctx));

	if (mp == NULL) {
		return SMFIS_CONTINUE;
	}

	milter_priv_delete(mp);

	smfi_setpriv(ctx, NULL);

	log_debug("milter_close: connection closed");

	if (pthread_rwlock_unlock(&milter_reload_lock))
	{
		log_error("milter_close: pthread_rwlock_unlock");
	}

	return SMFIS_CONTINUE;
}


static void
milter_load_symbols(void)
{
	milter_symbol_t *ms;
	milter_macro_t *mm;
	char **macro;

	/*
	 * Symbols without callbacks set by milter.c (the other one)
	 */
	for (ms = milter_symbols; ms->ms_name; ++ms)
	{
		acl_symbol_register(ms->ms_name, ms->ms_stage, NULL, AS_CACHE);
	}

	/*
	 * Initialize Postfix macro table
	 */
	milter_postfix_macro_table = sht_create(BUCKETS, NULL);
	if (milter_postfix_macro_table == NULL)
	{
		log_die(EX_SOFTWARE, "milter: init: sht_create failed");
	}

	for (mm = milter_postfix_macros; mm->mm_name; ++mm)
	{
		if (sht_insert(milter_postfix_macro_table, mm->mm_name, mm))
		{
			log_die(EX_SOFTWARE, "milter: init: sht_insert failed");
		}
	}

	/*
	 * Initialize Sendmail macro table
	 */
	milter_sendmail_macro_table = sht_create(BUCKETS, NULL);
	if (milter_sendmail_macro_table == NULL)
	{
		log_die(EX_SOFTWARE, "milter: init: sht_create failed");
	}

	for (mm = milter_sendmail_macros; mm->mm_name; ++mm)
	{
		if (sht_insert(milter_sendmail_macro_table, mm->mm_name, mm))
		{
			log_die(EX_SOFTWARE,
			    "milter: init: sht_insert failed");
		}
	}

	for (macro = milter_macros; *macro; ++macro)
	{
		acl_symbol_register(*macro, MS_ANY, milter_macro_lookup,
		    AS_CACHE);
	}

	return;
}


void
milter_init(void)
{
	char *workdir;

	/*
	 * Load configuration
	 */
	cf_init();

	/*
	 * Change working directory
	 */
	workdir = cf_workdir_path ? cf_workdir_path : defs_mopherd_dir;

	log_debug("new working directory: %s", workdir);

	if (chdir(workdir))
	{
		log_die(EX_OSERR, "milter_init: chdir to \"%s\"",
		    cf_workdir_path);
	}
		
	/*
	 * Other initializations
	 */
	dbt_init();
	acl_init();
	greylist_init();
	tarpit_init();
	module_init();
	milter_load_symbols();

	dbt_open_databases();
	acl_read();

	return;
}


void
milter_clear(void)
{
	acl_clear();
	dbt_clear();
	module_clear();
	cf_clear();

	/*
	 * Free macro tables
	 */
	sht_delete(milter_postfix_macro_table);
	sht_delete(milter_sendmail_macro_table);

	return;
}


static void
milter_reload(int signal)
{
	if (pthread_rwlock_wrlock(&milter_reload_lock))
	{
		log_die(EX_SOFTWARE, "milter_reload: pthread_rwlock_wrlock");
	}

	util_block_signals(SIGUSR1, 0);

	milter_clear();
	milter_init();

	util_unblock_signals(SIGUSR1, 0);

	if (pthread_rwlock_unlock(&milter_reload_lock))
	{
		log_die(EX_SOFTWARE, "milter_reload: pthread_rwlock_unlock");
	}

	return;
}


int8_t
milter(void)
{
	int8_t r;
	struct smfiDesc smfid;
	mode_t mask;

	/*
	 * Prepare smfiDesc
	 */
	bzero(&smfid, sizeof(struct smfiDesc));

	smfid.xxfi_name = "libmilter";
	smfid.xxfi_version = SMFI_VERSION;
	smfid.xxfi_flags = SMFIF_ADDHDRS | SMFIF_CHGHDRS | SMFIF_CHGFROM |
	    SMFIF_ADDRCPT | SMFIF_ADDRCPT_PAR | SMFIF_DELRCPT | SMFIF_CHGBODY;
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

	/*
	 * Control socket permissions via umask
	 */
	mask = umask(cf_milter_socket_umask);

	log_debug("milter: using socket: %s", cf_milter_socket);

	if (smfi_setconn(cf_milter_socket) == MI_FAILURE) {
		log_die(EX_SOFTWARE, "milter: smfi_setconn failed");
	}

	if (cf_milter_socket_timeout >= 0)
	{
		smfi_settimeout(cf_milter_socket_timeout);
	}

	if (smfi_register(smfid) == MI_FAILURE) {
		log_die(EX_SOFTWARE, "milter: smfi_register failed");
	}

	/*
	 * Reload on SIGUSR1
	 */
	if (util_signal(SIGUSR1, milter_reload))
	{
		log_die(EX_SOFTWARE, "milter: util_signal failed");
	}

	r = smfi_main();

	if (r == MI_SUCCESS)
	{
		log_debug("milter: smfi_main returned successful");

		/*
		 * Unlink socket
		 */
		sock_unix_unlink(cf_milter_socket);
	}
	else
	{
		log_error("milter: smfi_main returned with errors");
	}

	/*
	 * Reset umask
	 */
	umask(mask);

	milter_running = 0;

	return r;
}
