#include <GeoIP.h>
#include <mopher.h>

#define GEOIP_DATABASE "geoip_database"
#define GEOIPV6_DATABASE "geoipv6_database"
#define GEOIP_CC "geoip_country_code"
#define GEOIP_CN "geoip_country_name"

static GeoIP *geoip_handle;
static GeoIP *geoipv6_handle;
static pthread_mutex_t geoip_mutex = PTHREAD_MUTEX_INITIALIZER;

int
geoip_query(milter_stage_t stage, char *name, var_t *attrs)
{
	char *hostaddr;
	char *country_name = NULL;
	char *country_code = NULL;
	GeoIP *handle;
	int ipv6;
	char *(*cc_by_addr)(GeoIP *, char *);
	char *(*cn_by_addr)(GeoIP *, char *);

	hostaddr = vtable_get(attrs, "hostaddr_str");
	if (hostaddr == NULL)
	{
		log_error("geoip_query: vtable_get failed");
		return -1;
	}

	// Quick and dirty
	ipv6 = strchr(hostaddr, ':')? 1: 0;

	if (ipv6)
	{
		handle = geoipv6_handle;
		cc_by_addr = (void *) GeoIP_country_code_by_addr_v6;
		cn_by_addr = (void *) GeoIP_country_name_by_addr_v6;
	}
	else
	{
		handle = geoip_handle;
		cc_by_addr = (void *) GeoIP_country_code_by_addr;
		cn_by_addr = (void *) GeoIP_country_name_by_addr;
	}
	
	if (handle == NULL)
	{
		log_message(LOG_ERR, attrs, "geoip_query: no database for %s",
			ipv6? "IPv6": "IPv4");
	}
	else
	{
		if (pthread_mutex_lock(&geoip_mutex))
		{
			log_error("geoip_query: pthread_mutex_lock failed");
			return -1;
		}

		country_code = cc_by_addr(handle, hostaddr);
		country_name = cn_by_addr(handle, hostaddr);

		pthread_mutex_unlock(&geoip_mutex);
	}

	if (vtable_setv(attrs,
		VT_STRING, GEOIP_CC, country_code, VF_KEEP,
		VT_STRING, GEOIP_CN, country_name, VF_KEEP,
		NULL))
	{
		log_error("geoip_query: vtable_setv failed");
		return -1;
	}

	if (country_code == NULL)
	{
		log_message(LOG_DEBUG, attrs, "geoip: no record");
	}
	else
	{
		log_message(LOG_ERR, attrs, "geoip: country=%s code=%s", country_name,
			country_code);
	}

	return 0;
}

int
geoip_init(void)
{
	char *path;

	path = cf_get_value(VT_STRING, GEOIP_DATABASE, NULL);
	if (path == NULL)
	{
		log_error("geoip_init: unkown config key %s", GEOIP_DATABASE);
		return -1;
	}

	geoip_handle = GeoIP_open(path, GEOIP_MEMORY_CACHE);
	if (geoip_handle == NULL) {
		log_error("geoip_init: %s: GeoIP_open failed", GEOIP_DATABASE);
	}

	path = cf_get_value(VT_STRING, GEOIPV6_DATABASE, NULL);
	if (path == NULL)
	{
		log_error("geoip_init: unkown config key %s", GEOIPV6_DATABASE);
		return -1;
	}

	geoipv6_handle = GeoIP_open(path, GEOIP_MEMORY_CACHE);
	if (geoipv6_handle == NULL) {
		log_error("geoip_init: %s: GeoIP_open failed", GEOIPV6_DATABASE);
	}

	acl_symbol_register(GEOIP_CC, MS_OFF_CONNECT, geoip_query, AS_CACHE);
	acl_symbol_register(GEOIP_CN, MS_OFF_CONNECT, geoip_query, AS_CACHE);

	log_error("This product includes GeoLite data created by MaxMind, available "
		"from http://www.maxmind.com.");

	return 0;
}

void
geoip_fini(void)
{
	GeoIP_delete(geoip_handle);
	GeoIP_delete(geoip6_handle);

	return;
}
