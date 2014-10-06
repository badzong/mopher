#include <mopher.h>


static var_t *
list_contains(int argc, ll_t *args)
{
	var_t *haystack, *needle, *v;
	ll_t *list;
	ll_entry_t *pos;
	int cmp;

	pos = LL_START(args);
	haystack = ll_next(args, &pos);
	needle = ll_next(args, &pos);

	if (argc != 2)
	{
		goto usage;
	}

	if (haystack == NULL || needle == NULL)
	{
		goto usage;
	}

	if (haystack->v_type != VT_LIST)
	{
		goto usage;
	}

	list = haystack->v_data;

	pos = LL_START(list);
	while ((v = ll_next(list, &pos)))
	{
		if (var_compare(&cmp, needle, v))
		{
			log_error("list_contains: var_compare failed");
		}

		if (cmp == 0)
		{
			return EXP_TRUE;
		}
	}

	return EXP_FALSE;


usage:

	log_error( "list_contains: usage: list_contains(haystack, needle)");
	return NULL;
}


int
list_init(void)
{
	acl_function_register("list_contains", AF_COMPLEX,
	    (acl_function_callback_t) list_contains);

	return 0;
}
