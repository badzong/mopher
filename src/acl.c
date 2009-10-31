#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <glob.h>
#include <string.h>

#include "mopher.h"

#define BUFLEN 1024

/*
 * External declarations
 */

extern FILE *acl_in;
extern int acl_parse(void);

/*
 * Globals
 */
static ht_t *acl_symbols;
static ht_t *acl_functions;
static ht_t *acl_tables;


static void
acl_function_delete(acl_function_t *af)
{
	free(af);
}

static acl_function_t *
acl_function_create(char *name, acl_fcallback_t callback)
{
	acl_function_t *af;

	if((af = (acl_function_t *) malloc(sizeof(acl_function_t))) == NULL) {
		log_warning("acl_function_create: malloc");
		return NULL;
	}

	af->af_name = name;
	af->af_callback = callback;

	return af;
}

static void
acl_symbol_delete(acl_symbol_t * as)
{
	if (as->as_type == AS_STATIC) {
		var_delete(as->as_data);
	}

	free(as);
}

static int
acl_function_match(acl_function_t *af1, acl_function_t *af2)
{
	if (strcmp(af1->af_name, af2->af_name)) {
		return 0;
	}

	return 1;
}

static hash_t
acl_function_hash(acl_function_t *af)
{
	return HASH(af->af_name, strlen(af->af_name));
}

static acl_symbol_t *
acl_symbol_create(acl_symbol_type_t type, char *name, milter_stage_t stage,
	void *data)
{
	acl_symbol_t *as;

	if ((as = (acl_symbol_t *) malloc(sizeof(acl_symbol_t))) == NULL) {
		log_warning("acl_symbol_create: malloc");
		return NULL;
	}

	as->as_type = type;
	as->as_name = name;
	as->as_stage = stage;
	as->as_data = data;

	return as;
}

static int
acl_symbol_match(acl_symbol_t * as1, acl_symbol_t * as2)
{
	if (strcmp(as1->as_name, as2->as_name)) {
		return 0;
	}

	return 1;
}

static hash_t
acl_symbol_hash(acl_symbol_t * as)
{
	return HASH(as->as_name, strlen(as->as_name));
}


int
acl_symbol_register(acl_symbol_type_t type, char *name, milter_stage_t stage,
	void *data)
{
	acl_symbol_t *as;

	as = acl_symbol_create(type, name, stage, data);
	if (as  == NULL) {
		log_warning("acl_symbol_regeister: acl_symbol_create failed");
		return -1;
	}

	if (ht_insert(acl_symbols, as)) {
		log_warning("acl_symbol_register: ht_insert failed");
		acl_symbol_delete(as);
		return -1;
	}

	log_debug("acl_symbol_register: symbol \"%s\" registered", name);

	return 0;
}


int
acl_static_register(var_type_t type, char *name, void *data, int flags)
{
	var_t *v;

	v = var_create(type, name, data, flags);
	if (v == NULL) {
		log_warning("acl_static_register: var_create failed");
		return -1;
	}

	if(acl_symbol_register(AS_STATIC, name, MS_CONNECT | MS_HELO |
		MS_ENVFROM | MS_ENVRCPT | MS_HEADER | MS_EOH | MS_BODY |
		MS_EOM, v)) {
		log_warning("acl_static_register: acl_symbol_register failed");
		return -1;
	}

	return 0;
}

int
acl_symbol_add(var_t * attrs, var_type_t type, char *name, void *data, int flags)
{
	if (var_table_set_new(attrs, type, name, data, flags)) {
		log_warning("acl_symbol_add: var_table_set_new failed");
		return -1;
	}

	return 0;
}

int
acl_function_register(char *name, acl_fcallback_t callback)
{
	acl_function_t *af;

	if ((af = acl_function_create(name, callback)) == NULL) {
		log_warning("acl_function_regeister: acl_function_create"
				" failed");
		return -1;
	}

	if (ht_insert(acl_functions, af)) {
		log_warning("acl_function_register: ht_insert failed");
		acl_function_delete(af);
		return -1;
	}

	log_debug("acl_function_register: function \"%s\" registered", name);

	return 0;
}

