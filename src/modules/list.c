#include "mopher.h"


static var_t *
list_contains(int argc, ll_t *args)
{
	var_t *haystack, *needle, *v;
	ll_t *list;
	VAR_INT_T match = 0;

	haystack = ll_next(args);
	needle = ll_next(args);

	if (argc != 2)
	{
		printf("copunt = %d\n", argc);
		goto usage;
	}

	if (haystack == NULL || needle == NULL)
	{
		printf("value\n");
		goto usage;
	}

	if (haystack->v_type != VT_LIST)
	{
		printf("list\n");
		goto usage;
	}

	list = haystack->v_data;
	ll_rewind(list);

	while ((v = ll_next(list)))
	{
		if (var_compare(needle, v) == 0)
		{
			match = 1;
			break;
		}
	}

	v = var_create(VT_INT, NULL, &match, VF_COPYDATA);
	if (v == NULL)
	{
		log_error("list_contains: var_create failed");
		goto error;
	}

	return v;


usage:
	log_error( "list_contains: usage: list_contains(haystack, needle)");

error:
	return NULL;
}


int
list_init(void)
{
	acl_function_register("list_contains", AF_COMPLEX,
	    (acl_function_callback_t) list_contains);

	return 0;
}
