
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include <mopher.h>

#include "cf_yacc.h"

// Default hashtable size (var.c) before config is initialized
#define BUCKETS 128

/*
 * Configuration file path
 */
static char *cf_filename;

/*
 * Configuration table
 */
static var_t *cf_config;

/*
 * Yacc Parser
 */
extern FILE *cf_in;
extern int cf_parse(void);

/*
 * Default Configuration cf_defaults.conf is compiled in.
 */
extern char _binary_cf_defaults_conf_start;
extern char _binary_cf_defaults_conf_end;

/*
 * Parser stucture for parser()
 */
parser_t cf_parser;

/*
 * Extern configuration symbols
 */
VAR_INT_T	 cf_random_milter_id;
VAR_INT_T	 cf_hashtable_buckets = BUCKETS;
VAR_INT_T	 cf_syslog_facility;
char		*cf_workdir_path;
char		*cf_mopherd_group;
char		*cf_mopherd_user;
char		*cf_module_path;
VAR_INT_T	 cf_greylist_deadline;
VAR_INT_T	 cf_greylist_visa;
char		*cf_acl_path;
char		*cf_milter_socket;
VAR_INT_T	 cf_milter_socket_timeout;
VAR_INT_T	 cf_milter_socket_permissions;
VAR_INT_T	 cf_milter_wait;
VAR_INT_T	 cf_acl_log_level;
VAR_INT_T	 cf_dbt_cleanup_interval;
VAR_INT_T        cf_dbt_fatal_errors;
char		*cf_hostname;
VAR_INT_T	 cf_client_retry_interval;
char		*cf_control_socket;
VAR_INT_T	 cf_control_socket_permissions;
VAR_INT_T	 cf_tarpit_progress_interval;
VAR_INT_T	 cf_counter_expire_low;
VAR_INT_T	 cf_counter_expire_high;
VAR_INT_T	 cf_counter_threshold;
VAR_INT_T	 cf_dblog_expire;
VAR_INT_T	 cf_mopher_header;
char		*cf_mopher_header_name;
VAR_INT_T	 cf_connect_timeout;
VAR_INT_T	 cf_connect_retries;

/*
 * Symbol table
 */
static cf_symbol_t cf_symbols[] = {
	{ "random_milter_id", &cf_random_milter_id },
	{ "hashtable_buckets", &cf_hashtable_buckets },
	{ "syslog_facility", &cf_syslog_facility },
	{ "workdir_path", &cf_workdir_path },
	{ "mopherd_group", &cf_mopherd_group },
	{ "mopherd_user", &cf_mopherd_user },
	{ "module_path", &cf_module_path },
	{ "greylist_deadline", &cf_greylist_deadline },
	{ "greylist_visa", &cf_greylist_visa },
	{ "acl_path", &cf_acl_path },
	{ "acl_log_level", &cf_acl_log_level },
	{ "milter_socket", &cf_milter_socket },
	{ "milter_socket_timeout", &cf_milter_socket_timeout },
	{ "milter_socket_permissions", &cf_milter_socket_permissions },
	{ "milter_wait", &cf_milter_wait },
	{ "cleanup_interval", &cf_dbt_cleanup_interval },
	{ "fatal_database_errors", &cf_dbt_fatal_errors },
	{ "hostname", &cf_hostname },
	{ "client_retry_interval", &cf_client_retry_interval },
	{ "control_socket", &cf_control_socket },
	{ "control_socket_permissions", &cf_control_socket_permissions },
	{ "tarpit_progress_interval", &cf_tarpit_progress_interval },
	{ "counter_expire_low", &cf_counter_expire_low },
	{ "counter_expire_high", &cf_counter_expire_high },
	{ "counter_threshold", &cf_counter_threshold },
	{ "dblog_expire", &cf_dblog_expire },
	{ "mopher_header", &cf_mopher_header },
	{ "mopher_header_name", &cf_mopher_header_name },
	{ "connect_timeout", &cf_connect_timeout },
	{ "connect_retries", &cf_connect_retries },
	{ NULL, NULL }
};

/*
 * Constants
 */