static void
acl_call_delete(acl_call_t * ac)
{
	if (ac->ac_args) {
		ll_delete(ac->ac_args, (void *) acl_value_delete);
	}

	free(ac);

	return;
}

static acl_call_t *
acl_call_create(acl_function_t * function, ll_t * args)
{
	acl_call_t *ac;

	if ((ac = (acl_call_t *) malloc(sizeof(acl_call_t))) == NULL) {
		log_warning("acl_call_create: malloc");
		return NULL;
	}

	ac->ac_function = function;
	ac->ac_args = args;

	return ac;
}

void
acl_value_delete(acl_value_t * av)
{
	switch (av->av_type) {
	case AV_CONST:
		var_delete(av->av_data);
		break;

	case AV_FUNCTION:
		acl_call_delete(av->av_data);
		break;

	/*
	 * TODO: AV_SYMBOL
	 */


	default:
		break;
	}

	free(av);

	return;
}

acl_value_t *
acl_value_create(acl_value_type_t type, void * data)
{
	acl_value_t *av;

	if ((av = (acl_value_t *) malloc(sizeof(acl_value_t))) == NULL) {
		log_warning("acl_value_create: malloc");
		return NULL;
	}

	av->av_type = type;
	av->av_data = data;

	return av;
}

acl_value_t *
acl_value_create_symbol(char *name)
{
	acl_symbol_t lookup, *as;
	acl_value_t *av;

	lookup.as_name = name;

	if ((as = ht_lookup(acl_symbols, &lookup)) == NULL) {
		log_warning ("acl_value_create_symbol: unknown symbol \"%s\"",
			name);
		return NULL;
	}

	if ((av = acl_value_create(AV_SYMBOL, as)) == NULL) {
		log_warning("acl_value_create_attribute: acl_value_create"
			" failed");
		return NULL;
	}

	return av;
}

acl_value_t *
acl_value_create_function(char *name, ll_t * args)
{
	acl_function_t lookup, *af;
	acl_call_t *ac = NULL;
	acl_value_t *av = NULL;

	lookup.af_name = name;

	if ((af = ht_lookup(acl_functions, &lookup)) == NULL) {
		log_warning ("acl_value_create_function: unknown function "
			"\"%s\"", name);
		goto error;
	}

	if ((ac = acl_call_create(af, args)) == NULL) {
		log_warning
		    ("acl_value_create_function: acl_call_create failed");
		goto error;
	}

	if ((av = acl_value_create(AV_FUNCTION, ac)) == NULL) {
		log_warning
		    ("acl_value_create_function: acl_value_create failed");
		goto error;
	}

	return av;

error:

	if (ac) {
		acl_call_delete(ac);
	}

	if (av) {
		acl_value_delete(av);
	}

	return NULL;
}


void
acl_condition_delete(acl_condition_t * ac)
{
	if (ac->ac_left) {
		acl_value_delete(ac->ac_left);
	}

	if (ac->ac_right) {
		acl_value_delete(ac->ac_right);
	}

	free(ac);
}

acl_condition_t *
acl_condition_create(acl_not_t not, acl_gate_t gate, acl_cmp_t cmp,
		     acl_value_t * left, acl_value_t * right)
{
	acl_condition_t *ac = NULL;

	if ((ac = (acl_condition_t *) malloc(sizeof(acl_condition_t))) == NULL) {
		log_warning("acl_condition_create: malloc");
		goto error;
	}

	ac->ac_not = not;
	ac->ac_gate = gate;
	ac->ac_cmp = cmp;
	ac->ac_left = left;
	ac->ac_right = right;

	return ac;

error:

	if (ac) {
		acl_condition_delete(ac);
	}

	return NULL;
}

void
acl_action_delete(acl_action_t * aa)
{
	if (aa->aa_jump) {
		free(aa->aa_jump);
	}

	if (aa->aa_type == AA_DELAY && aa->aa_data) {
		acl_delay_delete(aa->aa_data);
	}

	if (aa->aa_type == AA_LOG && aa->aa_data) {
		acl_log_delete(aa->aa_data);
	}

	free(aa);

	return;
}

