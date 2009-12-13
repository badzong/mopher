#include <stdio.h>
#include <string.h>

#include "mopher.h"


var_t *
string_strlen(ll_t *args)
{
	var_t *v;
	VAR_INT_T len;

	if((v = ll_next(args)) == NULL) {
		return NULL;
	}

	if(v->v_type != VT_STRING) {
		return NULL;
	}

	len = strlen(v->v_data);

	return var_create(VT_INT, NULL, &len, VF_COPYDATA);
}


int
string_init(void)
{
	acl_function_register("string_strlen",
	    (acl_function_callback_t) string_strlen);

	return 0;
}
