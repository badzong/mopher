#include <stdint.h>
#include <string.h>
#include <netinet/in.h>
#include <unistd.h>
#include <mopher.h>

#define P0F_QUERY_MAGIC		0x50304601
#define P0F_RESP_MAGIC		0x50304602
#define P0F_STATUS_BADQUERY	0x00
#define P0F_STATUS_OK		0x10
#define P0F_STATUS_NOMATCH	0x20
#define P0F_ADDR_IPV4		0x04
#define P0F_ADDR_IPV6		0x06
#define P0F_STR_MAX		31
#define P0F_MATCH_FUZZY		0x01
#define P0F_MATCH_GENERIC	0x02

#define P0F_S_STATUS		"p0f_status"
#define P0F_S_FIRST_SEEN	"p0f_first_seen"
#define P0F_S_LAST_SEEN		"p0f_last_seen"
#define P0F_S_TOTAL_CONN	"p0f_total_conn"
#define P0F_S_UPTIME_MIN	"p0f_uptime_min"
#define P0F_S_UP_MOD_DAYS	"p0f_up_mod_days"
#define P0F_S_LAST_NAT		"p0f_last_nat"
#define P0F_S_LAST_CHG		"p0f_last_chg"
#define P0F_S_DISTANCE		"p0f_distance"
#define P0F_S_OS_NAME		"p0f_os_name"
#define P0F_S_OS_FLAVOR		"p0f_os_flavor"
#define P0F_S_OS_GENERIC	"p0f_os_generic"
#define P0F_S_OS_FUZZY		"p0f_os_fuzzy"
#define P0F_S_HTTP_NAME		"p0f_http_name"
#define P0F_S_HTTP_FLAVOR	"p0f_http_flavor"
#define P0F_S_HTTP_FAKE		"p0f_http_fake"
#define P0F_S_HTTP_OS_MISMATCH	"p0f_http_os_mismatch"
#define P0F_S_HTTP_LEGIT	"p0f_http_legit"
#define P0F_S_LINK_TYPE		"p0f_link_type"
#define P0F_S_LANGUAGE		"p0f_language"

struct p0f_api_query {
	uint32_t magic;				/* Must be P0F_QUERY_MAGIC		*/
	uint8_t	addr_type;			/* P0F_ADDR_*				*/
	uint8_t	addr[16];			/* IP address (big endian left align)	*/
} __attribute__((packed));

struct p0f_api_response {
	uint32_t magic;				/* Must be P0F_RESP_MAGIC		*/
	uint32_t status;			/* P0F_STATUS_*				*/
	uint32_t first_seen;			/* First seen (unix time)		*/
	uint32_t last_seen;			/* Last seen (unix time)		*/
	uint32_t total_conn;			/* Total connections seen		*/
	uint32_t uptime_min;			/* Last uptime (minutes)		*/
	uint32_t up_mod_days;			/* Uptime modulo (days)			*/
	uint32_t last_nat;			/* NAT / LB last detected (unix time)	*/
	uint32_t last_chg;			/* OS chg last detected (unix time)	*/
	int16_t  distance;			/* System distance			*/
	uint8_t  bad_sw;			/* Host is lying about U-A / Server	*/
	uint8_t  os_match_q;			/* Match quality			*/
	char     os_name[P0F_STR_MAX + 1];	/* Name of detected OS			*/
	char     os_flavor[P0F_STR_MAX + 1];	/* Flavor of detected OS		*/
	char     http_name[P0F_STR_MAX + 1];	/* Name of detected HTTP app		*/
	char     http_flavor[P0F_STR_MAX + 1];	/* Flavor of detected HTTP app		*/
	char     link_type[P0F_STR_MAX + 1];	/* Link type				*/
	char     language[P0F_STR_MAX + 1];	/* Language				*/
} __attribute__((packed));

static char *p0f_symbols[] = {
	P0F_S_STATUS, P0F_S_FIRST_SEEN, P0F_S_LAST_SEEN, P0F_S_TOTAL_CONN,
	P0F_S_UPTIME_MIN, P0F_S_UP_MOD_DAYS, P0F_S_LAST_NAT, P0F_S_LAST_CHG,
	P0F_S_DISTANCE, P0F_S_OS_NAME, P0F_S_OS_FLAVOR, P0F_S_OS_GENERIC,
	P0F_S_OS_FUZZY, P0F_S_HTTP_NAME	, P0F_S_HTTP_FLAVOR,
	P0F_S_HTTP_FAKE, P0F_S_HTTP_OS_MISMATCH, P0F_S_HTTP_LEGIT,
	P0F_S_LINK_TYPE, P0F_S_LANGUAGE, NULL
};