acl_action_t *
acl_action_create(acl_action_type_t type, char *jump, void *data)
{
	acl_action_t *aa = NULL;

	if ((aa = (acl_action_t *) malloc(sizeof(acl_action_t))) == NULL) {
		log_warning("acl_action_create: malloc");
		goto error;
	}

	aa->aa_type = type;
	aa->aa_jump = jump;
	aa->aa_data = data;

	return aa;

error:

	if (aa) {
		acl_action_delete(aa);
	}

	return NULL;
}

void
acl_delay_delete(acl_delay_t * ad)
{
	free(ad);

	return;
}

acl_delay_t *
acl_delay_create(int delay, int valid, int visa)
{
	acl_delay_t *ad;

	ad = (acl_delay_t *) malloc(sizeof(acl_delay_t));
	if (ad == NULL) {
		log_warning("acl_delay_create: malloc");
		return NULL;
	}

	ad->ad_delay = delay;
	ad->ad_valid = valid;
	ad->ad_visa = visa;

	return ad;
}


void
acl_log_delete(acl_log_t *al)
{
	if (al->al_format) {
		free(al->al_format);
	}

	free(al);

	return;
}


acl_log_t *
acl_log_create(char *format)
{
	acl_log_t *al;

	al = (acl_log_t *) malloc(sizeof(acl_log_t));
	if (al == NULL) {
		log_warning("acl_delay_create: malloc");
		return NULL;
	}

	al->al_format = format;
	al->al_level = LOG_ERR;

	return al;
}


static void
acl_table_delete(acl_table_t * at)
{
	if (at->at_name) {
		free(at->at_name);
	}

	if (at->at_rules) {
		ll_delete(at->at_rules, (void *) acl_rule_delete);
	}

	if (at->at_default) {
		acl_action_delete(at->at_default);
	}

	free(at);

	return;
}

static acl_table_t *
acl_table_create(char *name, acl_action_t * target)
{
	acl_table_t *at = NULL;
	ll_t *ll = NULL;

	if ((at = (acl_table_t *) malloc(sizeof(acl_table_t))) == NULL) {
		log_warning("acl_table_create: malloc");
		goto error;
	}

	if ((ll = ll_create()) == NULL) {
		log_warning("acl_table_create: ll_create failed");
		goto error;
	}

	at->at_name = name;
	at->at_rules = ll;
	at->at_default = target;

	return at;

error:

	if (at) {
		acl_table_delete(at);
	}

	if (ll) {
		ll_delete(ll, NULL);
	}

	return NULL;
}

static hash_t
acl_table_hash(acl_table_t * at)
{
	return HASH(at->at_name, strlen(at->at_name));
}

static int
acl_table_match(acl_table_t * at1, acl_table_t * at2)
{
	if (strcmp(at1->at_name, at2->at_name) == 0) {
		return 1;
	}

	return 0;
}

void
acl_rule_delete(acl_rule_t * ar)
{
	if (ar->ar_conditions) {
		ll_delete(ar->ar_conditions, (void *) acl_condition_delete);
	}

	if (ar->ar_action) {
		acl_action_delete(ar->ar_action);
	}

	free(ar);

	return;
}

acl_rule_t *
acl_rule_create(ll_t * conditions, acl_action_t * action)
{
	acl_rule_t *ar;

	if ((ar = (acl_rule_t *) malloc(sizeof(acl_rule_t))) == NULL) {
		log_warning("acl_rule_create: malloc");
		return NULL;
	}

	ar->ar_conditions = conditions;
	ar->ar_action = action;

	return ar;
}

acl_table_t *
acl_table_lookup(char *name)
{
	acl_table_t lookup;

	lookup.at_name = name;

	return (acl_table_t *) ht_lookup(acl_tables, &lookup);
}

