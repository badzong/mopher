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
#define DNSBL_BUCKETS 32
#define DNSBL_NAME "dnsbl"

static sht_t *dnsbl_table;

static int
dnsbl_register(char *name, char *domain)
{
	if (sht_insert(dnsbl_table, name, domain))
	{
		log_error("dnsbl_register: sht_insert failed");
		return -1;
	}

	return 0;
}

int
dnsbl_list(milter_stage_t stage, char *name, var_t *attrs)
{
	var_t *list;

	list = vtable_list_get(attrs, DNSBL_NAME);
	if (list == NULL)
	{
		log_error("dnsbl_query: vtable_list_get failed");
		return -1;
	}

	return 0;
}

static int
dnsbl_ipv6_query(char *buffer, int size, char *addr, char *domain)
{
	static const char zeroes[] = "0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.";
	int len;
	char *p, *end;
	int byte = 0;
	int blocks;

	len = strlen(zeroes) + strlen(domain) + 1;
	if (size < len)
	{
		log_error("dnsbl_ipv6_query: buffer exhausted");
		return -1;
	}

	end = addr + strlen(addr) - 1;

	// Count colons to calculate missing blocks
	for (blocks = 0, p = addr; (p = strchr(p, ':')); ++blocks, ++p);

	if (addr[0] == ':')
	{
		--blocks;
	}
	if (*end == ':')
	{
		--blocks;
	}

	// All missing bytes are zero
	strcpy(buffer, zeroes);
	strcat(buffer, domain);

	for (p = end; p >= addr; --p)
	{
		// Transcribe regular byte
		if (*p != ':')
		{
			*(buffer + (byte * 2)) = *p;
			++byte;

			continue;
		}

		// Colon found. Calculate missing bytes
		if (byte % 4)
		{
			byte += 4 - (byte % 4);
		}

		// Double colon. Calculate missing blocks
		if (*(p - 1) == ':')
		{
			byte += (8 - blocks) * 4;
			--p;
		}
	}

	util_tolower(buffer);

	return 0;
}

static int
dnsbl_ipv4_query(char *buffer, int size, char *addr, char *domain)
{
	char addrbytes[16];
	char *b[4];
	char *p;
	int i, n;

	if (strlen(addr) + 1 > sizeof addrbytes)
	{
		log_error("dbsbl_ipv4_query: buffer exhausted");
		return -1;
	}
	strcpy(addrbytes, addr);
		
	/*
	 * Split the address bytes
	 */
	for(i = 0, p = addrbytes; i < 4 && p != NULL; p = strchr(p, '.'), ++i)
	{
		if(*p == '.')
		{
			*p++ = 0;
		}

		b[i] = p;
	}

	/*
	 * Build query string
	 */
	n = snprintf(buffer, size, "%s.%s.%s.%s.%s", b[3], b[2], b[1], b[0], domain);
	if (n >= size)
	{
		log_error("dbsbl_ipv4_query: buffer exhausted");
		return -1;
	}

	return 0;
}

int
dnsbl_query(milter_stage_t stage, char *name, var_t *attrs)
{
	char *domain;
	struct sockaddr_storage *addr;
	char *hostaddr_str = NULL;
	char query[BUFLEN];
	struct addrinfo *ai = NULL;
	struct addrinfo hints;
	void *data = NULL;
	char *resultstr = NULL;
	int e;

	// Make sure dnsbl list exists
	dnsbl_list(stage, name, attrs);

	domain = sht_lookup(dnsbl_table, name);
	if (domain == NULL)
	{
		log_error("dnsbl_query: unknown dnsbl \"%s\"", name);
		goto error;
	}

	if (acl_symbol_dereference(attrs, "hostaddr", &addr, "hostaddr_str",
		&hostaddr_str, NULL))
	{
		log_error("dnsbl_query: acl_symbol_dereference failed");
		goto error;
	}

	/*
         * Address not set. See milter_connect for details.
	 */
	if (addr == NULL)
	{
		log_debug("dnsbl_query: address is NULL");

		if (vtable_set_new(attrs, VT_ADDR, name, NULL, VF_COPYNAME))
		{
			log_error("dnsbl_query: vtable_setv failed");
			goto error;
		}

		return 0;
	}

	if (addr->ss_family == AF_INET6)
	{
		if (dnsbl_ipv6_query(query, sizeof query, hostaddr_str, domain))
		{
			log_error("dnsbl_query: dnsbl_ipv6_query failed");
			goto error;
		}
	}
	else
	{
		if (dnsbl_ipv4_query(query, sizeof query, hostaddr_str, domain))
		{
			log_error("dnsbl_query: dnsbl_ipv4_query failed");
			goto error;
		}
	}

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
		log_error("dnsbl_query: getaddrinfo: %s", gai_strerror(e));
		goto error;
	}

	/*
	 * EAI_NONAME || EAI_NODATA
	 */
	if (e)
	{
		log_debug("dnsbl_query: DNSBL record \"%s\" not found", query);

		if (vtable_set_new(attrs, VT_ADDR, name, NULL, VF_COPYNAME))
		{
			log_error("dnsbl_query: vtable_setv failed");
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
			log_error("dnsbl_query: util_hostaddr failed");
			goto error;
		}

		resultstr = util_addrtostr(data);
		if (resultstr == NULL)
		{
			log_error("dnsbl_query: util_addrtostr failed");
			goto error;
		}

		log_message(LOG_ERR, attrs, "dnsbl_query: addr=%s dnsbl=%s "
		    "result=%s", hostaddr_str, domain, resultstr);

		free(resultstr);

		// Set named symbol
		if (vtable_set_new(attrs, VT_ADDR, name, data, VF_COPYNAME))
		{
			log_error("dnsbl_query: vtable_setv failed");
			goto error;
		}

		// Append name to dnsbl list
		if (vtable_list_append_new(attrs, VT_STRING, DNSBL_NAME,
			name, VF_KEEP))
		{
			log_error("dnsbl_query: vtable_append_new failed");
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
		free(hostaddr_str);
	}

	if (ai)
	{
		freeaddrinfo(ai);
	}

	return -1;
}


int
dnsbl_init(void)
{
	var_t *dnsbl;
	ht_t *config;
	ht_pos_t pos;
	var_t *v;

	dnsbl = cf_get(VT_TABLE, DNSBL_NAME, NULL);
	if (dnsbl == NULL)
	{
		log_notice("dnsbl: init: no DNSBLs configured");
		return 0;
	}

	dnsbl_table = sht_create(DNSBL_BUCKETS, NULL);
	if (dnsbl_table == NULL)
	{
		log_die(EX_SOFTWARE, "dnsbl: init: sht_create failed");
	}
		
	config = dnsbl->v_data;
	ht_start(config, &pos);
	while ((v = ht_next(config, &pos)))
	{
		if (dnsbl_register(v->v_name, v->v_data))
		{
			log_error("dnsbl: init: dnsbl_register failed");
			return -1;
		}
		
		acl_symbol_register(v->v_name, MS_OFF_CONNECT, dnsbl_query,
		    AS_CACHE);
	}

	acl_symbol_register(DNSBL_NAME, MS_OFF_CONNECT, dnsbl_list, AS_CACHE);

	return 0;
}


void
dnsbl_fini(void)
{
	if (dnsbl_table != NULL)
	{
		sht_delete(dnsbl_table);
	}

	return;
}
