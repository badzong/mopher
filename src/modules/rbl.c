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
#define RBL_NAME "rbl"

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
rbl_list(milter_stage_t stage, char *name, var_t *attrs)
{
	var_t *list;

	list = vtable_list_get(attrs, RBL_NAME);
	if (list == NULL)
	{
		log_error("rbl_query: vtable_list_get failed");
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
	char addrbytes[16];
	char query[BUFLEN];
	struct addrinfo *ai = NULL;
	struct addrinfo hints;
	void *data = NULL;
	char *resultstr = NULL;
	int e;
	char *b[4];
	char *p;
	int i;

	// Make sure rbl list exists
	rbl_list(stage, name, attrs);

	domain = sht_lookup(rbl_table, name);
	if (domain == NULL)
	{
		log_error("rbl_query: unknown rbl \"%s\"", name);
		goto error;
	}

	if (acl_symbol_dereference(attrs, "milter_hostaddr", &addr,
	    "milter_addrstr", &addrstr, NULL))
	{
		log_error("rbl_query: acl_symbol_dereference failed");
		goto error;
	}

	/*
         * Address not set. See milter_connect for details.
	 */
	if (addr == NULL)
	{
		log_debug("rbl_query: address is NULL");

		if (vtable_set_new(attrs, VT_ADDR, name, NULL, VF_COPYNAME))
		{
			log_error("rbl_query: vtable_setv failed");
			goto error;
		}

		return 0;
	}

	/*
	 * No IPv6 support yet.
	 */
	if (addr->ss_family != AF_INET)
	{
		log_message(LOG_ERR, attrs, "rbl_query: %s: address family not"
			" supported", name);

		if (vtable_set_new(attrs, VT_ADDR, name, NULL, VF_COPYNAME))
		{
			log_error("rbl_query: vtable_setv failed");
			goto error;
		}

		return 0;
	}

	strncpy(addrbytes, addrstr, sizeof addrbytes);
	addrbytes[sizeof addrbytes - 1] = 0;
		
	/*
	 * Split the address bytes
	 */
	for(i = 0, p = addrbytes; i < 4 && p != NULL; p = strchr(p, '.'), ++i) {
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
	switch (e)
	{
	case 0:
#ifdef EAI_NONAME
	case EAI_NONAME:
#endif
#ifdef EAI_NODATA
	case EAI_NODATA:
#endif
		break;

	default:
		log_error("rbl_query: getaddrinfo: %s", gai_strerror(e));
		goto error;
	}

	/*
	 * EAI_NONAME || EAI_NODATA
	 */
	if (e)
	{
		log_debug("rbl_query: RBL record \"%s\" not found", query);

		if (vtable_set_new(attrs, VT_ADDR, name, NULL, VF_COPYNAME))
		{
			log_error("rbl_query: vtable_setv failed");
			goto error;
		}
	}

	else
	{
		/*
		 * Hit: Copy first address
		 */
		data = util_hostaddr((struct sockaddr_storage *) ai->ai_addr);
		if (data == NULL)
		{
			log_error("rbl_query: util_hostaddr failed");
			goto error;
		}

		resultstr = util_addrtostr(data);
		if (resultstr == NULL)
		{
			log_error("rbl_query: util_addrtostr failed");
			goto error;
		}

		log_message(LOG_ERR, attrs, "rbl_query: addr=%s rbl=%s "
		    "result=%s", addrstr, domain, resultstr);

		free(resultstr);

		// Set named symbol
		if (vtable_set_new(attrs, VT_ADDR, name, data, VF_COPYNAME))
		{
			log_error("rbl_query: vtable_setv failed");
			goto error;
		}

		// Append name to rbl list
		if (vtable_list_append_new(attrs, VT_STRING, RBL_NAME,
			name, VF_KEEP))
		{
			log_error("rbl_query: vtable_append_new failed");
			goto error;
		}
	}

	if (ai)
	{
		freeaddrinfo(ai);
	}

	return 0;


error:
	if (data)
	{
		free(data);
	}

	if (resultstr)
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
	ht_pos_t pos;
	var_t *v;

	rbl_table = sht_create(RBL_BUCKETS, NULL);

	if (rbl_table == NULL)
	{
		log_error("rbl: init: sht_create failed");
		return 0;
	}
		
	rbl = cf_get(VT_TABLE, RBL_NAME, NULL);
	if (rbl == NULL)
	{
		log_notice("rbl: init: no RBLs configured");
		return 0;
	}

	config = rbl->v_data;
	ht_start(config, &pos);
	while ((v = ht_next(config, &pos)))
	{
		if (rbl_register(v->v_name, v->v_data))
		{
			log_error("rbl: init: rbl_register failed");
			return -1;
		}
		
		acl_symbol_register(v->v_name, MS_OFF_CONNECT, rbl_query,
		    AS_CACHE);
	}

	acl_symbol_register(RBL_NAME, MS_OFF_CONNECT, rbl_list, AS_CACHE);

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
