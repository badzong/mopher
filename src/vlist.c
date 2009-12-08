#include <stdarg.h>

#include "mopher.h"


var_t *
vlist_create(char *name, int flags)
{
	var_t *list;

	list = var_create(VT_LIST, name, NULL, flags | VF_CREATE);	
	if (list == NULL)
	{
		log_error("vlist_create: var_create failed");
	}

	return list;
}


int
vlist_append(var_t *list, var_t *item)
{
	ll_t *ll = list->v_data;

	if((LL_INSERT(ll, item)) == -1)
	{
		log_warning("vlist_append: LL_INSERT failed");
		return -1;
	}

	return 0;
}


int
vlist_append_new(var_t *list, var_type_t type, char *name, void *data,
    int flags)
{
	var_t *v;

	v = var_create(type, name, data, flags);
	if (v == NULL)
	{
		log_warning("vlist_append_new: var_create failed");
		return -1;
	}

	return vlist_append(list, v);
}


int
vlist_dereference(var_t *list, ...)
{
	va_list ap;
	var_t *v;
	void **p;

	va_start(ap, list);

	ll_rewind(list->v_data);
	while ((v = ll_next(list->v_data)))
	{
		p = va_arg(ap, void **);

		/*
		 * Skip NULL Pointers.
		 */
		if (p == NULL) {
			continue;
		}

		*p = v->v_data;
	}

	va_end(ap);

	return 0;
}


var_t *
vlist_scheme(char *scheme, ...)
{
	va_list ap;
	var_t *record = NULL;
	var_type_t type;
	int flags;
	char *name;
	
	record = vlist_create(scheme, VF_COPYNAME);
	if (record == NULL)
	{
		log_warning("vlist_scheme: vlist_create failed");
		return NULL;
	}

	va_start(ap, scheme);

	for (;;)
	{
		name = va_arg(ap, char *);
		if (name == NULL)
		{
			break;
		}

		type = va_arg(ap, var_type_t);
		flags = va_arg(ap, int);

		if (vlist_append_new(record, type, name, NULL, flags) == -1)
		{
			log_warning("vlist_scheme: vlist_append_new failed");

			var_delete(record);
			return NULL;
		}
	}

	va_end(ap);

	return record;
}


var_t *
vlist_record(var_t *scheme, ...)
{
	va_list ap;
	var_t *record = NULL, *v;
	int flags;
	void *data;

	va_start(ap, scheme);

	record = vlist_create(scheme->v_name, VF_KEEPNAME);
	if (record == NULL)
	{
		log_warning("vlist_record: vlist_create failed");
		return NULL;
	}

	ll_rewind(scheme->v_data);
	while ((v = ll_next(scheme->v_data)))
	{
		flags = v->v_flags;
		flags |= VF_KEEPDATA | VF_KEEPNAME;
		flags &= ~(VF_COPYNAME | VF_COPYDATA);

		data = va_arg(ap, void *);

		if (vlist_append_new(record, v->v_type, v->v_name, data, flags)
		    == -1)
		{
			log_warning("vlist_record: vlist_append_new failed");

			var_delete(record);
			return NULL;
		}
	}

	va_end(ap);

	return record;
}
