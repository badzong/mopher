#include <stdio.h>
#include <string.h>

#include <mopher.h>

static char *cast_keys[] = { "INT", "FLOAT", "STRING", NULL };
static var_type_t cast_values[] = { VT_INT, VT_FLOAT, VT_STRING, VT_NULL };

static var_t *
type(int argc, ll_t *args)
{
	var_t *var;
	ll_entry_t *pos;
	char *ts;

	pos = LL_START(args);
	var = ll_next(args, &pos);

	if (argc != 1)
	{
		goto error;
	}

	if (var == NULL)
	{
		ts = "NULL";
	}
	else
	{
		ts = var_type_string(var);
	}

	return var_create(VT_STRING, NULL, ts, VF_COPY | VF_EXP_FREE);

error:
	log_error("type: bad arguments: usage type(expression)");
	return NULL;
}

static var_t *
size(int argc, ll_t *args)
{
	var_t *var;
	ll_entry_t *pos;
	VAR_INT_T len;

	pos = LL_START(args);
	var = ll_next(args, &pos);

	if (argc != 1)
	{
		goto error;
	}

	if (var == NULL)
	{
		len = 0;
	}
	else
	{
		len = var_data_size(var);
	}

	return var_create(VT_INT, NULL, &len, VF_COPY | VF_EXP_FREE);

error:
	log_error("len: bad arguments: usage len(expression)");
	return NULL;
}

static var_t *
cast(int argc, ll_t *args)
{
	var_t *type, *var, *copy;
	VAR_INT_T *vt;
	ll_entry_t *pos;

	pos = LL_START(args);

	type = ll_next(args, &pos);
	var = ll_next(args, &pos);

	if (argc != 2)
	{
		goto error;
	}

	if (type == NULL || var == NULL)
	{
		goto error;
	}

	if (type->v_type != VT_INT)
	{
		goto error;
	}

	vt = type->v_data;

	copy = var_cast_copy(*vt, var);

	if (copy)
	{
		copy->v_flags |= VF_EXP_FREE;
	}

	return copy;


error:

	log_error("cast: bad arguments: usage cast(TYPE, expression)");
	return NULL;
}

static var_t *
null(int argc, ll_t *args)
{
	return EXP_EMPTY;
}

static int
mopher_header(milter_stage_t stage, acl_action_type_t at, var_t *mailspec)
{
	void *ctx;
	int count;
	VAR_INT_T *action;
	var_t *v;
	char **p;
	char buffer[8192];
	int n;
	int len = 0;
	
	static const char *symbols[] = {
		"id",
		"acl_response",
		"acl_rule",
		"acl_line",
		"counter_relay",
		"counter_penpal",
		"greylist_delayed",
		"greylist_passed",
		"tarpit_delayed",
		"dnsbl_str",
		"spf",
		"spamd_score",
		"spamd_spam",
		NULL
	};

	if (stage != MS_EOM)
	{
		return 0;
	}

	if (vtable_dereference(mailspec, "milter_ctx", &ctx, "action", &action,
		NULL) != 2)
	{
		log_error("mopher_header: vtable_dereference failed");
		return -1;
	}

	/*
	 * Action needs to be ACCEPT or CONTINUE
	 */
	if (*action != ACL_ACCEPT && *action != ACL_CONTINUE)
	{
		return 0;
	}

	for (p = symbols; *p != NULL; ++p)
	{
		if (vtable_is_null(mailspec, *p))
		{
			continue;
		}

		v = vtable_lookup(mailspec, *p);
		if (v == NULL)
		{
			continue;
		}

		if (len && len + 2 < sizeof buffer)
		{
			buffer[len] = ' ';
			++len;
			buffer[len] = 0;
		}

		n = var_dump(v, buffer + len, sizeof buffer - len);
		if (n == -1)
		{
			log_error("mopher_header: var_dump failed");
			return -1;
		}

		len += n;
	}

	if (len)
	{
		if (smfi_chgheader(ctx, cf_mopher_header_name, 1, buffer) != MI_SUCCESS)
        	{
                	log_error("mopher_header: smfi_chgheader failed");
                	return -1;
        	}
	}

	return 0;
}


int
base_init(void)
{
	char **k;
	var_type_t *v;
	VAR_INT_T i;

	/*
	 * Functions
	 */
	acl_function_register("type", AF_COMPLEX,
	    (acl_function_callback_t) type);
	acl_function_register("size", AF_COMPLEX,
	    (acl_function_callback_t) size);
	acl_function_register("cast", AF_COMPLEX,
	    (acl_function_callback_t) cast);
	acl_function_register("null", AF_COMPLEX,
	    (acl_function_callback_t) null);

	/*
	 * Constants
	 */
	for (k = cast_keys, v = cast_values; *k && *v; ++k, ++v)
	{
		i = *v;
		acl_constant_register(VT_INT, *k, &i,
		    VF_KEEPNAME | VF_COPYDATA);
	}

	/*
	 * ACL update callback
	 */
	if (cf_mopher_header)
	{
		acl_update_callback(mopher_header);
	}

	return 0;
}
