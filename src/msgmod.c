#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <mopher.h>

static char *msgmod_targets[] = {
	"from",
	"rcpt",
	"header",
	"body",
	NULL
};

msgmod_target_t
msgmod_get_target(char *id)
{
	char **p;
	int i = 0;

	for (p = msgmod_targets; *p != NULL; ++p, ++i)
	{
		if(strcasecmp(*p, id) == 0)
		{
			free(id);
			return i;
		}
	}

	parser_error("bad keyword: %s", id);

	return -1;
}

msgmod_callback_t
msgmod_get_callback(msgmod_mod_t mod, msgmod_target_t target)
{
	switch(mod)
	{
	case MO_ADD:
		switch(target)
		{
			case MT_RCPT: return msgmod_add_rcpt;
			case MT_HEADER: return msgmod_add_header;
			default: break;
		}
		break;
	case MO_INS:
		switch(target)
		{
			case MT_HEADER: return msgmod_insert_header;
			default: break;
		}
		break;
	case MO_CHG:
		switch(target)
		{
			case MT_FROM: return msgmod_change_from;
			case MT_HEADER: return msgmod_change_header;
			case MT_BODY: return msgmod_change_body;
			default: break;
		}
		break;
	case MO_DEL:
		switch(target)
		{
			case MT_RCPT: return msgmod_delete_rcpt;
			case MT_HEADER: return msgmod_delete_header;
			default: break;
		}
		break;
	default:
		break;
	}

	return NULL;
}

msgmod_t *
msgmod_create(msgmod_mod_t mod, msgmod_target_t target, ...)
{
	msgmod_t *mm;
	va_list ap;
	exp_t *exp;

	mm = (msgmod_t *) malloc(sizeof (msgmod_t));
	if (mm == NULL)
	{
		log_sys_die(EX_OSERR, "msgmod_create: malloc");
	}

	mm->mm_callback = msgmod_get_callback(mod, target);
	if (mm->mm_callback == NULL)
	{
		parser_error("syntax error (msgmod callback)");

		// Not reached.
		return NULL;
	}
	
	mm->mm_args = ll_create();
	if (mm->mm_args == NULL)
	{
		log_die(EX_SOFTWARE, "msgmod_create: ll_create failed");
	}

	va_start(ap, target);
	while ((exp = va_arg(ap, exp_t *)))
	{
		if (LL_INSERT(mm->mm_args, exp) == -1)
		{
			log_die(EX_SOFTWARE, "msgmod_create: LL_INSERT failed");
		}
	}

	return mm;
}

int
msgmod_add_header(void *ctx, int argc, var_t *args[])
{
	char *headerf;
	char *headerv;

	if (argc != 2)
	{
		log_error("msgmod_add_header: bad argument count");
		return -1;
	}

	headerf = args[0]->v_data;
	headerv = args[1]->v_data;

	if (smfi_addheader(ctx, headerf, headerv) != MI_SUCCESS)
	{
		log_error("msgmod_add_header: smfi_addheader failed");
		return -1;
	}

	log_debug("msgmod_add_header: %s: %s", headerf, headerv);

	return 0;
}


int
msgmod_change_header(void *ctx, int argc, var_t *args[])
{
	char *headerf;
	char *headerv;
	long index = 1;

	if (argc < 2 || argc > 3)
	{
		log_error("msgmod_change_header: bad argument count");
		return -1;
	}

	headerf = args[0]->v_data;
	headerv = args[1]->v_data;

	if (argc == 3)
	{
		index = atol(args[2]->v_data);
	}

	if (smfi_chgheader(ctx, headerf, index, headerv) != MI_SUCCESS)
	{
		log_error("msgmod_change_header: smfi_chgheader failed");
		return -1;
	}

	log_debug("msgmod_change_header: %s: %s (%d)", headerf, headerv,
		index);

	return 0;
}


int
msgmod_delete_header(void *ctx, int argc, var_t *args[])
{
	char *headerf;

	if (argc != 1)
	{
		log_error("msgmod_delete_header: bad argument count");
		return -1;
	}

	headerf = args[0]->v_data;

	if (smfi_chgheader(ctx, headerf, 1, NULL) != MI_SUCCESS)
	{
		log_error("msgmod_delete_header: smfi_chgheader failed");
		return -1;
	}

	log_debug("msgmod_delete_header: %s (1)", headerf);

	return 0;
}


int
msgmod_insert_header(void *ctx, int argc, var_t *args[])
{
	char *headerf;
	char *headerv;
	long index = 0;

	if (argc < 2 || argc > 3)
	{
		log_error("msgmod_insert_header: bad argument count");
		return -1;
	}

	headerf = args[0]->v_data;
	headerv = args[1]->v_data;

	if (argc == 3)
	{
		index = atol(args[2]->v_data);
	}

	if (smfi_insheader(ctx, index, headerf, headerv) != MI_SUCCESS)
	{
		log_error("msgmod_insert_header: smfi_insheader failed");
		return -1;
	}

	log_debug("msgmod_insert_header: %s: %s (%d)", headerf, headerv,
		index);

	return 0;
}

