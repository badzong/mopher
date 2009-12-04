#include <string.h>
#include <stdlib.h>

#include "mopher.h"
#include "acl_yacc.h"

#define EXP_BUCKETS 128
#define EXP_STRLEN 1024
#define EXP_VAR "VAR"

static sht_t *exp_defs;


static void
exp_operation_delete(exp_t *exp)
{
	exp_operation_t *eo = exp->ex_data;

	if (eo->eo_operand[0])
	{
		exp_delete(eo->eo_operand[0]);
	}

	if (eo->eo_operand[1])
	{
		exp_delete(eo->eo_operand[1]);
	}

	return;
}


void
exp_delete(exp_t *exp)
{
	switch (exp->ex_type)
	{
	case EX_CONSTANT:
		var_delete(exp->ex_data);
		break;

	case EX_LIST:
		ll_delete(exp->ex_data, (ll_delete_t) exp_delete);
		break;

	case EX_SYMBOL:
	case EX_FUNCTION:
		log_die(EX_SOFTWARE, "TODO: CAN'T FREE SYMBOLS OR FUNCTIONS");
	
	case EX_VARIABLE:
		free(exp->ex_data);
		break;

	case EX_OPERATION:
		exp_operation_delete(exp);
		break;

	default:
		log_die(EX_SOFTWARE, "exp_delete: bad type");
	}
		
	free(exp);

	return;
}


static exp_t *
exp_create(exp_type_t type, void *data)
{
	exp_t *exp;

	exp = (exp_t *) malloc(sizeof (exp_t));
	if (exp == NULL)
	{
		log_die(EX_OSERR, "exp_create: malloc");
	}

	exp->ex_type = type;
	exp->ex_data = data;

	return exp;
}


void
exp_define(char *name, exp_t *exp)
{
	if (sht_insert(exp_defs, name, exp))
	{
		log_die(EX_SOFTWARE, "exp_define: sht_insert failed");
	}

	return;
}


exp_t *
exp_symbol(char *symbol)
{
	exp_t *exp;

	/*
	 * Check definitions
	 */
	exp = sht_lookup(exp_defs, symbol);
	if (exp)
	{
		return exp;
	}

	/*
	 * Check symbols
	 */
	if (acl_symbol_lookup(symbol) == NULL)
	{
		parser_error("unknown variable \"%s\"", symbol);
	}
		
	return exp_create(EX_SYMBOL, symbol);
}


exp_t *
exp_variable(char *variable)
{
	return exp_create(EX_VARIABLE, variable);
}


exp_t *
exp_list(exp_t *list, exp_t *exp)
{
	ll_t *ll;

	if (list->ex_type != EX_LIST)
	{
		ll = ll_create();
		if (ll == NULL)
		{
			log_die(EX_SOFTWARE, "exp_list: ll_create failed");
		}

		if (LL_INSERT(ll, list) == -1)
		{
			log_die(EX_SOFTWARE, "exp_list: LL_INSERT failed");
		}

		list = exp_create(EX_LIST, ll);
	}

	if (LL_INSERT(ll, exp) == -1)
	{
		log_die(EX_SOFTWARE, "exp_list: LL_INSERT failed");
	}

	return list;
}


exp_t *
exp_constant(var_type_t type, void *data)
{
	var_t *v;

	switch (type)
	{
	case VT_STRING:
	case VT_ADDR:
		v = var_create(type, NULL, data, VF_REF);
		break;
	case VT_INT:
	case VT_FLOAT:
		v = var_create(type, NULL, data, VF_COPYDATA);
		break;
	default:
		log_die(EX_SOFTWARE, "exp_constant: bad type");
	}

	if (v == NULL)
	{
		log_die(EX_SOFTWARE, "exp_constant: var_create failed");
	}

	return exp_create(EX_CONSTANT, v);
}


exp_t *
exp_operation(int operator, exp_t *op1, exp_t *op2)
{
	exp_operation_t *eo;

	eo = (exp_operation_t *) malloc(sizeof (exp_operation_t));
	if (eo == NULL)
	{
		log_die(EX_OSERR, "exp_operation_create: malloc");
	}

	eo->eo_operator = operator;
	eo->eo_operand[0] = op1;
	eo->eo_operand[1] = op2;

	return exp_create(EX_OPERATION, eo);
}


