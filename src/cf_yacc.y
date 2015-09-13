%{

#include <stdio.h>
#include <arpa/inet.h>

#include "mopher.h"

#define cf_error cf_parser_error

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

configuration	: /*empty*/
		| statements
		;

statements	: statements statement
		| statement
		;

statement	: key '=' value
	  	  {
			cf_set_keylist(NULL, $1, $3);
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

value		: '{' table '}'     { $$ = $2; }
		| '{' table ',' '}' { $$ = $2; }
		| '(' list ')'      { $$ = $2; }
		| '(' list ',' ')'  { $$ = $2; }
		| SCALAR
		;

list		: list ',' value
		  {
			if(vlist_append($$, $3) == -1) {
				log_die(EX_CONFIG, "cf_yacc.y: vlist_append failed");
			}
		  }
       		| value
		  {
			if(($$ = vlist_create(NULL, 0)) == NULL) {
				log_die(EX_CONFIG, "cf_yacc.y: vlist_create failed");
			}

			if(vlist_append($$, $1) == -1) {
				log_die(EX_CONFIG, "cf_yacc.y: vlist_append failed");
			}
		  }
		;

table		: table ',' ID '=' value
		  {
			$5->v_name = $3;
			if(vtable_insert($$, $5)) {
				log_die(EX_CONFIG, "cf_yacc.y: vtable_insert failed");
			}
		  }
       		| ID '=' value
		  {
			if(($$ = vtable_create(NULL, 0)) == NULL) {
				log_die(EX_CONFIG, "cf_yacc.y: vtable_create failed");
			}

			$3->v_name = $1;
			if(vtable_insert($$, $3)) {
				log_die(EX_CONFIG, "cf_yacc.y: vtable_insert failed");
			}
		  }
		;
