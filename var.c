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


static void
var_clear_data(var_t * v)
{
	switch (v->v_type) {

	case VT_LIST:
		ll_delete(v->v_data, (void *) var_delete);
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
		var_clear_data(v);
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
var_copy_list(ll_t *list)
{
	ll_t *copy = NULL;
	var_t *vo, *vc;

	if ((copy = ll_create()) == NULL) {
		log_warning("var_copy_list: ll_create failed");
		goto error;
	}

	ll_rewind(list);
	while ((vo = ll_next(list))) {
		if ((vc = var_create(vo->v_type, vo->v_name, vo->v_data,
			VF_COPYNAME | VF_COPYDATA))
			== NULL) {
			log_warning("var_copy_list: var_create failed");
			goto error;
		}

		if(LL_INSERT(copy, vc)) {
			log_warning("var_copy_list: LL_INSERT failed");
			goto error;
		}
	}

	return copy;

error:
	if(copy) {
		ll_delete(copy, (void *) var_delete);
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
	 * Lists deserve special treatment.
	 */
	case VT_LIST:
		return var_copy_list(data);

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


int
var_init(var_t *v, var_type_t type, char *name, void *data, int flags)
{
	memset(v, 0, sizeof(var_t));

	v->v_type = type;
	v->v_flags = flags;

	if(flags & VF_COPYNAME) {
		if((v->v_name = strdup(name)) == NULL) {
			log_warning("var_init: strdup");
			return -1;
		}
	}
	else {
		v->v_name = name;
	}

	if(flags & VF_COPYDATA) {
		if((v->v_data = var_copy_data(type, data)) == NULL) {
			log_warning("var_init: var_copy_data failed");
			return -1;
		}
	}
	else {
		v->v_data = data;
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
		log_error("var_compare: comparing differnt types");

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
		log_error("var_compare: bad type");
		break;
	}

	return 0;
}


int
var_true(const var_t * v)
{
	ll_t *ll;
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

	case VT_NULL:
		return 0;	
	}

	log_die(EX_SOFTWARE, "var_true: bad type");
	return 0;
}

int
var_dump_data(var_t * v, char *buffer, int size)
{
	int len, n;
	sa_family_t saf;
	void *sin;
	char addrstr[INET6_ADDRSTRLEN];
	var_t *tmp;

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
		saf = ((struct sockaddr_storage *) v->v_data)->ss_family;
		if (saf == AF_INET6) {
			sin =
			    (void *) &((struct sockaddr_in6 *) v->
				       v_data)->sin6_addr;
		}
		else {
			sin =
			    (void *) &((struct sockaddr_in *) v->
				       v_data)->sin_addr;
		}

		if (inet_ntop(saf, sin, addrstr, sizeof(addrstr)) == NULL) {
			log_warning("var_dump_data: inet_ntop failed");
			return -1;
		}
		len = snprintf(buffer, size, "[%s]", addrstr);
		break;

	case VT_LIST:
		if (size > 1) {
			buffer[0] = '(';
		}
		len = 1;

		ll_rewind(v->v_data);
		while ((tmp = ll_next(v->v_data)) && len < size) {
			if ((n =
			     var_dump_data(tmp, buffer + len,
					   size - len)) == -1) {
				log_warning
				    ("var_dump_data: var_dump_data failed");
				return -1;
			}
			len += n;

			if (size > len + 1) {
				buffer[len] = ',';
				buffer[++len] = 0;
			}
		}

		if (size > len + 1) {
			buffer[len] = ')';
			buffer[++len] = 0;
		}
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

	return var_dump_data(v, buffer + len + 1, size - len - 1);
}


hash_t
var_hash(var_t * v)
{
	return HASH(v->v_name, strlen(v->v_name));
}

int
var_match(var_t * v1, var_t * v2)
{
	if (strcmp(v1->v_name, v2->v_name) == 0) {
		return 1;
	}

	return 0;
}

ht_t *
var_table_create(int buckets)
{
	ht_t *ht;

	if ((ht = ht_create(buckets, (ht_hash_t) var_hash,
		(ht_match_t) var_match, (ht_delete_t) var_delete)) == NULL) {
		log_warning("var_table_create: ht_create failed");
	}

	return ht;
}

void
var_table_delete(ht_t *table)
{
	ht_delete(table);

	return;
}


void
var_table_unset(ht_t * ht, char *name)
{
	var_t lookup, *v;

	memset(&lookup, 0, sizeof(var_t));
	lookup.v_name = name;

	if ((v = ht_lookup(ht, &lookup))) {
		ht_remove(ht, v);
	}

	return;
}

int
var_table_set(ht_t * ht, var_type_t type, char *name, void *data, int flags)
{
	var_t *v;

	var_table_unset(ht, name);

	if ((v = var_create(type, name, data, flags)) == NULL) {
		log_warning("var_table_save: var_create failed");
		return -1;
	}

	if (ht_insert(ht, v)) {
		log_warning("var_table_save: ht_insert failed");
		return -1;
	}

	return 0;
}


void *
var_table_get(ht_t *ht, char *name)
{
	var_t lookup, *v;

	lookup.v_type = VT_NULL;
	lookup.v_name = name;

	if((v = ht_lookup(ht, &lookup)) == NULL) {
		return NULL;
	}

	return v->v_data;
}


int
var_table_list_insert(ht_t * ht, var_type_t type, char *name, void *data, int flags)
{
	var_t lookup, *list;
	var_t *entry = NULL;
	ll_t *ll = NULL;

	lookup.v_type = VT_LIST;
	lookup.v_name = name;

	if ((list = ht_lookup(ht, &lookup)) == NULL) {
		if ((ll = ll_create()) == NULL) {
			log_warning("var_table_list_append: ll_create failed");
			goto error;
		}

		if ((list = var_create(VT_LIST, name, ll, VF_COPYNAME)) == NULL) {
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

	if(ll) {
		ll_delete(ll, (void *) var_delete);
	}

	if (entry) {
		var_delete(entry);
	}

	return -1;
}
