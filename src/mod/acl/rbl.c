#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <malloc.h>

#include "mopher.h"

#define BUFLEN 1024
#define RBL_BUCKETS 32

typedef struct rbl {
	char *rbl_name;
	char *rbl_domain;
} rbl_t;

static ht_t *rbl_table;

static void
rbl_delete(rbl_t *rbl)
{
	free(rbl);

	return;
}


static int
rbl_register(char *name, char *domain)
{
	rbl_t *rbl;

	rbl = (rbl_t *) malloc(sizeof (rbl_t));
	if (rbl == NULL)
	{
		log_error("rbl_register: malloc");
		return -1;
	}

	rbl->rbl_name = name;
	rbl->rbl_domain = domain;

	if (ht_insert(rbl_table, rbl))
	{
		log_error("rbl_register: ht_insert failed");
		rbl_delete(rbl);
		return -1;
	}

	return 0;
}


static hash_t
rbl_hash(rbl_t *rbl)
{
	printf("HASH: %s\n", rbl->rbl_name);
	return HASH(rbl->rbl_name, strlen(rbl->rbl_name));
}


static int
rbl_match(rbl_t *r1, rbl_t *r2)
{
	if (strcmp(r1->rbl_name, r2->rbl_name) == 0)
	{
		return 1;
	}

	return 0;
}


int
rbl_query(milter_stage_t stage, char *name, var_t *attrs)
{
	rbl_t lookup, *rbl;
	struct sockaddr_storage *addr;
	char *addrstr = NULL;
	char query[BUFLEN];
	struct addrinfo *ai = NULL;
	struct addrinfo hints;
	void *data;
	int flags;
	int e;
	char *b[4];
	char *p;
	int i;

	lookup.rbl_name = name;

	rbl = ht_lookup(rbl_table, &lookup);
	if (rbl == NULL)
	{
		log_error("rbl_query: unknown rbl \"%s\"", name);
		goto error;
	}

	if (var_table_dereference(attrs, "milter_hostaddr", &addr, NULL))
	{
		log_error("rbl_query: var_table_dereference failed");
		goto error;
	}

	/*
	 * No IPv6 support yet.
	 */
	if (addr->ss_family != AF_INET)
	{
		log_error("rbl_query: address family not supported");
		goto error;
	}
		
	addrstr = util_addrtostr(addr);
	if (addrstr == NULL)
	{
		log_error("rbl_query: util_addrtostr failed");
		goto error;
	}

	/*
	 * Split the address bytes
	 */
	for(i = 0, p = addrstr; i < 4 && p != NULL; p = strchr(p, '.'), ++i) {
		if(*p == '.') {
			*p++ = 0;
		}

		b[i] = p;
	}

	/*
	 * Build query string
	 */
	snprintf(query, BUFLEN, "%s.%s.%s.%s.%s", b[3], b[2], b[1], b[0],
	    rbl->rbl_domain);

	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	e = getaddrinfo(query, NULL, &hints, &ai);
	if(e && e != EAI_NONAME)
	{
		log_error("rbl_query: getaddrinfo: %s", gai_strerror(e));
		goto error;
	}

	/*
	 * Hit: Copy first address
	 */
	if (e == 0)
	{
		data = ai->ai_addr;
		flags = VF_COPYDATA;
	}
	else
	{
		data = NULL;
		flags = VF_KEEPDATA;
	}

	if (var_table_setv(attrs, VT_ADDR, name, data, flags, VT_NULL))
	{
		log_error("rbl_query: var_table_setv failed");
		goto error;
	}

	free(addrstr);
	freeaddrinfo(ai);

	return 0;


error:

	if (addrstr)
	{
		free(addrstr);
	}

	if (ai)
	{
		freeaddrinfo(ai);
	}

	return -1;
}


int
init(void)
{
	var_t *rbl;
	ht_t *config;
	var_t *v;

	rbl_table = ht_create(RBL_BUCKETS, (ht_hash_t) rbl_hash,
	    (ht_match_t) rbl_match, (ht_delete_t) rbl_delete);

	if (rbl_table == NULL)
	{
		log_error("rbl: init: ht_create failed");
		return 0;
	}
		
	rbl = cf_get(VT_TABLE, "rbl", NULL);
	if (rbl == NULL)
	{
		log_warning("rbl: init: no RBLs configured");
		return 0;
	}

	config = rbl->v_data;
	for (ht_rewind(config); (v = ht_next(config));)
	{
		if (rbl_register(v->v_name, v->v_data))
		{
			log_error("rbl: init: rbl_register failed");
			return -1;
		}
		
		if (acl_symbol_register(AS_CALLBACK, v->v_name, MS_CONNECT,
		    rbl_query))
		{
			log_error("rbl: init: acl_symbol_register failed");
			return -1;
		}
	}

	return 0;
}
