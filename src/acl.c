#include <stdlib.h>
#include <string.h>

#include "mopher.h"

#define ACL_BUCKETS 256
#define ACL_LOGLEN 1024


extern FILE *acl_in;
extern int acl_parse(void);

static ht_t *acl_tables;
static ht_t *acl_symbols;

static acl_action_handler_t acl_action_handlers[] = {
	NULL,					/* ACL_NULL 	*/
	NULL,					/* ACL_NONE 	*/
	NULL,					/* ACL_CONTINUE	*/
	NULL,					/* ACL_REJECT	*/
	NULL,					/* ACL_DISCARD	*/
	NULL,					/* ACL_ACCEPT	*/
	NULL,					/* ACL_TEMPFAIL	*/
	(acl_action_handler_t) acl_jump,	/* ACL_JUMP	*/
	(acl_action_handler_t) acl_set,		/* ACL_SET	*/
	(acl_action_handler_t) acl_log,		/* ACL_LOG	*/
	(acl_action_handler_t) greylist,	/* ACL_GREYLIST	*/
	(acl_action_handler_t) tarpit		/* ACL_TARPIT	*/
};


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
	free(ar);

	return;
}


static void
acl_table_delete(acl_table_t *at)
{
	free(at->at_name);

	if (at->at_rules)
	{
		ll_delete(at->at_rules, (ll_delete_t) acl_rule_delete);
	}

	return;
}

static acl_table_t *
acl_table_create(char *name)
{
	acl_table_t *at = NULL;

	at = (acl_table_t *) malloc(sizeof (acl_table_t));
	if (at == NULL)
	{
		log_error("acl_table_create: malloc");
		goto error;
	}

	at->at_name = name;
	at->at_rules = ll_create();

	if (at->at_rules == NULL)
	{
		log_error("acl_table_create: ll_create failed");
		goto error;
	}

	if (ht_insert(acl_tables, at))
	{
		log_error("acl_table_create: ht_insert failed");
		goto error;
	}
	

	return at;

error:

	if (at)
	{
		acl_table_delete(at);
	}

	return NULL;
}


static hash_t
acl_table_hash(acl_table_t *at)
{
	return HASH(at->at_name, strlen(at->at_name));
}


static int
acl_table_match(acl_table_t *at1, acl_table_t *at2)
{
	if (strcmp(at1->at_name, at2->at_name))
	{
		return 0;
	}

	return 1;
}


