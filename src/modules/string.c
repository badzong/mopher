#include <stdio.h>
#include <string.h>

#include "mopher.h"


static var_t *
string_strlen(int argc, void **argv)
{
	VAR_INT_T len;

	len = strlen(argv[0]);

	return var_create(VT_INT, NULL, &len, VF_COPYDATA);
}


static var_t *
string_strcmp(int argc, void **argv)
{
	VAR_INT_T cmp;

	cmp = strcmp(argv[0], argv[1]);

	return var_create(VT_INT, NULL, &cmp, VF_COPYDATA);
}


int
string_init(void)
{
	acl_function_register("string_strlen", AF_SIMPLE,
	    (acl_function_callback_t) string_strlen, VT_STRING, 0);
	acl_function_register("string_strcmp", AF_SIMPLE,
	    (acl_function_callback_t) string_strcmp, VT_STRING, VT_STRING, 0);

	return 0;
}
