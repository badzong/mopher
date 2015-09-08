#ifndef _CF_H_
#define _CF_H_

#include <ll.h>
#include <var.h>
#include <parser.h>

extern parser_t cf_parser;
#define cf_parser_error(...) parser_error(&cf_parser, __VA_ARGS__)

typedef struct cf_symbol {
	char		*cs_name;
	void		*cs_ptr;
} cf_symbol_t;

typedef void (*cf_callback_t)(void **data);

typedef struct cf_function {
	var_type_t	 cf_type;
	char		*cf_name;
	cf_callback_t	 cf_callback;
} cf_function_t;


/*
 * Global configuration variables
 */

extern char		*cf_workdir_path;
extern char		*cf_mopherd_group;
extern char		*cf_mopherd_user;
extern char		*cf_module_path;
extern VAR_INT_T	 cf_greylist_deadline;
extern VAR_INT_T	 cf_greylist_visa;
extern char		*cf_acl_path;
extern VAR_INT_T	 cf_acl_log_level;
extern char		*cf_milter_socket;
extern VAR_INT_T	 cf_milter_socket_timeout;
extern VAR_INT_T	 cf_milter_socket_permissions;
extern VAR_INT_T	 cf_milter_wait;
extern VAR_INT_T	 cf_dbt_cleanup_interval;
extern char		*cf_hostname;
extern char		*cf_spamd_socket;
extern char		*cf_clamav_socket;
extern VAR_INT_T	 cf_client_retry_interval;
extern char		*cf_control_socket;
extern VAR_INT_T	 cf_control_socket_permissions;
extern VAR_INT_T	 cf_tarpit_progress_interval;
extern VAR_INT_T	 cf_counter_expire_low;
extern VAR_INT_T	 cf_counter_expire_high;
extern VAR_INT_T	 cf_counter_threshold;
extern VAR_INT_T	 cf_dblog_expire;
extern VAR_INT_T	 cf_mopher_header;
extern char		*cf_mopher_header_name;
extern VAR_INT_T	 cf_connect_timeout;
extern VAR_INT_T	 cf_connect_retries;

/*
 * Prototypes
 */

void cf_run_parser(void);
int cf_yyinput(char *buffer, int size);
void cf_clear(void);
void cf_path(char *path);
void cf_init(void);
void cf_set_new(var_type_t type, char *name, void *data, int flags);
void cf_set_keylist(var_t *table, ll_t *keys, var_t *v);
var_t * cf_get(var_type_t type, ...);
void * cf_get_value(var_type_t type, ...);
int cf_load_list(ll_t *list, char *key, var_type_t type);

#endif /* _CF_H_ */
