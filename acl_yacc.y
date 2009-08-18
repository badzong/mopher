%{

#include <stdio.h>

#include "var.h"
#include "acl.h"
#include "log.h"
#include "cf.h"

extern int acl_line;
int acl_lex(void);
int acl_error(char *);

%}

%token DEFAULT JUMP PASS BLOCK DELAY CONTINUE DISCARD VISA VALID ANY COMMA OB
%token CB AND OR NOT INTEGER FLOAT IP4 IP6 MULTIPLIER STRING ID EQ NE LT GT LE
%token GE

%union {
	char			 c;
	char			*str;
	long			 i;
	double			 d;
	var_t			*v;
	ll_t			*ll;
	acl_cmp_t		 cm;
	acl_gate_t		 ga;
	acl_value_t		*va;
	acl_condition_t		*co;
	acl_action_type_t	 at;
	acl_action_t		*ac;
	acl_delay_t		*ad;
	struct sockaddr_storage *ss;
}

%type <c>	MULTIPLIER
%type <str>	STRING ID jump
%type <i>	INTEGER integer size period
%type <d>	FLOAT
%type <v>	constant
%type <ll>	conditions parameters
%type <cm>	EQ NE LT LE GT GE comparator
%type <ga>	AND OR gate
%type <va>	value attribute function
%type <co>	condition
%type <at>	PASS BLOCK DISCARD CONTINUE DELAY JUMP terminal
%type <ad>	delay
%type <ac>	action
%type <ss>	IP6 IP4 addr

%%

statements	: statements statement
		| statement
		;


statement	: setting
		| rule
		;


setting		: ID DEFAULT action
		  { 
			acl_table_t *at;

			if ((at = acl_table_lookup($1))) {
				at->at_default = $3;
				free($1);
			}
			else {
				if (acl_table_register($1, $3) == NULL) {
					log_die(EX_CONFIG, "acl_yacc.y: "
					    "acl_table_register failed");
				}
			}
		  }
		;


rule		: ID conditions action
		  {
			acl_table_t *at;

			if ((at = acl_table_lookup($1)) == NULL) {
				if ((at = acl_table_register($1, NULL)) ==
				   NULL) {
					log_die(EX_CONFIG, "acl_yacc.y: "
					    "acl_table_register failed");
				}
			}
			else {
				free($1);
			}

			if (acl_rule_register(at, $2, $3)) {
				log_die(EX_CONFIG, "acl_yacc.y: "
				    "acl_rule_register failed");
			}
		  }
		;


conditions	: conditions gate condition
		  {
			$3->ac_gate = $2;

			if (LL_INSERT($$, $3) == -1) {
				log_die(EX_CONFIG, "acl_yacc.y: LL_INSERT "
				    "failed");
			}
		  }

		| condition
		  {
			if (($$ = ll_create()) == NULL) {
				log_die(EX_CONFIG, "acl_yacc.y: ll_create "
				    "failed");
			}

			if (LL_INSERT($$, $1) == -1) {
				log_die(EX_CONFIG, "acl_yacc.y: LL_INSERT "
				    "failed");
			}
		  }
		;


gate		: AND
		| OR
		;

condition	: NOT condition
		  {
			$$ = $2;
			$$->ac_not ^= AN_NOT;
		  }

		| value comparator value
		  {
			if (($$ = acl_condition_create(AN_NULL, AG_NULL, $2,
			    $1, $3)) == NULL) {
				log_die(EX_CONFIG, "acl_yacc.y: "
				    "acl_condition_create failed");
			}
		  }

		| value
		  {
			if (($$ = acl_condition_create(AN_NULL, AG_NULL,
			    AC_NULL, $1, NULL)) == NULL) {
				log_die(EX_CONFIG, "acl_yacc.y: "
				    "acl_condition_create failed");
			}
		  }
		;


comparator	: EQ
		| NE
		| LT
		| GT
		| LE
		| GE
		;


value		: constant
		  {
			acl_value_data_t vd;

			vd.vd_var = $1;

			if (($$ = acl_value_create(AV_CONST, vd)) == NULL) {
				log_die(EX_CONFIG, "acl_yacc.y: "
				    "acl_value_create_constant failed");
			}
		  }

		| attribute
		| function
		;


attribute	: ID
		  {
			if (($$ = acl_value_create_attribute($1)) == NULL) {
				log_die(EX_CONFIG, "unknown attribute \"%s\" "
				    "on line: %d", $1, acl_line);
			}

			free($1);
		  }
		;


