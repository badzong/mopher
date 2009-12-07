#include "config.h"

#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

#include "mopher.h"

#define BUCKETS 256
#define BUFLEN 1024


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
	if (v->v_data == NULL || (v->v_flags & VF_KEEPDATA))
	{
		return;
	}

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
var_clear_name(var_t *v)
{
	if (v->v_name && !(v->v_flags & VF_KEEPNAME))
	{
		free(v->v_name);
	}

	return;
}


void
var_clear(var_t *v)
{
	/*
	printf("DELETE %d@%p ", v->v_type, v);
	if (v) {
		printf("NAME=%s @ %p\n", v->v_name, v->v_name);
	}
	*/

	var_clear_name(v);
	var_data_clear(v);

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
	int	 is_table, r;
	void	*copy = NULL;
	ht_t	*ht = src;
	ll_t	*ll = src;
	var_t	*vo, *vc = NULL;

	is_table = (type == VT_TABLE);

	if (is_table)
	{
		ht_rewind(ht);

		copy = (void *) ht_create(ht->ht_buckets, ht->ht_hash,
		    ht->ht_match, ht->ht_delete);
	}
	else
	{
		ll_rewind(ll);

		copy = (void *) ll_create();
	}

	if (copy == NULL)
	{
		log_warning("var_list_copy: %s_create failed",
		    is_table ? "ht" : "ll");
		goto error;
	}

	while ((vo = (is_table ? ht_next(ht) : ll_next(ll)))) {
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


int
var_data_size(var_t *v)
{
	if(v->v_type == VT_STRING) {
		return strlen((char *) v->v_data) + 1;
	}

	if(v->v_type == VT_INT) {
		return sizeof(VAR_INT_T);
	}

	if(v->v_type == VT_FLOAT) {
		return sizeof(VAR_FLOAT_T);
	}

	if(v->v_type == VT_ADDR) {
		return sizeof(var_sockaddr_t);
	}

	log_warning("var_data_size: bad type");
	return 0;
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

	case VT_INT:
		p = malloc(sizeof (VAR_INT_T));
		break;

	case VT_FLOAT:
		p = malloc(sizeof (VAR_FLOAT_T));
		break;

	case VT_STRING:
		p = malloc(VAR_STRING_BUFFER_LEN);
		break;

	case VT_ADDR:
		p = malloc(sizeof (struct sockaddr_storage));
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

	if (name == NULL)
	{
		return NULL;
	}

	if((flags & VF_COPYNAME) == 0)
	{
		return name;
	}

	if ((copy = strdup(name)) == NULL)
	{
		log_warning("var_name_init: strdup");
	}

	return copy;
}


void
var_rename(var_t *v, char *name, int flags)
{
	var_clear_name(v);

	flags &= VF_COPYNAME | VF_KEEPNAME;

	v->v_name = var_name_init(name, flags);

	/* Copy only name flags */
	v->v_flags &= ~(VF_COPYNAME | VF_KEEPNAME);
	v->v_flags |= flags;

	return;
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

	/*
	 * Sanitize flags
	 */
	if (flags & VF_COPYNAME && flags & VF_KEEPNAME) {
		flags ^= VF_KEEPNAME;
	}

	if (flags & VF_COPYDATA && flags & VF_KEEPDATA) {
		flags ^= VF_KEEPDATA;
	}

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


void *
var_scan_data(var_type_t type, char *str)
{
	void *src, *copy;
	VAR_INT_T i;
	VAR_FLOAT_T d;
	struct sockaddr_storage *ss;

	switch (type)
	{
	case VT_STRING:
		src = str;
		break;

	case VT_INT:
		i = atol(str);
		src = &i;
		break;

	case VT_FLOAT:
		d = atof(str);
		src = &d;

	case VT_ADDR:
		ss = util_strtoaddr(str);
		if (ss == NULL)
		{
			log_warning("var_scan_data: util_strtoaddr failed");
			return NULL;
		}

		return ss;

	//TODO Add more types

	default:
		log_warning("var_scan_data: bad type");
		return NULL;
	}

	copy = var_copy_data(type, src);
	if (copy == NULL)
	{
		log_warning("var_scan_data: var_copy_data failed");
		return NULL;
	}

	return copy;
}

var_t *
var_scan_scheme(var_t *scheme, char *str)
{
	char *name;
	char *copy = NULL;
	char *p, *q;
	int len;
	ll_t *list;
	var_t *v;
	var_t *output = NULL;
	void *data;

	output = var_create(VT_LIST, scheme->v_name, NULL,
	    VF_KEEPNAME | VF_CREATE);
	if (output == NULL)
	{
		log_warning("var_scan_scheme: var_create failed");
		goto error;
	}

	p = strchr(str, '=');
	if (p == NULL)
	{
		log_warning("var_scan_scheme: no '=' found");
		goto error;
	}

	len = p - str;
	name = strndup(str, len);
	if (name == NULL)
	{
		log_warning("var_scan_scheme: strndup");
		goto error;
	}

	copy = util_strdupenc(p + 1, "()");
	if (copy == NULL)
	{
		log_warning("var_scan_scheme: util_strndupenc failed");
		goto error;
	}

	list = scheme->v_data;
	
	for (p = copy, ll_rewind(list);
	    (v = ll_next(list)) && p != NULL;
	    p = q)
	{
		q = strchr(p, ',');
		if (q != NULL)
		{
			*(q++) = 0;
			
		}

		data = var_scan_data(v->v_type, p);
		if (data == NULL)
		{
			log_warning("var_scan_scheme: var_scan_data failed");
			goto error;
		}

		if (var_list_append_new(output, v->v_type, v->v_name, data,
		    VF_KEEPNAME))
		{
			log_warning("var_scan_scheme: var_list_append_new "
			    "failed");
			goto error;
		}
	}

	if (v != NULL || p != NULL)
	{
		log_warning("var_scan_scheme: bad string");
		goto error;
	}

	return output;
	
		
error:

	if (output)
	{
		var_delete(output);
	}

	if (copy)
	{
		free(copy);
	}

	return NULL;
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
		return util_addrcmp(v1->v_data, v2->v_data);

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

	if (v->v_data == 0)
	{
		return 0;
	}

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
		if (strcmp(v->v_data, "0") == 0)
		{
			return 0;
		}

		if (strlen(v->v_data))
		{
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

		if(util_addrcmp(pss, &ss)) {
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

	case VT_POINTER:
		if(v->v_data) {
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
		len = strlen(v->v_data);
		if (len < size)
		{
			strcpy(buffer, v->v_data);
		}

		break;

	case VT_ADDR:
		if (ss->ss_family == AF_INET6)
		{
			p = (void *) &sin6->sin6_addr;
		}
		else
		{
			p = (void *) &sin->sin_addr;
		}

		if (inet_ntop(ss->ss_family, p, addrstr, sizeof(addrstr))
			== NULL) {
			log_warning("var_dump_data: inet_ntop failed");
			return -1;
		}

		len = strlen(addrstr);
		if (len < size)
		{
			strcpy(buffer, addrstr);
		}

		break;

	case VT_LIST:
	case VT_TABLE:
		len = var_dump_list_or_table(v, buffer, size);
		break;

	case VT_POINTER:
		len = snprintf(buffer, size, "(%p)", v->v_data);
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


var_t *
var_table_getva(var_type_t type, var_t *table, va_list ap)
{
	char *key;
	var_t *v = table;

	for(;;) {
		key = va_arg(ap, char *);

		if (key == NULL) {
			break;
		}

		if (v->v_type != VT_TABLE) {
			log_warning("var_table_getv: key \"%s\" is not a"
				" table", key);
			return NULL;
		}

		v = var_table_lookup(v, key);
		if (v == NULL) {
			log_debug("var_table_getv: no data for key \"%s\"",
				key);
			return NULL;
		}
	}

	if (v->v_type != type) {
		log_warning("var_table_getv: type mismatch for key \"%s\"", key);
		return NULL;
	}

	return v;
}


var_t *
var_table_getv(var_type_t type, var_t *table, ...)
{
	va_list ap;
	var_t *v;

	va_start(ap, table);

	v = var_table_getva(type, table, ap);
	
	va_end(ap);

	return v;
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
var_table_set_new(var_t *table, var_type_t type, char *name, void *data,
	int flags)
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
var_table_setv(var_t *table, ...)
{
	va_list ap;
	var_type_t type;
	char *name;
	void *data;
	int flags;

	va_start(ap, table);

	while ((type = va_arg(ap, var_type_t))) {
		name = va_arg(ap, char *);
		data = va_arg(ap, void *);
		flags = va_arg(ap, int);

		if (var_table_set_new(table, type, name, data, flags)) {
			log_warning("var_table_setv: var_table_set_new failed");
			return -1;
		}
	}

	return 0;
}


int
var_table_printstr(var_t *table, char *buffer, int len, char *format)
{
	static const char *braces = "{}";
	char *p, *q;
	int i, pos;
	var_t *v;

	/*
	 * Check len
	 */
	if (len <= 0) {
		goto error;
	}

	/*
	 * Get a copy of format
	 */
	format = strdup(format);
	if (format == NULL) {
		log_warning("var_table_printstr: strdup");
		goto error;
	}

	/*
	 * We use strncat. Make sure buffer starts and ends terminated.
	 */
	buffer[0] = 0;
	buffer[--len] = 0;

	for (i = 0, p = format;; ++i, p = q + 1) {
		q = strchr(p, braces[i % 2]);

		if (q == NULL) {
			/*
			 * No more opening braces
			 */
			if (i % 2 == 0) {
				strncat(buffer, p, len);
				break;
			}

			log_error("var_table_printstr: unmatched \"{\" in"
				" format string");
			goto error;
		}

		*q = 0;

		/*
		 * Inline Text
		 */
		if (i % 2 == 0) {
			strncat(buffer, p, len);
			continue;
		}

		pos = strlen(buffer);
		v = var_table_lookup(table, p);
		if (v == NULL) {
			log_notice("var_table_printstr: no entry for \"%s\"",
				p);
			snprintf(buffer + pos, len - pos, "{%s}", p);
			continue;
		}

		var_dump_data(v, buffer + pos, len - pos);
	}

	free(format);

	return 0;

error:
	if (format) {
		free(format);
	}

	return -1;
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
var_list_append_new(var_t *list, var_type_t type, char *name, void *data,
	int flags)
{
	var_t *v;

	v = var_create(type, name, data, flags);
	if (v == NULL) {
		log_warning("var_list_append_new: var_create failed");
		return -1;
	}

	return var_list_append(list, v);
}


int
var_table_list_append(var_t *table, char *listname, var_t *v)
{
	var_t *list;
	ht_t *ht = table->v_data;

	list = var_table_lookup(table, listname);
	if (list == NULL)
	{
		if ((list = var_create(VT_LIST, listname, NULL,
		    VF_COPYNAME | VF_CREATE)) == NULL)
		{
			log_warning("var_table_list_append: var_create failed");
			return -1;
		}

		if (ht_insert(ht, list)) {
			log_warning("var_table_list_append: ht_insert failed");
			var_delete(list);
			return -1;
		}
	}

	return var_list_append(list, v);
}


int
var_table_list_append_new(var_t *table, var_type_t type, char *name, void *data,
	int flags)
{
	var_t *list;
	ht_t *ht = table->v_data;

	if ((list = var_table_lookup(table, name)) == NULL) {
		if ((list = var_create(VT_LIST, name, NULL,
		    VF_COPYNAME | VF_CREATE)) == NULL)
		{
			log_warning("var_table_list_append_new: var_create failed");
			return -1;
		}

		if (ht_insert(ht, list)) {
			log_warning("var_table_list_append_new: ht_insert failed");
			var_delete(list);
			return -1;
		}
	}

	if (var_list_append_new(list, type, name, data, flags)) {
		log_warning("var_table_list_append_new: var_list_append_new failed");
		return -1;
	}

	return 0;
}


var_t *
var_scheme_create(char *scheme, ...)
{
	va_list ap;
	var_t *v = NULL, *list = NULL;
	var_type_t type;
	int flags;
	char *name;
	
	list = var_create(VT_LIST, scheme, NULL, VF_COPYNAME | VF_CREATE);
	if (list == NULL) {
		log_warning("var_list_scheme: var_create failed");
		goto error;
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

		v = var_create(type, name, NULL, flags);
		if (v == NULL) {
			log_warning("var_list_scheme: var_create failed");
			goto error;
		}

		if (var_list_append(list, v) == -1) {
			log_warning("var_list_scheme: var_list_append failed");
			goto error;
		}
	};

	va_end(ap);

	return list;


error:
	va_end(ap);

	if(v) {
		var_delete(v);
	}

	if(list) {
		var_delete(list);
	}

	return NULL;
}


var_t *
var_list_scheme(var_t *scheme, ...)
{
	va_list ap;
	var_t *item, *new = NULL, *list = NULL;
	int flags;
	void *data;

	va_start(ap, scheme);

	list = var_create(VT_LIST, scheme->v_name, NULL,
		VF_KEEPNAME | VF_CREATE);

	if (list == NULL) {
		log_warning("var_list_scheme: var_create_failed");
		goto error;
	}

	ll_rewind(scheme->v_data);
	while ((item = ll_next(scheme->v_data))) {

		flags = item->v_flags;
		flags |= VF_KEEPDATA | VF_KEEPNAME;
		flags &= ~(VF_COPYNAME | VF_COPYDATA);

		data = va_arg(ap, void *);

		new = var_create(item->v_type, item->v_name, data, flags);

		if (new == NULL) {
			log_warning("var_list_scheme: var_create failed");
			goto error;
		}

		if (var_list_append(list, new) == -1) {
			log_warning("var_list_scheme: var_list_append failed");
			goto error;
		}
	}

	va_end(ap);

	return list;


error:

	va_end(ap);

	if (new) {
		var_delete(new);
	}

	if (list) {
		var_delete(list);
	}

	return NULL;
}


int
var_list_dereference(var_t *list, ...)
{
	va_list ap;
	var_t *v;
	void **p;

	if (list->v_type != VT_LIST) {
		log_warning("var_list_dereference: bad type");
		return -1;
	}

	va_start(ap, list);

	ll_rewind(list->v_data);
	while ((v = ll_next(list->v_data))) {
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

int
var_table_dereference(var_t *table, ...)
{
	va_list ap;
	char *name;
	void **p;

	if (table->v_type != VT_TABLE) {
		log_warning("var_table_dereference: bad type");
		return -1;
	}

	va_start(ap, table);

	while ((name = va_arg(ap, char *))) {
		p = va_arg(ap, void **);
		*p = var_table_get(table, name);
	}

	va_end(ap);

	return 0;
}

var_t *
var_scheme_refcopy(var_t *scheme, ...)
{
	va_list ap;
	var_t *item, *arg, *new = NULL, *list = NULL;
	int flags;

	va_start(ap, scheme);

	list = var_create(VT_LIST, scheme->v_name, NULL,
		VF_KEEPNAME | VF_CREATE);

	if (list == NULL) {
		log_warning("var_scheme_refcopy: var_create_failed");
		goto error;
	}

	ll_rewind(scheme->v_data);
	while ((item = ll_next(scheme->v_data))) {

		flags = item->v_flags;
		flags |= VF_KEEPDATA | VF_KEEPNAME;
		flags &= ~(VF_COPYNAME | VF_COPYDATA);

		arg = va_arg(ap, void *);

		new = var_create(item->v_type, item->v_name, arg, flags);

		if (new == NULL) {
			log_warning("var_scheme_refcopy: var_create failed");
			goto error;
		}

		if (var_list_append(list, new) == -1) {
			log_warning("var_scheme_refcopy: var_list_append failed");
			goto error;
		}
	}

	va_end(ap);

	return list;


error:

	va_end(ap);

	if (new) {
		var_delete(new);
	}

	if (list) {
		var_delete(list);
	}

	return NULL;
}


static var_compact_t *
var_compact_create(void)
{
	var_compact_t *vc;

	vc = (var_compact_t *) malloc(sizeof(var_compact_t));
	if (vc == NULL) {
		log_warning("var_compact_create: malloc");
		return NULL;
	}

	memset(vc, 0, sizeof(var_compact_t));

	return vc;
}

void
var_compact_delete(var_compact_t *vc)
{
	if (vc->vc_key) {
		free(vc->vc_key);
	}

	if (vc->vc_data) {
		free(vc->vc_data);
	}

	free(vc);

	return;
}


static int
var_compress_data(char **buffer, int *len, var_t *v)
{
	char *p;
	int n;

	n = var_data_size(v);

	p = realloc(*buffer, *len + n);
	if (p == NULL) {
		log_warning("var_compress_data: realloc");
		return -1;
	}

	memcpy(p + *len, v->v_data, n);

	*buffer = p;
	*len += n;

	return *len;
}

var_compact_t *
var_compress(var_t *v)
{
	var_compact_t *vc = NULL;
	var_t *item;
	char **p;
	int *i;

	if(v->v_type != VT_LIST) {
		log_warning("var_compress: bad type");
		return NULL;
	}

	vc = var_compact_create();
	if (vc == NULL) {
		log_warning("var_compress: var_compact_create failed");
		goto error;
	}

	/*
	 * Build two buffers for keys and values.
	 */
	ll_rewind(v->v_data);
	while ((item = ll_next(v->v_data))) {
		if (item->v_data == NULL) {
			continue;
		}

		p = item->v_flags & VF_KEY ? &vc->vc_key : &vc->vc_data;
		i = item->v_flags & VF_KEY ? &vc->vc_klen : &vc->vc_dlen;

		if (var_compress_data(p, i, item) == -1) {
			log_warning("var_record_pack: var_compress_data"
				" failed");
			goto error;
		}
	}

	return vc;


error:

	if(vc) {
		free(vc);
	}

	return NULL;
}


var_t *
var_decompress(var_compact_t *vc, var_t *scheme)
{
	var_t *v, *item = NULL, *list = NULL;
	void *p;
	int k = 0, d = 0;
	int *i;

	if(scheme->v_type != VT_LIST) {
		log_warning("var_decompress: bad type");
		goto error;
	}

	list = var_create(VT_LIST, NULL, NULL, VF_CREATE);

	if(list == NULL) {
		log_warning("var_decompress: var_create failed");
		goto error;
	}

	ll_rewind(scheme->v_data);
	while ((item = ll_next(scheme->v_data))) {
		p = item->v_flags & VF_KEY ? vc->vc_key : vc->vc_data;
		i = item->v_flags & VF_KEY ? &k : &d;

		v = var_create(item->v_type, item->v_name, p + *i,
			VF_COPYNAME | VF_COPYDATA | item->v_flags);

		if(v == NULL) {
			log_warning("var_decompress: var_create failed");
			goto error;
		}

		*i += var_data_size(v);

		if(var_list_append(list, v) == -1) {
			log_warning("var_decompress: var_list_append failed");
			goto error;
		}
	}

	return list;


error:

	if (v) {
		var_delete(v);
	}

	if (list) {
		var_delete(list);
	}

	return NULL;
}


static var_t *
var_as_int(var_t *v)
{
	VAR_INT_T i;
	VAR_FLOAT_T *f;
	var_t *copy;

	switch (v->v_type)
	{
	case VT_FLOAT:
		f = v->v_data;
		i = *f;
		break;

	case VT_STRING:
		errno = 0;

		i = strtol(v->v_data, NULL, 10);

		/*
		 * Arbitrary strings should return a true value
		 */
		if (errno && strcmp(v->v_data, "0"))
		{
			i = strlen(v->v_data);
		}

		break;

	default:
		log_error("var_as_int: bad type");
		return NULL;
	}

	copy = var_create(VT_INT, v->v_name, &i, VF_COPY);
	if (copy == NULL)
	{
		log_error("var_as_int: var_create failed");
	}

	return copy;
}


static var_t *
var_as_float(var_t *v)
{
	VAR_FLOAT_T f;
	VAR_INT_T *i;
	var_t *copy;

	switch (v->v_type)
	{
	case VT_INT:
		i = v->v_data;
		f = *i;
		break;

	case VT_STRING:
		f = strtof(v->v_data, NULL);
		break;

	default:
		log_error("var_as_int: bad type");
		return NULL;
	}

	copy = var_create(VT_FLOAT, v->v_name, &f, VF_COPY);
	if (copy == NULL)
	{
		log_error("var_as_int: var_create failed");
	}

	return copy;
}


static var_t *
var_as_string(var_t *v)
{
	char buffer[BUFLEN];
	var_t *copy;

	if (var_dump_data(v, buffer, sizeof buffer) == -1)
	{
		log_error("var_as_string: var_dump_data failed");
		return NULL;
	}

	copy = var_create(VT_STRING, NULL, buffer, VF_COPY);
	if (copy == NULL)
	{
		log_error("var_as_string: var_create failed");
	}

	return copy;
}


var_t *
var_cast_copy(var_type_t type, var_t *v)
{
	switch (type)
	{
	case VT_INT:
		return var_as_int(v);

	case VT_FLOAT:
		return var_as_float(v);

	case VT_STRING:
		return var_as_string(v);

	default:
		log_error("var_cast_copy: bad type");
	}

	return NULL;
}