void
acl_append(char *table, exp_t *exp, acl_action_t *aa)
{
	acl_table_t *at = NULL, lookup;
	acl_rule_t *ar;

	lookup.at_name = table;
	at = ht_lookup(acl_tables, &lookup);

	/*
	 * Create table if not exists
	 */
	if (at == NULL)
	{
		at = acl_table_create(table);
		if (at == NULL)
		{
			log_die(EX_SOFTWARE,
			    "acl_append: acl_table_create failed");
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
	if (LL_INSERT(at->at_rules, ar) == -1)
	{
		log_die(EX_SOFTWARE, "acl_append: LL_INSERT failed");
	}

	return;
}


static void
acl_symbol_delete(acl_symbol_t *as)
{
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
	as->as_name = name;
	as->as_stages = stages;
	as->as_data = data;

	return as;
}


static hash_t
acl_symbol_hash(acl_symbol_t *as)
{
	return HASH(as->as_name, strlen(as->as_name));
}


static int
acl_symbol_match(acl_symbol_t *as1, acl_symbol_t *as2)
{
	if (strcmp(as1->as_name, as2->as_name))
	{
		return 0;
	}

	return 1;
}


void
acl_symbol_insert(acl_symbol_t *as)
{
	if (ht_insert(acl_symbols, as))
	{
		log_die(EX_SOFTWARE, "acl_symbol_register: ht_insert failed");
	}
	
	return;
}


void
acl_symbol_register(char *name, milter_stage_t stages,
    acl_symbol_callback_t callback)
{
	acl_symbol_t *as;

	as = acl_symbol_create(AS_SYMBOL, name, stages, callback);
	acl_symbol_insert(as);
	
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
	acl_symbol_insert(as);
	
	log_debug("acl_constant_register: \"%s\" registered", name);

	return;
}

void
acl_function_register(char *name, acl_function_callback_t callback)
{
	acl_symbol_t *as;

	as = acl_symbol_create(AS_FUNCTION, name, MS_ANY, callback);
	acl_symbol_insert(as);
	
	log_debug("acl_function_register: \"%s\" registered", name);

	return;
}


acl_function_callback_t
acl_function_lookup(char *name)
{
	acl_symbol_t *as, lookup;

	lookup.as_name = name;
	as = ht_lookup(acl_symbols, &lookup);
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
	acl_symbol_t *as, lookup;

	lookup.as_name = name;
	as = ht_lookup(acl_symbols, &lookup);
	
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

	stage = var_table_get(mailspec, "milter_stage");
	if (stage == NULL)
	{
		log_debug("acl_symbol_get: milter stage not set");
		return NULL;
	}


	if ((as->as_stages & *stage) == 0)
	{
		stagename = var_table_get(mailspec, "milter_stagename");
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
	v = var_table_lookup(mailspec, name);
	if (v)
	{
		return v;
	}

	callback = as->as_data;
	if (callback == NULL)
	{
		log_error("acl_symbol_get: \"%s\" is not set and has no "
		    "callback", name);
	}

	if (callback(*stage, name, mailspec))
	{
		log_error("acl_symbol_get: callback for \"%s\" failed",
		    name);
		return NULL;
	}

	v = var_table_lookup(mailspec, name);
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
acl_log(var_t *mailspec, acl_log_t *al)
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

	return ACL_NONE;
}


acl_action_type_t
acl_jump(var_t *mailspec, char *table)
{
	log_debug("acl_jump: jump to \"%s\"", table);

	return acl(table, mailspec);
}


acl_action_type_t
acl_set(var_t *mailspec, exp_t *exp)
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


acl_action_type_t
acl(char *stage, var_t *mailspec)
{
	acl_table_t *at, lookup;
	acl_rule_t *ar;
	acl_action_t *aa;
	acl_action_type_t response;
	acl_action_handler_t action_handler;
	int i;

	log_debug("acl: stage \"%s\"", stage);

	lookup.at_name = stage;

	/*
	 * Lookup table
	 */
	at = ht_lookup(acl_tables, &lookup);
	if (at == NULL)
	{
		log_info("acl: no rules for \"%s\": continue", stage);
		return ACL_CONTINUE;
	}

	ll_rewind(at->at_rules);
	for (i = 1; (ar = ll_next(at->at_rules)); ++i)
	{
		/*
		 * Expression doesn't match
		 */
		if (!exp_true(ar->ar_expression, mailspec))
		{
			continue;
		}

		log_debug("acl: rule %d in \"%s\" matched", i, stage);

		aa = ar->ar_action;

		/*
		 * Action needs evaluation
		 */
		action_handler = acl_action_handlers[aa->aa_type];
		if (action_handler)
		{
			response = action_handler(mailspec,
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
			    i, stage);
		}

		return response;
	}

	/*
	 * No rule matched
	 */
	log_info("acl: no match in \"%s\": continue", stage);

	return ACL_CONTINUE;
}

void
acl_init(char *mail_acl)
{
	acl_symbols = ht_create(ACL_BUCKETS, (ht_hash_t) acl_symbol_hash,
	    (ht_match_t) acl_symbol_match, (ht_delete_t) acl_symbol_delete);

	if (acl_symbols == NULL)
	{
		log_die(EX_SOFTWARE, "acl_symbol_register: ht_create failed");
	}


	acl_tables = ht_create(ACL_BUCKETS, (ht_hash_t) acl_table_hash,
	    (ht_match_t) acl_table_match, (ht_delete_t) acl_table_delete);

	if (acl_tables == NULL)
	{
		log_die(EX_SOFTWARE, "acl_append: ht_create failed");
	}

	/*
	 * Initialize exp
	 */
	exp_init();

	/*
	 * Load table modules
	 */
	MODULE_LOAD_ACL;

	/*
	 * run parser
	 */
	parser(mail_acl, &acl_in, acl_parse);

	return;
}


void
acl_clear(void)
{
	if (acl_tables)
	{
		ht_delete(acl_tables);
	}

	if (acl_symbols)
	{
		ht_delete(acl_symbols);
	}

	exp_clear();

	return;
}