exp_t *
exp_function(char *id, exp_t *args)
{
	exp_function_t *ef;

	ef = (exp_function_t *) malloc(sizeof (exp_function_t));
	if (ef == NULL)
	{
		log_die(EX_OSERR, "exp_function: malloc");
	}

	ef->ef_name = id;
	ef->ef_args = args;

	return exp_create(EX_FUNCTION, ef);
}


var_t *
exp_eval_list(exp_t *exp, var_t *mailspec)
{
	ll_t *exp_list;
	exp_t *exp_item;
	var_t *var_item, *var_list = NULL;

	exp_list = exp->ex_data;
	ll_rewind(exp_list);

	var_list = var_create(VT_LIST, NULL, NULL, VF_CREATE);
	if (var_list == NULL)
	{
		log_error("exp_eval_list: malloc");
		goto error;
	}

	while ((exp_item = ll_next(exp_list)))
	{
		var_item = exp_eval(exp_item, mailspec);

		if (var_list_append(var_list, var_item))
		{
			log_error("exp_eval_list: malloc");
			goto error;
		}
	}

	return var_list;


error:

	if (var_list)
	{
		var_delete(var_list);
	}

	return NULL;
}


var_t *
exp_eval_function(exp_t *exp, var_t *mailspec)
{
	acl_function_callback_t function;
	exp_function_t *ef = exp->ex_data;
	var_t *args, *v;

	function = acl_function_lookup(ef->ef_name);
	if (function == NULL)
	{
		log_error("exp_eval_function: unknown function \"%s\"",
		    ef->ef_name);
		return NULL;
	}

	args = exp_eval(ef->ef_args, mailspec);
	if (args == NULL)
	{
		log_error("exp_eval_function: exp_eval failed");
		return NULL;
	}

	if (args->v_type != VT_LIST)
	{
		v = args;

		args = var_create(VT_LIST, NULL, NULL, VF_CREATE);
		if (args == NULL)
		{
			log_error("exp_eval_function: var_create failed");
			return NULL;
		}

		if (var_list_append(args, v))
		{
			log_error("exp_eval_function: var_list_append failed");
			var_delete(args);
			return NULL;
		}
	}

	ll_rewind(args->v_data);
	v = function(args->v_data);
	if (v == NULL)
	{
		log_error("exp_eval_function: function callback \"%s\" failed",
		    ef->ef_name);
	}

	return v;
}


static var_t *
exp_eval_variable(exp_t *exp, var_t *mailspec)
{
	var_t *variables, *value;

	if (exp->ex_type != EX_VARIABLE)
	{
		log_error("exp_eval_variable: bad type");
		return NULL;
	}

	variables = var_table_lookup(mailspec, EXP_VAR);
	if (variables == NULL)
	{
		log_error("exp_eval_variable: no variables set");
		return NULL;
	}

	value = var_table_lookup(variables, exp->ex_data);
	if (value == NULL)
	{
		log_error("exp_eval_variable: unknown variable \"%s\"",
		    exp->ex_data);
	}

	return value;
}

	
static var_t *
exp_assign(exp_t *left, exp_t *right, var_t *mailspec)
{
	var_t *variables, *value;
	char *name;

	if (left->ex_type != EX_VARIABLE)
	{
		log_error("exp_assign: bad use of '=' operator");
		return NULL;
	}

	name = left->ex_data;

	/*
	 * Get variable space
	 */
	variables = var_table_lookup(mailspec, EXP_VAR);

	/*
	 * No variables yet. Create variable space
	 */
	if (variables == NULL)
	{
		variables = var_create(VT_TABLE, EXP_VAR, NULL,
		    VF_CREATE | VF_KEEPNAME);

		if (variables == NULL)
		{
			log_error("exp_eval_variable: var_create failed");
			return NULL;
		}

		if (var_table_set(mailspec, variables))
		{
			log_error("exp_eval_variable: var_table_set failed");
			var_delete(variables);
			return NULL;
		}
	}

	/*
	 * Evaluate expression
	 */
	value = exp_eval(right, mailspec);
	if (value == NULL)
	{
		log_error("exp_eval_variables: exp_eval failed");
		return NULL;
	}

	var_rename(value, name, VF_KEEPNAME);

	if (var_table_set(variables, value))
	{
		log_error("exp_eval_variable: var_table_set failed");
		return NULL;
	}

	return value;
}


