#ifndef _CF_H_
#define _CF_H_

#include "ll.h"

/*
 * Global configuration variables
 */

extern char	*cf_file;
extern int	 cf_foreground;
extern int	 cf_log_level;
extern ll_t	*cf_master_processes;
extern char	*cf_judge_socket;
extern int	 cf_judge_dump_interval;
extern char	*cf_acl_path;
extern int	 cf_greylist_valid;
extern char	*cf_milter_socket;
extern ll_t	*cf_milter_dnsrbl;
extern char	*cf_milter_spamd_socket;
extern int	 cf_milter_socket_timeout;
extern int	 cf_greylist_default_delay;
extern int	 cf_greylist_default_visa;
extern int	 cf_greylist_default_valid;
extern int	 cf_greylist_dynamic_factor;
extern int	 cf_greylist_dynamic_divisor;
extern char	*cf_acl_mod_path;


/*
 * Prototypes
 */

void cf_clear(void);
void cf_init(int argc, char **argv);

#endif /* _CF_H_ */
