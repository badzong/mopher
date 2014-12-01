#include <config.h>

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

#include <mopher.h>

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
#define MSN_ABORT	"abort"
#define MSN_CLOSE	"close"

#define BUCKETS 127

/*
 * Symbols without callback
 */
static milter_symbol_t milter_symbols[] = {
	{ "milter_ctx", MS_ANY },
	{ "action", MS_OFF_CONNECT },
	{ "id", MS_ANY },
	{ "stage", MS_ANY },
	{ "stagename", MS_ANY }, 
	{ "unknown_command", MS_UNKNOWN },
	{ "received", MS_ANY },
	{ "hostaddr", MS_ANY },
	{ "hostaddr_str", MS_ANY },
	{ "hostname", MS_ANY },
	{ "origin", MS_ANY },
	{ "helo", MS_OFF_HELO },
	{ "envfrom", MS_OFF_ENVFROM },
	{ "envfrom_addr", MS_OFF_ENVFROM },
	{ "envrcpt", MS_OFF_ENVRCPT },
	{ "envrcpt_addr", MS_OFF_ENVRCPT },
	{ "recipients", MS_OFF_ENVRCPT },
	{ "recipient_list", MS_OFF_DATA },
	{ "header_name", MS_HEADER },
	{ "header_value", MS_HEADER },
	{ "headers", MS_OFF_EOH },
	{ "headers_size", MS_OFF_EOH },
	{ "message_id", MS_OFF_EOH },
	{ "queue_id", MS_OFF_DATA },
	{ "subject", MS_OFF_EOH },
	{ "body", MS_EOM },
	{ "body_size", MS_EOM },
	{ "message_size", MS_EOM },
	{ NULL, 0 }
};


/*
 * Rwlock for reloading
 */
static pthread_rwlock_t milter_reload_lock = PTHREAD_RWLOCK_INITIALIZER;

/*
 * Milter state database
 */
static dbt_t milter_state_dbt;
static var_t *milter_state_record;
static var_t *milter_state_scheme;
static VAR_INT_T milter_state_version = 1; // State database scheme version

/*
 * Saved id
 */
static VAR_INT_T milter_id_int;
static VAR_INT_T *milter_id;
static pthread_mutex_t milter_id_mutex = PTHREAD_MUTEX_INITIALIZER;


int milter_running = 1;


static VAR_INT_T
milter_get_id(void)
{
	VAR_INT_T r = -1;

	if (pthread_mutex_lock(&milter_id_mutex))
	{
		log_sys_error("milter_get_id: pthread_mutex_lock");
		return -1;
	}

	r = ++(*milter_id);

	/*
	 * That's a serious problem. Keep running for stability.
	 */
	if (dbt_db_set(&milter_state_dbt, milter_state_record))
	{
		log_warning("milter_get_id: dbt_db_set failed");
	}

	if (pthread_mutex_unlock(&milter_id_mutex))
	{
		log_sys_error("milter_get_id: pthread_mutex_unlock");
	}

	return r;
}


static int
milter_origin(char *buffer, int size, char *hostname, char *hostaddr)
{
	/*
	 * Save only the domainname in the greylist tuple. Useful for
	 * greylisting mail farms.
	 */
	int len;

	// Hostname is known
	if (strcmp(hostaddr, "(null)") && strncmp(hostname + 1, hostaddr, strlen(hostaddr)))
	{
		// Make sure we have punycode
		len = regdom_punycode(buffer, size, hostname);
		if (len == -1)
		{
			log_error("milter_origin: regdom failed");
		}

		return len;
	}

 	// Hostname is unset "(null)" or set to hostaddr
	len = strlen(hostaddr);
	if (len >= size)
	{
		log_error("milter_origin: buffer exhausted");
		return -1;
	}

	strcpy(buffer, hostaddr);

	return len;
}


char *
milter_macro_lookup(milter_stage_t stage, char *macro, var_t *attrs)
{
	char *value;
	SMFICTX *ctx;
	char *stagename;
	int r;

	r = acl_symbol_dereference(attrs, "milter_ctx", &ctx,
		"stagename", &stagename, NULL);
	if (r)
	{
		log_error("milter_macro_lookup: acl_symbol_dereference "
		    "failed");
		return NULL;
	}

	value = smfi_getsymval(ctx, macro);
	if (value == NULL)
	{
		log_error("milter_macro_lookup: smfi_getsymval failed for "
			"macro %s in %s stage", macro, stagename);
		return NULL;
	}

	return value;
}


