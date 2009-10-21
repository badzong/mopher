#include "config.h"

#include <time.h>
#include <stdio.h>

#include "mopher.h"

static dbt_t *greylist_dbt;

void
greylist_init(void)
{
	greylist_dbt = dbt_lookup("greylist");
	if (greylist_dbt == NULL) {
		log_die(EX_SOFTWARE, "greylist_init: greylist not found");
	}

	return;
}


static int
greylist_lookup(var_t *attrs, var_t **record)
{
	var_t *lookup = NULL;
	char *hostaddr;
	char *envfrom;
	char *envrcpt;
	
	if (var_table_dereference(attrs, "milter_hostaddr", &hostaddr,
		"milter_envfrom", &envfrom, "milter_envrcpt", &envrcpt, NULL))
	{
		log_error("greylist_lookup: var_table_dereference failed");
		goto error;
	}

	lookup = var_list_scheme(greylist_dbt->dbt_scheme, hostaddr, envfrom,
		envrcpt, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

	if (lookup == NULL) {
		log_warning("greylist_lookup: var_list_scheme failed");
		goto error;
	}

	if (dbt_db_get(greylist_dbt, lookup, record))
	{
		log_warning("greylist_lookup: var_db_get failed");
		goto error;
	}

	var_delete(lookup);

	return 0;

error:
	if (lookup)
	{
		var_delete(lookup);
	}
	
	return -1;
}


static int
greylist_add(var_t *attrs, acl_delay_t *ad)
{
	var_t *record;
	char *hostaddr;
	char *envfrom;
	char *envrcpt;
	time_t now;
	VAR_INT_T created, updated, delay, visa, valid, retries, passed;
	
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
	updated = now;
	delay = ad->ad_delay;
	visa = 0;
	valid = ad->ad_valid;
	retries = 1;
	passed = 0;

	record = var_scheme_refcopy(greylist_dbt->dbt_scheme, hostaddr,
		envfrom, envrcpt, &created, &updated, &valid, &delay, &retries,
		&visa, &passed);

	if (record == NULL) {
		log_warning("greylist_add: var_scheme_refcopy failed");
		return -1;
	}

	if (dbt_db_set(greylist_dbt, record))
	{
		log_error("greylist_add: dbt_db_set failed");
		return -1;
	}
	
	var_delete(record);

	return 0;
}

greylist_response_t
greylist(var_t *attrs, acl_delay_t *ad)
{
	var_t *record;
	time_t now;
	VAR_INT_T *created;
	VAR_INT_T *updated;
	VAR_INT_T *delay;
	VAR_INT_T *retries;
	VAR_INT_T *visa;
	VAR_INT_T *passed;
	VAR_INT_T *valid;
	greylist_response_t glr = GL_DELAY;

	if (greylist_lookup(attrs, &record))
	{
		log_error("greylist: greylist_lookup failed");
		goto error;
	}

	if (record == NULL)
	{
		log_info("greylist: create new record");
		goto add;
	}

	if (var_list_dereference(record, NULL, NULL, NULL, &created, &updated,
	    &valid, &delay, &retries, &visa, &passed))
	{
		log_warning("greylist: var_list_unpack failed");
		goto error;
	}

	now = time(NULL);
	if (now == -1)
	{
		log_warning("greylist: time");
		goto error;
	}

	/*
	 * Record expired.
	 */
	if (*updated + *valid < now) {
		log_info("greylist: record expired %d seconds ago",
			now - *created - *valid);
		goto add;
	}

	/*
	 * Delay smaller than requested.
	 */
	if (*delay < ad->ad_delay) {
		log_info("greylist: record delay too small. Extension: %d"
			" seconds", ad->ad_delay);
		*delay = ad->ad_delay;
		*visa = 0;
		goto update;
	}

	/*
	 * Valid visa
	 */
	if (*visa) {
		log_info("greylist: valid visa found. expiry: %d seconds",
			*updated + *valid - now);
		*passed += 1;
		glr = GL_PASS;
		goto update;
	}

	/*
	 * Delay passed
	 */
	if (*created + *delay < now) {
		log_info("greylist: delay passed. create visa for %d seconds",
			ad->ad_visa);
		*visa = ad->ad_visa;
		*valid = ad->ad_visa;
		*passed = 1;
		glr = GL_PASS;
		goto update;
	}

	log_info("greylist: remaining delay: %d seconds retries: %d",
		*created + *delay - now, *retries);

	*retries += 1;


update:
	*updated = now;

	if (dbt_db_set(greylist_dbt, record)) {
		log_warning("greylist: DBT_DB_SET failed");
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
