#include <stdlib.h>
#include <string.h>

#include "mopher.h"

#define ACL_BUCKETS 256
#define ACL_LOGLEN 1024


extern FILE *acl_in;
extern int acl_parse(void);

static sht_t *acl_tables;
static sht_t *acl_symbols;

static ll_t *acl_update_callbacks;

static acl_handler_stage_t acl_action_handlers[] = {
    { NULL,	NULL,	MS_ANY },		/* ACL_NULL 	*/ 
    { NULL,	NULL,	MS_ANY },		/* ACL_NONE 	*/
    { NULL,	NULL,	MS_ANY },		/* ACL_CONTINUE	*/
    { NULL,	NULL,	MS_ANY },		/* ACL_REJECT	*/
    { NULL,	NULL,	MS_ANY },		/* ACL_DISCARD	*/
    { NULL,	NULL,	MS_ANY },		/* ACL_ACCEPT	*/
    { NULL,	NULL,	MS_ANY },		/* ACL_TEMPFAIL	*/
    { acl_jump,	free,	MS_ANY },		/* ACL_JUMP	*/
    { acl_set,	NULL,	MS_ANY },		/* ACL_SET	*/
    { acl_log,	free,	MS_ANY },		/* ACL_LOG	*/
    { greylist,	free,	MS_OFF_ENVRCPT },	/* ACL_GREYLIST	*/
    { tarpit,	NULL,	MS_ANY }		/* ACL_TARPIT	*/
};


static void
acl_action_delete(acl_action_t *aa)
{
	acl_action_delete_t delete;

	delete = acl_action_handlers[aa->aa_type].ah_delete;
	if (delete && aa->aa_data)
	{
		delete(aa->aa_data);
	}

	free(aa);

	return;
}

static acl_action_t *
acl_action_create(acl_action_type_t type, void *data)
{
	acl_action_t *aa;

	aa = (acl_action_t *) malloc(sizeof (acl_action_t));
	if (aa == NULL)
	{
		log_error("acl_action_create: malloc");
		return NULL;
	}

	aa->aa_type = type;
	aa->aa_data = data;

	return aa;
}

acl_action_t *
acl_action(acl_action_type_t type, void *data)
{
	acl_action_t *aa;

	aa = acl_action_create(type, data);
	if (aa == NULL)
	{
		log_die(EX_SOFTWARE, "acl_action: acl_action_create failed");
	}

	return aa;
}


static acl_rule_t *
acl_rule_create(exp_t *exp, acl_action_t *aa)
{
	acl_rule_t *ar;

	ar = (acl_rule_t *) malloc(sizeof (acl_rule_t));
	if (ar == NULL)
	{
		log_error("acl_rule_create: malloc");
		return NULL;
	}

	ar->ar_expression = exp;
	ar->ar_action = aa;

	return ar;
}


static void
acl_rule_delete(acl_rule_t *ar)
{
	if (ar->ar_action)
	{
		acl_action_delete(ar->ar_action);
	}

	free(ar);

	return;
}


static void
acl_rules_delete(ll_t *rules)
{
	if (rules)
	{
		ll_delete(rules, (ll_delete_t) acl_rule_delete);
	}

	return;
}


void
acl_append(char *table, exp_t *exp, acl_action_t *aa)
{
	ll_t *rules;
	acl_rule_t *ar;

	rules = sht_lookup(acl_tables, table);

	/*
	 * Create rules if table not exists
	 */
	if (rules == NULL)
	{
		rules = ll_create();
		if (rules == NULL)
		{
			log_die(EX_SOFTWARE, "acl_append: ll_create failed");
		}

		if (sht_insert(acl_tables, table, rules))
		{
			log_die(EX_SOFTWARE, "acl_append: sht_insert failed");
		}
	}

	/*
	 * Create rule
	 */
	ar = acl_rule_create(exp, aa);
	if (ar == NULL)
	{
		log_die(EX_SOFTWARE, "acl_append: acl_rule_create failed");
	}

	/*
	 * Append rule to rules
	 */
	if (LL_INSERT(rules, ar) == -1)
	{
		log_die(EX_SOFTWARE, "acl_append: LL_INSERT failed");
	}

	/*
	 * table is stduped by acl_lex.l
	 */
	free(table);

	return;
}


static void
acl_symbol_delete(acl_symbol_t *as)
{
	if (as->as_type == AS_CONSTANT)
	{
		var_delete(as->as_data);
	}

	free(as);

	return;
}

static acl_symbol_t *
acl_symbol_create(acl_symbol_type_t type, char *name, milter_stage_t stages,
    void *data)
{
	acl_symbol_t *as;

	as = (acl_symbol_t *) malloc(sizeof (acl_symbol_t));
	if (as == NULL)
	{
		log_die(EX_OSERR, "acl_symbol_create: malloc");
	}

	as->as_type = type;
	as->as_stages = stages;
	as->as_data = data;

	return as;
}


static void
acl_symbol_insert(char *symbol, acl_symbol_t *as)
{
	if (sht_insert(acl_symbols, symbol, as))
	{
		log_die(EX_SOFTWARE, "acl_symbol_register: sht_insert failed");
	}
	
	return;
}


