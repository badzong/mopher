#include <string.h>
#include <stdlib.h>

#include <mopher.h>
#include "acl_yacc.h"

#define EXP_BUCKETS 128
#define EXP_STRLEN 1024

#define EXP_VAR "VARIABLES"
#define EXP_GARBAGE "GARBAGE"


static sht_t *exp_defs;
static ll_t *exp_garbage;

static VAR_INT_T exp_true_int  = 1;
static VAR_INT_T exp_false_int = 0;

var_t exp_empty = { VT_INT, NULL, NULL, VF_KEEP };
var_t exp_true = { VT_INT, NULL, &exp_true_int, VF_KEEP };
var_t exp_false = { VT_INT, NULL, &exp_false_int, VF_KEEP };


static void
exp_function_delete(exp_t *exp)
{
	exp_function_t *ef = exp->ex_data;;

	free(ef->ef_name);
	free(ef);

	return;
}


void
exp_delete(exp_t *exp)
{
	switch (exp->ex_type)
	{
	case EX_PARENTHESES:
		break;

	case EX_CONSTANT:
		var_delete(exp->ex_data);
		break;

	case EX_FUNCTION:
		exp_function_delete(exp);
		break;

	case EX_LIST:
		ll_delete(exp->ex_data, NULL);
		break;

	case EX_SYMBOL:
	case EX_VARIABLE:
	case EX_OPERATION:
		free(exp->ex_data);
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

	if (LL_INSERT(exp_garbage, exp) == -1)
	{
		log_die(EX_OSERR, "exp_create: LL_INSERT failed");
	}

	return exp;
}


void
exp_define(char *name, exp_t *exp)
{
	if (sht_insert(exp_defs, name, exp))
	{
		log_die(EX_SOFTWARE, "exp_define: sht_insert failed");
	}

	free(name);

	return;
}


exp_t *
exp_parentheses(exp_t *exp)
{
	exp_t *p;

	p = exp_create(EX_PARENTHESES, exp);
	if (p == NULL)
	{
		log_die(EX_SOFTWARE, "exp_parentheses: exp_create failed");
	}

	return p;
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
		free(symbol);

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
	else
	{
		ll = list->ex_data;
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
	var_t *v = NULL;

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

	if (operator == '=' && op1->ex_type != EX_VARIABLE)
	{
		parser_error("bad use of '=' operator");
	}

	return exp_create(EX_OPERATION, eo);
}


exp_t *
exp_function(char *id, exp_t *args)
{
	exp_function_t *ef;

	if (acl_function_lookup(id) == NULL)
	{
		parser_error("unknown function \"%s\"", id);
	}
		
	ef = (exp_function_t *) malloc(sizeof (exp_function_t));
	if (ef == NULL)
	{
		log_die(EX_OSERR, "exp_function: malloc");
	}

	ef->ef_name = id;
	ef->ef_args = args;

	return exp_create(EX_FUNCTION, ef);
}


void
exp_free_list(ll_t *list)
{
	var_t *item;
	ll_entry_t *pos;

	pos = LL_START(list);
	while ((item = ll_next(list, &pos)))
	{
		exp_free(item);
	}

	return;
}

void
exp_free(var_t *v)
{
	if ((v->v_flags & VF_EXP_FREE) == 0)
	{
		return;
	}

	if (v->v_type == VT_LIST && v->v_data)
	{
		exp_free_list(v->v_data);
		ll_delete(v->v_data, NULL);
		v->v_data = NULL;
	}

	var_delete(v);

	return;
}


var_t *
exp_eval_list(exp_t *exp, var_t *mailspec)
{
	ll_t *exp_list = exp->ex_data;
	ll_entry_t *pos;
	exp_t *exp_item;
	var_t *var_item, *var_list = NULL;

	var_list = vlist_create(NULL, VF_EXP_FREE);
	if (var_list == NULL)
	{
		log_error("exp_eval_list: malloc");
		goto error;
	}

	pos = LL_START(exp_list);
	while ((exp_item = ll_next(exp_list, &pos)))
	{
		var_item = exp_eval(exp_item, mailspec);

		if (vlist_append(var_list, var_item))
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


static var_t *
exp_eval_function_complex(char *name, acl_function_t *af, ll_t *args)
{
	var_t *v;

	v = af->af_callback.fc_complex(args->ll_size, args);

	return v;
}


static var_t *
exp_eval_function_simple(char *name, acl_function_t *af, ll_t *args)
{
	ll_t garbage;
	ll_entry_t *pos;
	void **argv = NULL;
	var_t *v;
	int argc;
	int size;
	int i;
	var_t *arg;

	/*
	 * Initialize garbage
	 */
	ll_init(&garbage);

	/*
	 * Check argc
	 */
	argc = args->ll_size;
	if (argc != af->af_argc)
	{
		log_error("exp_eval_function_simple: function \"%s\" requires "
		    "%d arguments", name, af->af_argc);
		return NULL;
	}

	size = (argc + 1) * sizeof (void *);

	argv = (void **) malloc(size);
	if (argv == NULL)
	{
		log_error("exp_eval_function_simple: malloc");
		return NULL;
	}

	memset(argv, 0, size);

	/*
	 * Prepare argv
	 */
	pos = LL_START(args);
	for (i = 0; (arg = ll_next(args, &pos)); ++i)
	{
		if (af->af_types[i] == arg->v_type)
		{
			argv[i] = arg->v_data;
			continue;
		}

		/*
		 * Type casting required. Don't care about the remains of arg
		 * (freed with args!).
		 */
		arg = var_cast_copy(af->af_types[i], arg);
		if (arg == NULL)
		{
			log_error("exp_eval_function_simple: var_cast_copy "
			    "failed");

			goto error;
		}

		/*
		 * Need to free copy later
		 */
		if (LL_INSERT(&garbage, arg) == -1)
		{
			log_error("exp_eval_function_simlpe: LL_INSERT "
			    "failed");

			var_delete(arg);
			goto error;
		}

		argv[i] = arg->v_data;
	}

	v = af->af_callback.fc_simple(argc, argv);


error:
	ll_clear(&garbage, (ll_delete_t) var_delete);

	if (argv)
	{
		free(argv);
	}

	return v;
}


var_t *
exp_eval_function(exp_t *exp, var_t *mailspec)
{
	exp_function_t *ef = exp->ex_data;
	acl_function_t *af;
	var_t *args = NULL;
	ll_t *single = NULL;
	var_t *v = NULL;

	af = acl_function_lookup(ef->ef_name);
	if (af == NULL)
	{
		log_error("exp_eval_function: unknown function \"%s\"",
		    ef->ef_name);
		goto error;
	}

	/*
	 * Function has arguments
	 */
	if (ef->ef_args)
	{
		args = exp_eval(ef->ef_args, mailspec);
		if (args == NULL)
		{
			log_error("exp_eval_function: exp_eval failed");
			goto error;
		}

		/*
		 * Convert single argument into list.
		 */
		if (args->v_type != VT_LIST)
		{
			single = ll_create();
			if (single == NULL)
			{
				log_error("exp_eval_function: ll_create failed");
				goto error;
			}

			if (LL_INSERT(single, args) == -1)
			{
				log_error("exp_eval_function: LL_INSERT failed");
				goto error;
			}
		}
	}
	else
	{
		/*
		 * Function has no arguments -> empty list
		 */
		single = ll_create();
		if (single == NULL)
		{
			log_error("exp_eval_function: ll_create failed");
			goto error;
		}
	}

	if (af->af_type == AF_SIMPLE)
	{
		v = exp_eval_function_simple(ef->ef_name, af,
		    single ? single : args->v_data);
	}
	else
	{
		v = exp_eval_function_complex(ef->ef_name, af,
		    single ? single : args->v_data);
	}

	if (v == NULL)
	{
		log_error("exp_eval_function: function \"%s\" failed",
		    ef->ef_name);
		goto error;
	}

	v->v_flags |= VF_EXP_FREE;

error:
	if (single)
	{
		ll_delete(single, NULL);
	}

	if (args)
	{
		exp_free(args);
	}

	return v;
}


static var_t *
exp_eval_variable(exp_t *exp, var_t *mailspec)
{
	var_t *variables, *value;

	if (exp->ex_type != EX_VARIABLE)
	{
		log_debug("exp_eval_variable: bad type");
		return NULL;
	}

	variables = vtable_lookup(mailspec, EXP_VAR);
	if (variables == NULL)
	{
		log_debug("exp_eval_variable: no variables set");
		return EXP_EMPTY;
	}

	value = vtable_lookup(variables, exp->ex_data);
	if (value == NULL)
	{
		log_debug("exp_eval_variable: unknown variable \"%s\"",
		    exp->ex_data);
		return EXP_EMPTY;
	}

	return value;
}

	
static var_t *
exp_assign(exp_t *left, exp_t *right, var_t *mailspec)
{
	var_t *variables, *value, *copy;

	/*
	 * Get variable space
	 */
	variables = vtable_lookup(mailspec, EXP_VAR);

	/*
	 * No variables yet. Create variable space
	 */
	if (variables == NULL)
	{
		variables = vtable_create(EXP_VAR, VF_KEEPNAME);
		if (variables == NULL)
		{
			log_error("exp_eval_variable: vtable_create failed");
			return NULL;
		}

		if (vtable_set(mailspec, variables))
		{
			log_error("exp_eval_variable: vtable_set failed");
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

	/*
	 * Sava a copy
	 */
	copy = VAR_COPY(value);
	if (copy == NULL)
	{
		log_error("exp_assign: VAR_COPY failed");
		return NULL;
	}

	var_rename(copy, left->ex_data, VF_KEEPNAME);

	if (vtable_set(variables, copy))
	{
		log_error("exp_eval_variable: vtable_set failed");
		return NULL;
	}

	exp_free(value);

	return value;
}


var_t *
exp_math_int(int op, var_t *left, var_t *right)
{
	VAR_INT_T *l, *r, x, *p = NULL;
	var_t *v;

	l = left->v_data;
	r = right->v_data;

	if (l == NULL || r == NULL)
	{
		log_debug("exp_math_int: empty value");
		goto exit;
	}

	switch (op)
	{
	case '+':	x = *l + *r;	break;
	case '-':	x = *l - *r;	break;
	case '*':	x = *l * *r;	break;
	case '/':	x = *l / *r;	break;
	case '<':	x = *l < *r;	break;
	case '>':	x = *l > *r;	break;
	case '!':	x = ! *l;	break;
	case EQ:	x = *l == *r;	break;
	case NE:	x = *l != *r;	break;
	case LE:	x = *l <= *r;	break;
	case GE:	x = *l >= *r;	break;
	case AND:	x = *l && *r;	break;
	case OR:	x = *l || *r;	break;

	default:
		log_error("exp_math_int: bad operation");
		return NULL;
	}

	p = &x;

exit:
	v = var_create(VT_INT, NULL, p, VF_COPYDATA | VF_EXP_FREE);
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

	if (l == NULL || r == NULL)
	{
		log_debug("exp_math_float: empty value");
		goto error;
	}

	switch (op)
	{
	case '+':	x = *l + *r;	break;
	case '-':	x = *l - *r;	break;
	case '*':	x = *l * *r;	break;
	case '/':	x = *l / *r;	break;

	case '<':	i = *l < *r;	break;
	case '>':	i = *l > *r;	break;
	case '!':	i = ! *l;	break;
	case EQ:	i = *l == *r;	break;
	case NE:	i = *l != *r;	break;
	case LE:	i = *l <= *r;	break;
	case GE:	i = *l >= *r;	break;
	case AND:	i = *l && *r;	break;
	case OR:	i = *l || *r;	break;

	default:
		log_error("exp_math_float: bad operation");
		return NULL;
	}

	switch (op)
	{
	case '+':
	case '-':
	case '*':
	case '/':
		v = var_create(VT_FLOAT, NULL, &x, VF_COPYDATA | VF_EXP_FREE);
		break;

	default:
		v = var_create(VT_INT, NULL, &i, VF_COPYDATA | VF_EXP_FREE);
	}

	if (v == NULL)
	{
		log_error("exp_math_float: var_create failed");
	}

	return v;


error:

	v = var_create(VT_INT, NULL, NULL, VF_KEEP | VF_EXP_FREE);
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

	if (l == NULL)
	{
		l = "(null)";
	}
	if (r == NULL)
	{
		r = "(null)";
	}

	switch (op)
	{
	case '+':						break;
	case '<':	i = strcmp(l, r) == -1;			break;
	case '>':	i = strcmp(l, r) == 1;			break;
	case EQ:	i = strcmp(l, r) == 0;			break;
	case NE:	i = strcmp(l, r) != 0;			break;
	case LE:	i = strcmp(l, r) <= 0;			break;
	case GE:	i = strcmp(l, r) >= 0;			break;
	case AND:	i = var_true(left) && var_true(right);	break;
	case OR:	i = var_true(left) || var_true(right);	break;

	default:
		log_error("exp_math_string: bad operation");
		return NULL;
	}

	if (op == '+')
	{
		if (util_concat(x, sizeof x, l, r, NULL) == -1)
		{
			log_error("exp_math_string: util_concat: buffer "
			    "exhausted");
			return NULL;
		}

		v = var_create(VT_STRING, NULL, x, VF_COPYDATA | VF_EXP_FREE);
	}
	else
	{
		v = var_create(VT_INT , NULL, &i, VF_COPYDATA | VF_EXP_FREE);
	}
		
	if (v == NULL)
	{
		log_error("exp_math_string: var_create failed");
	}

	return v;
}


var_t *
exp_math_addr(int op, var_t *left, var_t *right)
{
	struct sockaddr_storage *l, *r;
	VAR_INT_T i;
	var_t *v;

	l = left->v_data;
	r = right->v_data;

	if (l == NULL || r == NULL)
	{
		log_debug("exp_math_addr: empty value");
		return NULL;
	}

	switch (op)
	{
	case '<':	i = util_addrcmp(l, r) == -1;		break;
	case '>':	i = util_addrcmp(l, r) == 1;		break;
	case EQ:	i = util_addrcmp(l, r) == 0;		break;
	case NE:	i = util_addrcmp(l, r) != 0;		break;
	case LE:	i = util_addrcmp(l, r) <= 0;		break;
	case GE:	i = util_addrcmp(l, r) >= 0;		break;
	case AND:	i = var_true(left) && var_true(right);	break;
	case OR:	i = var_true(left) || var_true(right);	break;

	default:
		log_error("exp_math_addr: bad operation");
		return NULL;
	}

	v = var_create(VT_INT , NULL, &i, VF_COPYDATA | VF_EXP_FREE);
	if (v == NULL)
	{
		log_error("exp_math_addr: var_create failed");
	}

	return v;
}


var_t *
exp_is_null(var_t *v)
{
	var_t *result = &exp_false;

	if (v == NULL)
	{
		result = &exp_true;
	}
	else if (v->v_data == NULL)
	{
		result = &exp_true;
	}

	exp_free(v);

	return result;
}


var_t *
exp_eval_operation(exp_t *exp, var_t *mailspec)
{
	var_t *left = NULL, *right = NULL, *copy;
	exp_operation_t *eo = exp->ex_data;
	var_t *result;
	var_type_t type;

	/*
	 * Variable assigment
	 */
	if (eo->eo_operator == '=')
	{
		return exp_assign(eo->eo_operand[0], eo->eo_operand[1],
		    mailspec);
	}

	left = exp_eval(eo->eo_operand[0], mailspec);

	/*
	 * Hack: IS_NULL operator
	 */
	if (eo->eo_operator == IS_NULL)
	{
		result = exp_is_null(left);

		if (left)
		{
			exp_free(left);
		}

		return result;
	}

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

		if (right->v_data == NULL)
		{
			log_debug("exp_eval_operation: exp_eval for "
			     "right-hand value returned no data");
		}

		/*
		 * Check data types
		 */
		if (left->v_type != right->v_type)
		{
			/*
			 * The biggest type has precedence (see exp.h)
			 * STRING > FLOAT > INT
			 */
			type = VAR_MAX_TYPE(left, right);

			if (type == left->v_type)
			{
				copy = var_cast_copy(type, right);
			}
			else
			{
				copy = var_cast_copy(type, left);
			}

			if (copy == NULL)
			{
				log_error("exp_eval_operation: var_cast_copy "
				    "failed");
				return NULL;
			}

			if (type == left->v_type)
			{
				exp_free(right);
				right = copy;
				right->v_flags |= VF_EXP_FREE;
			}
			else
			{
				exp_free(left);
				left = copy;
				left->v_flags |= VF_EXP_FREE;
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

	case VT_ADDR:
		result = exp_math_addr(eo->eo_operator, left, right);
		break;

	default:
		log_error("exp_eval_operation: bad type");
		result = NULL;
	}

	exp_free(left);

	if (right)
	{
		exp_free(right);
	}

	return result;
}


var_t *
exp_eval(exp_t *exp, var_t *mailspec)
{
	if (exp == NULL)
	{
		log_debug("exp_eval: expression is null");
		return NULL;
	}

	switch (exp->ex_type)
	{
	case EX_PARENTHESES:	return exp_eval(exp->ex_data, mailspec);
	case EX_CONSTANT:	return exp->ex_data;
	case EX_LIST:		return exp_eval_list(exp, mailspec);
	case EX_SYMBOL:		return acl_symbol_get(mailspec, exp->ex_data);
	case EX_FUNCTION:	return exp_eval_function(exp, mailspec);
	case EX_OPERATION:	return exp_eval_operation(exp, mailspec);
	case EX_VARIABLE:	return exp_eval_variable(exp, mailspec);

	default:
		log_error("exp_eval: bad type");
	}

	return NULL;
}


int
exp_is_true(exp_t *exp, var_t *mailspec)
{
	var_t *v;
	int r;

	if (exp == NULL)
	{
		return 1;
	}

	v = exp_eval(exp, mailspec);

	if (v == NULL)
	{
		log_notice("exp_true: evaluation failed");
		return -1;
	}

	r = var_true(v);

	exp_free(v);

	return r;
}


void
exp_init(void)
{
	exp_defs = sht_create(EXP_BUCKETS, NULL);
	if (exp_defs == NULL)
	{
		log_die(EX_SOFTWARE, "exp_init: sht_create failed");
	}

	exp_garbage = ll_create();
	if (exp_garbage == NULL)
	{
		log_die(EX_SOFTWARE, "exp_init: ll_create failed");
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

	if (exp_garbage)
	{
		ll_delete(exp_garbage, (ll_delete_t) exp_delete);
	}

	return;
}