int
msgmod_change_from(void *ctx, int argc, var_t *args[])
{
	char *from;
	char *esmtp_args = NULL;

	if (argc < 1 || argc > 2)
	{
		log_error("msgmod_change_from: bad argument count");
		return -1;
	}

	from = args[0]->v_data;

	if (argc == 2)
	{
		esmtp_args = args[1]->v_data;
	}

	if (smfi_chgfrom(ctx, from, esmtp_args) != MI_SUCCESS)
	{
		log_error("msgmod_change_from: smfi_chgfrom failed");
		return -1;
	}


	log_debug("msgmod_change_from: %s %s", from,
		esmtp_args? esmtp_args: "");

	return 0;
}
	

int
msgmod_add_rcpt(void *ctx, int argc, var_t *args[])
{
	char *rcpt;
	char *esmtp_args = NULL;
	int r;

	if (argc < 1 || argc > 2)
	{
		log_error("msgmod_add_rcpt: bad argument count");
		return -1;
	}

	rcpt = args[0]->v_data;

	if (argc == 2)
	{
		esmtp_args = args[1]->v_data;
		r = smfi_addrcpt_par(ctx, rcpt, esmtp_args);
	}
	else
	{
		r = smfi_addrcpt(ctx, rcpt);
	}

	if (r != MI_SUCCESS)
	{
		log_error("msgmod_add_rcpt: smfi_addrcpt failed");
		return -1;
	}

	log_debug("msgmod_add_rcpt: %s %s", rcpt,
		esmtp_args? esmtp_args: "");

	return 0;
}


int
msgmod_delete_rcpt(void *ctx, int argc, var_t *args[])
{
	char *rcpt;

	if (argc != 1)
	{
		log_error("msgmod_delete_rcpt: bad argument count");
		return -1;
	}

	rcpt = args[0]->v_data;

	if (smfi_delrcpt(ctx, rcpt) != MI_SUCCESS)
	{
		log_error("mmsgmod_delete_rcpt: smfi_delrcpt failed");
		return -1;
	}

	log_debug("mmsgmod_delete_rcpt: %s", rcpt);

	return 0;
}


int
msgmod_change_body(void *ctx, int argc, var_t *args[])
{
	char *body;
	int size;

	if (argc != 1)
	{
		log_error("msgmod_change_nody: bad argument count");
		return -1;
	}

	body = args[0]->v_data;
	size = strlen(body);

	if (smfi_replacebody(ctx, (void *) body, size) != MI_SUCCESS)
	{
		log_error("msgmod_change_body: smfi_replacebody failed");
		return -1;
	}

	return 0;
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
	exp_t *exp;
	ll_t *ll;
	ll_entry_t *pos;

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
		log_sys_error("msgmod: malloc");
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

		// Cast all aruments to VT_STRING
		if (v->v_type != VT_STRING)
		{
			copy = var_cast_copy(VT_STRING, v);
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

	if (mm->mm_callback(ctx, argc, args))
	{
		log_error("msgmod: mm_callback failed");
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

#ifdef DEBUG

void
msgmod_test(int n)
{
	TEST_ASSERT(msgmod_get_target(strdup("from")) == MT_FROM);
	TEST_ASSERT(msgmod_get_target(strdup("rcpt")) == MT_RCPT);
	TEST_ASSERT(msgmod_get_target(strdup("header")) == MT_HEADER);
	TEST_ASSERT(msgmod_get_target(strdup("body")) == MT_BODY);
	TEST_ASSERT(msgmod_get_target(strdup("FROM")) == MT_FROM);
	TEST_ASSERT(msgmod_get_target(strdup("Header")) == MT_HEADER);

	TEST_ASSERT(msgmod_get_callback(MO_ADD, MT_HEADER) == msgmod_add_header);
	TEST_ASSERT(msgmod_get_callback(MO_CHG, MT_HEADER) == msgmod_change_header);
	TEST_ASSERT(msgmod_get_callback(MO_DEL, MT_HEADER) == msgmod_delete_header);
	TEST_ASSERT(msgmod_get_callback(MO_INS, MT_HEADER) == msgmod_insert_header);
	TEST_ASSERT(msgmod_get_callback(MO_CHG, MT_FROM) == msgmod_change_from);
	TEST_ASSERT(msgmod_get_callback(MO_ADD, MT_RCPT) == msgmod_add_rcpt);
	TEST_ASSERT(msgmod_get_callback(MO_DEL, MT_RCPT) == msgmod_delete_rcpt);
	TEST_ASSERT(msgmod_get_callback(MO_CHG, MT_BODY) == msgmod_change_body);

	TEST_ASSERT(msgmod_get_callback(MO_ADD, MT_BODY) == NULL);
	TEST_ASSERT(msgmod_get_callback(MO_ADD, MT_FROM) == NULL);
	TEST_ASSERT(msgmod_get_callback(MO_CHG, MT_RCPT) == NULL);
	TEST_ASSERT(msgmod_get_callback(MO_INS, MT_FROM) == NULL);

	return;
}

#endif
