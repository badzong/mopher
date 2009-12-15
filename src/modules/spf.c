#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <inttypes.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "config.h"

#include <spf2/spf.h>

#include "mopher.h"

static SPF_server_t *spf_server;

static char *spf_static_keys[] = { "SPF_NEUTRAL", "SPF_PASS", "SPF_FAIL",
	"SPF_SOFTFAIL", NULL };
static char *spf_static_values[] = { "neutral", "pass", "fail", "softfail",
	NULL };

static int
spf(milter_stage_t stage, char *name, var_t *attrs)
{
	SPF_request_t *req = NULL;
	SPF_response_t *res = NULL;
	SPF_response_t *res_2mx = NULL;
	char *helo;
	char *envfrom;
	char *envrcpt;
	char *spfstr;
	struct sockaddr_storage *ss;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	int r;

	if (acl_symbol_dereference(attrs, "milter_hostaddr", &ss,
		"milter_envfrom", &envfrom, "milter_envrcpt", &envrcpt,
		"milter_helo", &helo, NULL))
	{
		log_error("spf: acl_symbol_dereference failed");
		goto error;
	}
	sin = (struct sockaddr_in *) ss;
	sin6 = (struct sockaddr_in6 *) ss;

	req = SPF_request_new(spf_server);
	if (req == NULL) {
		log_error("spf: SPF_request_new failed");
		goto error;
	}

	/*
	 * Set client address
	 */
	if (ss->ss_family == AF_INET6) {
		r = SPF_request_set_ipv6(req, sin6->sin6_addr);
	}
	else {
		r = SPF_request_set_ipv4(req, sin->sin_addr);
	}

	if (r) {
		log_error("spf: SPF_request_set_ip failed");
		goto error;
	}

	/*
	 * Set helo
	 */
	r = SPF_request_set_helo_dom(req, helo);
	if (r) {
		log_error("spf: SPF_request_set_helo_dom failed");
		goto error;
	}

	/*
	 * Set envelope from
	 */
	r = SPF_request_set_env_from(req, envfrom);
	if (r) {
		log_error("spf_query: SPF_request_set_env_from failed");
		goto error;
	}

	/*
	 * Perform SPF query
	 */
	SPF_request_query_mailfrom(req, &res);

	if(SPF_response_result(res) == SPF_RESULT_PASS) {
		goto result;
	}

	/*
	 * If SPF fails check if we received the email from a secondary mx.
	 */
	SPF_request_query_rcptto(req, &res_2mx, envrcpt);

	if(SPF_response_result(res_2mx) != SPF_RESULT_PASS) {
		goto result;
	}

	/*
	 * Secondary mx
	 */
	log_notice("spf: \"%s\" is a secodary mx for \"%s\"", helo, envrcpt);

	goto exit;


result:
	spfstr = (char *) SPF_strresult(SPF_response_result(res));
	if (spfstr == NULL) {
		log_error("spf: SPF_strresult failed");
		goto error;
	}

	log_debug("spf: helo:%s envfrom:%s spf:%s", helo,
		envfrom, spfstr);

	if (vtable_setv(attrs, VT_STRING, "spf", spfstr,
	    VF_KEEPNAME | VF_KEEPDATA, VT_NULL))
	{
		log_error("spf: vtable_setv failed");
		goto error;
	}


exit:
	SPF_request_free(req);
	SPF_response_free(res);

	if(res_2mx) {
		SPF_response_free(res_2mx);
	}

	return 0;


error:
	if(req) {
		SPF_request_free(req);
	}

	if(res) {
		SPF_response_free(res);
	}

	if(res_2mx) {
		SPF_response_free(res_2mx);
	}

	return -1;
}

int
spf_init(void)
{
	char **k, **v;

	if((spf_server = SPF_server_new(SPF_DNS_CACHE, 0)) == NULL) {
		log_error("spf: init: SPF_server_new failed");
		return -1;
	}

	acl_symbol_register("spf", MS_OFF_ENVFROM, spf);

	for (k = spf_static_keys, v = spf_static_values; *k && *v; ++k, ++v)
	{
		acl_constant_register(VT_STRING, *k, *v,
		    VF_KEEPNAME | VF_KEEPDATA);
	}


	return 0;
}


void
spf_fini(void)
{
	SPF_server_free(spf_server);

	return;
}