acl_table_t *
acl_table_register(char *name, acl_action_t * target)
{
	acl_table_t *at = NULL;

	if ((at = acl_table_create(name, target)) == NULL) {
		log_warning("acl_table_register: acl_table_create failed");
		goto error;
	}

	if (ht_insert(acl_tables, at) == -1) {
		log_warning("acl_table_register: ht_insert failed");
		goto error;
	}

	return at;

error:

	if (at) {
		acl_table_delete(at);
	}

	return NULL;
}

int
acl_rule_register(acl_table_t * at, ll_t * conditions, acl_action_t * action)
{
	acl_rule_t *ar = NULL;

	if ((ar = acl_rule_create(conditions, action)) == NULL) {
		log_warning("acl_rule_register: acl_rule_create failed");
		goto error;
	}

	if (LL_INSERT(at->at_rules, ar) == -1) {
		log_warning("acl_rule_register: LL_INSERT failed");
		goto error;
	}

	return 0;

error:

	if (ar) {
		acl_rule_delete(ar);
	}

	return -1;
}

int
acl_init(char *mail_acl)
{
	if ((acl_tables = ht_create(ACL_TABLE_BUCKETS,
		(ht_hash_t) acl_table_hash, (ht_match_t) acl_table_match,
		(ht_delete_t) acl_table_delete)) == NULL) {
		log_warning("acl_init: ht_create failed");
		return -1;
	}

	if ((acl_symbols = ht_create(ACL_SYMBOL_BUCKETS,
		(ht_hash_t) acl_symbol_hash, (ht_match_t) acl_symbol_match,
		(ht_delete_t) acl_symbol_delete)) == NULL) {
		log_warning("acl_init: ht_create failed");
		return -1;
	}

	if ((acl_functions = ht_create(ACL_FUNCTION_BUCKETS,
		(ht_hash_t) acl_function_hash, (ht_match_t) acl_function_match,
		(ht_delete_t) acl_function_delete)) == NULL) {
		log_warning("acl_init: ht_create failed");
		return -1;
	}

	/*
	 * Load modules
	 */
	MODULE_LOAD_ACL;

	/*
	 * Run parser
	 */
	util_parser(mail_acl, &acl_in, acl_parse);

	return 0;
}

void
acl_clear(void)
{
	ht_delete(acl_tables);
	ht_delete(acl_symbols);
	ht_delete(acl_functions);

	return;
}


var_t *
acl_symbol_eval(acl_value_t * av, var_t *attrs)
{
	var_t *v;
	acl_symbol_t *as;
	VAR_INT_T *ms;
	char *sn;
	acl_scallback_t callback;

	as = av->av_data;

	if (as->as_type == AS_STATIC) {
		return as->as_data;
	}

	v = var_table_lookup(attrs, as->as_name);
	if (v) {
		return v;
	}

	ms = var_table_get(attrs, "milter_stage");
	if (ms == NULL) {
		log_error("acl_symbol_eval: milter_stage not set");
		return NULL;
	}

	if (!(*ms & as->as_stage)) {
		sn = var_table_get(attrs, "milter_stagename");
		if (sn == NULL) {
			log_error("acl_symbol_eval: milter_stagename not set");
			sn = "(unknown)";
		}

		log_error("acl_symbol_eval: symbol \"%s\" not available at %s",
			as->as_name, sn);

		return NULL;
	}

	if (as->as_type == AS_NULL) {
		log_error("acl_symbol_eval: \"%s\" is not callable",
			as->as_name);
		return as->as_data;
	}

	if (as->as_data == NULL) {
		log_error("acl_symbol_eval: symbol \"%s\" is not defined and"
			" has no callback", as->as_name);
		return NULL;
	}

	callback = as->as_data;
	if (callback(*ms, as->as_name, attrs)) {
		log_error("acl_symbol_eval: callback for symbol \"%s\" failed",
			as->as_name);
		return NULL;
	}

	/*
	 * Check if callback set the requested symbol
	 */
	v = var_table_lookup(attrs, as->as_name);
	if (v == NULL) {
		log_error("acl_symbol_eval: symbol \"%s\" is empty",
			as->as_name);
		return NULL;
	}

	return v;
}