void
acl_symbol_register(char *name, milter_stage_t stages,
    acl_symbol_callback_t callback)
{
	acl_symbol_t *as;

	as = acl_symbol_create(AS_SYMBOL, name, stages, callback);
	acl_symbol_insert(name, as);
	
	log_debug("acl_symbol_register: \"%s\" registered", name);

	return;
}


void
acl_constant_register(var_type_t type, char *name, void *data, int flags)
{
	var_t *v;
	acl_symbol_t *as;

	v = var_create(type, name, data, flags);
	if (v == NULL)
	{
		log_die(EX_SOFTWARE, "acl_constant_register: var_create "
		    "failed");
	}

	as = acl_symbol_create(AS_CONSTANT, name, MS_ANY, v);
	acl_symbol_insert(name, as);
	
	log_debug("acl_constant_register: \"%s\" registered", name);

	return;
}

void
acl_function_register(char *name, acl_function_callback_t callback)
{
	acl_symbol_t *as;

	as = acl_symbol_create(AS_FUNCTION, name, MS_ANY, callback);
	acl_symbol_insert(name, as);
	
	log_debug("acl_function_register: \"%s\" registered", name);

	return;
}


acl_function_callback_t
acl_function_lookup(char *name)
{
	acl_symbol_t *as;

	as = sht_lookup(acl_symbols, name);
	if (as == NULL)
	{
		log_error("acl_function_lookup: unknwon function \"%s\"",
		    name);
		return NULL;
	}

	if (as->as_type != AS_FUNCTION)
	{
		log_error("acl_function_lookup: \"%s\" not a function", name);
		return NULL;
	}

	return as->as_data;
}


acl_symbol_t *
acl_symbol_lookup(char *name)
{
	acl_symbol_t *as;

	as = sht_lookup(acl_symbols, name);
	if (as == NULL)
	{
		log_debug("acl_symbol_lookup: unknown symbol \"%s\"", name);
		return NULL;
	}

	return as;
}


var_t *
acl_symbol_get(var_t *mailspec, char *name)
{
	acl_symbol_t *as;
	acl_symbol_callback_t callback;
	char *stagename;
	VAR_INT_T *stage;
	var_t *v;

	as = acl_symbol_lookup(name);
	if (as == NULL)
	{
		log_error("acl_symbol_get: acl_symbol_lookup failed");
		return NULL;
	}

	stage = vtable_get(mailspec, "milter_stage");
	if (stage == NULL)
	{
		log_debug("acl_symbol_get: milter stage not set");
		return NULL;
	}


	if ((as->as_stages & *stage) == 0)
	{
		stagename = vtable_get(mailspec, "milter_stagename");
		log_notice("acl_symbol_get: symbol \"%s\" not available at %s",
		    name, stagename);

		return NULL;
	}

	/*
	 * Check type. acl_symbol_get should only be used for constants and
	 * symbols.
	 */
	switch (as->as_type)
	{
	case AS_CONSTANT:
	case AS_SYMBOL:
		break;

	default:
		log_error("acl_symbol_get: bad type");
		return NULL;
	}
	
	/*
	 * Return constant
	 */
	if (as->as_type == AS_CONSTANT)
	{
		return as->as_data;
	}

	/*
	 * Lookup symbol
	 */
	v = vtable_lookup(mailspec, name);
	if (v)
	{
		return v;
	}

	callback = as->as_data;
	if (callback == NULL)
	{
		log_error("acl_symbol_get: \"%s\" is not set and has no "
		    "callback", name);
		return NULL;
	}

	if (callback(*stage, name, mailspec))
	{
		log_error("acl_symbol_get: callback for \"%s\" failed",
		    name);
		return NULL;
	}

	v = vtable_lookup(mailspec, name);
	if (v == NULL)
	{
		log_error("acl_symbol_get: symbol \"%s\" not set", name);
	}

	return v;
}


int
acl_symbol_dereference(var_t *mailspec, ...)
{
	va_list ap;
	char *name;
	void **data;
	var_t *v;
	int errors = 0;

	va_start(ap, mailspec);

	for(;;)
	{
		name = va_arg(ap, char *);
		if (name == NULL)
		{
			break;
		}

		data = va_arg(ap, void **);

		v = acl_symbol_get(mailspec, name);

		if (v == NULL)
		{
			++errors;
			*data = NULL;
		}
		else
		{
			*data = v->v_data;
		}
	}

	return errors;
}


void
acl_log_delete(acl_log_t *al)
{
	if (al->al_exp)
	{
		exp_delete(al->al_exp);
	}

	free(al);
}


acl_log_t *
acl_log_create(exp_t *exp)
{
	acl_log_t *al;

	al = (acl_log_t *) malloc(sizeof (acl_log_t));
	if (al == NULL)
	{
		log_die(EX_OSERR, "acl_log: malloc");
	}

	al->al_exp = exp;
	al->al_level = cf_acl_log_level;

	return al;
}


acl_log_t *
acl_log_level(acl_log_t *al, int level)
{
	al->al_level = level;

	return al;
}


