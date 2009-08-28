%{

#include <stdio.h>
#include <arpa/inet.h>

#include "log.h"
#include "ll.h"
#include "var.h"
#include "cf.h"

extern int cf_line;
int cf_lex(void);
int cf_error(char *);
extern var_t *cf_config;

%}

%union {
	char	*str;
	var_t	*v;
	ll_t	*ll;
}

%token <str>	ID
%token <v>	SCALAR

%type <v>	value table list
%type <ll>	key

%%

statements	: statements statement
		| statement
		;

statement	: key '=' value
	  	  {
			cf_set(NULL, $1, $3);
		  }
		;

key		: key '[' ID ']'
     		  {
			if(LL_ENQUEUE($$, $3) == -1) {
				log_die(EX_CONFIG, "cf_yacc.y: LL_INSERT failed");
			}
		  }
      		| ID
     		  {
			if (($$ = ll_create()) == NULL) {
				log_die(EX_CONFIG, "cf_yacc.y: ll_create failed");
			}

			if(LL_ENQUEUE($$, $1) == -1) {
				log_die(EX_CONFIG, "cf_yacc.y: LL_INSERT failed");
			}
		  }
		;

value		: '{' table '}'
       		  {
			$$ = $2;
		  }

		| '(' list ')'
       		  {
			$$ = $2;
		  }

		| SCALAR
		;

list		: list ',' value
		  {
			if(var_list_append($$, $3)) {
				log_die(EX_CONFIG, "cf_lex.l: var_list_insert failed");
			}
		  }
       		| value
		  {
			if(($$ = var_create(VT_LIST, NULL, NULL, VF_CREATE)) == NULL) {
				log_die(EX_CONFIG, "cf_lex.l: var_create failed");
			}

			if(var_list_append($$, $1)) {
				log_die(EX_CONFIG, "cf_lex.l: var_list_insert failed");
			}
		  }
		;

table		: table ',' ID '=' value
		  {
			$5->v_name = $3;
			if(var_table_insert($$, $5)) {
				log_die(EX_CONFIG, "cf_lex.l: var_list_insert failed");
			}
		  }
       		| ID '=' value
		  {
			if(($$ = var_create(VT_TABLE, NULL, NULL, VF_CREATE)) == NULL) {
				log_die(EX_CONFIG, "cf_lex.l: var_create failed");
			}

			$3->v_name = $1;
			if(var_table_insert($$, $3)) {
				log_die(EX_CONFIG, "cf_lex.l: var_list_insert failed");
			}
		  }
		;
