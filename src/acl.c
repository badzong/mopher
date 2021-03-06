#include <config.h>

#include <stdlib.h>
#include <string.h>

#include <mopher.h>

#define ACL_BUCKETS 256
#define ACL_LOGLEN 1024
#define MAX_RECURSION 256


extern FILE *acl_in;
extern int acl_parse(void);
parser_t acl_parser;

static sht_t *acl_tables;
static sht_t *acl_symbols;

static ll_t *acl_update_callbacks;

static acl_handler_stage_t acl_action_handlers[] = {
    { NULL,		NULL,		MS_ANY | MS_INIT},	/* ACL_NULL 	*/ 
    { NULL,		NULL,		MS_ANY | MS_INIT},	/* ACL_NONE 	*/
    { NULL,		NULL,		MS_ANY | MS_INIT},	/* ACL_RETURN 	*/
    { NULL,		NULL,		MS_ANY | MS_INIT},	/* ACL_CONTINUE	*/
    { NULL,		NULL,		MS_ANY },		/* ACL_REJECT	*/
    { NULL,		NULL,		MS_ANY },		/* ACL_DISCARD	*/
    { NULL,		NULL,		MS_ANY },		/* ACL_ACCEPT	*/
    { NULL,		NULL,		MS_ANY },		/* ACL_TEMPFAIL	*/
    { acl_jump,		free,		MS_ANY | MS_INIT},	/* ACL_JUMP	*/
    { acl_call,		free,		MS_ANY | MS_INIT},	/* ACL_CALL	*/
    { acl_set,		NULL,		MS_ANY | MS_INIT},	/* ACL_SET	*/
    { acl_log,		free,		MS_ANY | MS_INIT},	/* ACL_LOG	*/
    { greylist,		free,		MS_OFF_ENVRCPT },	/* ACL_GREYLIST	*/
    { tarpit,		NULL,		MS_ANY },		/* ACL_TARPIT	*/
    { msgmod,		msgmod_delete,	MS_EOM },		/* ACL_MOD	*/
    { pipe_action,	NULL,		MS_EOM }		/* ACL_PIPE	*/
};


static acl_log_level_t acl_log_levels[] = {
    { LOG_EMERG,	"LOG_EMERG" },
    { LOG_ALERT,	"LOG_ALERT" },
    { LOG_CRIT,		"LOG_CRIT" },
    { LOG_ERR,		"LOG_ERR" },
    { LOG_WARNING,	"LOG_WARNING" },
    { LOG_NOTICE,	"LOG_NOTICE" },
    { LOG_INFO,		"LOG_INFO" },
    { LOG_DEBUG,	"LOG_DEBUG" },
    { 0,		NULL }
};


static char *acl_stati[] = {
	"NULL",
	"NONE",
	"RETURN",
	"CONTINUE",
	"REJECT",
	"DISCARD",
	"ACCEPT",
	"TEMPFAIL",
	"JUMP",
	"CALL",
	"SET",
	"LOG",
	"GREYLIST",
	"TARPIT",
	"MOD",
	"PIPE"
};

static char *acl_match_symbols[] = {
	"acl_matched",
	"acl_stage_matched",
	"acl_rule",
	"acl_filename",
	"acl_line",
	"acl_response",
	NULL
};

static void
acl_reply_delete(acl_reply_t *ar)
{
	/*
	 * Expressions in struct acl_reply are freed through exp_garbage!
	 */

	free(ar);

	return;
}


static acl_reply_t *
acl_reply_create(void)
{
	acl_reply_t *ar;

	ar = (acl_reply_t *) malloc(sizeof (acl_reply_t));
	if (ar == NULL)
	{
		log_sys_die(EX_OSERR, "acl_reply_create: malloc");
	}

	memset(ar, 0, sizeof (acl_reply_t));

	return ar;
}


acl_reply_t *
acl_reply(exp_t *code, exp_t *xcode, exp_t *msg)
{
	acl_reply_t *ar;

	ar = acl_reply_create();
	
	ar->ar_code = code;
	ar->ar_xcode = xcode;
	ar->ar_message = msg;

	return ar;
}