static VAR_INT_T cf_log_facility_auth     = LOG_AUTH;
static VAR_INT_T cf_log_facility_authpriv = LOG_AUTHPRIV;
static VAR_INT_T cf_log_facility_cron     = LOG_CRON;
static VAR_INT_T cf_log_facility_daemon   = LOG_DAEMON;
static VAR_INT_T cf_log_facility_ftp      = LOG_FTP;
static VAR_INT_T cf_log_facility_kern     = LOG_KERN;
static VAR_INT_T cf_log_facility_local0   = LOG_LOCAL0;
static VAR_INT_T cf_log_facility_local1   = LOG_LOCAL1;
static VAR_INT_T cf_log_facility_local2   = LOG_LOCAL2;
static VAR_INT_T cf_log_facility_local3   = LOG_LOCAL3;
static VAR_INT_T cf_log_facility_local4   = LOG_LOCAL4;
static VAR_INT_T cf_log_facility_local5   = LOG_LOCAL5;
static VAR_INT_T cf_log_facility_local6   = LOG_LOCAL6;
static VAR_INT_T cf_log_facility_local7   = LOG_LOCAL7;
static VAR_INT_T cf_log_facility_lpr      = LOG_LPR;
static VAR_INT_T cf_log_facility_mail     = LOG_MAIL;
static VAR_INT_T cf_log_facility_news     = LOG_NEWS;
static VAR_INT_T cf_log_facility_syslog   = LOG_SYSLOG;
static VAR_INT_T cf_log_facility_user     = LOG_USER;
static VAR_INT_T cf_log_facility_uucp     = LOG_UUCP;
static VAR_INT_T cf_log_level_emerg       = LOG_EMERG;
static VAR_INT_T cf_log_level_alert       = LOG_ALERT;
static VAR_INT_T cf_log_level_crit        = LOG_CRIT;
static VAR_INT_T cf_log_level_err         = LOG_ERR;
static VAR_INT_T cf_log_level_warning     = LOG_WARNING;
static VAR_INT_T cf_log_level_notice      = LOG_NOTICE;
static VAR_INT_T cf_log_level_info        = LOG_INFO;
static VAR_INT_T cf_log_level_debug       = LOG_DEBUG;

static cf_constant_t cf_constants[] = {
	{ VT_INT, "LOG_AUTH",     &cf_log_facility_auth },
	{ VT_INT, "LOG_AUTHPRIV", &cf_log_facility_authpriv },
	{ VT_INT, "LOG_CRON",     &cf_log_facility_cron },
	{ VT_INT, "LOG_DAEMON",   &cf_log_facility_daemon },
	{ VT_INT, "LOG_FTP",      &cf_log_facility_ftp },
	{ VT_INT, "LOG_KERN",     &cf_log_facility_kern },
	{ VT_INT, "LOG_LOCAL0",   &cf_log_facility_local0 },
	{ VT_INT, "LOG_LOCAL1",   &cf_log_facility_local1 },
	{ VT_INT, "LOG_LOCAL2",   &cf_log_facility_local2 },
	{ VT_INT, "LOG_LOCAL3",   &cf_log_facility_local3 },
	{ VT_INT, "LOG_LOCAL4",   &cf_log_facility_local4 },
	{ VT_INT, "LOG_LOCAL5",   &cf_log_facility_local5 },
	{ VT_INT, "LOG_LOCAL6",   &cf_log_facility_local6 },
	{ VT_INT, "LOG_LOCAL7",   &cf_log_facility_local7 },
	{ VT_INT, "LOG_LPR",      &cf_log_facility_lpr },
	{ VT_INT, "LOG_MAIL",     &cf_log_facility_mail },
	{ VT_INT, "LOG_NEWS",     &cf_log_facility_news },
	{ VT_INT, "LOG_SYSLOG",   &cf_log_facility_syslog },
	{ VT_INT, "LOG_USER",     &cf_log_facility_user },
	{ VT_INT, "LOG_UUCP",     &cf_log_facility_uucp },

	{ VT_INT, "LOG_EMERG",    &cf_log_level_emerg },
	{ VT_INT, "LOG_ALERT",    &cf_log_level_alert },
	{ VT_INT, "LOG_CRIT",     &cf_log_level_crit },
	{ VT_INT, "LOG_ERR",      &cf_log_level_err },
	{ VT_INT, "LOG_WARNING",  &cf_log_level_warning },
	{ VT_INT, "LOG_NOTICE",   &cf_log_level_notice },
	{ VT_INT, "LOG_INFO",     &cf_log_level_info },
	{ VT_INT, "LOG_DEBUG",    &cf_log_level_debug },
	{ VT_NULL, NULL, NULL }
};

