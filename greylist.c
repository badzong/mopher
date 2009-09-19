#include <time.h>
#include <stdio.h>

#include "log.h"
#include "table.h"
#include "acl.h"
#include "greylist.h"


static table_t *greylist_table;

void
greylist_init(void)
{
	greylist_table = table_lookup("greylist");
	if (greylist_table == NULL) {
		log_die(EX_SOFTWARE, "greylist_init: greylist not found");
	}

	return;
}


static var_t *
greylist_lookup(var_t *attrs)
{
	var_t *lookup, *record;
	char *hostaddr;
	char *envfrom;
	char *envrcpt;
	
	if (var_table_dereference(attrs, "milter_hostaddr", &hostaddr,
		"milter_envfrom", &envfrom, "milter_envrcpt", &envrcpt, NULL))
	{
		log_error("greylist_lookup: var_table_dereference failed");
		return NULL;
	}

	lookup = var_list_schema(greylist_table->t_schema, hostaddr, envfrom,
		envrcpt, NULL, NULL, NULL, NULL, NULL, NULL);

	if (lookup == NULL) {
		log_warning("greylist_lookup: var_record_build failed");
		return NULL;
	}

	record = dbt_get(greylist_table->t_dbt, lookup);

	var_delete(lookup);

	return record;
}


static int
greylist_update(var_t *record)
{
	return dbt_set(greylist_table->t_dbt, record);
}

static int
greylist_add(var_t *attrs, acl_delay_t *ad)
{
	var_t *record;
	char *hostaddr;
	char *envfrom;
	char *envrcpt;
	time_t now;
	VAR_INT_T created, delay, visa, valid, retries, delivered;
	int r;
	
	if (var_table_dereference(attrs, "milter_hostaddr", &hostaddr,
		"milter_envfrom", &envfrom, "milter_envrcpt", &envrcpt, NULL))
	{
		log_error("greylist_lookup: var_table_dereference failed");
		return -1;
	}

	now = time(NULL);
	if (now == -1) {
		log_warning("greylist: time");
		return -1;
	}

	created = now;
	delay = ad->ad_delay;
	visa = 0;
	valid = ad->ad_valid;
	retries = 1;
	delivered = 0;

	record = var_schema_refcopy(greylist_table->t_schema, hostaddr,
		envfrom, envrcpt, &created, &delay, &retries, &visa,
		&delivered, &valid);

	if (record == NULL) {
		log_warning("greylist_add: var_schema_refcopy failed");
		return -1;
	}

	 r = dbt_set(greylist_table->t_dbt, record);

	 var_delete(record);

	 return r;
}

greylist_response_t
greylist(var_t *attrs, acl_delay_t *ad)
{
	var_t *record;
	time_t now;
	VAR_INT_T *created;
	VAR_INT_T *delay;
	VAR_INT_T *retries;
	VAR_INT_T *visa;
	VAR_INT_T *delivered;
	VAR_INT_T *valid;
	greylist_response_t glr = GL_DELAY;

	record = greylist_lookup(attrs);
	if (record == NULL) {
		log_info("greylist: create new record");
		goto add;
	}

	if (var_list_dereference(record, NULL, NULL, NULL, &created, &delay,
		&retries, &visa, &delivered, &valid)) {
		log_warning("greylist: var_list_unpack failed");
		goto error;
	}

	now = time(NULL);
	if (now == -1) {
		log_warning("greylist: time");
		goto error;
	}

	/*
	 * Record expired.
	 */
	if (*created + *valid < now) {
		log_info("greylist: record expired");
		goto add;
	}

	/*
	 * Delay smaller than requested.
	 */
	if (*delay < ad->ad_delay) {
		log_info("greylist: record delay too small");
		*delay = ad->ad_delay;
		goto update;
	}

	/*
	 * Valid visa
	 */
	if (*visa) {
		log_info("greylist: valid visa found");
		*delivered += 1;
		glr = GL_PASS;
		goto update;
	}

	/*
	 * Delay passed
	 */
	if (*created + *delay < now) {
		log_info("greylist: delay passed. create visa");
		*visa = 1;
		*delivered = 1;
		glr = GL_PASS;
		goto update;
	}

	log_info("greylist: remaining delay: %d seconds retries: %d",
		*created + *delay - now, *retries);

	*retries += 1;


update:

	if (greylist_update(record)) {
		log_warning("greylist: greylist_update failed");
		goto error;
	}

	if (record) {
		var_delete(record);
	}

	return glr;

add:

	if (greylist_add(attrs, ad)) {
		log_warning("greylist: greylist_add failed");
		goto error;
	}

	if (record) {
		var_delete(record);
	}

	return glr;

error:

	if (record) {
		var_delete(record);
	}

	return GL_ERROR;
}
