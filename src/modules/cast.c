#include <stdio.h>
#include <string.h>

#include <mopher.h>

static char *cast_keys[] = { "INT", "FLOAT", "STRING", NULL };
static var_type_t cast_values[] = { VT_INT, VT_FLOAT, VT_STRING, VT_NULL };

static var_t *
type(int argc, ll_t *args)
{
	var_t *type, *var;
	ll_entry_t *pos;
	char *ts;

	pos = LL_START(args);
	var = ll_next(args, &pos);

	if (argc != 1)
	{
		goto error;
	}

	if (var == NULL)
	{
		ts = "NULL";
	}
	else
	{
		ts = var_type_string(var);
	}

	return var_create(VT_STRING, NULL, ts, VF_COPY | VF_EXP_FREE);

error:
	log_error("type: bad arguments: usage type(expression)");
	return NULL;
}

static var_t *
cast(int argc, ll_t *args)
{
	var_t *type, *var, *copy;
	VAR_INT_T *vt;
	ll_entry_t *pos;

	pos = LL_START(args);

	type = ll_next(args, &pos);
	var = ll_next(args, &pos);

	if (argc != 2)
	{
		goto error;
	}

	if (type == NULL || var == NULL)
	{
		goto error;
	}

	if (type->v_type != VT_INT)
	{
		goto error;
	}

	vt = type->v_data;

	copy = var_cast_copy(*vt, var);

	if (copy)
	{
		copy->v_flags |= VF_EXP_FREE;
	}

	return copy;


error:

	log_error("cast: bad arguments: usage cast(TYPE, expression)");
	return NULL;
}


int
cast_init(void)
{
	char **k;
	var_type_t *v;
	VAR_INT_T i;

	acl_function_register("type", AF_COMPLEX,
	    (acl_function_callback_t) type);
	acl_function_register("cast", AF_COMPLEX,
	    (acl_function_callback_t) cast);

	for (k = cast_keys, v = cast_values; *k && *v; ++k, ++v)
	{
		i = *v;
		acl_constant_register(VT_INT, *k, &i,
		    VF_KEEPNAME | VF_COPYDATA);
	}

	return 0;
}