var_t *
exp_math_int(int op, var_t *left, var_t *right)
{
	VAR_INT_T *l, *r, x;
	var_t *v;

	l = left->v_data;
	r = right->v_data;

	switch (op)
	{
	case '+':
		x = *l + *r;
		break;

	case '-':
		x = *l - *r;
		break;

	case '*':
		x = *l * *r;
		break;

	case '/':
		x = *l / *r;
		break;

	case '<':
		x = *l < *r;
		break;

	case '>':
		x = *l + *r;
		break;

	case '!':
		x = ! *l;
		break;

	case EQ:
		x = *l == *r;
		break;

	case NE:
		x = *l != *r;
		break;

	case LE:
		x = *l <= *r;
		break;

	case GE:
		x = *l >= *r;
		break;

	case AND:
		x = *l && *r;
		break;

	case OR:
		x = *l || *r;
		break;

	default:
		log_error("exp_math_int: bad operation");
		return NULL;
	}

	v = var_create(VT_INT, NULL, &x, VF_COPYDATA);
	if (v == NULL)
	{
		log_error("exp_math_int: var_create failed");
	}

	return v;
}


var_t *
exp_math_float(int op, var_t *left, var_t *right)
{
	VAR_FLOAT_T *l, *r, x;
	VAR_INT_T i;
	var_t *v;

	l = left->v_data;
	r = right->v_data;

	switch (op)
	{
	case '+':
		x = *l + *r;
		v = var_create(VT_FLOAT, NULL, &x, VF_COPYDATA);
		break;

	case '-':
		x = *l - *r;
		v = var_create(VT_FLOAT, NULL, &x, VF_COPYDATA);
		break;

	case '*':
		x = *l * *r;
		v = var_create(VT_FLOAT, NULL, &x, VF_COPYDATA);
		break;

	case '/':
		x = *l / *r;
		v = var_create(VT_FLOAT, NULL, &x, VF_COPYDATA);
		break;

	case '<':
		i = *l < *r;
		v = var_create(VT_INT, NULL, &i, VF_COPYDATA);
		break;

	case '>':
		i = *l > *r;
		v = var_create(VT_INT, NULL, &i, VF_COPYDATA);
		break;

	case '!':
		i = ! *l;
		v = var_create(VT_INT, NULL, &i, VF_COPYDATA);
		break;

	case EQ:
		i = *l == *r;
		v = var_create(VT_INT, NULL, &i, VF_COPYDATA);
		break;

	case NE:
		i = *l != *r;
		v = var_create(VT_INT, NULL, &i, VF_COPYDATA);
		break;

	case LE:
		i = *l <= *r;
		v = var_create(VT_INT, NULL, &i, VF_COPYDATA);
		break;

	case GE:
		i = *l >= *r;
		v = var_create(VT_INT, NULL, &i, VF_COPYDATA);
		break;

	case AND:
		i = *l && *r;
		v = var_create(VT_INT, NULL, &i, VF_COPYDATA);
		break;

	case OR:
		x = *l || *r;
		v = var_create(VT_INT, NULL, &i, VF_COPYDATA);
		break;

	default:
		log_error("exp_math_float: bad operation");
		return NULL;
	}

	if (v == NULL)
	{
		log_error("exp_math_float: var_create failed");
	}

	return v;
}


