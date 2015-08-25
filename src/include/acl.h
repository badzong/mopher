#ifndef _ACL_H_
#define _ACL_H_

#include <exp.h>
#include <ll.h>
#include <milter.h>

#define ACL_VARIABLES "VARIABLES"
#define ACL_INCLUDE_DEPTH 10

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
	ACL_TARPIT,
	ACL_MOD,
	ACL_PIPE
};

typedef enum acl_action_type acl_action_type_t;

typedef acl_action_type_t (*acl_action_handler_t)(milter_stage_t stage,
    char *stagename, var_t *mailspec, void *data);
typedef void (*acl_action_delete_t)(void *data);

struct acl_handler_stage
{
	acl_action_handler_t	ah_handler;
	acl_action_delete_t	ah_delete;
	milter_stage_t		ah_stages;
};
typedef struct acl_handler_stage acl_handler_stage_t;


struct acl_reply
{
	exp_t	*ar_code;
	exp_t	*ar_xcode;
	exp_t	*ar_message;
};
typedef struct acl_reply acl_reply_t;

struct acl_action
{
	acl_action_type_t	 aa_type;
	void			*aa_data;
	acl_reply_t		*aa_reply;

	// ACL filename and line number
	char			*aa_filename;
	VAR_INT_T		 aa_line;
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
	AS_FUNCTION,
	AS_MACRO
};
typedef enum acl_symbol_type acl_symbol_type_t;


enum acl_symbol_flag
{
	AS_NONE		= 0,
	AS_CACHE	= 0,
	AS_NOCACHE	= 1
};
typedef enum acl_symbol_flag acl_symbol_flag_t;

struct acl_symbol
{
	acl_symbol_type_t	 as_type;
	milter_stage_t		 as_stages;
	void			*as_data;
	acl_symbol_flag_t	 as_flags;
};
typedef struct acl_symbol acl_symbol_t;

typedef int (*acl_symbol_callback_t)(milter_stage_t stage, char *name, var_t *mailspec);

struct acl_log
{
	exp_t	*al_message;
	exp_t	*al_level;
};
typedef struct acl_log acl_log_t;

struct acl_log_level
{
	VAR_INT_T	 ll_level;
	char		*ll_name;
};
typedef struct acl_log_level acl_log_level_t;

typedef int (*acl_update_t)(milter_stage_t stage, acl_action_type_t at,
    var_t *mailspec);

typedef var_t *(*acl_function_simple_t)(int argc, void **argv);
typedef var_t *(*acl_function_complex_t)(int argc, ll_t *argv);

union acl_function_callback
{
	acl_function_simple_t	 fc_simple;
	acl_function_complex_t	 fc_complex;
};
typedef union acl_function_callback acl_function_callback_t;

enum acl_function_type { AF_SIMPLE, AF_COMPLEX };
typedef enum acl_function_type acl_function_type_t;

struct acl_function
{
	acl_function_type_t		 af_type;
	acl_function_callback_t		 af_callback;
	int				 af_argc;
	var_type_t			*af_types;
};

typedef struct acl_function acl_function_t;

/*
 * Prototypes
 */

acl_reply_t * acl_reply(exp_t *code, exp_t *xcode, exp_t *msg);
acl_action_t * acl_action(acl_action_type_t type, void *data);
acl_action_t * acl_action_reply(acl_action_t *aa, acl_reply_t *ar);
void acl_append(char *table, exp_t *exp, acl_action_t *aa);
void acl_symbol_register(char *name, milter_stage_t stages,acl_symbol_callback_t callback, acl_symbol_flag_t flags);
void acl_constant_register(var_type_t type, char *name, void *data, int flags);
void acl_function_delete(acl_function_t *af);
acl_function_t * acl_function_create(acl_function_type_t type, acl_function_callback_t callback,int argc, var_type_t *types);
void acl_function_register(char *name, acl_function_type_t type,acl_function_callback_t callback, ...);
acl_function_t * acl_function_lookup(char *name);
acl_symbol_t * acl_symbol_lookup(char *name);
var_t * acl_symbol_get(var_t *mailspec, char *name);
int acl_variable_assign(var_t *mailspec, char *name, var_t *value);
var_t * acl_variable_get(var_t *mailspec, char *name);
int acl_symbol_dereference(var_t *mailspec, ...);
void acl_log_delete(acl_log_t *al);
acl_log_t * acl_log_create(exp_t *message);
acl_log_t * acl_log_level(acl_log_t *al, exp_t *level);
acl_action_type_t acl_log(milter_stage_t stage, char *stagename, var_t *mailspec, void *data);
acl_action_type_t acl_jump(milter_stage_t stage, char *stagename, var_t *mailspec, void *data);
acl_action_type_t acl_set(milter_stage_t stage, char *stagename, var_t *mailspec, void *data);
void acl_match(var_t *mailspec, VAR_INT_T matched, VAR_INT_T stage, char *stagename, VAR_INT_T *rule, char *filename, VAR_INT_T *line, char *response);
void acl_update_callback(acl_update_t callback);
acl_action_type_t acl(milter_stage_t stage, char *stagename, var_t *mailspec);
void acl_init(void);
void acl_read(void);
void acl_clear(void);
#endif /* _ACL_H_ */
