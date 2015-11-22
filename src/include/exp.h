#ifndef _EXP_H_
#define _EXP_H_

#include <var.h>
#include <ll.h>

enum exp_type
{
	EX_NULL,
	EX_PARENTHESES,
	EX_CONSTANT,
	EX_LIST,
	EX_SYMBOL,
	EX_FUNCTION,
	EX_VARIABLE,
	EX_OPERATION,
	EX_TERNARY_COND,
	EX_MACRO
};
typedef enum exp_type exp_type_t;

struct exp
{
	exp_type_t	 ex_type;
	void		*ex_data;
};
typedef struct exp exp_t;


struct exp_operation
{
	int	 eo_operator;
	exp_t	*eo_operand[2];
};
typedef struct exp_operation exp_operation_t;

struct exp_function
{
	char	*ef_name;
	exp_t	*ef_args;
};
typedef struct exp_function exp_function_t;

struct exp_ternary_condition
{
	exp_t	*etc_condition;
	exp_t	*etc_true;
	exp_t	*etc_false;
};
typedef struct exp_ternary_condition exp_ternary_condition_t;


extern var_t exp_empty;
extern var_t exp_true;
extern var_t exp_false;

#define EXP_EMPTY &exp_empty
#define EXP_TRUE &exp_true
#define EXP_FALSE &exp_false

/*
 * Prototypes
 */

exp_t * exp_create(exp_type_t type, void *data);
void exp_delete(exp_t *exp);
void exp_define(char *name, exp_t *exp);
exp_t * exp_parentheses(exp_t *exp);
exp_t * exp_symbol(char *symbol);
exp_t * exp_variable(char *variable);
exp_t * exp_list(exp_t *list, exp_t *exp);
exp_t * exp_constant(var_type_t type, void *data, int flags);
exp_t * exp_operation(int operator, exp_t *op1, exp_t *op2);
exp_t * exp_function(char *id, exp_t *args);
exp_t * exp_ternary_cond(exp_t *condition, exp_t *cond_true, exp_t *cond_false);
void exp_free_list(ll_t *list);
void exp_free(var_t *v);
var_t * exp_math_int(int op, var_t *left, var_t *right);
var_t * exp_math_float(int op, var_t *left, var_t *right);
var_t * exp_math_string(int op, var_t *left, var_t *right);
var_t * exp_math_addr(int op, var_t *left, var_t *right);
var_t * exp_is_null(var_t *v);
var_t * exp_eval_operation(exp_t *exp, var_t *mailspec);
var_t * exp_eval(exp_t *exp, var_t *mailspec);
int exp_is_true(exp_t *exp, var_t *mailspec);
void exp_init(void);
void exp_clear(void);
int exp_test_init(void);
void exp_test(int n);
#endif /* _EXP_H_ */