function	: ID OB parameters CB
		  {
			if (($$ = acl_value_create_function($1, $3)) == NULL) {
				log_die(EX_CONFIG, "unknown function \"%s\" "
				    "on line: %d", $1, acl_line);
			}

			free($1);
		  }
		;


parameters	: parameters COMMA value
		  {
			if (LL_INSERT($$, $3) == -1) {
				log_die(EX_CONFIG, "acl_yacc.y: LL_INSERT "
				    "failed");
			}
		  }

		| value
		  {
			if (($$ = ll_create()) == NULL) {
				log_die(EX_CONFIG, "acl_yacc.y: ll_create "
				    "failed");
			}

			if (LL_INSERT($$, $1) == -1) {
				log_die(EX_CONFIG, "acl_yacc.y: LL_INSERT "
				    "failed");
			}
		  }
		;


constant	: STRING
		  {
			if (($$ = var_create_reference(VT_STRING, NULL, $1)) ==
			    NULL) {
				log_die(EX_CONFIG, "acl_yacc.y: "
				    "var_create_reference failed"); 
			}
		  }

		| FLOAT
		  {
			if (($$ = var_create_copy(VT_FLOAT, NULL, &$1)) ==
			    NULL) {
				log_die(EX_CONFIG, "acl_yacc.y: "
				    "var_create_copy failed"); 
			}
		  }

		| integer
		  {
			if (($$ = var_create_copy(VT_INT, NULL, &$1)) ==
			    NULL) {
				log_die(EX_CONFIG, "acl_yacc.y: "
				    "var_create_copy failed"); 
			}
		  }

		| addr
		  {
			if (($$ = var_create_reference(VT_ADDR, NULL, $1)) ==
			    NULL) {
				log_die(EX_CONFIG, "acl_yacc.y: "
				    "var_create_reference failed"); 
			}
		  }
		;


integer		: INTEGER
		| size
		;


size		: INTEGER MULTIPLIER
		  {
			switch($2) {
			case 'G':
			case 'g':
				$$ = $1 * 1024 * 1024 * 1024;
				break;

			case 'M':
			case 'm':
				$$ = $1 * 1024 * 1024;
				break;

			case 'K':
			case 'k':
				$$ = $1 * 1024;
				break;

			default:
				acl_error("bad size unit");
			}
		  }
		;


addr		: IP4
		| IP6
		;


action		: delay
		  {
			if (($$ = acl_action_create(AA_DELAY, NULL, $1)) ==
			    NULL) {
				log_die(EX_CONFIG, "acl_yacc.y: "
				    "acl_action_create failed");
			}
		  }

		| jump
		  {
			if (($$ = acl_action_create(AA_JUMP, $1, NULL)) ==
			    NULL) {
				log_die(EX_CONFIG, "acl_yacc.y: "
				    "acl_action_create failed");
			}
		  }

		| terminal
		  {
			if (($$ = acl_action_create($1, NULL, NULL)) == NULL) {
				log_die(EX_CONFIG, "acl_yacc.y: "
				    "acl_action_create failed");
			}
		  }
			;

terminal	: PASS
		| BLOCK
		| DISCARD
		| CONTINUE
		;

jump		: JUMP ID
		  {
			$$ = $2;
		  }
		;


delay		: delay VALID period
		  {
			$$->ad_valid = $3;
		  }

		| delay VISA period
		  {
			$$->ad_visa = $3;
		  }

		| delay period
		  {
			$$->ad_delay = $2;
		  }

		| DELAY
		  {
			if (($$ = acl_delay_create(cf_greylist_default_delay,
			    cf_greylist_default_valid,
			    cf_greylist_default_visa)) == NULL) {
				log_die(EX_CONFIG, "acl_yacc.y: "
				    "acl_delay_create failed");
			}
		  }
		;


period		: INTEGER MULTIPLIER
		  {
			switch($2) {
			case 'y':
				$$ = $1 * 60 * 60 * 24 * 365;
				break;

			case 'M':
				$$ = $1 * 60 * 60 * 24 * 30;
				break;

			case 'w':
				$$ = $1 * 60 * 60 * 24 * 7;
				break;

			case 'd':
				$$ = $1 * 60 * 60 * 24;
				break;

			case 'h':
				$$ = $1 * 60 * 60;
				break;

			case 'm':
				$$ = $1 * 60;
				break;

			case 's':
				$$ = $1;
				break;

			default:
				acl_error("bad time unit");
			}
		  }

		| INTEGER
		;
