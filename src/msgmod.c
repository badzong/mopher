#include <stdlib.h>
#include <string.h>

#include <mopher.h>


static var_type_t msgmod_args_addhdr[] = { VT_STRING, VT_STRING, VT_NULL };
static var_type_t msgmod_args_chghdr[] = { VT_STRING, VT_STRING, VT_NULL };
static var_type_t msgmod_args_chghdr_x[] = { VT_STRING, VT_STRING, VT_INT,
    VT_NULL };
static var_type_t msgmod_args_inshdr[] = { VT_STRING, VT_STRING, VT_NULL };
static var_type_t msgmod_args_inshdr_x[] = { VT_STRING, VT_STRING, VT_INT,
    VT_NULL };
static var_type_t msgmod_args_chgfrom[] = { VT_STRING, VT_NULL };
static var_type_t msgmod_args_chgfrom_x[] = { VT_STRING, VT_STRING, VT_NULL };
static var_type_t msgmod_args_addrcpt[] = { VT_STRING, VT_NULL };
static var_type_t msgmod_args_addrcpt_x[] = { VT_STRING, VT_STRING, VT_NULL };
static var_type_t msgmod_args_delrcpt[] = { VT_STRING, VT_NULL };
static var_type_t msgmod_args_chgbody[] = { VT_STRING, VT_INT, VT_NULL };


static var_type_t *msgmod_args[] = {
    	msgmod_args_addhdr,		/* MM_ADDHDR, */
    	msgmod_args_chghdr,		/* MM_CHGHDR, */
    	msgmod_args_chghdr_x,		/* MM_CHGHDR_X, */
    	msgmod_args_inshdr,		/* MM_INSHDR, */
    	msgmod_args_inshdr_x,		/* MM_INSHDR_X, */
    	msgmod_args_chgfrom,		/* MM_CHGFROM, */
    	msgmod_args_chgfrom_x,		/* MM_CHGFROM_X, */
    	msgmod_args_addrcpt,		/* MM_ADDRCPT, */
    	msgmod_args_addrcpt_x,		/* MM_ADDRCPT_X, */
    	msgmod_args_delrcpt,		/* MM_DELRCPT, */
    	msgmod_args_chgbody		/* MM_CHGBODY, */
};


msgmod_t *
msgmod_create(msgmod_type_t type, ...)
{
	msgmod_t *mm;
	va_list ap;
	exp_t *exp;

	mm = (msgmod_t *) malloc(sizeof (msgmod_t));
	if (mm == NULL)
	{
		log_die(EX_OSERR, "msgmod: malloc");
	}

	mm->mm_type = type;
	
	mm->mm_args = ll_create();
	if (mm->mm_args == NULL)
	{
		log_die(EX_SOFTWARE, "msgmod: ll_create failed");
	}
		
	va_start(ap, type);

	while ((exp = va_arg(ap, exp_t *)))
	{
		if (LL_INSERT(mm->mm_args, exp) == -1)
		{
			log_die(EX_SOFTWARE, "msgmod: LL_INSERT failed");
		}
	}

	return mm;
}


void
msgmod_delete(void *data)
{
	msgmod_t *mm = data;

	ll_delete(mm->mm_args, NULL);
	free(mm);

	return;
}


