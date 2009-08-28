#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

#include "var.h"
#include "ll.h"
#include "log.h"

#define ADDR6_LEN 16
#define BUCKETS 256


static hash_t
var_hash(var_t * v)
{
	return HASH(v->v_name, strlen(v->v_name));
}

static int
var_match(var_t * v1, var_t * v2)
{
	if (strcmp(v1->v_name, v2->v_name) == 0) {
		return 1;
	}

	return 0;
}

static void
var_data_clear(var_t * v)
{
	switch (v->v_type) {

	case VT_LIST:
		ll_delete(v->v_data, (void *) var_delete);
		break;

	case VT_TABLE:
		ht_delete(v->v_data);
		break;

	default:
		free(v->v_data);
	}

	v->v_data = NULL;

	return;
}

void
var_clear(var_t * v)
{
	if (v->v_name && !(v->v_flags & VF_KEEPNAME)) {
		free(v->v_name);
	}

	if (v->v_data && !(v->v_flags & VF_KEEPDATA)) {
		var_data_clear(v);
	}

	memset(v, 0, sizeof(var_t));

	return;
}

void
var_delete(var_t *v)
{
	var_clear(v);
	free(v);

	return;
}

static void *
var_copy_list_or_table(var_type_t type, void *src)
{
	int is_table, r;
	void *copy = NULL;
	ht_t *ht = src;
	var_t *vo, *vc = NULL;

	is_table = (type == VT_TABLE);

	copy = is_table ?  (void *) ht_create(ht->ht_buckets, ht->ht_hash,
		ht->ht_match, ht->ht_delete) : (void *) ll_create();

	if (copy == NULL) {
		log_warning("var_list_copy: %s_create failed",
			is_table ? "ht" : "ll");
		goto error;
	}

	if(is_table) {
		ht_rewind(copy);
	}
	else {
		ll_rewind(copy);
	}

	while ((vo = is_table ? ht_next(copy) : ll_next(copy))) {
		if ((vc = var_create(vo->v_type, vo->v_name, vo->v_data,
			VF_COPYNAME | VF_COPYDATA))
			== NULL) {
			log_warning("var_list_copy: var_create failed");
			goto error;
		}

		r = is_table ? ht_insert(copy, vc) : LL_INSERT(copy, vc);
		if(r == -1) {
			log_warning("var_list_copy: %s failed",
				is_table ? "ht_insert" : "LL_INSERT");
			goto error;
		}
	}

	return copy;

error:
	if(copy) {
		if(is_table) {
			ht_delete(copy);
		}
		else {
			ll_delete(copy, (void *) var_delete);
		}
	}

	if(vc) {
		var_delete(vc);
	}

	return NULL;
}

static void *
var_copy_data(var_type_t type, void *data)
{
	int size = 0;
	void *copy;

	switch (type) {
	/*
	 * Lists and tables deserve special treatment.
	 */
	case VT_LIST:
	case VT_TABLE:
		return var_copy_list_or_table(type, data);

	case VT_INT:
		size = sizeof(VAR_INT_T);
		break;
	case VT_FLOAT:
		size = sizeof(VAR_FLOAT_T);
		break;
	case VT_STRING:
		size = strlen((char *) data) + 1;
		break;
	case VT_ADDR:
		size = sizeof(var_sockaddr_t);
		break;
	default:
		log_warning("var_copy: bad type");
		return NULL;
	}

	if((copy = malloc(size)) == NULL) {
		log_warning("var_copy_data: malloc");
		return NULL;
	}

	memcpy(copy, data, size);

	return copy;
}

static void *
var_data_create(var_type_t type)
{
	void *p;

	switch(type) {
	case VT_LIST:
		p = (void *) ll_create();
		break;

	case VT_TABLE:
		p = (void *) ht_create(BUCKETS, (ht_hash_t) var_hash,
			(ht_match_t) var_match, (ht_delete_t) var_delete);
		break;

	default:
		log_warning("var_data_create: bad type");
		return NULL;
	}

	if(p == NULL) {
		log_warning("var_data_create: %s failed",
			type == VT_LIST ? "ll_create" : "ht_create");
	}

	return p;
}


