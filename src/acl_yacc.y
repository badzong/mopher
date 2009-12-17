%{

#include <stdio.h>
#include "mopher.h"

#define acl_error parser_error

int acl_lex(void);

%}

%token ID INTEGER FLOAT STRING ADDR VARIABLE CONTINUE XREJECT DISCARD ACCEPT
%token TEMPFAIL GREYLIST DELAY TARPIT SET LOG LEVEL VALID VISA MULTIPLIER EQ
%token NE LE GE AND OR DEFINE ADD HEADER VALUE INSERT CHANGE INDEX FROM ESMTP
%token RCPT JUMP BODY SIZE DELETE

%union {
	char			 c;
	char			*str;
	long			 i;
	double			 d;
	struct sockaddr_storage *ss;
	exp_t 			*exp;
	acl_action_t		*aa;
	greylist_t		*gl;
	acl_log_t		*al;
	msgmod_t		*mm;
}

%type <c>	MULTIPLIER
%type <str>	STRING ID VARIABLE jump
%type <i>	INTEGER number
%type <d>	FLOAT
%type <ss>	ADDR
%type <exp>	exp function symbol constant set tarpit
%type <aa>	action
%type <gl>	greylist
%type <al>	log
%type <mm>	mod

%left ','
%left AND OR
%left EQ NE LE GE '<' '>'
%left '+' '-'
%left '*' '/'
%right '='
%right '!'

%%

statements	: statements statement
		| statement
		;

statement	: ID exp action		{ acl_append($1, $2, $3); }
		| ID action		{ acl_append($1, NULL, $2); }
		| DEFINE ID exp		{ exp_define($2, $3); }
		;

action		: CONTINUE		{ $$ = acl_action(ACL_CONTINUE, NULL); }
		| XREJECT		{ $$ = acl_action(ACL_REJECT, NULL); }
		| DISCARD		{ $$ = acl_action(ACL_DISCARD, NULL); }
		| ACCEPT		{ $$ = acl_action(ACL_ACCEPT, NULL); }
		| TEMPFAIL		{ $$ = acl_action(ACL_TEMPFAIL, NULL); }
		| greylist		{ $$ = acl_action(ACL_GREYLIST, $1); }
		| tarpit		{ $$ = acl_action(ACL_TARPIT, $1); }
		| log			{ $$ = acl_action(ACL_LOG, $1); }
		| set			{ $$ = acl_action(ACL_SET, $1); }
		| mod			{ $$ = acl_action(ACL_MOD, $1); }
		| jump			{ $$ = acl_action(ACL_JUMP, $1); }
		;

greylist	: greylist VALID exp	{ $$ = greylist_valid($1, $3); }
		| greylist VISA exp	{ $$ = greylist_visa($1, $3); }
		| GREYLIST exp		{ $$ = greylist_create($2); }
		;

tarpit		: TARPIT exp		{ $$ = $2; }
		;

set		: SET VARIABLE '=' exp	{ $$ = exp_operation('=', exp_variable($2), $4); }
		;

jump		: JUMP ID		{ $$ = $2; }

log		: log LEVEL number	{ $$ = acl_log_level($1, $3); }
		| LOG exp		{ $$ = acl_log_create($2); }
		;

mod		: ADD HEADER exp VALUE exp
					{ $$ = msgmod_create(MM_ADDHDR, $3, $5, NULL); }
		| INSERT HEADER exp VALUE exp
					{ $$ = msgmod_create(MM_INSHDR, $3, $5, NULL); }
		| INSERT HEADER exp VALUE exp INDEX exp
					{ $$ = msgmod_create(MM_INSHDR_X, $3, $5, $7, NULL); }
		| CHANGE HEADER exp VALUE exp
					{ $$ = msgmod_create(MM_CHGHDR, $3, $5, NULL); }
		| CHANGE HEADER exp VALUE exp INDEX exp
					{ $$ = msgmod_create(MM_CHGHDR_X, $3, $5, $7, NULL); }
		| CHANGE FROM exp
					{ $$ = msgmod_create(MM_CHGFROM, $3, NULL); }
		| CHANGE FROM exp ESMTP exp
					{ $$ = msgmod_create(MM_CHGFROM_X, $3, $5, NULL); }
		| ADD RCPT exp
					{ $$ = msgmod_create(MM_ADDRCPT, $3, NULL); }
		| ADD RCPT exp ESMTP exp
					{ $$ = msgmod_create(MM_ADDRCPT_X, $3, $5, NULL); }
		| DELETE RCPT exp
					{ $$ = msgmod_create(MM_DELRCPT, $3, NULL); }
		| CHANGE BODY exp SIZE exp
					{ $$ = msgmod_create(MM_CHGBODY, $3, $5, NULL); }
		;

exp		: '(' exp ')'		{ $$ = $2; }
		| exp ',' exp		{ $$ = exp_list($1, $3); }
		| '!' exp		{ $$ = exp_operation('!', $2, NULL); }
		| exp '+' exp		{ $$ = exp_operation('+', $1, $3); }
		| exp '-' exp		{ $$ = exp_operation('-', $1, $3); }
		| exp '*' exp		{ $$ = exp_operation('*', $1, $3); }
		| exp '/' exp		{ $$ = exp_operation('/', $1, $3); }
		| exp '<' exp		{ $$ = exp_operation('<', $1, $3); }
		| exp '>' exp		{ $$ = exp_operation('>', $1, $3); }
		| exp '=' exp		{ $$ = exp_operation('=', $1, $3); }
		| exp EQ exp		{ $$ = exp_operation(EQ, $1, $3); }
		| exp NE exp		{ $$ = exp_operation(NE, $1, $3); }
		| exp GE exp		{ $$ = exp_operation(GE, $1, $3); }
		| exp LE exp		{ $$ = exp_operation(LE, $1, $3); }
		| exp AND exp		{ $$ = exp_operation(AND, $1, $3); }
		| exp OR exp		{ $$ = exp_operation(OR, $1, $3); }
		| VARIABLE		{ $$ = exp_variable($1); }
		| constant
		| symbol
		| function
		;

function	: ID '(' exp ')'	{ $$ = exp_function($1, $3); }
		;

symbol		: ID 			{ $$ = exp_symbol($1); }
		;

constant	: STRING		{ $$ = exp_constant(VT_STRING, $1); }
		| number		{ $$ = exp_constant(VT_INT, &$1); }
		| FLOAT			{ $$ = exp_constant(VT_FLOAT, &$1); }
		| ADDR			{ $$ = exp_constant(VT_ADDR, $1); }
		;

number		: INTEGER 's'		{ $$ = $1; }
		| INTEGER 'm'		{ $$ = $1 * 60; }
		| INTEGER 'h'		{ $$ = $1 * 60 * 60; }
		| INTEGER 'd'		{ $$ = $1 * 60 * 60 * 24; }
		| INTEGER 'B'		{ $$ = $1; }
		| INTEGER 'K'		{ $$ = $1 * 1024; }
		| INTEGER 'M'		{ $$ = $1 * 1024 * 1024; }
		| INTEGER 'G'		{ $$ = $1 * 1024 * 1024 * 1024; }
		| INTEGER 
		;
