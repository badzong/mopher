#include <stdlib.h>

#include <mopher.h>

static unsigned int random_seed;

static var_t *
random_number(int argc, ll_t *args)
{
	VAR_INT_T r;

	r = rand_r(&random_seed);

	return var_create(VT_INT, NULL, &r, VF_COPY | VF_EXP_FREE);
}

int
random_init(void)
{
	struct timespec now;

	if(util_now(&now))
	{
		log_die(EX_SOFTWARE, "random_init: util_now failed");
	}

	random_seed = now.tv_nsec;

	acl_function_register("random", AF_SIMPLE,
	    (acl_function_callback_t) random_number, 0);

	return 0;
}
