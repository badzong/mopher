#ifndef _EXP_H_
#define _EXP_H_

#include "var.h"
#include "ll.h"

enum exp_type
{
	EX_NULL,
	EX_PARENTHESES,
	EX_CONSTANT,
	EX_LIST,
	EX_SYMBOL,
	EX_FUNCTION,
	EX_VARIABLE,
	EX_OPERATION
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


/*
 * Prototypes
 */

void exp_delete(exp_t *exp);
void exp_define(char *name, exp_t *exp);
exp_t * exp_symbol(char *symbol);
exp_t * exp_variable(char *variable);
exp_t * exp_list(exp_t *list, exp_t *exp);
exp_t * exp_constant(var_type_t type, void *data);
exp_t * exp_operation(int operator, exp_t *op1, exp_t *op2);
exp_t * exp_function(char *id, exp_t *args);
void exp_free_list(ll_t *list);
void exp_free(var_t *v);
var_t * exp_eval_list(exp_t *exp, var_t *mailspec);
var_t * exp_eval_function(exp_t *exp, var_t *mailspec);
var_t * exp_math_int(int op, var_t *left, var_t *right);
var_t * exp_math_float(int op, var_t *left, var_t *right);
var_t * exp_math_string(int op, var_t *left, var_t *right);
var_t * exp_eval_operation(exp_t *exp, var_t *mailspec);
var_t * exp_eval(exp_t *exp, var_t *mailspec);
int exp_true(exp_t *exp, var_t *mailspec);
void exp_init(void);
void exp_clear(void);
#endif /* _EXP_H_ */
