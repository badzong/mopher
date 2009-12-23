#ifndef _CF_H_
#define _CF_H_

#include "ll.h"
#include "var.h"

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
extern VAR_INT_T	 cf_greylist_visa;
extern VAR_INT_T	 cf_greylist_valid;
extern char		*cf_acl_path;
extern VAR_INT_T	 cf_acl_log_level;
extern char		*cf_milter_socket;
extern VAR_INT_T	 cf_milter_socket_timeout;
extern VAR_INT_T	 cf_milter_socket_umask;
extern VAR_INT_T	 cf_log_level;
extern VAR_INT_T	 cf_foreground;
extern VAR_INT_T	 cf_dbt_cleanup_interval;
extern char		*cf_hostname;
extern char		*cf_spamd_socket;
extern char		*cf_sync_socket;
extern VAR_INT_T	 cf_client_retry_interval;
extern char		*cf_server_socket;
extern VAR_INT_T	 cf_tarpit_progress_interval;
extern VAR_INT_T	 cf_delivered_valid;

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
#endif /* _CF_H_ */