static char *
var_name_init(char *name, int flags)
{
	char *copy;

	if((flags & VF_COPYNAME) == 0) {
		return name;
	}

	if ((copy = strdup(name)) == NULL) {
		log_warning("var_name_init: strdup");
	}

	return copy;
}


static void *
var_data_init(var_type_t type, void *data, int flags)
{
	void *p;

	/*
	 * Allocate and initialize data (lists and tables only).
	 */
	if(data == NULL && (flags & VF_CREATE)) {
		if((p = var_data_create(type)) == NULL) {
			log_warning("var_data_init: var_data_create failed");
		}

		return p;
	}

	/*
	 * No copy needed.
	 */
	if((flags & VF_COPYDATA) == 0) {
		return data;
	}

	/*
	 * Return a copy
	 */
	if((p = var_copy_data(type, data)) == NULL) {
		log_warning("var_init: var_copy_data failed");
	}

	return p;
}


int
var_init(var_t *v, var_type_t type, char *name, void *data, int flags)
{
	memset(v, 0, sizeof(var_t));

	v->v_type = type;
	v->v_flags = flags;

	/*
	 * If name is set var_name_init never returns NULL.
	 */
	v->v_name = var_name_init(name, flags);
	if (name && v->v_name == NULL) {
		log_warning("var_init: var_name_init failed");
		return -1;
	}

	/*
	 * If data is set var_data_init never returns NULL.
	 */
	v->v_data = var_data_init(type, data, flags);
	if (data && v->v_data == NULL) {
		log_warning("var_init: var_data_init failed");
		return -1;
	}

	return 0;
}


var_t *
var_create(var_type_t type, char *name, void *data, int flags)
{
	var_t *v;

	if((v = (var_t *) malloc(sizeof(var_t))) == NULL) {
		log_warning("var_create: malloc");
		return NULL;
	}

	if(var_init(v, type, name, data, flags)) {
		log_warning("var_create: var_init failed");
		var_delete(v);
		return NULL;
	}

	return v;
}


int
var_compare_addr(struct sockaddr_storage *ss1, struct sockaddr_storage *ss2)
{
	unsigned long inaddr1, inaddr2;

	if (ss1->ss_family < ss2->ss_family) {
		return -1;
	}

	if (ss1->ss_family > ss2->ss_family) {
		return 1;
	}

	switch (ss1->ss_family) {

	case AF_INET:
		inaddr1 = ntohl(((struct sockaddr_in *) ss1)->sin_addr.s_addr);
		inaddr2 = ntohl(((struct sockaddr_in *) ss2)->sin_addr.s_addr);

		if (inaddr1 < inaddr2) {
			return -1;
		}

		if (inaddr1 > inaddr2) {
			return 1;
		}

		return 0;

	case AF_INET6:
		/*
		 * XXX: Simple implementation.
		 */
		return memcmp(&((struct sockaddr_in6 *) ss1)->sin6_addr.s6_addr,
			      &((struct sockaddr_in6 *) ss2)->sin6_addr.s6_addr,
			      ADDR6_LEN);

	default:
		log_warning("var_compare_addr: bad address family");
	}

	return 0;
}


