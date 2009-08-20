#include <stdio.h>

#include "acl.h"
#include "log.h"


var_t *
testfunc(ll_t *args)
{
	var_t *v;

	while((v = ll_next(args))) {
		log_debug("testfunc: arg@%p type=%d", v, v->v_type);
	}

	return NULL;
}


int
init(void)
{
	log_debug("test_init");

	acl_symbol_register(AS_FUNC, "testfunc", (acl_callback_t) testfunc);

	return 0;
}

void
fini(void)
{
	printf("test fini\n");
}