int
acl_symbol_dereference(var_t *attrs, ...)
{
	va_list ap;
	acl_value_t av;
	acl_symbol_t *as, lookup;
	char *symbol;
	void **data;
	var_t *v;

	va_start(ap, attrs);

	while ((symbol = va_arg(ap, char *)))
	{
		data = va_arg(ap, void **);

		*data = var_table_get(attrs, symbol);
		if (*data)
		{
			continue;
		}

		/*
		 * Lookup symbol
		 */
		lookup.as_name = symbol;
		as = ht_lookup(acl_symbols, &lookup);
		if (as == NULL)
		{
			log_error("acl_symbol_dereference: unknown symbol "
			    "\"%s\"", symbol);
			return -1;
		}

		av.av_type = AV_SYMBOL;
		av.av_data = as;

		v = acl_symbol_eval(&av, attrs);
		if (v == NULL)
		{
			log_error("acl_symbol_dereference: acl_symbol_eval for"
			    "\"%s\" failed", symbol);
			return -1;
		}

		*data = v->v_data;
	}

	return 0;
}


var_t *
acl_function_eval(acl_value_t * av, var_t *attrs)
{
	ll_t *args = NULL;
	acl_call_t *call;
	acl_value_t *arg;
	var_t *v;

	// callback = av->av_data.vd_call->ac_function;
	// args = av->av_data.vd_call->ac_args;

	if ((args = ll_create()) == NULL) {
		log_error("acl_function_eval: ll_create failed");
		goto error;
	}

	/*
	 * Build argument list
	 */
	call = av->av_data;
	ll_rewind(call->ac_args);
	while ((arg = ll_next(call->ac_args))) {
		if ((v = acl_value_eval(arg, attrs)) == NULL) {
			log_error("acl_function_eval: acl_value_eval failed");
			goto error;
		}

		if (LL_INSERT(args, v) == -1) {
			log_error("acl_function_eval: LL_INSERT failed");
			goto error;
		}
	}

	v = call->ac_function->af_callback(args);

	ll_delete(args, NULL);

	return v;

error:
	if (args) {
		ll_delete(args, NULL);
	}

	return NULL;
}

var_t *
acl_value_eval(acl_value_t * av, var_t *attrs)
{
	switch (av->av_type) {

	case AV_CONST:
		return av->av_data;

	case AV_FUNCTION:
		return acl_function_eval(av, attrs);

	case AV_SYMBOL:
		return acl_symbol_eval(av, attrs);

	default:
		log_die(EX_SOFTWARE, "acl_value_eval: bad acl value type");
	}

	return NULL;
}

int
acl_compare(var_t * v1, var_t * v2, acl_cmp_t ac)
{
	int cmp;

	cmp = var_compare(v1, v2);

	if (ac == AC_EQ && cmp == 0) {
		return 1;
	}

	if (ac == AC_NE && cmp != 0) {
		return 1;
	}

	if (ac == AC_LT && cmp < 0) {
		return 1;
	}

	if (ac == AC_GT && cmp > 0) {
		return 1;
	}

	if (ac == AC_LE && cmp <= 0) {
		return 1;
	}

	if (ac == AC_GE && cmp >= 0) {
		return 1;
	}

	return 0;
}

