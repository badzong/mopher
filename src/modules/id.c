#include "mopher.h"

#define BUFLEN 9

static unsigned long id_id;
static pthread_mutex_t id_mutex = PTHREAD_MUTEX_INITIALIZER;

static int
id(milter_stage_t stage, char *name, var_t *mailspec)
{
	char buffer[BUFLEN];
	unsigned long id_stacked;

	if (pthread_mutex_lock(&id_mutex))
	{
		log_error("id: pthread_mutex_lock");
		return -1;
	}

	id_stacked = ++id_id;

	if (pthread_mutex_unlock(&id_mutex))
	{
		log_error("id: pthread_mutex_unlock");
	}

	snprintf(buffer, sizeof buffer, "%08lx", id_stacked);

	if (vtable_set_new(mailspec, VT_STRING, name, buffer, VF_COPY))
	{
		log_error("id: vtable_set_new failed");
		return -1;
	}

	return 0;
}


int
id_init(void)
{
	acl_symbol_register("id", MS_ANY, id);

	return 0;
}
