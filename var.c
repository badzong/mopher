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

void
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
	if (v->v_name) {
		free(v->v_name);
	}

	if (v->v_data == NULL) {
		v->v_type = VT_NULL;
		return;
	}

	var_clear_data(v);
	v->v_type = VT_NULL;

	return;
}

int
var_set_reference(var_t * v, var_type_t type, void *data)
{
	if (v->v_data) {
		var_clear_data(v);
	}

	v->v_type = type;
	v->v_data = data;

	return 0;
}

int
var_set_copy(var_t * v, var_type_t type, const void *data)
{
	var_t *tmp, *new;

	union {
		void *v;
		VAR_INT_T *i;
		VAR_FLOAT_T *f;
		char *s;
		var_sockaddr_t *sa;
		ll_t *ll;
	} copy;

	if (data == NULL) {
		v->v_type = type;
		v->v_data = NULL;
		return 0;
	}

	switch (type) {

	case VT_INT:
		if ((copy.v = malloc(sizeof(VAR_INT_T))) == NULL) {
			log_warning("var_set: malloc");
			return -1;
		}

		*copy.i = *(VAR_INT_T *) data;
		break;

	case VT_FLOAT:
		if ((copy.v = malloc(sizeof(VAR_FLOAT_T))) == NULL) {
			log_warning("var_set: malloc");
			return -1;
		}

		*copy.f = *(VAR_FLOAT_T *) data;
		break;

	case VT_STRING:
		if ((copy.s = strdup(data)) == NULL) {
			log_warning("var_set: strdup");
			return -1;
		}
		break;

	case VT_ADDR:
		if ((copy.v = malloc(sizeof(var_sockaddr_t))) == NULL) {
			log_warning("var_set: malloc");
			return -1;
		}

		memcpy(copy.v, data, sizeof(var_sockaddr_t));
		break;

	case VT_LIST:
		if ((copy.ll = ll_create()) == NULL) {
			log_warning("var_set: ll_create failed");
			return -1;
		}

		ll_rewind((ll_t *) data);
		while ((tmp = ll_next((ll_t *) data))) {
			if ((new = var_create_copy(tmp->v_type, tmp->v_name,
						   tmp->v_data)) == NULL) {
				log_warning
				    ("var_set: var_create (recursive) failed");
				ll_delete(copy.ll, (void *) var_delete);
				return -1;
			}

			if (LL_INSERT(copy.ll, new)) {
				log_warning("var_set: LL_INSERT failed");
				ll_delete(copy.ll, (void *) var_delete);
				return -1;
			}
		}
		break;

	default:
		log_warning("var_set: bad type");
		return -1;
	}

	return var_set_reference(v, type, copy.v);
}

int
var_init_copy(var_t * v, var_type_t type, const char *name, const void *data)
{
	memset(v, 0, sizeof(var_t));

	if (var_set_copy(v, type, data)) {
		log_warning("var_init_copy: var_set_copy failed");
		return -1;
	}

	if (name == NULL) {
		return 0;
	}

	if ((v->v_name = strdup(name)) == NULL) {
		log_warning("var_init_copy: strdup");
		return -1;
	}

	return 0;
}

int
var_init_reference(var_t * v, var_type_t type, const char *name, void *data)
{
	memset(v, 0, sizeof(var_t));

	if (var_set_reference(v, type, data)) {
		log_warning("var_init_reference: var_set_reference failed");
		return -1;
	}

	if (name == NULL) {
		return 0;
	}

	if ((v->v_name = strdup(name)) == NULL) {
		log_warning("var_init_copy: strdup");
		return -1;
	}

	return 0;
}

var_t *
var_create_copy(var_type_t type, const char *name, const void *data)
{
	var_t *v = NULL;

	if ((v = (var_t *) malloc(sizeof(var_t))) == NULL) {
		log_warning("var_create_copy: malloc");
		goto error;
	}

	if (var_init_copy(v, type, name, data)) {
		log_warning("var_create_copy: var_init_copy failed");
		goto error;
	}

	return v;

error:

	if (v) {
		free(v);
	}

	return NULL;
}