acl_action_type_t
msgmod(milter_stage_t stage, char *stagename, var_t *mailspec, void *data)
{
	msgmod_t *mm = data;
	void *ctx;
	acl_action_type_t action = ACL_ERROR;
	var_t **args = NULL;
	int argc;
	int size;
	var_t *v, *copy;
	int i;
	VAR_INT_T *x;
	exp_t *exp;
	ll_t *ll;
	ll_entry_t *pos;
	var_type_t type;

	/*
	 * Get milter ctx pointer
	 */
	ctx = vtable_get(mailspec, "milter_ctx");
	if (ctx == NULL)
	{
		log_error("msgmod: ctx not set");
		goto error;
	}

	/*
	 * Evaluate arguments
	 */
	argc = mm->mm_args->ll_size;
	size = (argc + 1) * sizeof (var_t *);

	args = (var_t **) malloc(size);
	if (args == NULL)
	{
		log_error("msgmod: malloc");
		goto error;
	}

	memset(args, 0, size);

	ll = mm->mm_args;
	pos = LL_START(ll);

	for (i = 0; i < argc; ++i)
	{
		exp = ll_next(ll, &pos);
		if (exp == NULL)
		{
			log_die(EX_SOFTWARE, "msgmod: empty argument");
		}

		v = exp_eval(exp, mailspec);
		if (v == NULL)
		{
			log_error("msgmod: exp_eval failed");
			goto error;
		}

		/*
		 * Check argument type (cast if neccessary)
		 */
		type = msgmod_args[mm->mm_type][i];
		if (type == VT_NULL)
		{
			log_error("msgmod: bad argument count");
			goto error;
		}

		if (type != v->v_type)
		{
			copy = var_cast_copy(type, v);
			if (copy == NULL)
			{
				log_error("msgmod: var_cast_copy failed");
				goto error;
			}

			exp_free(v);

			/*
			 * args are freed using exp_free. Set VF_EXP_FREE to
			 * free copy.
			 */
			copy->v_flags |= VF_EXP_FREE;
			
			v = copy;
		}
		
		args[i] = v;
	}

	switch (mm->mm_type)
	{
	case MM_ADDHDR:
		if (smfi_addheader(ctx, args[0]->v_data, args[1]->v_data)
		    != MI_SUCCESS)
		{
			log_error("msgmod: smfi_addheader failed");
			goto error;
		}

		log_debug("msgmod: add header: %s", args[0]->v_data);

		break;

	case MM_CHGHDR:
		if (smfi_chgheader(ctx, args[0]->v_data, 1, args[1]->v_data)
		    != MI_SUCCESS)
		{
			log_error("msgmod: smfi_chgheader failed");
			goto error;
		}

		log_debug("msgmod: change header: %s (1)", args[0]->v_data);

		break;


	case MM_CHGHDR_X:
		x = args[3]->v_data;

		if (smfi_chgheader(ctx, args[0]->v_data, *x, args[1]->v_data)
		    != MI_SUCCESS)
		{
			log_error("msgmod: smfi_chgheader failed");
			goto error;
		}

		log_debug("msgmod: change header: %s (%ld)", args[0]->v_data,
		    *x);

		break;
	

	case MM_INSHDR:
		if (smfi_insheader(ctx, 0, args[0]->v_data, args[1]->v_data)
		    != MI_SUCCESS)
		{
			log_error("msgmod: smfi_insheader failed");
			goto error;
		}

		log_debug("msgmod: insert header: %s (0)", args[0]->v_data);

		break;
	

	case MM_INSHDR_X:
		x = args[3]->v_data;

		if (smfi_insheader(ctx, *x, args[0]->v_data, args[1]->v_data)
		    != MI_SUCCESS)
		{
			log_error("msgmod: smfi_insheader failed");
			goto error;
		}

		log_debug("msgmod: insert header: %s (%ld)", args[0]->v_data,
		    *x);

		break;
	

	case MM_CHGFROM:
		if (smfi_chgfrom(ctx, args[0]->v_data, NULL) != MI_SUCCESS)
		{
			log_error("msgmod: smfi_chgfrom failed");
			goto error;
		}

		log_debug("msgmod: change from: %s", args[0]->v_data);

		break;
	

	case MM_CHGFROM_X:
		if (smfi_chgfrom(ctx, args[0]->v_data, args[1]->v_data)
		    != MI_SUCCESS)
		{
			log_error("msgmod: smfi_chgfrom failed");
			goto error;
		}

		log_debug("msgmod: change from: %s (%s)", args[0]->v_data,
		    args[1]->v_data);

		break;
	

	case MM_ADDRCPT:
		if (smfi_addrcpt(ctx, args[0]->v_data) != MI_SUCCESS)
		{
			log_error("msgmod: smfi_addrcpt failed");
			goto error;
		}

		log_debug("msgmod: add rcpt: %s", args[0]->v_data);

		break;
	

	case MM_ADDRCPT_X:
		if (smfi_addrcpt_par(ctx, args[0]->v_data, args[1]->v_data)
		    != MI_SUCCESS)
		{
			log_error("msgmod: smfi_addrcpt_par failed");
			goto error;
		}

		log_debug("msgmod: add rcpt: %s (%s)", args[0]->v_data,
		    args[1]->v_data);

		break;
	

	case MM_DELRCPT:
		if (smfi_delrcpt(ctx, args[0]->v_data) != MI_SUCCESS)
		{
			log_error("msgmod: smfi_delrcpt failed");
			goto error;
		}

		log_debug("msgmod: del rcpt: %s", args[0]->v_data);

		break;
	

	case MM_CHGBODY:
		x = args[1]->v_data;

		if (smfi_replacebody(ctx, args[0]->v_data, *x) != MI_SUCCESS)
		{
			log_error("msgmod: smfi_replacebody failed");
			goto error;
		}

		log_debug("msgmod: change body: %ld bytes", *x);

		break;
	

	default:
		log_error("msgmod: bad type");
		goto error;
	}

	action = ACL_NONE;


error:

	/*
	 * Free args
	 */
	for (i = 0; args[i]; ++i)
	{
		exp_free(args[i]);
	}

	if (args)
	{
		free(args);
	}

	return action;
}
