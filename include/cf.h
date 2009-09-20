#ifndef _CF_H_
#define _CF_H_

#include "ll.h"
#include "var.h"

typedef struct cf_symbol {
	var_type_t	 cs_type;
	char		*cs_name;
	void		*cs_ptr;
} cf_symbol_t;

/*
 * Global configuration variables
 */

extern VAR_INT_T	 cf_greylist_default_delay;
extern VAR_INT_T	 cf_greylist_default_visa;
extern VAR_INT_T	 cf_greylist_default_valid;
extern char		*cf_acl_path;
extern char		*cf_acl_mod_path;
extern char		*cf_milter_socket;
extern VAR_INT_T	 cf_milter_socket_timeout;
extern VAR_INT_T	 cf_log_level;
extern VAR_INT_T	 cf_foreground;
extern char		*cf_dbt_mod_path;
extern char		*cf_tables_mod_path;
extern VAR_INT_T	 cf_table_cleanup_interval;

/*
 * Prototypes
 */

void cf_run_parser(void);
int cf_yyinput(char *buffer, int size);
void cf_clear(void);
void cf_init(char *file);
void cf_set(var_t *table, ll_t *keys, var_t *v);
var_t * cf_get(var_type_t type, ...);

#endif /* _CF_H_ */
