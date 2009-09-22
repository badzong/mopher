#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "log.h"
#include "var.h"
#include "ll.h"
#include "cf.h"
#include "util.h"

#include "cf_yacc.h"

#define BUCKETS 256

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

static char *cf_file_start = &_binary_cf_defaults_conf_start;
static char *cf_file_end = &_binary_cf_defaults_conf_end;
static char *cf_file_pos;

/*
 * Configuration file buffer
 */
static char *cf_file;

/*
 * Extern configuration symbols
 */
VAR_INT_T	 cf_greylist_default_delay;
VAR_INT_T	 cf_greylist_default_visa;
VAR_INT_T	 cf_greylist_default_valid;
char		*cf_acl_path;
char		*cf_acl_mod_path;
char		*cf_milter_socket;
VAR_INT_T	 cf_milter_socket_timeout;
VAR_INT_T	 cf_log_level;
VAR_INT_T	 cf_foreground;
char		*cf_dbt_mod_path;
char		*cf_tables_mod_path;
VAR_INT_T	 cf_table_cleanup_interval;
char		*cf_hostname;

/*
 * Symbol table
 */
static cf_symbol_t cf_symbols[] = {
	{ "greylist_default_delay", &cf_greylist_default_delay },
	{ "greylist_default_visa", &cf_greylist_default_visa },
	{ "greylist_default_valid", &cf_greylist_default_valid },
	{ "acl_path", &cf_acl_path },
	{ "acl_mod_path", &cf_acl_mod_path },
	{ "milter_socket", &cf_milter_socket },
	{ "milter_socket_timeout", &cf_milter_socket_timeout },
	{ "dbt_mod_path", &cf_dbt_mod_path },
	{ "tables_mod_path", &cf_tables_mod_path },
	{ "table_cleanup_interval", &cf_table_cleanup_interval },
	{ "hostname", &cf_hostname },
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
		log_error("cf_setup_hostname: gethostname failed");
		strcpy(buffer, "unknown");
	}

	*data = strdup(buffer);
	if (*data == NULL) {
		log_die(EX_CONFIG, "cf_setup_hostname: strdup");
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

		if(var_table_get(cf_config, function->cf_name)) {
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

		if(var_table_set(cf_config, v) == -1) {
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

		v = var_table_lookup(cf_config, symbol->cs_name);
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

	size = util_file(file, &cf_file);
	if (size == -1) {
		log_die(EX_CONFIG, "cf_load_file: util_file failed");
	}

	if(size == 0) {
		log_warning("cf_load_file: \"%s\" is empty. Using defaults",
			file);
		return;
	}

	cf_file_pos = cf_file_start = cf_file;
	cf_file_end = cf_file_start + size;

	cf_run_parser();

	free(cf_file);
	
	return;
}



void
cf_clear(void)
{
	var_delete(cf_config);

	return;
}

void
cf_init(char *file)
{
	//char buffer[4096];

	if ((cf_config = var_create(VT_TABLE, "config", NULL,
		VF_KEEPNAME | VF_CREATE)) == NULL) {
		log_die(EX_CONFIG, "cf_init: var_table_create failed");
	}

	log_debug("cf_init: load default configuration");

	cf_run_parser();

	//var_dump(cf_config, buffer, sizeof(buffer));
	//printf(buffer);
	
	cf_load_file(file);
	cf_load_functions();
	cf_load_symbols();

	return;
}

void
cf_set(var_t *table, ll_t *keys, var_t *v)
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

		if(var_table_set(table, v) == -1) {
			log_die(EX_CONFIG, "cf_set: var_table_set failed");
		}

		return;
	}

	if((sub = var_table_lookup(table, key))) {
		cf_set(sub, keys, v);

		/*
		 * key is strdupd in cf_yacc.y and no longer needed.
		 */
		free(key);

		return;
	}

	if ((sub = var_create(VT_TABLE, key, NULL, VF_CREATE)) == NULL) {
		log_die(EX_CONFIG, "cf_setr: var_create failed");
	}

	if(var_table_set(table, sub) == -1) {
		log_die(EX_CONFIG, "cf_set: var_table_set failed");
	}

	cf_set(sub, keys, v);


	return;
}


var_t *
cf_get(var_type_t type, ...)
{
	va_list ap;
	var_t *v;

	va_start(ap, type);

	v = var_table_getva(type, cf_config, ap);

	va_end(ap);

	return v;
}

/*
var_t *
cf_get(var_type_t type, ...)
{
	va_list ap;
	char *key;
	var_t *v = cf_config;
	var_t lookup;

	memset(&lookup, 0, sizeof(lookup));

	va_start(ap, type);
	for(;;) {
		key = va_arg(ap, char *);
		if (key == NULL) {
			break;
		}

		if (v->v_type != VT_TABLE) {
			log_warning("cf_get: key \"%s\" is not a table", key);
			return NULL;
		}

		lookup.v_name = key;

		v = ht_lookup(v->v_data, &lookup);
		if (v == NULL) {
			log_debug("cf_get: no data for key \"%s\"", key);
			return NULL;
		}
	}

	va_end(ap);

	if (v->v_type != type) {
		log_warning("cf_get: type mismatch for key \"%s\"", key);
		return NULL;
	}

	return v;
}
*/
