#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#define __USE_GNU
#include <netdb.h>

#include <arpa/inet.h>
#include <stdlib.h>

#include <mopher.h>

#define BUFLEN 1024
#define RBL_BUCKETS 32

static sht_t *rbl_table;

static int
rbl_register(char *name, char *domain)
{
	if (sht_insert(rbl_table, name, domain))
	{
		log_error("rbl_register: sht_insert failed");
		return -1;
	}

	return 0;
}


int
rbl_query(milter_stage_t stage, char *name, var_t *attrs)
{
	char *domain;
	struct sockaddr_storage *addr;
	char *addrstr = NULL;
	char query[BUFLEN];
	struct addrinfo *ai = NULL;
	struct addrinfo hints;
	void *data = NULL;
	int e;
	char *b[4];
	char *p;
	int i;


	domain = sht_lookup(rbl_table, name);
	if (domain == NULL)
	{
		log_error("rbl_query: unknown rbl \"%s\"", name);
		goto error;
	}

	if (acl_symbol_dereference(attrs, "milter_hostaddr", &addr, NULL))
	{
		log_error("rbl_query: acl_symbol_dereference failed");
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
	snprintf(query, sizeof query, "%s.%s.%s.%s.%s", b[3], b[2], b[1], b[0],
	    domain);

	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	e = getaddrinfo(query, NULL, &hints, &ai);
	if(e && e != EAI_NONAME && e != EAI_NODATA)
	{
		log_error("rbl_query: getaddrinfo: %s", gai_strerror(e));
		goto error;
	}

	/*
	 * Hit: Copy first address
	 */
	if (e == 0)
	{
		data = util_hostaddr((struct sockaddr_storage *) ai->ai_addr);
		if (data == NULL)
		{
			log_error("rbl_query: util_hostaddr failed");
			goto error;
		}

		log_debug("rbl_query: RBL record \"%s\" exists", query);
	}
	else
	{
		data = NULL;
		log_debug("rbl_query: RBL record \"%s\" not found", query);
	}

	if (vtable_setv(attrs, VT_ADDR, name, data, VF_COPYNAME, VT_NULL))
	{
		log_error("rbl_query: vtable_setv failed");
		goto error;
	}

	free(addrstr);
	freeaddrinfo(ai);

	return 0;


error:
	if (data)
	{
		free(data);
	}

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
rbl_init(void)
{
	var_t *rbl;
	ht_t *config;
	var_t *v;

	rbl_table = sht_create(RBL_BUCKETS, NULL);

	if (rbl_table == NULL)
	{
		log_error("rbl: init: sht_create failed");
		return 0;
	}
		
	rbl = cf_get(VT_TABLE, "rbl", NULL);
	if (rbl == NULL)
	{
		log_notice("rbl: init: no RBLs configured");
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
		
		acl_symbol_register(v->v_name, MS_OFF_CONNECT, rbl_query,
		    AS_CACHE);
	}

	return 0;
}


void
rbl_fini(void)
{
	if (rbl_table)
	{
		sht_delete(rbl_table);
	}

	return;
}
