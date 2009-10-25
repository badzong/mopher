#include <stdio.h>
#include <string.h>
#include <libmilter/mfapi.h>

#include "mopher.h"


#define BUCKETS 64

#define POSTFIX "Postfix"
#define POSTFIX_LEN 7

#define SENDMAIL "Sendmail"
#define SENDMAIL_LEN 8


typedef struct milter_macro {
	char		*mm_name;
	char		*mm_macro;
	milter_stage_t	 mm_stage;
} milter_macro_t;


typedef struct milter_symbol {
	char		*ms_name;
	milter_stage_t	 ms_stage;
} milter_symbol_t;

/*
 * Symbols without callback set by milter.c (the other one)
 */
static milter_symbol_t milter_symbols[] = {
	{ "milter_stage", MS_ANY },
	{ "milter_stagename", MS_ANY }, 
	{ "milter_unknown_command", MS_UNKNOWN },
	{ "milter_received", MS_ANY },
	{ "milter_hostaddr", MS_ANY },
	{ "milter_hostname", MS_ANY },
	{ "milter_helo", MS_OFF_HELO },
	{ "milter_envfrom", MS_OFF_ENVFROM },
	{ "milter_envrcpt", MS_ENVRCPT },
	{ "milter_recipients", MS_OFF_DATA },
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
 * FIXME: This is a copy of milter_postfix_macros
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
static ht_t *milter_postfix_macros_ht;
static ht_t *milter_sendmail_macros_ht;


static hash_t 
milter_macro_hash(milter_macro_t *mm)
{
	return HASH(mm->mm_name, strlen(mm->mm_name));
}

static int
milter_macro_match(milter_macro_t *mm1, milter_macro_t *mm2)
{
	if (strcmp(mm1->mm_name, mm2->mm_name)) {
		return 0;
	}

	return 1;
}


static int
milter_macro_lookup(milter_stage_t stage, char *name, var_t *attrs)
{
	ht_t *macro_table;
	char *version;
	milter_macro_t *mm, lookup;
	char *value;
	SMFICTX *ctx;
	char *stagename;
	int r;

	r = var_table_dereference(attrs, "milter_ctx", &ctx,
		"milter_mta_version", &version, "milter_stagename", &stagename,
		NULL);
	if (r)
	{
		log_error("milter_macro_lookup: var_table_dereference failed");
		return -1;
	}

	if (strncmp(version, POSTFIX, POSTFIX_LEN) == 0) {
		macro_table = milter_postfix_macros_ht;
	}
	else if (strncmp(version, SENDMAIL, SENDMAIL_LEN) == 0) {
		macro_table = milter_postfix_macros_ht;
	}
	else {
		log_error("milter_macro_lookup: unkown MTA \"%s\"",
			version);
		return -1;
	}

	memset(&lookup, 0, sizeof(lookup));
	lookup.mm_name = name;

	mm = ht_lookup(macro_table, &lookup);
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

	if (var_table_set_new(attrs, VT_STRING, mm->mm_name, value,
		VF_KEEPNAME | VF_COPYDATA)) {
		log_error("milter_macro_lookup: var_table_set failed");
		return -1;
	}

	return 0;
}


int
milter_init(void)
{
	milter_symbol_t *ms;
	milter_macro_t *mm;
	char **macro;


	/*
	 * Symbols without callbacks set by milter.c (the other one)
	 */
	for (ms = milter_symbols; ms->ms_name; ++ms) {
		if (acl_symbol_register(AS_NULL, ms->ms_name, ms->ms_stage,
			NULL)) {
			log_error("milter: init: acl_symbol_register failed");
			return -1;
		}
	}

	/*
	 * Initialize Postfix macro table
	 */
	milter_postfix_macros_ht = ht_create(BUCKETS,
		(ht_hash_t) milter_macro_hash, (ht_match_t) milter_macro_match,
		NULL);
	if (milter_postfix_macros_ht == NULL) {
		log_error("milter: init: ht_create failed");
		return -1;
	}

	for (mm = milter_postfix_macros; mm->mm_name; ++mm) {
		if (ht_insert(milter_postfix_macros_ht, mm)) {
			log_error("milter: init: ht_insert failed");
			return -1;
		}
	}

	/*
	 * Initialize Sendmail macro table
	 */
	milter_sendmail_macros_ht = ht_create(BUCKETS,
		(ht_hash_t) milter_macro_hash, (ht_match_t) milter_macro_match,
		NULL);
	if (milter_sendmail_macros_ht == NULL) {
		log_error("milter: init: ht_create failed");
		return -1;
	}

	for (mm = milter_sendmail_macros; mm->mm_name; ++mm) {
		if (ht_insert(milter_sendmail_macros_ht, mm)) {
			log_error("milter: init: ht_insert failed");
			return -1;
		}
	}

	for (macro = milter_macros; *macro; ++macro) {
		if (acl_symbol_register(AS_CALLBACK, *macro, MS_ANY,
			milter_macro_lookup)) {
			log_error("milter: init: acl_symbol_register failed");
			return -1;
		}
	}

	return 0;
}

void
milter_fini(void)
{
	ht_delete(milter_postfix_macros_ht);
	ht_delete(milter_sendmail_macros_ht);

	return;
}