/*
 * Prototypes for cf_functions
 */

static void cf_setup_hostname(void **data);


static cf_function_t cf_functions[] = {
	{ VT_STRING, "hostname", cf_setup_hostname },
	{ VT_NULL, NULL, NULL }
};

static void
cf_setup_hostname(void **data)
{
	char buffer[1024];

	if (gethostname(buffer, sizeof(buffer))) {
		log_sys_error("cf_setup_hostname: gethostname failed");
		strcpy(buffer, "unknown");
	}

	*data = strdup(buffer);
	if (*data == NULL) {
		log_sys_die(EX_CONFIG, "cf_setup_hostname: strdup");
	}

	return;
}

static void
cf_load_functions(void)
{
	cf_function_t *function;
	var_t *v;
	void *data;

	for(function = cf_functions; function->cf_type; ++function) {

		if(vtable_get(cf_config, function->cf_name)) {
			continue;
		}

		function->cf_callback(&data);
		if (data == NULL) {
			log_die(EX_CONFIG, "cf_load_functions: callback for "
				"\"%s\" failed", function->cf_name);
		}

		v = var_create(function->cf_type, function->cf_name, data,
			VF_KEEPNAME);
		if (v == NULL) {
			log_die(EX_CONFIG, "cf_load_functions: var_create "
				"failed");
		}

		if(vtable_set(cf_config, v) == -1) {
			log_die(EX_CONFIG, "cf_load_functions: var_table_set "
				"failed");
		}
	}

	return;
}


static void
cf_load_symbols(void)
{
	cf_symbol_t *symbol;
	var_t *v;

	for(symbol = cf_symbols; symbol->cs_name; ++symbol) {

		v = vtable_lookup(cf_config, symbol->cs_name);
		if(v == NULL) {
			continue;
		}

		switch(v->v_type) {
		case VT_INT:
			*(VAR_INT_T *) symbol->cs_ptr =
				*(VAR_INT_T *) v->v_data;
			 break;

		case VT_FLOAT:
			*(VAR_FLOAT_T *) symbol->cs_ptr =
				*(VAR_FLOAT_T *) v->v_data;
			 break;

		case VT_STRING:
			*((char **) symbol->cs_ptr) = (char *) v->v_data;
			break;

		default:
			log_die(EX_CONFIG, "cf_symbols_load: bad type");
		}
	}

	return;
}

static void
cf_load_defaults(void)
{
	char *buffer = &_binary_cf_defaults_conf_start;
	size_t size = &_binary_cf_defaults_conf_end - buffer;

	cf_in = fmemopen(buffer, size, "r");
	if (cf_in == NULL)
	{
		log_sys_die(EX_OSERR, "cf_load_defaults: fmemopen failed");
	}

	log_debug("cf_init: load default configuration");

	parser(&cf_parser, "BUILT-IN CONFIGURATION", 0, &cf_in, cf_parse);
	parser_clear(&cf_parser);

	fclose(cf_in);

	return;
}


static void
cf_load_file(char *file)
{
	parser(&cf_parser, file, 1, &cf_in, cf_parse);
	parser_clear(&cf_parser);

	return;
}



void
cf_clear(void)
{
	if (cf_config)
	{
		var_delete(cf_config);
	}

	return;
}

void
cf_path(char *path)
{
	cf_filename = strdup(path);
	if (cf_filename == NULL)
	{
		log_sys_die(EX_OSERR, "cf_file: strdup");
	}

	return;
}

