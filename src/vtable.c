#include <stdarg.h>

#include <mopher.h>


var_t *
vtable_create(char *name, int flags)
{
	var_t *table;

	table = var_create(VT_TABLE, name, NULL, flags | VF_CREATE);
	if (table == NULL)
	{
		log_error("vtable_create: var_create failed");
	}

	return table;
}


var_t *
vtable_lookup(var_t *table, char *name)
{
	var_t lookup;
	ht_t *ht = table->v_data;

	lookup.v_name = name;

	return ht_lookup(ht, &lookup);
}


void *
vtable_get(var_t *table, char *name)
{
	var_t *v;

	if((v = vtable_lookup(table, name)) == NULL)
	{
		return NULL;
	}

	return v->v_data;
}


var_t *
vtable_getva(var_type_t type, var_t *table, va_list ap)
{
	char *key;
	var_t *v = table;

	for(;;) {
		key = va_arg(ap, char *);

		if (key == NULL)
		{
			break;
		}

		if (v->v_type != VT_TABLE)
		{
			log_info("vtable_getva: \"%s\" not a table", key);
			return NULL;
		}

		v = vtable_lookup(v, key);
		if (v == NULL)
		{
			log_debug("vtable_getva: no data for \"%s\"", key);
			return NULL;
		}
	}

	if (v->v_type != type)
	{
		log_warning("vtable_getva: type mismatch for \"%s\"", key);
		return NULL;
	}

	return v;
}


var_t *
vtable_getv(var_type_t type, var_t *table, ...)
{
	va_list ap;
	var_t *v;

	va_start(ap, table);

	v = vtable_getva(type, table, ap);
	
	va_end(ap);

	return v;
}


int
vtable_insert(var_t *table, var_t *v)
{
	ht_t *ht = table->v_data;

	if (ht_insert(ht, v))
	{
		log_error("vtable_insert: ht_insert failed");
		return -1;
	}

	return 0;
}


int
vtable_set(var_t *table, var_t *v)
{
	ht_t *ht = table->v_data;

	if (ht_lookup(ht, v) != NULL)
	{
		ht_remove(ht, v);
	}

	if (ht_insert(ht, v))
	{
		log_error("vtable_set: ht_insert failed");
		return -1;
	}

	return 0;
}


int
vtable_set_new(var_t *table, var_type_t type, char *name, void *data, int flags)
{
	var_t *v;

	v = var_create(type, name, data, flags);
	if (v == NULL)
	{
		log_error("vtable_set_new: var_create failed");
		return -1;
	}

	return vtable_set(table, v);
}


int
vtable_setv(var_t *table, ...)
{
	va_list ap;
	var_type_t type;
	char *name;
	void *data;
	int flags;

	va_start(ap, table);

	while ((type = va_arg(ap, var_type_t)))
	{
		name = va_arg(ap, char *);
		data = va_arg(ap, void *);
		flags = va_arg(ap, int);

		if (vtable_set_new(table, type, name, data, flags))
		{
			log_warning("vtable_setv: vtable_set_new failed");
			return -1;
		}
	}

	return 0;
}


int
vtable_rename(var_t *table, char *old, char *new)
{
	var_t *record;
	ht_t *ht = table->v_data;

	record = vtable_lookup(table, old);
	if (record == NULL)
	{
		log_debug("vtable_rename: \"%s\" not in table", old);
		return -1;
	}

	if (vtable_set_new(table, record->v_type, new, record->v_data,
	    VF_COPY))
	{
		log_error("vtable_rename: vtable_set_new failed");
		return -1;
	}

	ht_remove(ht, record);

	return 0;
}


var_t *
vtable_list_get(var_t *table, char *listname)
{
	var_t *list;

	list = vtable_lookup(table, listname);
	if (list == NULL)
	{
		list = vlist_create(listname, VF_COPYNAME);
		if (list == NULL)
		{
			log_error("vtable_list_append: vlist_create failed");
			return NULL;
		}

		if (vtable_set(table, list))
		{
			log_warning("vtable_list_append: vtable_set failed");
			var_delete(list);
			return NULL;
		}
	}

	return list;
}


int
vtable_list_append(var_t *table, char *listname, var_t *v)
{
	var_t *list;

	list = vtable_list_get(table, listname);
	if (list == NULL)
	{
		log_error("vtable_list_append: vtable_list_get failed");
		return -1;
	}

	return vlist_append(list, v);
}


int
vtable_list_append_new(var_t *table, var_type_t type, char *name, void *data,
    int flags)
{
	var_t *list;

	list = vtable_list_get(table, name);
	if (list == NULL)
	{
		log_error("vtable_list_append_new: vtable_list_get failed");
		return -1;
	}

	if (vlist_append_new(list, type, NULL, data, flags))
	{
		log_warning("vtable_list_append_new: vlist_append_new failed");
		return -1;
	}

	return 0;
}


int
vtable_dereference(var_t *table, ...)
{
	va_list ap;
	char *name;
	void **p;

	va_start(ap, table);

	while ((name = va_arg(ap, char *)))
	{
		p = va_arg(ap, void **);

		*p = vtable_get(table, name);
	}

	va_end(ap);

	return 0;
}


int
vtable_add_record(var_t *table, var_t *record)
{
	ll_t *list = record->v_data;
	var_t *item;

	ll_rewind(list);
	while ((item = ll_next(list)))
	{
		if (vtable_set_new(table, item->v_type, item->v_name,
		    item->v_data, VF_COPY))
		{
			log_error("vtable_add_record: vtable_set_new failed");
			return -1;
		}
	}

	return 0;
}