var_t *
var_create_reference(var_type_t type, const char *name, void *data)
{
	var_t *v = NULL;

	if ((v = (var_t *) malloc(sizeof(var_t))) == NULL) {
		log_warning("var_create_reference: malloc");
		goto error;
	}

	if (var_init_reference(v, type, name, data)) {
		log_warning("var_create_reference: var_init_reference failed");
		goto error;
	}

	return v;

error:

	if (v) {
		free(v);
	}

	return NULL;
}

void
var_delete(var_t * v)
{
	var_clear(v);

	free(v);

	return;
}

var_t *
var_strtoi(const char *name, const char *str)
{
	VAR_INT_T i;

	i = atoi(str);

	return var_create_copy(VT_INT, name, &i);
}

var_t *
var_strtof(const char *name, const char *str)
{
	VAR_FLOAT_T f;

	errno = 0;
	f = strtod(str, NULL);

	if (errno) {
		log_warning("var_create_float: strtod");
		return NULL;
	}

	return var_create_copy(VT_FLOAT, name, &f);
}

int
var_string_rencaps(const char *src, char **dst, const char *encaps)
{
	int len;

	*dst = NULL;
	len = strlen(src);

	/*
	 * No encapsulation found.
	 */
	if (!(src[0] == encaps[0] && src[len - 1] == encaps[1])) {
		return 0;
	}

	if ((*dst = strdup(src + 1)) == NULL) {
		log_warning("var_string_encaps: strdup");
		return -1;
	}

	(*dst)[len - 2] = 0;

	return 1;
}

var_t *
var_strtostr(const char *name, const char *str)
{
	char *rencaps;

	switch (var_string_rencaps(str, &rencaps, "\"\"")) {
	case 0:
		return var_create_copy(VT_STRING, name, str);

	case 1:
		return var_create_reference(VT_STRING, name, rencaps);

	default:
		log_warning("var_create_string: var_string_rencaps failed");
	}

	return NULL;
}