void
cf_init(void)
{
	if (cf_filename == NULL)
	{
		cf_filename = defs_mopherd_conf;
	}

	//char buffer[4096];

	cf_config = vtable_create("config", VF_KEEPNAME);
	if (cf_config == NULL)
	{
		log_die(EX_CONFIG, "cf_init: vtable_create failed");
	}

	//var_dump(cf_config, buffer, sizeof(buffer));
	//printf(buffer);
	
	cf_load_defaults();
	cf_load_file(cf_filename);
	cf_load_functions();
	cf_load_symbols();

	return;
}


void
cf_set_new(var_type_t type, char *name, void *data, int flags)
{
	if (vtable_set_new(cf_config, type, name, data, flags))
	{
		log_die(EX_SOFTWARE, "cf_set: vtable_set_new failed");
	}

	return;
}

var_t *
cf_constant(char *name)
{
	cf_constant_t *cc;

	for(cc = cf_constants; cc->cc_name; ++cc)
	{
		if (strcmp(cc->cc_name, name) == 0)
		{
			return var_create(cc->cc_type, NULL, cc->cc_ptr, VF_KEEP);
		}
	}

	parser_error(&cf_parser, "Unknown token %s", name);

	return NULL;
}


void
cf_set_keylist(var_t *table, ll_t *keys, var_t *v)
{
	char *key;
	var_t *sub;

	if (table == NULL) {
		table = cf_config;
	}

	/*
	 * Last key queued belongs to var_t *v itself.
	 */
	key = LL_DEQUEUE(keys);
	if(keys->ll_size == 0) {

		/*
		 * keys is created in cf_yacc.y and no longer needed.
		 */
		ll_delete(keys, NULL);

		if(v->v_name == NULL) {
			v->v_name = key;
		}

		if(vtable_set(table, v) == -1) {
			log_die(EX_CONFIG, "cf_set: vtable_set failed");
		}

		return;
	}

	if((sub = vtable_lookup(table, key))) {
		cf_set_keylist(sub, keys, v);

		/*
		 * key is strdupd in cf_yacc.y and no longer needed.
		 */
		free(key);

		return;
	}

	if ((sub = vtable_create(key, 0)) == NULL) {
		log_die(EX_CONFIG, "cf_setr: vtable_create failed");
	}

	if(vtable_set(table, sub) == -1) {
		log_die(EX_CONFIG, "cf_set: vtable_set failed");
	}

	cf_set_keylist(sub, keys, v);

	return;
}


var_t *
cf_get(var_type_t type, ...)
{
	va_list ap;
	var_t *v;

	va_start(ap, type);

	v = vtable_getva(type, cf_config, ap);

	va_end(ap);

	return v;
}

void *
cf_get_value(var_type_t type, ...)
{
	va_list ap;
	var_t *v;

	va_start(ap, type);

	v = vtable_getva(type, cf_config, ap);

	va_end(ap);

	if (v == NULL)
	{
		return NULL;
	}

	return v->v_data;
}

int
cf_load_list(ll_t *list, char *key, var_type_t type)
{
	var_t *v, *item;
	ll_entry_t *pos;

	v = vtable_lookup(cf_config, key);
	if (v == NULL)
	{
		log_error("cf_load_list: %s not found", key);
		return -1;
	}

	// Scalar
	if (v->v_type == type)
	{
		if (LL_INSERT(list, v->v_data) == -1)
		{
			log_error("cf_load_list: LL_INSERT failed");
			return -1;
		}
		return 0;
	}

	// Unexpected type
	if (v->v_type != VT_LIST)
	{
		log_error("config error: unexpected value for %s", key);
		return -1;
	}

	pos = LL_START((ll_t *) v->v_data);
	while ((item = ll_next(v->v_data, &pos)))
	{
		if (item->v_type != type)
		{
			log_error("config error: unexpected value in %s", key);
			return -1;
		}

		if (LL_INSERT(list, item->v_data) == -1)
		{
			log_error("cf_load_list: LL_INSERT failed");
			return -1;
		}
	}

	return 0;
}
