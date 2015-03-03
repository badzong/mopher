#include <GeoIP.h>
#include <mopher.h>

#define GEOIP_DATABASE "geoip_database"
#define GEOIP_SYMBOL "geoip_country_code"

static GeoIP *geoip_handle;
static pthread_mutex_t geoip_mutex = PTHREAD_MUTEX_INITIALIZER;

int
geoip_query(milter_stage_t stage, char *name, var_t *attrs)
{
	char *hostaddr;
	const char *country_code;

	hostaddr = vtable_get(attrs, "hostaddr_str");
	if (hostaddr == NULL)
	{
		log_error("geoip_query: vtable_get failed");
		return -1;
	}

	if (pthread_mutex_lock(&geoip_mutex))
	{
		log_error("geoip_query: pthread_mutex_lock failed");
		return -1;
	}

	country_code = GeoIP_country_code_by_addr(geoip_handle, hostaddr);

	pthread_mutex_unlock(&geoip_mutex);

	if (vtable_set_new(attrs, VT_STRING, GEOIP_SYMBOL,
		(char *) country_code, VF_KEEP))
	{
		log_error("geoip_query: vtable_set_new failed");
		return -1;
	}

	if (country_code == NULL)
	{
		log_message(LOG_DEBUG, attrs, "geoip: no record found");
	}
	else
	{
		log_message(LOG_ERR, attrs, "geoip: country_code=%s", country_code);
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
		return -1;
	}

	acl_symbol_register(GEOIP_SYMBOL, MS_CONNECT, geoip_query,
		AS_CACHE);

	log_error("This product includes GeoLite data created by MaxMind, available "
		"from http://www.maxmind.com.");

	return 0;
}

void
geoip_fini(void)
{
	GeoIP_delete(geoip_handle);

	return;
}
