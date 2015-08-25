%option nomain
%option noyywrap
%option nounput
%option noinput

include		include
define		define
continue	continue
reject		reject
discard		discard
accept		accept
tempfail	tempfail
greylist	greylist
tarpit		tarpit
log		log
level		level
set		set
deadline	deadline
visa		visa
delay		delay
attempts	attempts
add		add
value		value
insert		insert
change		change
index		index
esmtp		esmtp
jump		jump
delete		delete
reply		reply
xcode		xcode
message		message
pipe		pipe
isset		isset
in		in

eq		==
ne		!=
le		<=
ge		>=
and		&&
or		\|\|
nr		!~

op		[-+*/!()<>=,~%]
multiplier	[smhdwMy]
cbr		[{}]

/*
 * Hack: is NULL operator
 */
isnull		==[ \t]*null

id		[a-zA-Z_][a-zA-Z0-9_-]*
variable	\$[a-zA-Z][a-zA-Z0-9_-]+
integer		[+-]?[0-9]+
float		[+-]?[0-9]+\.[0-9]+
string		\"[^\"]*\"
addr		(([0-9]{1,3}\.){3}[0-9]{1,3}|[0-9a-fA-F:]{2,39})
macro		\{[^}]+\}

comment		#.*$

%x incl

%{

#include "mopher.h"
#include "acl_yacc.h"

#define acl_error parser_error

YY_BUFFER_STATE acl_include_stack[ACL_INCLUDE_DEPTH];

%}

%%

{include}	BEGIN(incl);

{define}	return DEFINE;
{continue}	return CONTINUE;
{reject}	return XREJECT; /* REJECT defined by flex */
{discard}	return DISCARD;
{accept}	return ACCEPT;
{tempfail}	return TEMPFAIL;
{greylist}	return GREYLIST;
{tarpit}	return TARPIT;
{set}		return SET;
{log}		return LOG;
{level}		return LEVEL;
{deadline}	return DEADLINE;
{visa}		return VISA;
{delay}		return DELAY;
{attempts}	return ATTEMPTS;
{eq}		return EQ;
{ne}		return NE;
{le}		return LE;
{ge}		return GE;
{and}		return AND;
{or}		return OR;
{nr}		return NR;
{add}		return ADD;
{value}		return VALUE;
{insert}	return INSERT;
{change}	return CHANGE;
{index}		return INDEX;
{esmtp}		return ESMTP;
{jump}		return JUMP;
{delete}	return DELETE;
{reply}		return REPLY;
{xcode}		return XCODE;
{message}	return MSG;
{pipe}		return PIPE;
{isset}		return IS_SET;
{in}		return IN;

{op}		return acl_text[0];
{multiplier}	return acl_text[0];
{cbr}		return acl_text[0];
{isnull}	return IS_NULL;

{integer}	return parser_tok_int(INTEGER, &acl_lval.i, acl_text);
{float}		return parser_tok_float(FLOAT, &acl_lval.d, acl_text);
{string}	return parser_tok_encstr(STRING, &acl_lval.str, acl_text);
{addr}		return parser_tok_addr(ADDR, &acl_lval.ss, acl_text);
{id}		return parser_tok_str(ID, &acl_lval.str, acl_text);
{variable}	return parser_tok_str(VARIABLE, &acl_lval.str, acl_text + 1);
{macro}		return parser_tok_str(MACRO, &acl_lval.str, acl_text);

{comment}	;
[ \t]		;
\n		parser_line();

.		parser_error("unknown token '%s'", acl_text);

<incl>[ \t]*	/* eat the whitespace */
<incl>[^ \t\n]+	{ /* got the include file name */
	acl_include_stack[parser_stack_ptr] = YY_CURRENT_BUFFER;
	parser_stack(yytext);

	yyin = fopen(yytext, "r");
	if (! yyin)
	{
		parser_error("Failed to open %s: %s", yytext, strerror(errno));
	}
     
	yy_switch_to_buffer(yy_create_buffer(yyin, YY_BUF_SIZE));
     
	BEGIN(INITIAL);
}
     
<<EOF>> {
	if (parser_pop() < 0)
	{
		yyterminate();
	}
	else
	{
		yy_delete_buffer(YY_CURRENT_BUFFER);
		yy_switch_to_buffer(acl_include_stack[parser_stack_ptr]);
	}
}

%%