int
var_compare(const var_t * v1, const var_t * v2)
{

	if (v1->v_type != v2->v_type) {
		log_warning("var_compare: comparing differnt types");

		if (v1->v_type < v2->v_type) {
			return -1;
		}

		return 1;
	}

	switch (v1->v_type) {

	case VT_INT:
		if (*(VAR_INT_T *) v1->v_data < *(VAR_INT_T *) v2->v_data) {
			return -1;
		}
		if (*(VAR_INT_T *) v1->v_data > *(VAR_INT_T *) v2->v_data) {
			return 1;
		}
		return 0;

	case VT_FLOAT:
		if (*(VAR_FLOAT_T *) v1->v_data < *(VAR_FLOAT_T *) v2->v_data) {
			return -1;
		}
		if (*(VAR_FLOAT_T *) v1->v_data > *(VAR_FLOAT_T *) v2->v_data) {
			return 1;
		}
		return 0;

	case VT_STRING:
		return strcmp(v1->v_data, v2->v_data);

	case VT_ADDR:
		return var_compare_addr(v1->v_data, v2->v_data);

	default:
		log_warning("var_compare: bad type");
		break;
	}

	return 0;
}


int
var_true(const var_t * v)
{
	ll_t *ll;
	ht_t *ht;
	struct sockaddr_storage ss, *pss;

	switch (v->v_type) {

	case VT_INT:
		if (*(VAR_INT_T *) v->v_data == 0) {
			return 0;
		}
		return 1;

	case VT_FLOAT:
		if (*(VAR_FLOAT_T *) v->v_data == 0) {
			return 0;
		}
		return 1;

	case VT_STRING:
		if (strlen(v->v_data)) {
			return 1;
		}
		return 0;

	case VT_ADDR:
		/*
		 * XXX: Simple implementation
		 */
		pss = (struct sockaddr_storage *) v->v_data;
		memset(&ss, 0, sizeof(ss));
		ss.ss_family = pss->ss_family;

		if(var_compare_addr(pss, &ss)) {
			return 1;
		}

		return 0;

	case VT_LIST:
		ll = v->v_data;
		if(ll->ll_size) {
			return 1;
		}

		return 0;

	case VT_TABLE:
		ht = v->v_data;
		if(ht->ht_records) {
			return 1;
		}

		return 0;

	case VT_NULL:
		return 0;	
	}

	log_die(EX_SOFTWARE, "var_true: bad type");
	return 0;
}

static int
var_dump_list_or_table(var_t * v, char *buffer, int size)
{
	int len, n, is_table;
	var_t *tmp;

	is_table = (v->v_type == VT_TABLE);

	if (size > 1) {
		buffer[0] = is_table ? '{' : '(';
	}
	len = 1;

	if(is_table) {
		ht_rewind(v->v_data);
	}
	else {
		ll_rewind(v->v_data);
	}

	while ((tmp = (is_table ? ht_next(v->v_data) : ll_next(v->v_data)))
		&& len < size) {

		if(len > 1 && size > len + 1) {
			buffer[len] = ',';
			buffer[++len] = 0;
		}

		if(is_table) {
			n = var_dump(tmp, buffer + len, size - len);
		}
		else {
			n = var_dump_data(tmp, buffer + len, size - len);
		}

		if(n == -1) {
			log_warning
			    ("var_dump_data: var_dump_data failed");
			return -1;
		}

		len += n;

	}

	if (size > len + 1) {
		buffer[len] = is_table ? '}' : ')';
		buffer[++len] = 0;
	}

	return len;
}

int
var_dump_data(var_t * v, char *buffer, int size)
{
	int len;
	char addrstr[INET6_ADDRSTRLEN];
	struct sockaddr_storage *ss = v->v_data;
	struct sockaddr_in *sin = v->v_data;
	struct sockaddr_in6 *sin6 = v->v_data;
	void *p;

	if(v->v_data == NULL) {
		return snprintf(buffer, size, "(null)");
	}

	switch (v->v_type) {

	case VT_INT:
		len = snprintf(buffer, size, "%ld", *(VAR_INT_T *) v->v_data);
		break;

	case VT_FLOAT:
		len =
		    snprintf(buffer, size, "%.2f", *(VAR_FLOAT_T *) v->v_data);
		break;

	case VT_STRING:
		len = snprintf(buffer, size, "\"%s\"", (char *) v->v_data);
		break;

	case VT_ADDR:
		p = (ss->ss_family == AF_INET6 ? (void *) &sin6->sin6_addr :
			(void *) &sin->sin_addr);

		if (inet_ntop(ss->ss_family, p, addrstr, sizeof(addrstr))
			== NULL) {
			log_warning("var_dump_data: inet_ntop failed");
			return -1;
		}

		len = snprintf(buffer, size, "[%s]", addrstr);
		break;

	case VT_LIST:
	case VT_TABLE:
		len = var_dump_list_or_table(v, buffer, size);
		break;

	default:
		log_warning("var_dump_data: bad type");
		return -1;
	}

	if (len >= size) {
		log_warning("var_dump_data: buffer exhasted");
		return -1;
	}

	return len;
}

