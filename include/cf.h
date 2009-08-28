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

//extern char	*cf_file;
//extern int	 cf_foreground;
//extern int	 cf_log_level;
//extern ll_t	*cf_master_processes;
//extern char	*cf_judge_socket;
//extern int	 cf_judge_dump_interval;
//extern char	*cf_acl_path;
//extern int	 cf_greylist_valid;
//extern char	*cf_milter_socket;
//extern ll_t	*cf_milter_dnsrbl;
//extern char	*cf_milter_spamd_socket;
//extern int	 cf_milter_socket_timeout;
//extern int	 cf_greylist_default_delay;
//extern int	 cf_greylist_default_visa;
//extern int	 cf_greylist_default_valid;
//extern int	 cf_greylist_dynamic_factor;
//extern int	 cf_greylist_dynamic_divisor;
//extern char	*cf_acl_mod_path;


/*
 * Prototypes
 */

void cf_clear(void);
void cf_init(char *cf_file);
void cf_set(var_t *table, ll_t *keys, var_t *v);
int cf_yyinput(char *buffer, int size);

#endif /* _CF_H_ */
