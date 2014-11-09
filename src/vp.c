#include <config.h>

#include <stdlib.h>
#include <string.h>

#include <mopher.h>

#define BUFLEN 8192


static vp_t *
vp_create(void)
{
	vp_t *vp;

	vp = (vp_t *) malloc(sizeof(vp_t));
	if (vp == NULL) {
		log_sys_error("vp_create: malloc");
		return NULL;
	}

	memset(vp, 0, sizeof(vp_t));

	return vp;
}

void
vp_delete(vp_t *vp)
{
	if (vp->vp_key) {
		free(vp->vp_key);
	}

	if (vp->vp_data) {
		free(vp->vp_data);
	}

	free(vp);

	return;
}

void
vp_init(vp_t *vp, void *key, int klen, void *data, int dlen)
{
	vp->vp_key = key;
	vp->vp_klen = klen;
	vp->vp_data = data;
	vp->vp_dlen = dlen;

	vp->vp_null_fields = (vp_null_t *) data;
	vp->vp_fields = data + sizeof (vp_null_t *);

	return;
}


static int
vp_field_is_null(vp_t *vp, int field)
{
	if (*vp->vp_null_fields & ((vp_null_t) 1 << field))
	{
		return 1;
	}
	return 0;
}

static void
vp_set_null(vp_t *vp, int field)
{
	// vp_data might be reallocd
	vp->vp_null_fields = (vp_null_t *) vp->vp_data;
	*vp->vp_null_fields |= ((vp_null_t) 1 << field);
	return;
}

static int
vp_prepend_meta(vp_t *vp)
{
	vp->vp_data = malloc(sizeof (vp_null_t));
	if (vp->vp_data == NULL)
	{
		log_sys_error("vp_prepend_meta: malloc");
		return -1;
	}
	vp->vp_null_fields = (vp_null_t *) vp->vp_data;

	*vp->vp_null_fields = 0;

	vp->vp_dlen += sizeof (vp_null_t);

	return 0;
}

static int
vp_pack_data(void **buffer, int *len, var_t *v)
{
	char *p;
	void *data;
	int size;
	char dump[BUFLEN];

	// CAVEAT: Lists and table are packed as strings
	if (v->v_type == VT_LIST || v->v_type == VT_TABLE)
	{
		size = var_dump_data(v, dump, sizeof dump);
		if (size == -1)
		{
			log_error("vp_pack: var_dump_data failed");
			return -1;
		}

		// Add \0 to size
		++size;
		data = dump;
	}
	else
	{
		size = var_data_size(v);
		data = v->v_data;
	}

	p = realloc(*buffer, *len + size);
	if (p == NULL) {
		log_sys_error("vp_pack_data: realloc");
		return -1;
	}

	memcpy(p + *len, data, size);

	*buffer = p;
	*len += size;


	return *len;
}

vp_t *
vp_pack(var_t *record)
{
	vp_t *vp = NULL;
	var_t *item;
	ll_t *ll;
	ll_entry_t *pos;
	int field_is_key = 0;
	int n;
	int r;

	if(record->v_type != VT_LIST) {
		log_error("vp_pack: bad type");
		goto error;
	}

	ll = record->v_data;
	if (ll->ll_size > VP_FIELD_LIMIT)
	{
		log_error("vp_pack: Too many fields to pack");
		goto error;
	}

	vp = vp_create();
	if (vp == NULL) {
		log_error("vp_pack: vp_create failed");
		goto error;
	}

	if(vp_prepend_meta(vp))
	{
		log_error("vp_pack: vp_prepend_meta failed");
		goto error;
	}

	pos = LL_START(ll);
	for (n = 0; (item = ll_next(ll, &pos)); ++n)
	{
		field_is_key = item->v_flags & VF_KEY;

		// NULL item
		if (item->v_data == NULL)
		{
			if (field_is_key)
			{
				log_error("vp_pack: key without value");
				goto error;
			}

			vp_set_null(vp, n);
			continue;
		}

		if (field_is_key)
		{
			r = vp_pack_data(&vp->vp_key, &vp->vp_klen, item);
		}
		else
		{
			r = vp_pack_data(&vp->vp_data, &vp->vp_dlen, item);
		}


		if (r == -1)
		{
			log_error("vp_pack: vp_pack_data failed");
			goto error;
		}
	}

	vp_init(vp, vp->vp_key, vp->vp_klen, vp->vp_data, vp->vp_dlen);

	return vp;

error:
	if(vp)
	{
		vp_delete(vp);
	}

	return NULL;
}