static void
acl_action_delete(acl_action_t *aa)
{
	acl_action_delete_t delete;

	delete = acl_action_handlers[aa->aa_type].ah_delete;
	if (delete && aa->aa_data)
	{
		delete(aa->aa_data);
	}

	if (aa->aa_reply)
	{
		acl_reply_delete(aa->aa_reply);
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
		log_sys_die(EX_OSERR, "acl_action_create: malloc");
	}

	memset(aa, 0, sizeof (acl_action_t));

	aa->aa_type = type;
	aa->aa_data = data;

	// HACK: Read line number and filename directly from parser.
	aa->aa_filename = parser_current_filename(&acl_parser);
	aa->aa_line = parser_current_line(&acl_parser);

	return aa;
}

acl_action_t *
acl_action(acl_action_type_t type, void *data)
{
	acl_action_t *aa;

	aa = acl_action_create(type, data);

	return aa;
}


acl_action_t *
acl_action_reply(acl_action_t *aa, acl_reply_t *ar)
{
	aa->aa_reply = ar;

	return aa;
}


static acl_rule_t *
acl_rule_create(exp_t *exp, acl_action_t *aa)
{
	acl_rule_t *ar;

	ar = (acl_rule_t *) malloc(sizeof (acl_rule_t));
	if (ar == NULL)
	{
		log_sys_error("acl_rule_create: malloc");
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
			log_die(EX_SOFTWARE, "acl_append: %s: sht_insert "
				"failed", table);
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
	switch (as->as_type)
	{
	case AS_CONSTANT:
		var_delete(as->as_data);
		break;

	case AS_FUNCTION:
		acl_function_delete(as->as_data);
		break;

	default:
		break;
	}

	free(as);

	return;
}

static acl_symbol_t *
acl_symbol_create(acl_symbol_type_t type, char *name, milter_stage_t stages,
    void *data, acl_symbol_flag_t flags)
{
	acl_symbol_t *as;

	as = (acl_symbol_t *) malloc(sizeof (acl_symbol_t));
	if (as == NULL)
	{
		log_sys_die(EX_OSERR, "acl_symbol_create: malloc");
	}

	as->as_type = type;
	as->as_stages = stages;
	as->as_data = data;
	as->as_flags = flags;

	return as;
}


static void
acl_symbol_insert(char *symbol, acl_symbol_t *as)
{
	if (sht_insert(acl_symbols, symbol, as))
	{
		log_die(EX_SOFTWARE, "acl_symbol_insert: %s: sht_insert "
			"failed", symbol);
	}
	
	return;
}


void
acl_symbol_register(char *name, milter_stage_t stages,
    acl_symbol_callback_t callback, acl_symbol_flag_t flags)
{
	acl_symbol_t *as;

	as = acl_symbol_create(AS_SYMBOL, name, stages, callback, flags);
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

	as = acl_symbol_create(AS_CONSTANT, name, MS_ANY | MS_INIT, v, AS_NONE);
	acl_symbol_insert(name, as);
	
	log_debug("acl_constant_register: \"%s\" registered", name);

	return;
}


void
acl_function_delete(acl_function_t *af)
{
	if (af->af_types)
	{
		free(af->af_types);
	}

	free(af);

	return;
}


acl_function_t *
acl_function_create(acl_function_type_t type, acl_function_callback_t callback,
    int argc, var_type_t *types)
{
	acl_function_t * af = NULL;

	af = (acl_function_t *) malloc(sizeof (acl_function_t));
	if (af == NULL)
	{
		log_sys_die(EX_OSERR, "acl_function_create: malloc");
	}

	memset(af, 0, sizeof (acl_function_t));

	af->af_type = type;
	af->af_callback = callback;
	af->af_argc = argc;
	af->af_types = types;

	return af;
}


static var_type_t *
acl_function_argv_types(int *argc, va_list ap)
{
	var_type_t type;
	var_type_t *types = NULL;
	int size;

	*argc = 0;

	for (*argc = 0; (type = va_arg(ap, var_type_t)); ++(*argc))
	{
		/*
		 * Store 0 at the end -> argc + 2
		 */
		size = (*argc + 2) * sizeof (var_type_t);

		/*
		 * Probably slow. However this is executed only once at
		 * startup and functions usually have only a few arguments.
		 */
		types = (var_type_t *) realloc(types, size);
		if (types == NULL)
		{
			log_sys_die(EX_OSERR, "acl_function_argv_types: realloc");
		}

		types[*argc] = type;
	}

	if (*argc)
	{
		types[*argc] = 0;
		return types;
	}

	return NULL;
}


void
acl_function_register(char *name, acl_function_type_t type,
    acl_function_callback_t callback, ...)
{
	acl_symbol_t *as;
	acl_function_t *af;
	va_list ap;
	var_type_t *types = NULL;
	int argc = 0;

	if (type == AF_SIMPLE)
	{
		va_start(ap, callback);
		types = acl_function_argv_types(&argc, ap);
		va_end(ap);
	}

	af = acl_function_create(type, callback, argc, types);
	as = acl_symbol_create(AS_FUNCTION, name, MS_ANY, af, AS_NONE);

	acl_symbol_insert(name, as);
	
	log_debug("acl_function_register: \"%s\" registered", name);

	return;
}


acl_function_t *
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

static var_t *
acl_variables(var_t * mailspec)
{
	var_t *variables = NULL;

	variables = vtable_lookup(mailspec, ACL_VARIABLES);
	if (variables != NULL)
	{
		return variables;
	}

	/*
	 * No variables yet. Create variable space
	 */
	variables = vtable_create(ACL_VARIABLES, VF_KEEPNAME);
	if (variables == NULL)
	{
		log_error("acl_variables: vtable_create failed");
		goto error;
	}

	/*
	 * Save variables to mailspec
	 */
	if (vtable_set(mailspec, variables))
	{
		log_error("acl_variables: vtable_set failed");
		goto error;
	}

	return variables;

error:
	if (variables != NULL)
	{
		var_delete(variables);
	}

	return NULL;
}

int
acl_variable_assign(var_t *mailspec, char *name, var_t *value)
{
	var_t *variables, *copy = NULL;

	variables = acl_variables(mailspec);
	if (variables == NULL)
	{
		log_error("acl_avriable_assign: acl_variables failed");
		return -1;
	}

	// Remove $ from variable name
	for (;*name == '$'; ++name);

	/*
	 * Sava a copy
	 */
	copy = var_create(value->v_type, name, value->v_data, VF_COPY);
	if (copy == NULL)
	{
		log_error("acl_variable_assign: VAR_COPY failed");
		return -1;
	}

	if (vtable_set(variables, copy))
	{
		log_error("exp_eval_variable: vtable_set failed");
		var_delete(copy);
		return -1;
	}

	return 0;
}

var_t *
acl_variable_get(var_t *mailspec, char *name)
{
	var_t *variables, *value;

	variables = acl_variables(mailspec);
	if (variables == NULL)
	{
		log_debug("acl_variable_get: acl_variables failed");
		return NULL;
	}

	// Remove $ from variable name
	for (;*name == '$'; ++name);

	value = vtable_lookup(variables, name);
	if (value == NULL)
	{
		log_debug("acl_variable_get: unknown variable \"%s\"", name);
		return NULL;
	}

	return value;
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

	stage = vtable_get(mailspec, "stage");
	if (stage == NULL)
	{
		log_debug("acl_symbol_get: milter stage not set");
		return NULL;
	}

	if ((as->as_stages & *stage) == 0)
	{
		stagename = vtable_get(mailspec, "stagename");
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
	 * Lookup symbol if caching is allowed
	 */
	if ((as->as_flags & AS_NOCACHE) == 0)
	{
		v = vtable_lookup(mailspec, name);
		if (v)
		{
			return v;
		}
	}

	callback = as->as_data;
	if (callback == NULL)
	{
		log_debug("acl_symbol_get: \"%s\" is not set and has no "
		    "callback", name);

		if (vtable_set_null(mailspec, name, VF_COPYNAME))
		{
			log_error("acl_symbol_get: vtable_set_null failed");
			return NULL;
		}
	}
	else
	{
		if (callback(*stage, name, mailspec))
		{
			log_error("acl_symbol_get: callback for \"%s\" failed",
			    name);
			return NULL;
		}
	}

	// Check if the callback has set the required symbol
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
	if (al->al_message)
	{
		exp_delete(al->al_message);
	}

	if (al->al_level)
	{
		exp_delete(al->al_level);
	}

	free(al);
}


acl_log_t *
acl_log_create(exp_t *message)
{
	acl_log_t *al;

	al = (acl_log_t *) malloc(sizeof (acl_log_t));
	if (al == NULL)
	{
		log_sys_die(EX_OSERR, "acl_log: malloc");
	}

	memset(al, 0, sizeof (acl_log_t));

	al->al_message = message;

	return al;
}


acl_log_t *
acl_log_level(acl_log_t *al, exp_t *level)
{
	al->al_level = level;

	return al;
}


acl_action_type_t
acl_log(milter_stage_t stage, char *stagename, var_t *mailspec, void *data,
	int depth)
{
	acl_log_t *al = data;
	var_t *v = NULL;
	char buffer[ACL_LOGLEN];
	VAR_INT_T level;
	char *message;

	/*
	 * Evaluate log level
	 */
	if (al->al_level)
	{
		v = exp_eval(al->al_level, mailspec);
		if (v == NULL)
		{
			log_error("acl_log: exp_eval failed");
			goto error;
		}

		level = var_intval(v);

		exp_free(v);
	}
	else
	{
		level = cf_acl_log_level;
	}

	/*
	 * Evaluate message
	 */
	v = exp_eval(al->al_message, mailspec);
	if (v == NULL)
	{
		log_error("acl_log: exp_eval failed");
		return ACL_ERROR;
	}

	if (v->v_type == VT_STRING)
	{
		message = v->v_data;
	}
	else
	{
		if (var_dump_data(v, buffer, sizeof buffer) == -1)
		{
			log_error("acl_log: var_dump_data failed");
			goto error;
		}
		message = buffer;
	}

	log_message(level, mailspec, "%s", message);

	exp_free(v);

	return ACL_NONE;


error:

	if (v)
	{
		exp_free(v);
	}

	return ACL_ERROR;
}


acl_action_type_t
acl_jump(milter_stage_t stage, char *stagename, var_t *mailspec, void *data,
	int depth)
{
	char *table = data;
	acl_action_type_t aa;
	log_debug("acl_jump: jump to \"%s\"", table);

	aa = acl(stage, table, mailspec, depth);

	if(aa == ACL_NONE)
	{
		return ACL_CONTINUE;
	}

	return aa;
}


acl_action_type_t
acl_call(milter_stage_t stage, char *stagename, var_t *mailspec, void *data,
	int depth)
{
	char *table = data;
	log_debug("acl_call: call \"%s\"", table);

	return acl(stage, table, mailspec, depth);
}


acl_action_type_t
acl_set(milter_stage_t stage, char *stagename, var_t *mailspec, void *data,
	int depth)
{
	exp_t *exp = data;
	var_t *v;

	v = exp_eval(exp, mailspec);
	if (v == NULL)
	{
		log_error("acl_set: exp_eval failed");
		return ACL_ERROR;
	}

	exp_free(v);

	return ACL_NONE;
}


void
acl_update_callback(acl_update_t callback)
{
	if (LL_INSERT(acl_update_callbacks, callback) == -1)
	{
		log_die(EX_SOFTWARE, "acl_update_callback: LL_INSERT failed");
	}

	return;
}

void
acl_match(var_t *mailspec, VAR_INT_T matched, VAR_INT_T stage,
    char *stagename, VAR_INT_T *rule, char *filename, VAR_INT_T *line,
    char *response)
{
	VAR_INT_T *acl_matched;
	VAR_INT_T *stage_matched;

	acl_matched = vtable_get(mailspec, "acl_matched");
	stage_matched = vtable_get(mailspec, "acl_stage_matched");

	// Variables not set yet
	if (acl_matched == NULL || stage_matched == NULL)
	{
		goto exit;
	}

	// Override symbols if stage_symbols were set in previous stage
	if (*stage_matched < stage)
	{
		goto exit;
	}

	// Do not override acl symbols if another rule in this stage already
	// matched.
	if (*acl_matched)
	{
		return;
	}

exit:
	// First match in this stage
	if (matched)
	{
		log_message(LOG_ERR, mailspec, "match: stage=%s rule=%d "
			"file=%s line=%d reply=%s", stagename, *rule, filename, *line, response);
	}

	if (vtable_setv(mailspec,
	    VT_INT, "acl_matched", &matched, VF_KEEPNAME | VF_COPYDATA,
	    VT_INT, "acl_stage_matched", &stage, VF_KEEPNAME | VF_COPYDATA,
	    VT_INT, "acl_rule", rule, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "acl_filename", filename, VF_KEEP,
	    VT_INT, "acl_line", line, VF_KEEPNAME | VF_COPYDATA,
	    VT_STRING, "acl_response", response, VF_KEEP,
	    VT_NULL))
	{
		log_error("acl_update_mailspec: vtable_setv failed");
	}

	return;
}

void
acl_update(milter_stage_t stage, acl_action_type_t action, var_t *mailspec)
{
	acl_update_t callback;
	ll_entry_t *pos;

	pos = LL_START(acl_update_callbacks);
	while ((callback = ll_next(acl_update_callbacks, &pos)))
	{
		if (callback(stage, action, mailspec))
		{
			log_error("acl_update: update callback failed");
		}
	}

	return;
}
	

static char *
acl_eval_reply(exp_t *exp, var_t *mailspec)
{
	var_t *v = NULL, *tmp = NULL;
	char *result;

	if (exp == NULL)
	{
		log_debug("acl_eval_reply: exp is null");
		return NULL;
	}

	v = exp_eval(exp, mailspec);
	if (v == NULL)
	{
		log_error("acl_eval_reply: exp_eval failed");
		goto error;
	}

	if (v->v_data == NULL)
	{
		log_error("acl_eval_reply: expression returned no data");
		goto error;
	}

	if (v->v_type != VT_STRING)
	{
		tmp = var_cast_copy(VT_STRING, v);
		if (tmp == NULL)
		{
			log_error("acl_eval_reply: var_cast_copy failed");
			goto error;
		}

		exp_free(v);

		v = tmp;
	}

	result = strdup(v->v_data);
	if (result == NULL)
	{
		log_sys_error("acl_eval_reply: strdup");
		goto error;
	}

	if (tmp)
	{
		var_delete(tmp);
	}
	else
	{
		exp_free(v);
	}

	return result;


error:

	if (tmp)
	{
		var_delete(tmp);
	}
	else if (v)
	{
		exp_free(v);
	}

	return NULL;
}


static int
acl_set_reply(acl_reply_t *ar, acl_action_type_t type, var_t *mailspec)
{
	char *code = NULL;
	char *xcode = NULL;
	char *message = NULL;
	int r = -1;

	code = acl_eval_reply(ar->ar_code, mailspec);
	if (code == NULL)
	{
		log_error("acl_set_reply: acl_eval_reply failed");
		goto error;
	}

	switch(type)
	{
	case ACL_REJECT:
		if (code[0] != '5')
		{
			log_error("acl_set_reply: bad code %s need 5XX", code);
			goto error;
		}
		break;

	case ACL_TEMPFAIL:
	case ACL_GREYLIST:
		if (code[0] != '4')
		{
			log_error("acl_set_reply: bad code %s need 4XX", code);
			goto error;
		}
		break;

	default:
		log_debug("acl_set_reply: bad smtp reply code for action");
		r = 0;

		goto error;

	}
	
	message = acl_eval_reply(ar->ar_message, mailspec);
	if (message == NULL)
	{
		log_error("acl_set_reply: acl_eval_reply failed");
		goto error;
	}
	
	if (ar->ar_xcode)
	{
		xcode = acl_eval_reply(ar->ar_xcode, mailspec);
		if (xcode == NULL)
		{
			log_error("acl_set_reply: acl_eval_reply failed");
			goto error;
		}
	}

	if (milter_set_reply(mailspec, code, xcode, message))
	{
		log_error("acl_set_reply: milter_set_reply failed");
		goto error;
	}

	free(code);
	free(message);

	if (xcode)
	{
		free(xcode);
	}

	return 0;


error:

	if (code)
	{
		free(code);
	}

	if (message)
	{
		free(message);
	}

	if (xcode)
	{
		free(xcode);
	}

	return r;
}

acl_action_type_t
acl(milter_stage_t stage, char *stagename, var_t *mailspec, int depth)
{
	ll_t *rules;
	ll_entry_t *pos;
	acl_rule_t *ar;
	acl_action_t *aa;
	acl_action_type_t response;
	acl_action_handler_t action_handler;
	VAR_INT_T i;

	log_debug("acl: stage \"%s\"", stagename);

	/*
         * Recursion depth
	 */
	if (++depth >= MAX_RECURSION)
	{
		log_message(LOG_ERR, mailspec,
			"acl: error: recursion limit %d reached", depth);
		return ACL_ERROR;
	}


	/*
	 * Lookup table
	 */
	rules = sht_lookup(acl_tables, stagename);
	if (rules == NULL)
	{
		log_info("acl: no rules for \"%s\"", stagename);
		goto exit;
	}

	pos = LL_START(rules);
	for (i = 1; (ar = ll_next(rules, &pos)); ++i)
	{
		switch (exp_is_true(ar->ar_expression, mailspec))
		{
		/*
		 * Expression doesn't match
		 */
		case 0:		continue;

		/*
		 * Expression failed
		 */
		case -1:	return ACL_ERROR;

		default:	break;
		}

		aa = ar->ar_action;

		log_message(LOG_NOTICE, mailspec, "acl: %s: rule number %d in "
			"table \"%s\" on line %d matched", aa->aa_filename, i,
			stagename, aa->aa_line);

		/*
		 * Check if action is allowed at this stage
		 */
		if ((acl_action_handlers[aa->aa_type].ah_stages & stage) == 0)
		{
			log_debug("acl: rule %d in %s: bad action",
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
			    ar->ar_action->aa_data, depth);
		}
		else
		{
			response = aa->aa_type;
		}

		switch(response)
		{
		case ACL_NONE:
			continue;

		case ACL_RETURN:
			return ACL_NONE;

		case ACL_ERROR:
			log_error("acl: %s: rule number %d in table \"%s\" on "
				"line %d failed", aa->aa_filename, i, stagename, aa->aa_line);
			return ACL_ERROR;

		default:
			break;
		}

		/*
		 * Evaluate reply if set
		 */
		if (aa->aa_reply)
		{
			if (acl_set_reply(aa->aa_reply, response, mailspec))
			{
				log_error("acl: acl_set_reply failed");
				return ACL_ERROR;
			}
		}

		acl_match(mailspec, 1, stage, stagename, &i, aa->aa_filename, &aa->aa_line,
			acl_stati[response]);

		return response;
	}

exit:
	/*
	 * No rule matched
	 */
	log_message(LOG_INFO, mailspec, "acl: no match in \"%s\"",
		stagename);

	return ACL_NONE;
}

void
acl_init(void)
{
	acl_log_level_t *ll;
	char **symbol;

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

	acl_update_callbacks = ll_create();
	if (acl_update_callbacks == NULL)
	{
		log_die(EX_SOFTWARE, "acl_update_register: ll_create failed");
	}

	/*
	 * Initialize exp
	 */
	exp_init();

	/*
	 * Load log symbols
	 */
	for (ll = acl_log_levels; ll->ll_name; ++ll)
	{
		acl_constant_register(VT_INT, ll->ll_name, &ll->ll_level,
		    VF_KEEP);
	}

	/*
	 * ACL symbols
	 */
	for (symbol = acl_match_symbols; *symbol != NULL; ++symbol)
	{
		acl_symbol_register(*symbol, MS_ANY, NULL, AS_NONE);
	}
	
	return;
}


void
acl_read(void)
{
	char *mopherd_acl;

	mopherd_acl = cf_acl_path ? cf_acl_path : defs_mopherd_acl;

	/*
	 * run parser
	 */
	parser(&acl_parser, mopherd_acl, 1, &acl_in, acl_parse);

	return;
}


void
acl_clear(void)
{
	/*
	 * Free expressions
	 */
	exp_clear();

	/*
	 * Free parser filename buffers
	 */
	parser_clear(&acl_parser);


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