var_t *
var_strtoa(const char *name, const char *str)
{
	var_t *v = NULL;
	struct sockaddr_storage ss;
	struct sockaddr_in *sin = (struct sockaddr_in *) &ss;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) &ss;
	char *wc;

	if (var_string_rencaps(str, &wc, "[]") == -1) {
		log_warning("var_create_addr: var_string_rencaps failed");
		return NULL;
	}

	if (wc == NULL) {
		wc = (char *) str;
	}

	if (inet_pton(AF_INET, wc, &sin->sin_addr) == 1) {
		ss.ss_family = AF_INET;
		v = var_create_copy(VT_ADDR, name, &ss);
	}
	else if (inet_pton(AF_INET6, wc, &sin6->sin6_addr) == 1) {
		ss.ss_family = AF_INET6;
		v = var_create_copy(VT_ADDR, name, &ss);
	}
	else {
		log_warning("var_create_addr: bad address: %s", wc);
	}

	if (wc != str) {
		free(wc);
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
var_true(const var_t * v)
{
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

	default:
		break;
		/*
		 * TODO: V_ADDR, V_LIST
		 */
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

// var_type_t
// var_gettype(
// char *str)
// {
// static char *signs = "+-";
// static char *numbers = "0123456789";
// char *p;
// int len;
// int period = 0;
// 
// if(!(len = strlen(str))) {
// log_warning("var_gettype: empty string has no type");
// return VT_NULL;
// }
// 
// /*
// * Addresses are enclosed with brackets.
// */
// if(str[0] == '[' && str[len - 1] == ']') {
// return VT_ADDR;
// }
// 
// /*
// * Strings are enclosed with double quotes.
// */
// if(str[0] == '"' && str[len - 1] == '"') {
// return VT_STRING;
// }
// 
// /*
// * Scan string for numeric types
// */
// for(p = signs; *p; ++p) {
// if(*str == *p) {
// ++str;
// break;
// }
// }
// 
// for(;*str ; ++str) {
// for(p = numbers; *p; ++p) {
// if(*str == *p) {
// break;
// }
// }
// 
// /*
// * Allow 1 period
// */
// if(*str == '.' && period == 0) {
// ++period;
// continue;
// }
// 
// if(!*p) {
// break;
// }
// }
// 
// /*
// * Illegal characters found. Fallback to string.
// */
// if(*str) {
// return VT_STRING;
// }
// 
// if(period) {
// return VT_FLOAT;
// }
// 
// return VT_INT;
// }
// 
// 
// var_t *
// var_read(
// const char *str)
// {
// char *wc, *p;
// var_type_t type;
// var_t *v = NULL;
// 
// if((wc = strdup(str)) == NULL) {
// log_warning("var_read: strdup");
// return NULL;
// }
// 
// if((p = strchr(wc, '=')) == NULL) {
// log_warning("var_read: assignment expected");
// return NULL;
// }
// 
// *p++ = 0;
// 
// type = var_gettype(p);
// 
// switch(type) {
// 
// case VT_INT:
// v = var_create_int(wc, p);
// break;
// 
// case VT_FLOAT:
// v = var_create_float(wc, p);
// break;
// 
// case VT_STRING:
// v = var_create_string(wc, p);
// break;
// 
// case VT_ADDR:
// v = var_create_addr(wc, p);
// break;
// 
// default:
// log_warning("var_read: bad type");
// }
// 
// free(wc);
// 
// return v;
// }

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

int
var_table_unset(ht_t * ht, const char *name)
{
	var_t lookup, *v;

	if (var_init_reference(&lookup, VT_NULL, name, NULL)) {
		log_warning("var_table_unset: var_create failed");
		return -1;
	}

	if ((v = ht_lookup(ht, &lookup))) {
		ht_remove(ht, v);
		var_delete(v);
	}

	var_clear(&lookup);

	return 0;
}

int
var_table_save(ht_t * ht, var_type_t type, const char *name, const void *data)
{
	var_t *v;

	if (var_table_unset(ht, name)) {
		log_warning("var_table_save: var_table_unset failed");
		return -1;
	}

	if ((v = var_create_copy(type, name, data)) == NULL) {
		log_warning("var_table_save: var_create failed");
		return -1;
	}

	if (ht_insert(ht, v)) {
		log_warning("var_table_save: ht_insert failed");
		return -1;
	}

	return 0;
}

int
var_table_list_insert(ht_t * ht, var_type_t type, char *name, void *data)
{
	var_t lookup, *list;
	var_t *entry = NULL;
	ll_t *ll = NULL;

	if (var_init_reference(&lookup, VT_LIST, name, NULL)) {
		log_warning("var_table_list_append: var_init failed");
		goto error;
	}

	if ((list = ht_lookup(ht, &lookup)) == NULL) {
		if ((ll = ll_create()) == NULL) {
			log_warning("var_table_list_append: ll_create failed");
			goto error;
		}

		if ((list = var_create_reference(VT_LIST, name, ll)) == NULL) {
			log_warning("var_table_list_append: var_create failed");
			ll_delete(ll, (void *) var_delete);
			goto error;
		}

		if (ht_insert(ht, list)) {
			log_warning("var_table_list_append: ht_insert failed");
			var_delete(list);
			goto error;
		}
	}

	if ((entry = var_create_copy(type, name, data)) == NULL) {
		log_warning("var_table_list_append: var_create failed");
		goto error;
	}

	if (LL_INSERT(list->v_data, entry) == -1) {
		log_warning("var_table_list_append: LL_INSERT failed");
		goto error;
	}

	var_clear(&lookup);

	return 0;

error:

	if (entry) {
		var_delete(entry);
	}

	return -1;
}
