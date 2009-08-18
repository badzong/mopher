#include <stdio.h>
#include <malloc.h>
#include <dlfcn.h>
#include <glob.h>
#include <string.h>

#include "var.h"
#include "ht.h"
#include "ll.h"
#include "log.h"
#include "cf.h"
#include "parser.h"
#include "modules.h"
#include "acl.h"
#include "acl_yacc.h"

#define BUFLEN 1024

/*
 * External declarations
 */

extern FILE *acl_in;
extern int acl_parse(void);

/*
 * Globals
 */
static ht_t *acl_tables;
static ht_t *acl_symbols;

static void
acl_symbol_delete(acl_symbol_t * as)
{
	free(as);
}

static acl_symbol_t *
acl_symbol_create(acl_symbol_type_t type, char *name, acl_callback_t callback)
{
	acl_symbol_t *as = NULL;

	if ((as = (acl_symbol_t *) malloc(sizeof(acl_symbol_t))) == NULL) {
		log_warning("acl_symbol_create: malloc");
		goto error;
	}

	as->as_type = type;
	as->as_name = name;
	as->as_callback = callback;

	return as;

error:

	if (as) {
		acl_symbol_delete(as);
	}

	return NULL;
}

static int
acl_symbol_match(acl_symbol_t * as1, acl_symbol_t * as2)
{
	if (as1->as_type != as2->as_type) {
		return 0;
	}

	if (strcmp(as1->as_name, as2->as_name)) {
		return 0;
	}

	return 1;
}

/*
 * acl_symbol_hash creates a hash value for the symbol name.
 *
 * THE CALCULATED HASH DOES NOT CONTAIN THE SYMBOL TYPE. IF A FUNCTION AND
 * AN ATTRIBUTE SHARE THE SAME NAME, THEY SHARE THE SAME BUCKET!
 */

static hash_t
acl_symbol_hash(acl_symbol_t * as)
{
	return HASH(as->as_name, strlen(as->as_name));
}

static acl_symbol_t *
acl_symbol_lookup(acl_symbol_type_t type, char *name)
{
	acl_symbol_t lookup, *as;

	lookup.as_type = type;
	lookup.as_name = name;

	if ((as = ht_lookup(acl_symbols, &lookup)) == NULL) {
		log_warning("acl_symbol_lookup: ht_lookup failed");
		return NULL;
	}

	return as;
}

int
acl_symbol_register(acl_symbol_type_t type, char *name, acl_callback_t callback)
{
	acl_symbol_t *as = NULL;

	if ((as = acl_symbol_create(type, name, callback)) == NULL) {
		log_warning("acl_symbol_regeister: acl_symbol_create failed");
		return -1;
	}

	if (ht_insert(acl_symbols, as)) {
		log_warning("acl_symbol_register: ht_insert failed");
		acl_symbol_delete(as);
		return -1;
	}

	log_debug("acl_symbol_register: \"%s\" registered", name);

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
acl_call_create(acl_function_t function, ll_t * args)
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
		var_delete(av->av_data.vd_var);
		break;

	case AV_FUNC:
		acl_call_delete(av->av_data.vd_call);
		break;

	default:
		break;
	}

	free(av);

	return;
}

acl_value_t *
acl_value_create(acl_value_type_t type, acl_value_data_t data)
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
acl_value_create_attribute(char *name)
{
	acl_symbol_t *as;
	acl_value_t *av;
	acl_value_data_t vd;

	if ((as = acl_symbol_lookup(AS_ATTR, name)) == NULL) {
		log_warning
		    ("acl_value_create_attribute: acl_symbol_lookup failed");
		return NULL;
	}

	vd.vd_attr = as->as_callback.ac_attribute;

	if ((av = acl_value_create(AV_ATTR, vd)) == NULL) {
		log_warning
		    ("acl_value_create_attribute: acl_value_create failed");
		return NULL;
	}

	return av;
}

