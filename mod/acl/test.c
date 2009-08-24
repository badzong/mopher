#include <stdio.h>

#include "acl.h"
#include "milter.h"
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
test_symbol(ht_t *attrs)
{
	VAR_INT_T i;

	i = 1;

	if(acl_symbol_add(attrs, VT_INT, "test_symbol_1", &i,
		VF_KEEPNAME | VF_COPYDATA)) {
		log_error("test_symbol: acl_symbol_add failed");
		return -1;
	}

	return 0;
}


int
init(void)
{
	log_debug("test_init");

	acl_function_register("test_func", (acl_fcallback_t) testfunc);
	acl_symbol_register("test_symbol_1", MS_CONNECT | MS_HEADER,
		(acl_scallback_t) test_symbol);
	acl_symbol_register("test_symbol_2", MS_ENVFROM | MS_EOM,
		(acl_scallback_t) test_symbol);

	return 0;
}

void
fini(void)
{
	printf("test fini\n");
}
