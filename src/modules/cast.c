#include <stdio.h>
#include <string.h>

#include <mopher.h>

static char *cast_keys[] = { "INT", "FLOAT", "STRING", NULL };
static var_type_t cast_values[] = { VT_INT, VT_FLOAT, VT_STRING, VT_NULL };

static var_t *
cast(int argc, ll_t *args)
{
	var_t *type, *var;
	VAR_INT_T *vt;

	type = ll_next(args);
	var = ll_next(args);

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

	return var_cast_copy(*vt, var);


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