var_t *
exp_math_string(int op, var_t *left, var_t *right)
{
	char *l, *r, x[EXP_STRLEN];
	VAR_INT_T i;
	var_t *v;

	l = left->v_data;
	r = right->v_data;

	switch (op)
	{
	case '+':
		if (util_concat(x, sizeof x, l, r, NULL) == -1)
		{
			log_error("exp_math_string: util_concat: buffer "
			    "exhausted");
			return NULL;
		}

		v = var_create(VT_STRING, NULL, x, VF_COPYDATA);
		break;

	case '<':
		i = strcmp(l, r) == -1;

		v = var_create(VT_INT, NULL, &i, VF_COPYDATA);
		break;

	case '>':
		i = strcmp(l, r) == 1;

		v = var_create(VT_INT, NULL, &i, VF_COPYDATA);
		break;

	case EQ:
		i = strcmp(l, r) == 0;

		v = var_create(VT_INT, NULL, &i, VF_COPYDATA);
		break;

	case NE:
		i = strcmp(l, r) != 0;

		v = var_create(VT_INT, NULL, &i, VF_COPYDATA);
		break;

	case LE:
		i = strcmp(l, r) <= 0;

		v = var_create(VT_INT, NULL, &i, VF_COPYDATA);
		break;

	case GE:
		i = strcmp(l, r) >= 0;

		v = var_create(VT_INT, NULL, &i, VF_COPYDATA);
		break;

	case AND:
		i = var_true(left) && var_true(right);

		v = var_create(VT_INT, NULL, &i, VF_COPYDATA);
		break;

	case OR:
		i = var_true(left) || var_true(right);

		v = var_create(VT_INT, NULL, &i, VF_COPYDATA);
		break;

	default:
		log_error("exp_math_string: bad operation");
		return NULL;
	}

	if (v == NULL)
	{
		log_error("exp_math_string: var_create failed");
	}

	return v;
}


var_t *
exp_eval_operation(exp_t *exp, var_t *mailspec)
{
	var_t *left, *right;
	exp_operation_t *eo = exp->ex_data;
	var_t *result;

	/*
	 * Variable assigment
	 */
	if (eo->eo_operator == '=')
	{
		return exp_assign(eo->eo_operand[0], eo->eo_operand[1],
		    mailspec);
	}

	left = exp_eval(eo->eo_operand[0], mailspec);
	if (left == NULL)
	{
		log_error("exp_eval_operation: exp_eval for left-hand value "
		    "failed");
		return NULL;
	}

	if (eo->eo_operand[1])
	{
		right = exp_eval(eo->eo_operand[1], mailspec);
		if (right == NULL)
		{
			log_error("exp_eval_operation: exp_eval for "
			    "right-hand value failed");
			return NULL;
		}

		if (left->v_type != right->v_type)
		{
			right = var_cast_copy(left->v_type, right);
			if (right == NULL)
			{
				log_error("exp_eval_operation: var_cast_copy "
				    "failed");
				return NULL;
			}
		}
	}

	switch (left->v_type)
	{
	case VT_INT:
		result = exp_math_int(eo->eo_operator, left, right);
		break;

	case VT_FLOAT:
		result = exp_math_float(eo->eo_operator, left, right);
		break;

	case VT_STRING:
		result = exp_math_string(eo->eo_operator, left, right);
		break;

	default:
		log_error("exp_eval_operation: bad type");
		result = NULL;
	}
	
	return result;
}


var_t *
exp_eval(exp_t *exp, var_t *mailspec)
{
	switch (exp->ex_type)
	{
	case EX_CONSTANT:
		return exp->ex_data;

	case EX_LIST:
		return exp_eval_list(exp, mailspec);

	case EX_SYMBOL:
		return acl_symbol_get(mailspec, exp->ex_data);

	case EX_FUNCTION:
		return exp_eval_function(exp, mailspec);

	case EX_OPERATION:
		return exp_eval_operation(exp, mailspec);

	case EX_VARIABLE:
		return exp_eval_variable(exp, mailspec);

	default:
		log_error("exp_eval: bad type");
	}

	return NULL;
}


int
exp_true(exp_t *exp, var_t *mailspec)
{
	var_t *v;

	if (exp == NULL)
	{
		return 1;
	}

	v = exp_eval(exp, mailspec);

	if (v == NULL)
	{
		log_notice("exp_true: evaluation failed");
		return 0;
	}

	return var_true(v);
}


void
exp_init(void)
{
	/*
	 * Create variable table if neccessary
	 */
	exp_defs = sht_create(EXP_BUCKETS, (sht_delete_t) exp_delete);
	if (exp_defs == NULL)
	{
		log_die(EX_SOFTWARE, "exp_init: sht_create failed");
	}

	return;
}


void
exp_clear(void)
{
	if (exp_defs)
	{
		sht_delete(exp_defs);
	}

	return;
}
