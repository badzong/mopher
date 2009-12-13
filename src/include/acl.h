#ifndef _ACL_H_
#define _ACL_H_

#include "exp.h"
#include "ll.h"
#include "milter.h"

enum acl_action_type
{
	ACL_ERROR	= -1,
	ACL_NULL	=  0,
	ACL_NONE,
	ACL_CONTINUE,
	ACL_REJECT,
	ACL_DISCARD,
	ACL_ACCEPT,
	ACL_TEMPFAIL,
	ACL_JUMP,
	ACL_SET,
	ACL_LOG,
	ACL_GREYLIST,
	ACL_TARPIT
};

typedef enum acl_action_type acl_action_type_t;

typedef acl_action_type_t (*acl_action_handler_t)(milter_stage_t stage,
    char *stagename, var_t *mailspec, void *data);

typedef void (*acl_action_delete_t)(void *data);

struct acl_action
{
	acl_action_type_t	 aa_type;
	void			*aa_data;
};
typedef struct acl_action acl_action_t;


struct acl_rule
{
	exp_t		*ar_expression;
	acl_action_t	*ar_action;
};
typedef struct acl_rule acl_rule_t;


enum acl_symbol_type
{
	AS_NULL = 0,
	AS_CONSTANT,
	AS_SYMBOL,
	AS_FUNCTION
};
typedef enum acl_symbol_type acl_symbol_type_t;


struct acl_symbol
{
	acl_symbol_type_t	 as_type;
	milter_stage_t		 as_stages;
	void			*as_data;
};
typedef struct acl_symbol acl_symbol_t;

typedef int (*acl_symbol_callback_t)(milter_stage_t stage, char *name, var_t *mailspec);
typedef var_t *(*acl_function_callback_t)(ll_t *args);

struct acl_log
{
	exp_t	*al_exp;
	int	 al_level;
};

typedef struct acl_log acl_log_t;

typedef int (*acl_update_t)(milter_stage_t stage, acl_action_type_t at,
    var_t *mailspec);

/*
 * Prototypes
 */

acl_action_t * acl_action(acl_action_type_t type, void *data);
void acl_append(char *table, exp_t *exp, acl_action_t *aa);
void acl_symbol_register(char *name, milter_stage_t stages,acl_symbol_callback_t callback);
void acl_constant_register(var_type_t type, char *name, void *data, int flags);
void acl_function_register(char *name, acl_function_callback_t callback);
acl_function_callback_t acl_function_lookup(char *name);
acl_symbol_t * acl_symbol_lookup(char *name);
var_t * acl_symbol_get(var_t *mailspec, char *name);
int acl_symbol_dereference(var_t *mailspec, ...);
void acl_log_delete(acl_log_t *al);
acl_log_t * acl_log_create(exp_t *exp);
acl_log_t * acl_log_level(acl_log_t *al, int level);
acl_action_type_t acl_log(milter_stage_t stage, char *stagename, var_t *mailspec, acl_log_t *al);
acl_action_type_t acl_jump(milter_stage_t stage, char *stagename, var_t *mailspec, char *table);
acl_action_type_t acl_set(milter_stage_t stage, char *stagename, var_t *mailspec, exp_t *exp);
void acl_update_callback(acl_update_t callback);
acl_action_type_t acl(milter_stage_t stage, char *stagename, var_t *mailspec);
void acl_init(void);
void acl_read(char *mail_acl);
void acl_clear(void);
#endif /* _ACL_H_ */
