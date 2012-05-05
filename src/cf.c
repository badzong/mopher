#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include <mopher.h>

#include "cf_yacc.h"

#define BUCKETS 256

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
extern int cf_parse(void);

int cf_line;

/*
 * Default Configuration cf_defaults.conf is compiled in.
 */
extern char _binary_cf_defaults_conf_start;
extern char _binary_cf_defaults_conf_end;

static char *cf_file_start;
static char *cf_file_end;
static char *cf_file_pos;

/*
 * Configuration file buffer
 */
static char *cf_file_buffer;

/*
 * Extern configuration symbols
 */
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
VAR_INT_T	 cf_log_level;
VAR_INT_T	 cf_acl_log_level;
VAR_INT_T	 cf_foreground;
VAR_INT_T	 cf_dbt_cleanup_interval;
char		*cf_hostname;
char		*cf_spamd_socket;
VAR_INT_T	 cf_client_retry_interval;
char		*cf_server_socket;
VAR_INT_T	 cf_tarpit_progress_interval;
VAR_INT_T	 cf_counter_expire_low;
VAR_INT_T	 cf_counter_expire_high;
VAR_INT_T	 cf_counter_threshold;

/*
 * Symbol table
 */
static cf_symbol_t cf_symbols[] = {
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
	{ "hostname", &cf_hostname },
	{ "spamd_socket", &cf_spamd_socket },
	{ "client_retry_interval", &cf_client_retry_interval },
	{ "server_socket", &cf_server_socket },
	{ "tarpit_progress_interval", &cf_tarpit_progress_interval },
	{ "counter_expire_low", &cf_counter_expire_low },
	{ "counter_expire_high", &cf_counter_expire_high },
	{ "counter_threshold", &cf_counter_threshold },
	{ NULL, NULL }
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

void
cf_run_parser(void)
{
	cf_line = 1;
	cf_parse();

	return;
}

int
cf_yyinput(char *buffer, int size)
{
	int n, avail;

	if(cf_file_pos == NULL) {
		cf_file_pos = cf_file_start;
	}

	avail = cf_file_end - cf_file_pos;
	n = avail >= size ? size : avail;

	memcpy(buffer, cf_file_pos, n);

	cf_file_pos += n;

	return n;
}

static void
cf_load_defaults(void)
{
	log_debug("cf_init: load default configuration");

	cf_file_start = &_binary_cf_defaults_conf_start;
	cf_file_end = &_binary_cf_defaults_conf_end;
	cf_file_pos = NULL;

	cf_run_parser();
}


static void
cf_load_file(char *file)
{
	int exists, size;

	exists = util_file_exists(file);
	if (exists == -1) {
		log_die(EX_CONFIG, "cf_load_file: util_file_exists failed");
	}

	if (exists == 0) {
		log_warning("cf_load_file: \"%s\" does not exist. Using defaults",
			file);
		return;
	}

	log_info("cf_load_file: loading configiration file \"%s\"", file);

	size = util_file(file, &cf_file_buffer);
	if (size == -1) {
		log_die(EX_CONFIG, "cf_load_file: util_file failed");
	}

	if(size == 0) {
		log_warning("cf_load_file: \"%s\" is empty. Using defaults",
			file);
		return;
	}

	cf_file_pos = cf_file_start = cf_file_buffer;
	cf_file_end = cf_file_start + size;

	cf_run_parser();

	free(cf_file_buffer);
	
	return;
}



void
cf_clear(void)
{
	var_delete(cf_config);

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