static char *p0f_const_keys[] = { "P0F_OK", "P0F_NO_MATCH", "P0F_BAD_QUERY", NULL };
static VAR_INT_T p0f_const_values[] = { P0F_STATUS_OK, P0F_STATUS_NOMATCH, P0F_STATUS_BADQUERY, 0 };

static pthread_mutex_t	p0f_mutex = PTHREAD_MUTEX_INITIALIZER;

int
p0f_query(milter_stage_t stage, char *name, var_t *attrs)
{
	struct p0f_api_query q;
	struct p0f_api_response r;
	int sock = 0;
	var_sockaddr_t *addr;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	int n;
	int rcode = -1;

	VAR_INT_T status;
	VAR_INT_T first_seen;
	VAR_INT_T last_seen;
	VAR_INT_T total_conn;
	VAR_INT_T os_generic;
	VAR_INT_T os_fuzzy;
	VAR_INT_T http_fake;
	VAR_INT_T http_os_mismatch;
	VAR_INT_T http_legit;
	VAR_INT_T uptime_min;
	VAR_INT_T up_mod_days;
	VAR_INT_T last_nat;
	VAR_INT_T last_chg;
	VAR_INT_T distance;

	VAR_INT_T *statusp = NULL;
	VAR_INT_T *first_seenp = NULL;
	VAR_INT_T *last_seenp = NULL;
	VAR_INT_T *total_connp = NULL;
	VAR_INT_T *os_genericp = NULL;
	VAR_INT_T *os_fuzzyp = NULL;
	VAR_INT_T *http_fakep = NULL;
	VAR_INT_T *http_os_mismatchp = NULL;
	VAR_INT_T *http_legitp = NULL;
	VAR_INT_T *uptime_minp = NULL;
	VAR_INT_T *up_mod_daysp = NULL;
	VAR_INT_T *last_natp = NULL;
	VAR_INT_T *last_chgp = NULL;
	VAR_INT_T *distancep = NULL;

	char *os_namep = NULL;
	char *os_flavorp = NULL;
	char *http_namep = NULL;
	char *http_flavorp = NULL;
	char *link_typep = NULL;
	char *languagep = NULL;

	addr = vtable_get(attrs, "hostaddr");
	if (addr == NULL)
	{
		log_error("p0f_query: vtable_get failed");
		goto exit;
	}

	sin = (void *) addr;
	sin6 = (void *) addr;

	memset(&q, 0, sizeof q);

	switch (sin->sin_family)
	{
	case AF_INET:
		memcpy(q.addr, &sin->sin_addr.s_addr, sizeof sin->sin_addr.s_addr);
		q.addr_type = P0F_ADDR_IPV4;
		break;

	case AF_INET6:
		memcpy(q.addr, &sin6->sin6_addr.s6_addr, sizeof sin6->sin6_addr.s6_addr);
		q.addr_type = P0F_ADDR_IPV6;
		break;

	default:
		log_error("p0f_query: unknown address family");
		goto exit;
	}

	q.magic = P0F_QUERY_MAGIC;

	// Do not query p0f parallel
	if (pthread_mutex_lock(&p0f_mutex))
	{
		log_error("p0f_query: pthread_mutex_lock failed");
		goto exit;
	}

	sock = sock_connect_config("p0f_socket");
	if (sock == -1)
	{
		log_error("p0f_query: sock_connect_config failed");
		goto exit;
	}

	n = write(sock, &q, sizeof(struct p0f_api_query));
	if (n != sizeof(struct p0f_api_query))
	{
		log_sys_error("p0f_query: write failed");
		goto exit;
	}

	n = read(sock, &r, sizeof(struct p0f_api_response));
	if (n != sizeof(struct p0f_api_response))
	{
		log_sys_error("p0f_query: read failed");
		goto exit;
	}
	
	close(sock);
	sock = 0;

	if (r.magic != P0F_RESP_MAGIC)
	{
		log_error("p0f_query: bad magic");
		goto exit;
	}

	switch (r.status)
	{
	case P0F_STATUS_BADQUERY:
		log_error("p0f_query: P0f did not understand the query");
		goto exit;
		
	case P0F_STATUS_NOMATCH:
		log_debug("p0f_query: no matching host in p0f cache");
		rcode = 0;
		goto exit;

	default:
		break;
	}

	status		= r.status;
	first_seen	= r.first_seen;
	last_seen	= r.last_seen;
	total_conn	= r.total_conn;

	statusp = &status;
	first_seenp = &first_seen;
	last_seenp = &last_seen;
	total_connp = &total_conn;

	if (r.os_name[0])
	{
		os_namep = r.os_name;
		os_flavorp = r.os_flavor;

         	os_generic = r.os_match_q & P0F_MATCH_GENERIC;
		os_genericp = &os_generic;

         	os_fuzzy = r.os_match_q & P0F_MATCH_FUZZY;
		os_fuzzyp = &os_fuzzy;

		log_message(LOG_ERR, attrs, "p0f: %s %s generic=%d fuzzy=%d",
			os_namep, os_flavorp, os_generic, os_fuzzy);
	}

	if (r.http_name[0])
	{
		http_namep = r.http_name;
		http_flavorp = r.http_flavor;

		switch(r.bad_sw)
		{
		case 2:
			http_fake = 1;
			http_fakep = &http_fake;
			break;

		case 0:
			http_legit = 1;
			http_legitp = &http_legit;
			break;

		default:
			http_os_mismatch = 1;
			http_os_mismatchp = &http_os_mismatch;
			break;
		}
	}

	if (r.link_type[0])
	{
		link_typep = r.link_type;
	}

	if (r.language[0])
	{
		languagep = r.language;
	}

	if (r.uptime_min)
	{
		uptime_min = r.uptime_min;
		uptime_minp = &uptime_min;

		up_mod_days = r.up_mod_days;
		up_mod_daysp = &up_mod_days;
	}

	if (r.last_nat)
	{
		last_nat = r.last_nat;
		last_natp = &last_nat;
	}

	if (r.last_chg)
	{
		last_chg = r.last_chg;
		last_chgp = &last_chg;
	}

	if (r.distance > -1)
	{
		distance = r.distance;
		distancep = &distance;
	}

	if (vtable_setv(attrs,
		VT_INT,		P0F_S_STATUS,		statusp,		VF_KEEPNAME | VF_COPYDATA,
		VT_INT,		P0F_S_FIRST_SEEN,	first_seenp,		VF_KEEPNAME | VF_COPYDATA,
		VT_INT,		P0F_S_LAST_SEEN,	last_seenp,		VF_KEEPNAME | VF_COPYDATA,
		VT_INT,		P0F_S_TOTAL_CONN,	total_connp,		VF_KEEPNAME | VF_COPYDATA,
		VT_INT,		P0F_S_UPTIME_MIN,	uptime_minp,		VF_KEEPNAME | VF_COPYDATA,
		VT_INT,		P0F_S_UP_MOD_DAYS,	up_mod_daysp,	VF_KEEPNAME | VF_COPYDATA,
		VT_INT,		P0F_S_LAST_NAT,		last_natp,		VF_KEEPNAME | VF_COPYDATA,
		VT_INT,		P0F_S_LAST_CHG,		last_chgp,		VF_KEEPNAME | VF_COPYDATA,
		VT_INT,		P0F_S_DISTANCE,		distancep,		VF_KEEPNAME | VF_COPYDATA,
		VT_STRING,	P0F_S_OS_NAME,		os_namep,		VF_KEEPNAME | VF_COPYDATA,
		VT_STRING,	P0F_S_OS_FLAVOR,	os_flavorp,		VF_KEEPNAME | VF_COPYDATA,
		VT_INT,		P0F_S_OS_GENERIC,	os_genericp,		VF_KEEPNAME | VF_COPYDATA,
		VT_INT,		P0F_S_OS_FUZZY,		os_fuzzyp,		VF_KEEPNAME | VF_COPYDATA,
		VT_STRING,	P0F_S_HTTP_NAME,	http_namep,		VF_KEEPNAME | VF_COPYDATA,
		VT_STRING,	P0F_S_HTTP_FLAVOR,	http_flavorp,		VF_KEEPNAME | VF_COPYDATA,
		VT_INT,		P0F_S_HTTP_FAKE,	http_fakep,		VF_KEEPNAME | VF_COPYDATA,
		VT_INT,		P0F_S_HTTP_OS_MISMATCH,	http_os_mismatchp,	VF_KEEPNAME | VF_COPYDATA,
		VT_INT,		P0F_S_HTTP_LEGIT,	http_legitp,		VF_KEEPNAME | VF_COPYDATA,
		VT_STRING,	P0F_S_LINK_TYPE,	link_typep,		VF_KEEPNAME | VF_COPYDATA,
		VT_STRING,	P0F_S_LANGUAGE,		languagep,		VF_KEEPNAME | VF_COPYDATA,
		VT_NULL,	NULL,			NULL,			0))
	{
		log_error("p0f_query: vtable_setv failed");
		goto exit;
	}

	rcode = 0;

exit:
	if (sock)
	{
		close(sock);
	}

	pthread_mutex_unlock(&p0f_mutex);

	return rcode;
}

int
p0f_init(void)
{
	char **p;
	VAR_INT_T *i;
	
	// Symbols
	for (p = p0f_symbols; *p; ++p)
	{
		acl_symbol_register(*p, MS_CONNECT, p0f_query, AS_CACHE);
	}

	// Constants
	for (p = p0f_const_keys, i = p0f_const_values; *p && *i; ++p, ++i)
	{
		acl_constant_register(VT_INT, *p, i, VF_KEEP);
	}

	return 0;
}