acl_action_type_t
acl_log(milter_stage_t stage, char *stagename, var_t *mailspec, acl_log_t *al)
{
	var_t *v;
	char buffer[ACL_LOGLEN];

	v = exp_eval(al->al_exp, mailspec);
	if (v == NULL)
	{
		log_error("acl_log: exp_eval failed");
		return ACL_ERROR;
	}

	if (v->v_type != VT_STRING)
	{
		var_dump_data(v, buffer, sizeof buffer);
		log_log(al->al_level, buffer);
	}
	else
	{
		log_log(al->al_level, v->v_data);
	}

	exp_free(v);

	return ACL_NONE;
}


acl_action_type_t
acl_jump(milter_stage_t stage, char *stagename, var_t *mailspec, char *table)
{
	log_debug("acl_jump: jump to \"%s\"", table);

	return acl(stage, table, mailspec);
}


acl_action_type_t
acl_set(milter_stage_t stage, char *stagename, var_t *mailspec, exp_t *exp)
{
	var_t *v;

	v = exp_eval(exp, mailspec);
	if (v == NULL)
	{
		log_error("acl_set: exp_eval failed");
		return ACL_ERROR;
	}

	return ACL_NONE;
}


void
acl_update_callback(acl_update_t callback)
{
	if (acl_update_callbacks == NULL)
	{
		acl_update_callbacks = ll_create();
		if (acl_update_callbacks == NULL)
		{
			log_die(EX_SOFTWARE,
			    "acl_update_register: ll_create failed");
		}
	}

	if (LL_INSERT(acl_update_callbacks, callback) == -1)
	{
		log_die(EX_SOFTWARE, "acl_update_callback: LL_INSERT failed");
	}

	return;
}


static void
acl_update(milter_stage_t stage, acl_action_type_t action, var_t *mailspec)
{
	acl_update_t callback;

	ll_rewind(acl_update_callbacks);
	while ((callback = ll_next(acl_update_callbacks)))
	{
		if (callback(stage, action, mailspec))
		{
			log_error("acl_update: update callback failed");
		}
	}

	return;
}
	

acl_action_type_t
acl(milter_stage_t stage, char *stagename, var_t *mailspec)
{
	ll_t *rules;
	acl_rule_t *ar;
	acl_action_t *aa;
	acl_action_type_t response;
	acl_action_handler_t action_handler;
	int i;

	log_debug("acl: stage \"%s\"", stagename);

	/*
	 * Lookup table
	 */
	rules = sht_lookup(acl_tables, stagename);
	if (rules == NULL)
	{
		log_info("acl: no rules for \"%s\": continue", stagename);
		return ACL_CONTINUE;
	}

	ll_rewind(rules);
	for (i = 1; (ar = ll_next(rules)); ++i)
	{
		/*
		 * Expression doesn't match
		 */
		if (!exp_true(ar->ar_expression, mailspec))
		{
			continue;
		}

		log_debug("acl: rule %d in \"%s\" matched", i, stagename);

		aa = ar->ar_action;

		/*
		 * Check if action is allowed at this stage
		 */
		if ((acl_action_handlers[aa->aa_type].ah_stages & stage) == 0)
		{
			log_debug("acl: rule %d in \"%s\": forbidden action",
			    i, stagename);
			return ACL_ERROR;
		}
		
		/*
		 * Action needs evaluation
		 */
		action_handler = acl_action_handlers[aa->aa_type].ah_handler;
		if (action_handler)
		{
			response = action_handler(stage, stagename, mailspec,
			    ar->ar_action->aa_data);
		}
		else
		{
			response = aa->aa_type;
		}

		if (response == ACL_NONE)
		{
			continue;
		}

		if (response == ACL_ERROR)
		{
			log_error("acl: rule number %d in table \"%s\" failed",
			    i, stagename);
		}

		acl_update(stage, response, mailspec);

		return response;
	}

	/*
	 * No rule matched
	 */
	log_info("acl: no match in \"%s\": continue", stagename);

	acl_update(stage, ACL_CONTINUE, mailspec);

	return ACL_CONTINUE;
}

void
acl_init(void)
{
	acl_tables = sht_create(ACL_BUCKETS, (sht_delete_t) acl_rules_delete);
	if (acl_tables == NULL)
	{
		log_die(EX_SOFTWARE, "acl_init: sht_create failed");
	}

	acl_symbols = sht_create(ACL_BUCKETS, (sht_delete_t) acl_symbol_delete);
	if (acl_symbols == NULL)
	{
		log_die(EX_SOFTWARE, "acl_init: sht_create failed");
	}

	/*
	 * Initialize exp
	 */
	exp_init();

	return;
}


void
acl_read(char *mail_acl)
{
	/*
	 * run parser
	 */
	parser(mail_acl, &acl_in, acl_parse);

	return;
}


void
acl_clear(void)
{
	exp_clear();

	if (acl_tables)
	{
		sht_delete(acl_tables);
	}

	if (acl_symbols)
	{
		sht_delete(acl_symbols);
	}

	if (acl_update_callbacks)
	{
		ll_delete(acl_update_callbacks, NULL);
	}


	return;
}