int
acl_conditions_eval(ll_t * conditions, var_t *attrs)
{
	acl_condition_t *ac;
	var_t *left = NULL, *right = NULL;
	int r, ax = 0;

	/*
	 * Empty rules always match
	 */
	if (conditions == NULL) {
		return 1;
	}

	/*
	 * Evaluation goes left to right. First condition as ac_gate == AG_NULL.
	 */
	ll_rewind(conditions);
	while ((ac = ll_next(conditions))) {
		/*
		 * Skip uneccessary calculations. Keep accumulator
		 */
		if ((ax == 0 && ac->ac_gate == AG_AND) ||
		    (ax == 1 && ac->ac_gate == AG_OR)) {
			continue;
		}

		/*
		 * Can't compare NULL.
		 */
		if ((left = acl_value_eval(ac->ac_left, attrs)) == NULL) {
			log_error("acl_conditions_eval: acl_value_eval returned"
				  " null");
			r = 0;
		}
		else if (ac->ac_right == NULL) {
			r = var_true(left) ^ ac->ac_not;
		}
		else {
			if ((right = acl_value_eval(ac->ac_right, attrs)) == NULL) {
				log_error("acl_conditions_eval: acl_value_eval"
					  " returned null");
				r = 0;
			}
			else {
				r = acl_compare(left, right, ac->ac_cmp) ^
				    ac->ac_not;
			}
		}

		/*
		 * Accumulate.
		 */
		switch (ac->ac_gate) {

		case AG_AND:
			ax = ax && r;
			break;

		case AG_OR:
			ax = ax || r;
			break;

		default:
			ax = r;
		}

		/*
		 * Functions create new variables. Free buffers
		 */
		if (left && ac->ac_left->av_type == AV_FUNCTION)
		{
			var_delete(left);
			left = NULL;
		}

		if (right && ac->ac_right->av_type == AV_FUNCTION)
		{
			var_delete(right);
			left = NULL;
		}
	}

	return ax;
}


static void
acl_log(var_t *attrs, acl_log_t *al)
{
	char buffer[BUFLEN];

	if (var_table_printstr(attrs, buffer, sizeof(buffer), al->al_format)) {
		log_error("acl_log: var_table_printstr failed");
		return;
	}

	log_log(al->al_level, buffer);

	return;
}


acl_action_type_t
acl(char *table, var_t *attrs)
{
	acl_table_t *at;
	acl_action_t *aa;
	acl_rule_t *ar;
	int i, r;
	greylist_response_t glr;


	if ((at = acl_table_lookup(table)) == NULL) {
		log_notice("acl: unknown table \"%s\": continue", table);
		return AA_CONTINUE;
	}

	log_debug("acl: table \"%s\"", table);

	aa = at->at_default;

	/*
	 * Each table has a list of rules. A rule has a list of conditions.
	 * If all conditions match, the rule matches.
	 */
	ll_rewind(at->at_rules);
	for (i = 1; (ar = ll_next(at->at_rules)); ++i) {
		r = acl_conditions_eval(ar->ar_conditions, attrs);
		if (r == 0) {
			continue;
		}

		if (r == 1) {
			log_info("rule %d in table \"%s\" matched", i, table);
			aa = ar->ar_action;

			if (aa->aa_type == AA_DELAY)
			{
				glr = greylist(attrs, aa->aa_data);

				switch(glr)
				{
				case GL_PASS:
					log_info("acl: greylisting passed");
					aa = NULL;
					continue;

				case GL_ERROR:
					log_info("acl: greylisting failed");
					return AA_ERROR;

				default:
					break;
				}

				log_info("acl: greylisting in action");
				break;
			}

			if (aa->aa_type == AA_LOG) {
				acl_log(attrs, aa->aa_data);
				aa = NULL;
				continue;
			}

			break;
		}

		log_error("acl: acl_conditions_eval failed");
		return AA_ERROR;
	}

	if (ar == NULL) {
		log_debug("acl: no more rules in \"%s\"", table);
	}

	if (aa == NULL) {
		log_error("acl: \"%s\" has no default action: continue", table);
		return AA_CONTINUE;
	}
	else if (ar == NULL) {

		/*
		 * Default Action is delay or log
		 */
		if (aa->aa_type == AA_DELAY) {
			glr = greylist(attrs, aa->aa_data);

			if(glr == GL_PASS) {
				log_info("acl: greylisting passed");
				return AA_CONTINUE;
			}

			return AA_DELAY;
		}

		if (aa->aa_type == AA_LOG) {
			acl_log(attrs, aa->aa_data);
			return AA_CONTINUE;
		}
	}

	switch (aa->aa_type) {

	case AA_PASS:
	case AA_BLOCK:
	case AA_CONTINUE:
	case AA_DISCARD:
	case AA_DELAY:
		return aa->aa_type;

	case AA_JUMP:
		return acl(aa->aa_jump, attrs);

	default:
		break;
	}

	log_die(EX_SOFTWARE, "acl: unhandled action");
	return AA_ERROR;	/* Never reached */
}