acl_value_t *
acl_value_create_function(char *name, ll_t * args)
{
	acl_symbol_t *as;
	acl_call_t *ac = NULL;
	acl_value_t *av = NULL;
	acl_value_data_t vd;

	if ((as = acl_symbol_lookup(AS_FUNC, name)) == NULL) {
		log_warning
		    ("acl_value_create_function: acl_symbol_lookup failed");
		goto error;
	}

	if ((ac = acl_call_create(as->as_callback.ac_function, args)) == NULL) {
		log_warning
		    ("acl_value_create_function: acl_call_create failed");
		goto error;
	}

	vd.vd_call = ac;

	if ((av = acl_value_create(AV_FUNC, vd)) == NULL) {
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

/*
 * acl_value_t * acl_const_register( var_t *var) { acl_value_t *av = NULL;
 * 
 * if((av = acl_value_create(AV_CONST, NULL, var)) == NULL) {
 * log_warning("acl_const_register: acl_value_create failed"); goto error; }
 * 
 * if(LL_INSERT(acl_constants, av) == -1) { log_warning("acl_const_register:
 * ll_insert failed"); goto error; }
 * 
 * return av;
 * 
 * 
 * error:
 * 
 * if(av) { acl_value_delete(av); }
 * 
 * return NULL; } 
 */

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

	memset(ac, 0, sizeof(acl_condition_t));

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

	if (aa->aa_delay) {
		acl_delay_delete(aa->aa_delay);
	}
	free(aa);

	return;
}

acl_action_t *
acl_action_create(acl_action_type_t type, char *jump, acl_delay_t * delay)
{
	acl_action_t *aa = NULL;

	if ((aa = (acl_action_t *) malloc(sizeof(acl_action_t))) == NULL) {
		log_warning("acl_action_create: malloc");
		goto error;
	}

	aa->aa_type = type;
	aa->aa_jump = jump;
	aa->aa_delay = delay;

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

	if ((ad = (acl_delay_t *) malloc(sizeof(acl_delay_t))) == NULL) {
		log_warning("acl_delay_create: malloc");
		return NULL;
	}

	ad->ad_delay = delay;
	ad->ad_valid = valid;
	ad->ad_visa = visa;

	return ad;
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
acl_init(void)
{
	if ((acl_tables = HT_CREATE_STATIC(ACL_TABLE_BUCKETS,
					   (void *) acl_table_hash,
					   (void *) acl_table_match)) == NULL) {
		log_warning("acl_init: HT_CREATE_STATIC failed");
		return -1;
	}

	if ((acl_symbols = HT_CREATE_STATIC(ACL_SYMBOL_BUCKETS,
					    (void *) acl_symbol_hash,
					    (void *) acl_symbol_match)) ==
	    NULL) {
		log_warning("acl_init: HT_CREATE_STATIC failed");
		return -1;
	}

	/*
	 * Load modules
	 */
	modules_load(cf_acl_mod_path);

	/*
	 * Run parser
	 */
	parser(cf_acl_path, &acl_in, acl_parse);

	return 0;
}

void
acl_clear(void)
{
	ht_delete(acl_tables, (void *) acl_table_delete);
	ht_delete(acl_symbols, (void *) acl_symbol_delete);

	return;
}

/*
 * var_t * acl_function_eval(acl_value_t *func) { ll_t *args = NULL;
 * acl_value_t *av; var_t *v; acl_function_t callback;
 * 
 * callback = func->av_data->vd_call.ac_function;
 * 
 * if((args = ll_create()) == NULL) { log_error("acl_function_eval: ll_create
 * failed"); goto error; }
 * 
 * ll_rewind(ac->ac_args); while((av = ll_next(args))) { if((v =
 * acl_value_eval(av)) == NULL) { log_error("acl_function_eval: acl_value_eval
 * failed"); goto error; }
 * 
 * if(LL_INSERT(args, v) == -1) { log_error("acl_function_eval: LL_INSERT
 * failed"); goto error; } }
 * 
 * if((v = ac->ac_function(args)) == NULL) { log_error("acl_function_eval:
 * callback failed"); goto error; }
 * 
 * return v;
 * 
 * error:
 * 
 * if(args) { ll_delete(args, NULL); }
 * 
 * return NULL; } 
 */

var_t *
acl_function_eval(acl_value_t * av)
{
	ll_t *args = NULL;
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
	ll_rewind(av->av_data.vd_call->ac_args);
	while ((arg = ll_next(av->av_data.vd_call->ac_args))) {
		if ((v = acl_value_eval(arg)) == NULL) {
			log_error("acl_function_eval: acl_value_eval failed");
			goto error;
		}

		if (LL_INSERT(args, v) == -1) {
			log_error("acl_function_eval: LL_INSERT failed");
			goto error;
		}
	}

	v = av->av_data.vd_call->ac_function(args);

	return v;

error:
	if (args) {
		ll_delete(args, (void *) var_delete);
	}

	return NULL;
}

var_t *
acl_value_eval(acl_value_t * av)
{
	// ll_t *args;
	// acl_value_t *arg;
	// acl_value_t *rv;

	switch (av->av_type) {

	case AV_CONST:
		return av->av_data.vd_var;

	case AV_FUNC:
		return acl_function_eval(av);

	case AV_ATTR:
		printf(">> attribute\n");
		break;
		// rv = acl_function_eval();
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
acl_conditions_eval(ll_t * conditions)
{
	acl_condition_t *ac;
	var_t *left, *right;
	int r, ax;

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
		if ((left = acl_value_eval(ac->ac_left)) == NULL) {
			log_error("acl_conditions_eval: acl_value_eval returned"
				  " null");
			r = 0;
		}
		else if (ac->ac_right == NULL) {
			r = var_true(left) ^ ac->ac_not;
		}
		else {
			if ((right = acl_value_eval(ac->ac_right)) == NULL) {
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
	}

	return ax;
}

acl_action_type_t
acl(char *table, ht_t * attrs)
{
	acl_table_t *at;
	acl_action_t *aa;
	acl_rule_t *ar;
	int r;

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
	while ((ar = ll_next(at->at_rules))) {
		r = acl_conditions_eval(ar->ar_conditions);
		if (r == 0) {
			continue;
		}

		if (r == 1) {
			aa = ar->ar_action;
			break;
		}

		printf("r == %d\n", r);

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

	switch (aa->aa_type) {

	case AA_PASS:
	case AA_BLOCK:
	case AA_CONTINUE:
	case AA_DISCARD:
		return aa->aa_type;

	case AA_JUMP:
		return acl(aa->aa_jump, attrs);

	default:
		break;
	}

	log_die(EX_SOFTWARE, "acl: unhandled action");
	return AA_ERROR;	/* Never reached */
}
