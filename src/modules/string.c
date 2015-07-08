#include <stdio.h>
#include <string.h>

#include <mopher.h>

#define BUFLEN 1024


static var_t *
string_strlen(int argc, void **argv)
{
	VAR_INT_T len;

	len = strlen(argv[0]);

	return var_create(VT_INT, NULL, &len, VF_COPYDATA | VF_EXP_FREE);
}


static var_t *
string_strcmp(int argc, void **argv)
{
	VAR_INT_T cmp;

	cmp = strcmp(argv[0], argv[1]);

	return var_create(VT_INT, NULL, &cmp, VF_COPYDATA | VF_EXP_FREE);
}


static var_t *
string_tolower(int argc, void **argv)
{
	util_tolower((char *) argv[0]);

	return var_create(VT_STRING, NULL, argv[0], VF_COPYDATA | VF_EXP_FREE);
}


static var_t *
string_mailaddr(int argc, void **argv)
{
	char buffer[BUFLEN];

	if (util_strmail(buffer, sizeof buffer, argv[0]) == -1)
	{
		log_error("string_mail: util_strmail failed");
		return NULL;
	}

	return var_create(VT_STRING, NULL, buffer, VF_COPYDATA | VF_EXP_FREE);
}


int
string_init(void)
{
	acl_function_register("strlen", AF_SIMPLE,
	    (acl_function_callback_t) string_strlen, VT_STRING, 0);
	acl_function_register("lower", AF_SIMPLE,
	    (acl_function_callback_t) string_tolower, VT_STRING, 0);
	acl_function_register("strcmp", AF_SIMPLE,
	    (acl_function_callback_t) string_strcmp, VT_STRING, VT_STRING, 0);
	acl_function_register("mailaddr", AF_SIMPLE,
	    (acl_function_callback_t) string_mailaddr, VT_STRING, 0);

	return 0;
}
