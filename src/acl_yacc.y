%{

#include <stdio.h>
#include "mopher.h"

#define acl_error parser_error

int acl_lex(void);

%}

%token ID INTEGER FLOAT STRING ADDR VARIABLE CONTINUE XREJECT DISCARD ACCEPT
%token TEMPFAIL GREYLIST VISA DEADLINE DELAY ATTEMPTS TARPIT SET LOG LEVEL 
%token EQ NE LE GE AND OR DEFINE ADD HEADER VALUE INSERT CHANGE INDEX FROM
%token ESMTP RCPT JUMP BODY SIZE DELETE REPLY XCODE MSG IS_NULL PIPE IS_SET
%token REGEX

%union {
	char			 c;
	char			*str;
	long			 i;
	double			 d;
	struct sockaddr_storage *ss;
	exp_t 			*exp;
	acl_action_t		*aa;
	acl_reply_t		*ar;
	greylist_t		*gl;
	acl_log_t		*al;
	msgmod_t		*mm;
}

%type <str>	STRING ID VARIABLE REGEX jump
%type <i>	INTEGER number
%type <d>	FLOAT
%type <ss>	ADDR
%type <exp>	exp function symbol constant set tarpit pipe variable macro regex
%type <aa>	action terminal
%type <ar>	reply
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

action		: terminal
		| terminal reply	{ $$ = acl_action_reply($1, $2); }
		;

terminal	: CONTINUE		{ $$ = acl_action(ACL_CONTINUE, NULL); }
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
		| pipe			{ $$ = acl_action(ACL_PIPE, $1); }
		;


reply		: REPLY exp MSG exp
					{ $$ = acl_reply($2, NULL, $4); }
		| REPLY exp XCODE exp MSG exp
					{ $$ = acl_reply($2, $4, $6); }
		;


greylist	: greylist VISA exp	{ $$ = greylist_visa($1, $3); }
		| greylist DEADLINE exp	{ $$ = greylist_deadline($1, $3); }
		| greylist DELAY exp	{ $$ = greylist_delay($1, $3); }
		| greylist ATTEMPTS exp	{ $$ = greylist_attempts($1, $3); }
		| GREYLIST		{ $$ = greylist_create(); }
		;

tarpit		: TARPIT exp		{ $$ = $2; }
		;

set		: SET VARIABLE '=' exp
					{ $$ = exp_operation('=', exp_create(EX_VARIABLE, $2), $4); }
		;

jump		: JUMP ID		{ $$ = $2; }

log		: log LEVEL exp		{ $$ = acl_log_level($1, $3); }
		| LOG exp		{ $$ = acl_log_create($2); }
		;

pipe		: PIPE exp		{ $$ = $2; }
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
		| DELETE HEADER exp
					{ $$ = msgmod_create(MM_DELHDR, $3, NULL); }
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
		| CHANGE BODY exp
					{ $$ = msgmod_create(MM_CHGBODY, $3, NULL); }
		;

exp		: '(' exp ')'		{ $$ = exp_parentheses($2); }
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
		| exp IS_NULL		{ $$ = exp_operation(IS_NULL, $1, NULL); }
		| IS_SET symbol		{ $$ = exp_operation(IS_SET, $2, NULL); }
		| exp '~' regex		{ $$ = exp_operation('~', $1, $3); }
		| variable
		| constant
		| symbol
		| function
		| macro
		;

function	: ID '(' exp ')'	{ $$ = exp_function($1, $3); }
		| ID '(' ')'		{ $$ = exp_function($1, NULL); }
		;

symbol		: ID 			{ $$ = exp_symbol($1); }
		;

variable	: VARIABLE		{ $$ = exp_create(EX_VARIABLE, $1); }
		;

macro		: '{' ID '}'		{ $$ = exp_create(EX_MACRO, $2); }
		;

regex		: REGEX 		{ $$ = exp_regex($1); }
		;

constant	: STRING		{ $$ = exp_constant(VT_STRING, $1, VF_REF); }
		| number		{ $$ = exp_constant(VT_INT, &$1, VF_COPY); }
		| FLOAT			{ $$ = exp_constant(VT_FLOAT, &$1, VF_COPY); }
		| ADDR			{ $$ = exp_constant(VT_ADDR, $1, VF_REF); }
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
