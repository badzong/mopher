#ifndef _ACL_H_
#define _ACL_H_

#include "var.h"
#include "ll.h"
#include "ht.h"
#include "milter.h"

#define ACL_TABLE_BUCKETS 128
#define ACL_FUNCTION_BUCKETS 128
#define ACL_SYMBOL_BUCKETS 128

typedef enum acl_cmp { AC_NULL = 0, AC_EQ, AC_NE, AC_LT, AC_LE, AC_GT, AC_GE }
    acl_cmp_t;

typedef enum acl_gate { AG_NULL = 0, AG_AND, AG_OR } acl_gate_t;

typedef enum acl_not { AN_NULL = 0, AN_NOT = 1 } acl_not_t;

typedef enum acl_value_type { AV_NULL = 0, AV_CONST, AV_FUNCTION, AV_SYMBOL }
    acl_value_type_t;

typedef enum acl_action_type { AA_ERROR = -1, AA_NULL = 0, AA_PASS, AA_BLOCK,
	AA_DISCARD, AA_CONTINUE, AA_DELAY, AA_JUMP
} acl_action_type_t;

/*
 * Function pointer types for functions and symbols
 */
typedef var_t *(*acl_fcallback_t) (ll_t * args);
typedef int (*acl_scallback_t) (var_t * attrs);

typedef struct acl_symbol {
	char		*as_name;
	acl_scallback_t	 as_callback;
	milter_stage_t	 as_stage;
} acl_symbol_t;

/*
 * Registered function callbacks (loaded modules)
 */
typedef struct acl_function {
	char		*af_name;
	acl_fcallback_t	 af_callback;
} acl_function_t;

/*
 * Function calls
 */
typedef struct acl_call {
	acl_function_t	*ac_function;
	ll_t		*ac_args;
} acl_call_t;

typedef struct acl_value {
	acl_value_type_t av_type;
	void *av_data;
} acl_value_t;

typedef struct acl_condition {
	acl_not_t ac_not;
	acl_gate_t ac_gate;
	acl_cmp_t ac_cmp;
	acl_value_t *ac_left;
	acl_value_t *ac_right;
} acl_condition_t;

typedef struct acl_table {
	char *at_name;
	ll_t *at_rules;
	struct acl_action *at_default;
} acl_table_t;

typedef struct acl_delay {
	int ad_delay;
	int ad_visa;
	int ad_valid;
} acl_delay_t;

typedef struct acl_action {
	acl_action_type_t aa_type;
	char *aa_jump;
	acl_delay_t *aa_delay;
} acl_action_t;

typedef struct acl_rule {
	ll_t *ar_conditions;
	acl_action_t *ar_action;
} acl_rule_t;


/*
 * Prototypes
 */

int acl_symbol_register(char *name, milter_stage_t stage, acl_scallback_t callback);
int acl_symbol_add(var_t * attrs, var_type_t type, char *name, void *data, int flags);
int acl_function_register(char *name, acl_fcallback_t callback);
void acl_value_delete(acl_value_t * av);
acl_value_t * acl_value_create(acl_value_type_t type, void * data);
acl_value_t * acl_value_create_symbol(char *name);
acl_value_t * acl_value_create_function(char *name, ll_t * args);
void acl_condition_delete(acl_condition_t * ac);
acl_condition_t * acl_condition_create(acl_not_t not, acl_gate_t gate, acl_cmp_t cmp,acl_value_t * left, acl_value_t * right);
void acl_action_delete(acl_action_t * aa);
acl_action_t * acl_action_create(acl_action_type_t type, char *jump, acl_delay_t * delay);
void acl_delay_delete(acl_delay_t * ad);
acl_delay_t * acl_delay_create(int delay, int valid, int visa);
void acl_rule_delete(acl_rule_t * ar);
acl_rule_t * acl_rule_create(ll_t * conditions, acl_action_t * action);
acl_table_t * acl_table_lookup(char *name);
acl_table_t * acl_table_register(char *name, acl_action_t * target);
int acl_rule_register(acl_table_t * at, ll_t * conditions, acl_action_t * action);
int acl_init(void);
void acl_clear(void);
var_t * acl_symbol_eval(acl_value_t * av, var_t *attrs);
var_t * acl_function_eval(acl_value_t * av, var_t *attrs);
var_t * acl_value_eval(acl_value_t * av, var_t *attrs);
int acl_compare(var_t * v1, var_t * v2, acl_cmp_t ac);
int acl_conditions_eval(ll_t * conditions, var_t *attrs);
acl_action_type_t acl(char *table, var_t *attrs);
#endif				/* _ACL_H_ */
