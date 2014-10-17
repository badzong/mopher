#include <stdarg.h>
#include <string.h>

#include <mopher.h>


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

	if (vlist_append(list, v) == 0)
	{
		return 0;
	}

	log_error("vlist_append_new: vlist_append failed");
	var_delete(v);

	return -1;
}


int
vlist_dereference(var_t *list, ...)
{
	va_list ap;
	var_t *v;
	void **p;
	ll_t *ll;
	ll_entry_t *pos;

	va_start(ap, list);

	ll = list->v_data;
	pos = LL_START(ll);

	while ((v = ll_next(ll, &pos)))
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
	ll_t *ll;
	ll_entry_t *pos;

	va_start(ap, scheme);

	record = vlist_create(scheme->v_name, VF_KEEPNAME);
	if (record == NULL)
	{
		log_warning("vlist_record: vlist_create failed");
		return NULL;
	}

	ll = scheme->v_data;
	pos = LL_START(ll);

	while ((v = ll_next(ll, &pos)))
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


var_t *
vlist_record_from_table(var_t *scheme, var_t *table)
{
	var_t *record = NULL, *vs, *vt;
	ll_t *ll;
	ll_entry_t *pos;
	void *data;
	var_type_t type;
	char *name;

	record = vlist_create(scheme->v_name, VF_KEEPNAME);
	if (record == NULL)
	{
		log_warning("vlist_record: vlist_create failed");
		return NULL;
	}

	ll = scheme->v_data;
	pos = LL_START(ll);

	while ((vs = ll_next(ll, &pos)))
	{
		name = vs->v_name;
		vt = vtable_lookup(table, name);

		if (vt == NULL && vs->v_flags & VF_KEY)
		{
			log_error("vlist_record_from_table: \"%s\" is missing "
				"in vtable and declared as key", name);
			goto error;
		}


		data = vt == NULL ? NULL : vt->v_data;
		type = vt == NULL ? VT_NULL : vt->v_type;

		if (vlist_append_new(record, type, name, data,
			VF_COPY | vs->v_flags) == -1)
		{
			log_warning("vlist_record_from_table: vlist_append_new"
				" failed");
			goto error;
		}
	}

	return record;

error:
	if(record)
	{
		var_delete(record);
	}
	return NULL;
}


void *
vlist_record_lookup(var_t *record, char *key)
{
	ll_t *list = record->v_data;
	ll_entry_t *pos;
	var_t *item;

	/*
	 * Bad search: but records usually only have less than 10 values.
	 */
	pos = LL_START(list);
	while ((item = ll_next(list, &pos)))
	{
		if (strcmp(item->v_name, key) == 0)
		{
			return item;
		}
	}

	return NULL;
}


void *
vlist_record_get(var_t *record, char *key)
{
	var_t *item;

	item = vlist_record_lookup(record, key);
	if (item != NULL)
	{
		return item->v_data;
	}	

	return NULL;
}


int
vlist_record_keys_missing(var_t *record, var_t *table)
{
	ll_t *list = record->v_data;
	ll_entry_t *pos;
	var_t *item;

	pos = LL_START(list);
	while ((item = ll_next(list, &pos)))
	{
		if ((item->v_flags & VF_KEY) == 0)
		{
			continue;
		}

		if (vtable_lookup(table, item->v_name) == NULL)
		{
			return -1;
		}
	}

	return 0;
}