var_t *
vp_unpack(vp_t *vp, var_t *scheme)
{
	var_t *v = NULL;
	var_t *item = NULL;
	var_t *record = NULL;
	ll_t *ll;
	ll_entry_t *pos;
	int field_is_key;
	void *field;
	int key_offset = 0;
	int field_offset = 0;
	int n;

	if(scheme->v_type != VT_LIST) {
		log_error("vp_unpack: bad type");
		goto error;
	}

	record = vlist_create(scheme->v_name, VF_COPYNAME);
	if(record == NULL) {
		log_error("vp_unpack: var_create failed");
		goto error;
	}

	// Make sure vp was correctly initialized
	if (vp->vp_null_fields == NULL || vp->vp_fields == NULL)
	{
		log_error("vp_unpack: vp struct not initialized");
		goto error;
	}

	ll = scheme->v_data;
	pos = LL_START(ll);

	for (n = 0; (item = ll_next(ll, &pos)); ++n) {
		field_is_key = item->v_flags & VF_KEY;

		// This should not be possible
		if (vp_field_is_null(vp, n) && field_is_key)
		{
			log_error("vp_unpack: afraid to unpack null key in "
				"field %d", n);
			goto error;
		}

		if (field_is_key)
		{
			field = vp->vp_key + key_offset;
		}
		else if (vp_field_is_null(vp, n))
		{
			field = NULL;
		}
		else
		{
			field = vp->vp_fields + field_offset;
		}

		v = var_create(item->v_type, item->v_name, field, VF_COPY);
		if(v == NULL)
		{
			log_error("vp_unpack: var_create failed");
			goto error;
		}

		if (field_is_key)
		{
			key_offset += var_data_size(v);
		}
		else
		{
			field_offset += var_data_size(v);
		}
 
		// Set VF_KEY for keys
		if (field_is_key)
		{
			v->v_flags |= VF_KEY;
		}

		// Append item to record
		if(vlist_append(record, v) == -1) {
			log_error("vp_unpack: vlist_append failed");
			goto error;
		}

		// Prevent multiple free in case of goto error
		v = NULL;
	}

	return record;


error:

	if (v) {
		var_delete(v);
	}

	if (record) {
		var_delete(record);
	}

	return NULL;
}

#ifdef DEBUG

void
vp_test(int n)
{
	var_t *scheme;
	vp_t *vp;

	VAR_INT_T key_int = 1;
	VAR_INT_T data_int = 2;
	VAR_FLOAT_T key_float = 3.3;
	VAR_FLOAT_T data_float = 4.4;
	char *key_string = "foo";
	char *data_string = "bar";
	var_t *record;
	var_t *v;

	VAR_INT_T *pi;
	VAR_FLOAT_T *pf;
	char *ps;

	scheme = vlist_scheme("test",
		"key_int",		VT_INT,		VF_KEEPNAME | VF_KEY,
		"key_float",		VT_FLOAT,	VF_KEEPNAME | VF_KEY,
		"key_string",		VT_STRING,	VF_KEEPNAME | VF_KEY,
		"data_int",		VT_INT,		VF_KEEPNAME,
		"data_int_null",	VT_INT,		VF_KEEPNAME,
		"data_float",		VT_FLOAT,	VF_KEEPNAME,
		"data_float_null",	VT_FLOAT,	VF_KEEPNAME,
		"data_string",		VT_STRING,	VF_KEEPNAME,
		"data_string_null",	VT_STRING,	VF_KEEPNAME,
		NULL);
	TEST_ASSERT(scheme != NULL, "vlist_scheme failed");

	record = vlist_record(scheme, &key_int, &key_float, key_string,
		&data_int, NULL, &data_float, NULL, data_string, NULL);
	TEST_ASSERT(record != NULL, "vlist_record failed");

	vp = vp_pack(record);
	TEST_ASSERT(vp != NULL, "vp_pack failed");

	var_delete(record);
	record = vp_unpack(vp, scheme);
	vp_delete(vp);
	TEST_ASSERT(record != NULL, "vp_unpack failed");

	v = vlist_record_lookup(record, "key_int");
	pi = (VAR_INT_T *) v->v_data;
	TEST_ASSERT((v->v_type | VF_KEY) != 0, "Key got lost");
	TEST_ASSERT(pi != NULL, "vlist_record_get key not found");
	TEST_ASSERT(*pi == key_int, "vp_unpack returnd bad value");
	pi = (VAR_INT_T *) vlist_record_get(record, "data_int");
	TEST_ASSERT(pi != NULL, "vlist_record_get key not found");
	TEST_ASSERT(*pi == data_int, "vp_unpack returnd bad value");
	pi = (VAR_INT_T *) vlist_record_get(record, "data_int_null");
	TEST_ASSERT(pi == NULL, "vp_unpack returnd bad value");

	v = vlist_record_lookup(record, "key_float");
	pf = (VAR_FLOAT_T *) v->v_data;
	TEST_ASSERT((v->v_type | VF_KEY) != 0, "Key got lost");
	TEST_ASSERT(pf != NULL, "vlist_record_get key not found");
	TEST_ASSERT(*pf == key_float, "vp_unpack returnd bad value");
	pf = (VAR_FLOAT_T *) vlist_record_get(record, "data_float");
	TEST_ASSERT(pf != NULL, "vlist_record_get key not found");
	TEST_ASSERT(*pf == data_float, "vp_unpack returnd bad value");
	pf = (VAR_FLOAT_T *) vlist_record_get(record, "data_float_null");
	TEST_ASSERT(pf == NULL, "vp_unpack returnd bad value");

	v = vlist_record_lookup(record, "key_string");
	ps = (char *) v->v_data;
	TEST_ASSERT((v->v_type | VF_KEY) != 0, "Key got lost");
	TEST_ASSERT(ps != NULL, "vlist_record_get key not found");
	TEST_ASSERT(strcmp(ps, "foo") == 0, "vp_unpack returnd bad value");
	ps = (char *) vlist_record_get(record, "data_string");
	TEST_ASSERT(ps != NULL, "vlist_record_get key not found");
	TEST_ASSERT(strcmp(ps, "bar") == 0, "vp_unpack returnd bad value");
	ps = (char *) vlist_record_get(record, "data_string_null");
	TEST_ASSERT(ps == NULL, "vp_unpack returnd bad value");

	var_delete(record);
	var_delete(scheme);
}
#endif