static sfsistat
milter_acl(milter_stage_t stage, char *stagename, milter_priv_t * mp)
{
	VAR_INT_T action;

	action = acl(stage, stagename, mp->mp_table);
	if (vtable_setv(mp->mp_table, VT_INT, "action", &action,
	    VF_KEEPNAME | VF_COPYDATA, VT_NULL))
	{
		log_error("milter_acl: vtable_setv failed");
		return SMFIS_TEMPFAIL;
	}

	/*
	 * Translate acl_action_type into sfsistat
	 */
	switch (action)
	{
	case ACL_CONTINUE:
		log_debug("milter_acl: stage %s returns SMFIS_CONTINUE",
			stagename);
		return SMFIS_CONTINUE;

	case ACL_REJECT:
		log_debug("milter_acl: stage %s returns SMFIS_REJECT",
			stagename);
		return SMFIS_REJECT;

	case ACL_DISCARD:
		log_debug("milter_acl: stage %s returns SMFIS_DISCARD",
			stagename);
		return SMFIS_DISCARD;

	case ACL_ACCEPT:
		log_debug("milter_acl: stage %s returns SMFIS_ACCEPT",
			stagename);
		return SMFIS_ACCEPT;

	case ACL_TEMPFAIL:
	case ACL_GREYLIST:
		log_debug("milter_acl: stage %s returns SMFIS_TEMPFAIL",
			stagename);
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
milter_priv_clear_msg(milter_priv_t *mp)
{
	milter_symbol_t *ms;
	milter_stage_t stages;

	stages = MS_CONNECT | MS_UNKNOWN | MS_HELO;

	for (ms = milter_symbols; ms->ms_name; ++ms)
	{
		if (ms->ms_stage | stages)
		{
			continue;
		}

		vtable_remove(mp->mp_table, ms->ms_name);
	}
	
	if (mp->mp_header)
	{
		free(mp->mp_header);
		mp->mp_header = NULL;
	}

	if (mp->mp_body)
	{
		free(mp->mp_body);
		mp->mp_body = NULL;
	}

	mp->mp_recipients = 0;
	mp->mp_headerlen  = 0;
	mp->mp_bodylen    = 0;

	return;
}


static void
milter_priv_delete(milter_priv_t * mp)
{
	if (mp->mp_header)
	{
		free(mp->mp_header);
	}

	if (mp->mp_body)
	{
		free(mp->mp_body);
	}

	if (mp->mp_table)
	{
		var_delete(mp->mp_table);
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
		log_sys_error("milter_priv_create: malloc");
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


static milter_priv_t *
milter_common_init(SMFICTX *ctx, VAR_INT_T stage, char *stagename)
{
	milter_priv_t *mp = NULL;
	char *queue_id;

	if (pthread_rwlock_rdlock(&milter_reload_lock))
	{
		log_sys_error("milter_common_init: pthread_rwlock_rdlock");
		return NULL;
	}

	if (stage == MS_CONNECT)
	{
		mp = milter_priv_create();
	}
	else
	{
		mp = ((milter_priv_t *) smfi_getpriv(ctx));
	}

	if (mp == NULL)
	{
		log_debug("milter_common_init: empty private data in \"%s\"",
		    stagename);
		goto error;
	}

	/*
	 * Call smfi_setpriv on connect
	 */
	if (stage == MS_CONNECT)
	{
		smfi_setpriv(ctx, mp);
	}

	/*
	 * Save last stage (used for example if client unexpectedly cloeses 
	 * connection).
	 */
	else
	{
		if (vtable_rename(mp->mp_table, "stage",
		    "laststage"))
		{
			log_error("milter_common_init: vtable_rename failed");
			goto error;
		}
	}

	if (vtable_setv(mp->mp_table,
	    VT_POINTER, "milter_ctx", ctx, VF_KEEP,
	    VT_INT, "stage", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "stagename", stagename, VF_KEEP,
	    VT_NULL))
	{
		log_error("milter_common_init: vtable_setv failed");
		goto error;
	}

	/*
	 * Try to lookup queue_id if neccessary.
	 */
	if (vtable_lookup(mp->mp_table, "queue_id"))
	{
		return mp;
	}

	queue_id = smfi_getsymval(ctx, "i");
	if (queue_id)
	{
		if (vtable_set_new(mp->mp_table, VT_STRING, "queue_id",
		    queue_id, VF_KEEPNAME | VF_COPYDATA))
		{
			log_error("milter_common_init: vtable_set_new failed");
			goto error;
		}

		log_message(LOG_ERR, mp->mp_table, "queue_id=%s", queue_id);
	}

	return mp;


error:

	/*
	 * Make sure reload_lock is released
	 */
	if (pthread_rwlock_unlock(&milter_reload_lock))
	{
		log_sys_error("milter_common_init: pthread_rwlock_unlock");
	}

	/*
	 * Free milter_priv if an error occures. The calling routine should
	 * return SMFIS_TEMPFAIL!
	 */
	if (mp)
	{
		milter_priv_delete(mp);
		smfi_setpriv(ctx, NULL);
	}

	return NULL;
}


static void
milter_common_fini(SMFICTX *ctx, milter_priv_t *mp, milter_stage_t stage)
{
	/*
	 * If mp is null theres no lock and nothing to free.
	 */
	if (mp == NULL)
	{
		log_debug("milter_common_fini: milter_priv is null");
		return;
	}

	/*
	 * Free resources at close
	 */
	if (stage == MS_CLOSE)
	{
		milter_priv_delete(mp);
		smfi_setpriv(ctx, NULL);
	}

	if (pthread_rwlock_unlock(&milter_reload_lock))
	{
		log_sys_error("milter_common_clear: pthread_rwlock_unlock");
	}

	return;
}

static sfsistat
milter_connect(SMFICTX *ctx, char *hostname, _SOCK_ADDR * hostaddr)
{
	milter_priv_t *mp = NULL;
	VAR_INT_T now;
	struct sockaddr_storage *ha_clean = NULL;
	char *hostaddr_str;
	char origin[256];
	VAR_INT_T id;
	sfsistat stat = SMFIS_TEMPFAIL;

	mp = milter_common_init(ctx, MS_CONNECT, MSN_CONNECT);
	if (mp == NULL)
	{
		log_error("milter_connect: milter_common_init failed");
		goto exit;
	}

	if ((id = milter_get_id()) == -1)
	{
		log_error("milter_connect: milter_get_id failed");
		goto exit;
	}

	if ((now = (VAR_INT_T) time(NULL)) == -1) {
		log_sys_error("milter_connect: time");
		goto exit;
	}

	// https://www.milter.org/developers/api/xxfi_connect
	// hostaddr: the host address, as determined by a getpeername(2) call
	// on the SMTP socket. NULL if the type is not supported in the current
	// version or if the SMTP connection is made via stdin.
	if (hostaddr)
	{
		ha_clean = util_hostaddr((struct sockaddr_storage *) hostaddr);
		if (ha_clean == NULL) {
			log_error("milter_connect: util_hostaddr failed");
			goto exit;
		}
	}

	hostaddr_str = util_addrtostr(ha_clean);
	if (hostaddr_str == NULL)
	{
		log_error("milter_connect: util_addrtostr failed");
		goto exit;
	}

	if (milter_origin(origin, sizeof origin, hostname, hostaddr_str) == -1)
	{
		log_error("milter_connect: milter_origin failed");
		goto exit;
	}


	if (vtable_setv(mp->mp_table,
	    VT_INT, "id", &id, VF_KEEPNAME | VF_COPYDATA,
	    VT_INT, "received", &now, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "hostname", hostname, VF_KEEPNAME | VF_COPYDATA,
	    VT_ADDR, "hostaddr", ha_clean, VF_KEEPNAME,
	    VT_STRING, "hostaddr_str", hostaddr_str, VF_KEEPNAME,
	    VT_STRING, "origin", origin,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_NULL))
	{
		log_error("milter_connect: vtable_setv failed");
		goto exit;
	}

	/*
 	 *  Set empty acl_* symbols
 	 */
	acl_match(mp->mp_table, 0, MS_NULL, NULL, NULL, NULL, NULL);

	log_message(LOG_ERR, mp->mp_table, "host=%s addr=%s", hostname,
	    hostaddr_str);

	stat = milter_acl(MS_CONNECT, MSN_CONNECT, mp);

exit:
	milter_common_fini(ctx, mp, MS_CONNECT);

	return stat;
}

static sfsistat
milter_unknown(SMFICTX * ctx, const char *cmd)
{
	milter_priv_t *mp;
	sfsistat stat = SMFIS_TEMPFAIL;

	mp = milter_common_init(ctx, MS_UNKNOWN, MSN_UNKNOWN);
	if (mp == NULL)
	{
		log_error("milter_unknown: milter_common_init failed");
		goto exit;
	}
		
	if (vtable_set_new(mp->mp_table, VT_STRING, "unknown_command",
	    (char *) cmd, VF_KEEPNAME | VF_COPYDATA))
	{
		log_error("milter_unknown: vtable_set_new failed");
		goto exit;
	}

	log_message(LOG_ERR, mp->mp_table, "command=%s", cmd);

	stat = milter_acl(MS_UNKNOWN, MSN_UNKNOWN, mp);

exit:
	milter_common_fini(ctx, mp, MS_UNKNOWN);

	return stat;
}

static sfsistat
milter_helo(SMFICTX * ctx, char *helostr)
{
	milter_priv_t *mp;
	sfsistat stat = SMFIS_TEMPFAIL;

	mp = milter_common_init(ctx, MS_HELO, MSN_HELO);
	if (mp == NULL)
	{
		log_error("milter_helo: milter_common_init failed");
		goto exit;
	}

	if (vtable_set_new(mp->mp_table, VT_STRING, "helo", helostr,
	    VF_KEEPNAME | VF_COPYDATA))
	{
		log_error("milter_helo: vtable_set_new failed");
		goto exit;
	}

	log_message(LOG_ERR, mp->mp_table, "name=%s", helostr);

	stat = milter_acl(MS_HELO, MSN_HELO, mp);

exit:
	milter_common_fini(ctx, mp, MS_HELO);

	return stat;
}

static sfsistat
milter_envfrom(SMFICTX * ctx, char **argv)
{
	milter_priv_t *mp;
	sfsistat stat = SMFIS_TEMPFAIL;
	char from[321];

	mp = milter_common_init(ctx, MS_ENVFROM, MSN_ENVFROM);
	if (mp == NULL)
	{
		log_error("milter_envfrom: milter_common_init failed");
		goto exit;
	}

	if (util_strmail(from, sizeof from, argv[0]) == -1)
	{
		log_error("milter_envfrom: util_strmail failed");
		goto exit;
	}

	if (vtable_setv(mp->mp_table,
	    VT_STRING, "envfrom", argv[0], VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "envfrom_addr", from, VF_KEEPNAME | VF_COPYDATA,
	    VT_NULL))
	{
		log_error("milter_envfrom: vtable_setv failed");
		goto exit;
	}

	log_message(LOG_ERR, mp->mp_table, "from=%s", from);

	stat = milter_acl(MS_ENVFROM, MSN_ENVFROM, mp);

exit:
	milter_common_fini(ctx, mp, MS_ENVFROM);

	return stat;
}

static sfsistat
milter_envrcpt(SMFICTX * ctx, char **argv)
{
	milter_priv_t *mp;
	sfsistat stat = SMFIS_TEMPFAIL;
	char rcpt[321];

	mp = milter_common_init(ctx, MS_ENVRCPT, MSN_ENVRCPT);
	if (mp == NULL)
	{
		log_error("milter_envrcpt: milter_common_init failed");
		goto exit;
	}

	++mp->mp_recipients;

	if (util_strmail(rcpt, sizeof rcpt, argv[0]) == -1)
	{
		log_error("milter_envrcpt: util_strmail failed");
		goto exit;
	}

	if (vtable_setv(mp->mp_table,
	    VT_STRING, "envrcpt", argv[0], VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "envrcpt_addr", rcpt, VF_KEEPNAME | VF_COPYDATA,
	    VT_INT, "recipients", &mp->mp_recipients,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_NULL))
	{
		log_error("milter_envrcpt: vtable_setv failed");
		goto exit;
	}

	log_message(LOG_ERR, mp->mp_table, "rcpt=%s", rcpt);

	stat = milter_acl(MS_ENVRCPT, MSN_ENVRCPT, mp);

	/*
	 * Add accepted recipients to recipient_list
	 */
	switch (stat)
	{
	case SMFIS_CONTINUE:
	case SMFIS_ACCEPT:
		break;

	default:
		goto exit;
	}

	if (vtable_list_append_new(mp->mp_table, VT_STRING,
	    "recipient_list", rcpt, VF_KEEPNAME | VF_COPYDATA))
	{
		log_error("milter_envrcpt: vtable_list_append_new failed");
		stat = SMFIS_TEMPFAIL;
		goto exit;
	}

exit:
	milter_common_fini(ctx, mp, MS_ENVRCPT);

	return stat;
}


static sfsistat
milter_data(SMFICTX * ctx)
{
	milter_priv_t *mp;
	sfsistat stat = SMFIS_TEMPFAIL;

	mp = milter_common_init(ctx, MS_DATA, MSN_DATA);
	if (mp == NULL)
	{
		log_error("milter_data: milter_common_init failed");
		goto exit;
	}

	if (vtable_set_new(mp->mp_table, VT_INT, "recipients",
	    &mp->mp_recipients, VF_KEEPNAME | VF_COPYDATA))
	{
		log_error("milter_data: vtable_set_new failed");
		goto exit;
	}

	log_message(LOG_DEBUG, mp->mp_table, "");

	stat = milter_acl(MS_DATA, MSN_DATA, mp);

exit:
	milter_common_fini(ctx, mp, MS_DATA);

	return stat;
}


static sfsistat
milter_header(SMFICTX * ctx, char *headerf, char *headerv)
{
	milter_priv_t *mp;
	int32_t len;
	sfsistat stat = SMFIS_TEMPFAIL;
	char *message_id;

	mp = milter_common_init(ctx, MS_HEADER, MSN_HEADER);
	if (mp == NULL)
	{
		log_error("milter_header: milter_common_init failed");
		goto exit;
	}

	/*
	 * headerf: headerv\r\n
	 */
	len = strlen(headerf) + strlen(headerv) + 5;

	if ((mp->mp_header = (char *) realloc(mp->mp_header,
	    mp->mp_headerlen + len)) == NULL)
	{
		log_sys_error("milter_header: realloc");
		goto exit;
	}

	snprintf(mp->mp_header + mp->mp_headerlen, len, "%s: %s\r\n", headerf,
	    headerv);

	/*
	 * Don't count \0
	 */
	mp->mp_headerlen += len - 1;

	if (vtable_setv(mp->mp_table,
	    VT_STRING, "header_name", headerf,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "header_value", headerv,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_NULL))
	{
		log_error("header: vtable_setv failed");
		goto exit;
	}

	/*
         * log message_id
         */
	if (strcasecmp(headerf, "Message-ID") == 0)
	{
		message_id = util_strdupenc(headerv, "<>");
		if (message_id)
		{
			if (vtable_set_new(mp->mp_table, VT_STRING,
				"message_id", message_id, VF_KEEPNAME))
			{
				log_error("milter_header: vtable_set_new failed");
				goto exit;
			}

			log_message(LOG_ERR, mp->mp_table, "message_id=%s",
				message_id);
		}
	}

	/*
	 * Store Subject in milter_subject
	 */
	if (strcasecmp(headerf, "Subject") == 0)
	{
		if (vtable_set_new(mp->mp_table, VT_STRING, "subject",
			headerv, VF_KEEPNAME | VF_COPYDATA))
		{
			log_error("milter_header: vtable_set_new failed");
			goto exit;
		}
	}

	log_message(LOG_DEBUG, mp->mp_table,
	    "header=%s size=%d", headerf, len);

	stat = milter_acl(MS_HEADER, MSN_HEADER, mp);

exit:
	milter_common_fini(ctx, mp, MS_HEADER);

	return stat;
}

static sfsistat
milter_eoh(SMFICTX * ctx)
{
	milter_priv_t *mp;
	sfsistat stat = SMFIS_TEMPFAIL;

	mp = milter_common_init(ctx, MS_EOH, MSN_EOH);
	if (mp == NULL)
	{
		log_error("milter_eoh: milter_common_init failed");
		goto exit;
	}

	if (vtable_setv(mp->mp_table,
	    VT_STRING, "headers", mp->mp_header, VF_KEEP,
	    VT_INT, "headers_size", &mp->mp_headerlen,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_NULL))
	{
		log_error("milter_eoh: vtable_setv failed");
		return SMFIS_TEMPFAIL;
	}

	log_message(LOG_DEBUG, mp->mp_table, "headers=%d",
	    mp->mp_headerlen);

	stat = milter_acl(MS_EOH, MSN_EOH, mp);

exit:
	milter_common_fini(ctx, mp, MS_EOH);

	return stat;
}

static sfsistat
milter_body(SMFICTX * ctx, unsigned char *body, size_t len)
{
	milter_priv_t *mp;
	sfsistat stat = SMFIS_TEMPFAIL;

	mp = milter_common_init(ctx, MS_BODY, MSN_BODY);
	if (mp == NULL)
	{
		log_error("milter_body: milter_common_init failed");
		goto exit;
	}

	if ((mp->mp_body = (char *) realloc(mp->mp_body,
	    mp->mp_bodylen + len + 1)) == NULL)
	{
		log_sys_error("milter_body: realloc");
		goto exit;
	}

	memcpy(mp->mp_body + mp->mp_bodylen, body, len);

	mp->mp_bodylen += len;
	mp->mp_body[mp->mp_bodylen] = 0;

	log_message(LOG_DEBUG, mp->mp_table, "body=%d", len);

	stat = milter_acl(MS_BODY, MSN_BODY, mp);

exit:
	milter_common_fini(ctx, mp, MS_BODY);

	return stat;
}


static sfsistat
milter_eom(SMFICTX * ctx)
{
	milter_priv_t *mp;
	sfsistat stat = SMFIS_TEMPFAIL;
	VAR_INT_T message_size;

	mp = milter_common_init(ctx, MS_EOM, MSN_EOM);
	if (mp == NULL)
	{
		log_error("milter_eom: milter_common_init failed");
		goto exit;
	}

	/*
	 * header + \r\n + body
	 */
	message_size = mp->mp_bodylen + mp->mp_headerlen + 2;

	if (vtable_setv(mp->mp_table,
	    VT_INT, "body_size", &mp->mp_bodylen,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_INT, "message_size", &message_size,
		VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "body", mp->mp_body, VF_KEEP,
	    VT_NULL))
	{
		log_error("milter_eom: vtable_setv failed");
		goto exit;
	}

	log_message(LOG_ERR, mp->mp_table,
	    "message=%d headers=%d body=%d",
	    message_size, mp->mp_headerlen, mp->mp_bodylen);

	stat = milter_acl(MS_EOM, MSN_EOM, mp);

exit:
	milter_common_fini(ctx, mp, MS_EOM);

	milter_priv_clear_msg(mp);

	return stat;
}


static sfsistat
milter_abort(SMFICTX * ctx)
{
	milter_priv_t *mp;

	mp = milter_common_init(ctx, MS_ABORT, MSN_ABORT);
	if (mp == NULL)
	{
		log_error("milter_abort: milter_common_init failed");
		goto exit;
	}

	log_message(LOG_ERR, mp->mp_table, "");

	milter_acl(MS_ABORT, MSN_ABORT, mp);

exit:
	milter_common_fini(ctx, mp, MS_ABORT);

	milter_priv_clear_msg(mp);

	return SMFIS_CONTINUE;
}


static sfsistat
milter_close(SMFICTX * ctx)
{
	milter_priv_t *mp;

	mp = milter_common_init(ctx, MS_CLOSE, MSN_CLOSE);
	if (mp == NULL)
	{
		log_error("milter_close: milter_common_init failed");
		goto exit;
	}

	/*
	 * milter_acl is called. Useful for logging
	 */
	milter_acl(MS_CLOSE, MSN_CLOSE, mp);

	log_message(LOG_ERR, mp->mp_table, "");

exit:
	milter_common_fini(ctx, mp, MS_CLOSE);

	return SMFIS_CONTINUE;
}


static void
milter_load_symbols(void)
{
	milter_symbol_t *ms;

	/*
	 * Symbols without callbacks set by milter.c (the other one)
	 */
	for (ms = milter_symbols; ms->ms_name; ++ms)
	{
		acl_symbol_register(ms->ms_name, ms->ms_stage, NULL, AS_CACHE);
	}

	return;
}


void
milter_db_init(void)
{
	/*
	 * milter_id database
	 */
	milter_state_scheme = vlist_scheme("state",
		"version",	VT_INT,		VF_KEEPNAME | VF_KEY,
		"id",		VT_INT,		VF_KEEPNAME,
		NULL);
	if (milter_state_scheme == NULL)
	{
		log_die(EX_SOFTWARE, "milter_db_init: vlist_scheme failed");
	}

	milter_state_dbt.dbt_scheme = milter_state_scheme;
	milter_state_dbt.dbt_cleanup_interval = -1;

	dbt_register("state", &milter_state_dbt);

	return;
}

void
milter_id_init(void)
{
	var_t *lookup;

	/*
	 * Lookup state record
	 */
	lookup = vlist_record(milter_state_scheme, &milter_state_version, NULL);
	if (lookup == NULL)
	{
		log_die(EX_SOFTWARE, "milter_db_init: vlist_record failed");
	}

	if (dbt_db_get(&milter_state_dbt, lookup, &milter_state_record))
	{
		log_die(EX_SOFTWARE, "milter_db_init: dbt_db_get failed");
	}

	var_delete(lookup);

	/*
	 * State record exists
	 */
	if (milter_state_record)
	{
		milter_id = vlist_record_get(milter_state_record, "id");
		if (milter_id == NULL)
		{
			log_die(EX_SOFTWARE, "milter_db_init: "
			    "vlist_record_get failed");
		}

		return;
	}

	/*
	 * Create new record
	 */
	milter_id = &milter_id_int;
	milter_state_record = vlist_record(milter_state_scheme,
		&milter_state_version, milter_id);
	if (milter_state_record == NULL)
	{
		log_die(EX_SOFTWARE, "milter_db_init: vlist_record failed");
	}

	if (dbt_db_set(&milter_state_dbt, milter_state_record))
	{
		log_die(EX_SOFTWARE, "milter_db_init: dbt_db_set failed");
	}

	return;
}


void
milter_init(void)
{
	int runs_as_nobody;
	int runs_as_nogroup;

	/*
	 * Load configuration
	 */
	cf_init();


	/*
	 * Drop privileges
	 */
	if (getuid() == 0)
	{
		runs_as_nobody = strcmp(cf_mopherd_user, "nobody") == 0;
		runs_as_nogroup = strcmp(cf_mopherd_group, "nogroup") == 0;

		if (runs_as_nobody || runs_as_nogroup)
		{
			log_warning("warning: running with%s%s",
				runs_as_nobody?  " user=nobody": "",
				runs_as_nogroup? " group=nogroup": "");
		}

		if (cf_mopherd_group)
		{
			log_debug("group: %s", cf_mopherd_group);
			util_setgid(cf_mopherd_group);
		}
		if (cf_mopherd_user)
		{
			log_debug("user: %s", cf_mopherd_user);
			util_setuid(cf_mopherd_user);
		}
	}
	else
	{
		log_error("milter_init: already running as unprivileged user");
	}

	/*
	 * Change working directory
	 */
	log_debug("new working directory: %s", cf_workdir_path);
	if (chdir(cf_workdir_path))
	{
		log_sys_die(EX_OSERR, "milter_init: chdir to \"%s\"",
		    cf_workdir_path);
	}
		

	/*
	 * Other initializations
	 */
	dbt_init(1);
	acl_init();
	greylist_init();
	tarpit_init();
	module_init(1, NULL);
	regdom_init();
	milter_load_symbols();
	milter_db_init();

	acl_read();
	dbt_open_databases();
	milter_id_init();

	return;
}


void
milter_clear(void)
{
	regdom_clear();
	acl_clear();
	dbt_clear();
	module_clear();
	cf_clear();

	/*
	 * Free milter_state_record
	 */
	if (milter_state_record)
	{
		var_delete(milter_state_record);
	}

	return;
}


static void
milter_reload(int signal)
{
	log_error("received SIGUSR1: reload");

	if (pthread_rwlock_wrlock(&milter_reload_lock))
	{
		log_sys_die(EX_SOFTWARE, "milter_reload: pthread_rwlock_wrlock");
	}

	util_block_signals(SIGUSR1, 0);

	milter_clear();
	milter_init();

	util_unblock_signals(SIGUSR1, 0);

	if (pthread_rwlock_unlock(&milter_reload_lock))
	{
		log_sys_die(EX_SOFTWARE, "milter_reload: pthread_rwlock_unlock");
	}

	return;
}


static void
milter_unlink_socket(void)
{
	char *path;

	if (strncmp(cf_milter_socket, "unix:", 5))
	{
		return;
	}

	path = cf_milter_socket + 5;

	if (!util_file_exists(path))
	{
		return;
	}

	log_debug("milter_unlink_socket: unlink \"%s\"", path);

	if (unlink(path))
	{
		log_sys_error("milter_unlink_socket: unlink");
	}

	return;
}


int8_t
milter(void)
{
	int8_t r;
	struct smfiDesc smfid;

	/*
	 * Prepare smfiDesc
	 */
	bzero(&smfid, sizeof(struct smfiDesc));

	smfid.xxfi_name		= "libmilter";
	smfid.xxfi_version	= SMFI_VERSION;
	smfid.xxfi_flags	= SMFIF_ADDHDRS | SMFIF_CHGHDRS |
	    SMFIF_CHGFROM | SMFIF_ADDRCPT | SMFIF_ADDRCPT_PAR | SMFIF_DELRCPT |
	    SMFIF_CHGBODY;
	smfid.xxfi_connect	= milter_connect;
	smfid.xxfi_unknown	= milter_unknown;
	smfid.xxfi_helo		= milter_helo;
	smfid.xxfi_envfrom	= milter_envfrom;
	smfid.xxfi_envrcpt	= milter_envrcpt;
	smfid.xxfi_data		= milter_data;
	smfid.xxfi_header	= milter_header;
	smfid.xxfi_eoh		= milter_eoh;
	smfid.xxfi_body		= milter_body;
	smfid.xxfi_eom		= milter_eom;
	smfid.xxfi_abort	= milter_abort;
	smfid.xxfi_close	= milter_close;


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
	 * Set socket permissions
	 */
	if (strncmp(cf_milter_socket, "unix:", 5) == 0)
	{
		if (smfi_opensocket(1) != MI_SUCCESS)
		{
			log_die(EX_SOFTWARE, "milter: smfi_opensocket failed");
		}

		if (util_chmod(cf_milter_socket + 5,
		    cf_milter_socket_permissions))
		{
			log_die(EX_SOFTWARE, "milter: util_chmod failed");
		}
	}

	/*
	 * Reload on SIGUSR1
	 */
	if (util_signal(SIGUSR1, milter_reload))
	{
		log_die(EX_SOFTWARE, "milter: util_signal failed");
	}

	r = smfi_main();

	/*
	 * Acquire reload lock to ensure all threads returned to libmilter
	 */
	if (pthread_rwlock_wrlock(&milter_reload_lock))
	{
		log_sys_error("milter_common_init: pthread_rwlock_wrlock");
	}

	/*
	 * FIXME: Is this really neccessary?
	 */
	log_error("milter: waiting %d second%s for libmilter threads to close",
	    cf_milter_wait, cf_milter_wait == 1? "": "s");
	sleep(cf_milter_wait);


	if (r == MI_SUCCESS)
	{
		log_debug("milter: smfi_main returned successful");
		milter_unlink_socket();
	}
	else
	{
		log_error("milter: smfi_main returned with errors");
	}

	milter_running = 0;

	return r;
}


int
milter_set_reply(var_t *mailspec, char *code, char *xcode, char *message)
{
	SMFICTX *ctx;

	ctx = vtable_get(mailspec, "milter_ctx");
	if (ctx == NULL)
	{
		log_error("milter_set_reply: vtable_get failed");
		return -1;
	}

	log_message(LOG_DEBUG, mailspec, "set_reply: rcode=%s xcode=%s "
	    "message=%s\n", code, xcode, message);

	if (smfi_setreply(ctx, code, xcode, message) != MI_SUCCESS)
	{
		log_error("milter_set_reply: smfi_set_reply failed");
		return -1;
	}

	return 0;
}


int
milter_dump_message(char *buffer, int size, var_t *mailspec)
{
	VAR_INT_T *header_size, *body_size;
	char *header, *body;
	int total;

	if (acl_symbol_dereference(mailspec, "headers", &header,
	    "headers_size", &header_size, "body", &body,
	    "body_size", &body_size, NULL))
	{
		log_error("milter_dump_message: vtable_dereference failed");
		return -1;
	}

	/*
	 * headers + \r\n + body + \0
	 */
	total = *header_size + 2 + *body_size;
	if (size < total + 1)
	{
		log_error("milter_dump_message: buffer exhausted %d < %d",
		    size, total + 1);
		return -1;
	}

	/*
	 * Build message
	 */
	strcpy(buffer, header);
	strcat(buffer, "\r\n");
	memcpy(buffer + *header_size + 2, body, *body_size);
	buffer[total] = 0;

	return total;
}


int
milter_message(var_t *mailspec, char **message)
{
	VAR_INT_T *message_size;
	int size;

	message_size = vtable_get(mailspec, "message_size");
	if (message_size == NULL)
	{
		log_error("milter_message: vtable_get failed");
		return -1;
	}

	*message = (char *) malloc(*message_size + 1);
	if (*message == NULL)
	{
		log_sys_error("milter_dump_message: malloc");
		return -1;
	}

	size = milter_dump_message(*message, *message_size + 1, mailspec);
	if (size == -1)
	{
		log_error("milter_message: milter_dump_message failed");
		free(*message);
		*message = NULL;
	}

	return size;
}