int
var_dump(var_t * v, char *buffer, int size)
{
	int len;

	len = strlen(v->v_name);

	if (len >= size) {
		log_warning("var_dump: buffer exhasted");
		return -1;
	}

	strncpy(buffer, v->v_name, len);

	buffer[len] = '=';
	buffer[++len] = 0;

	len += var_dump_data(v, buffer + len, size - len);

	return len;
}


var_t *
var_table_lookup(var_t *table, char *name)
{
	var_t lookup;
	ht_t *ht = table->v_data;

	memset(&lookup, 0, sizeof(var_t));
	lookup.v_name = name;

	return ht_lookup(ht, &lookup);
}

void *
var_table_get(var_t * table, char *name)
{
	var_t *v;

	if((v = var_table_lookup(table, name)) == NULL) {
		return NULL;
	}

	return v->v_data;
}

int
var_table_insert(var_t *table, var_t *v)
{
	ht_t *ht = table->v_data;

	if (ht_insert(ht, v)) {
		log_warning("var_table_insert: ht_insert failed");
		return -1;
	}

	return 0;
}

int
var_table_set(var_t *table, var_t *v)
{
	ht_t *ht = table->v_data;

	if (ht_lookup(ht, v) != NULL) {
		ht_remove(ht, v);
	}

	if (ht_insert(ht, v)) {
		log_warning("var_table_set: ht_insert failed");
		return -1;
	}

	return 0;
}

int
var_table_setv(var_t *table, var_type_t type, char *name, void *data, int flags)
{
	var_t *v;

	if (table->v_type != VT_TABLE) {
		log_warning("var_table_set: need table");
		return -1;
	}

	if ((v = var_create(type, name, data, flags)) == NULL) {
		log_warning("var_table_save: var_create failed");
		return -1;
	}

	return var_table_set(table, v);
}


int
var_list_append(var_t *list, var_t *item)
{
	ll_t *ll = list->v_data;

	if((LL_INSERT(ll, item)) == -1) {
		log_warning("var_list_append: LL_INSERT failed");
		return -1;
	}

	return 0;
}


int
var_table_list_insert(var_t *table, var_type_t type, char *name, void *data, int flags)
{
	var_t *list;
	var_t *entry = NULL;
	ht_t *ht = table->v_data;

	if ((list = var_table_lookup(table, name)) == NULL) {
		if ((list = var_create(VT_LIST, name, NULL,
		    VF_COPYNAME | VF_CREATE)) == NULL)
		{
			log_warning("var_table_list_append: var_create failed");
			goto error;
		}

		if (ht_insert(ht, list)) {
			log_warning("var_table_list_append: ht_insert failed");
			var_delete(list);
			goto error;
		}
	}

	if ((entry = var_create(type, name, data, flags)) == NULL) {
		log_warning("var_table_list_append: var_create failed");
		goto error;
	}

	if (LL_INSERT(list->v_data, entry) == -1) {
		log_warning("var_table_list_append: LL_INSERT failed");
		goto error;
	}

	return 0;

error:

	if (entry) {
		var_delete(entry);
	}

	return -1;
}
