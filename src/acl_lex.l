%option nomain
%option noyywrap
%option nounput
%option noinput

define		define
continue	continue
reject		reject
discard		discard
accept		accept
tempfail	tempfail
greylist	greylist
tarpit		tarpit
log		log
set		set
valid		valid
visa		visa

eq		==
ne		!=
le		<=
ge		>=
and		&&
or		\|\|

op		[-+*/!()<>=,]
multiplier	[smhdwMy]

id		[a-zA-Z][a-zA-Z0-9_-]+
variable	\$[a-zA-Z][a-zA-Z0-9_-]+
integer		[+-]?[0-9]+
float		[+-]?[0-9]+\.[0-9]+
string		\"[^\"]*\"
addr		(([0-9]{1,3}\.){3}[0-9]{1,3}|[0-9a-fA-F:]{1,34}:[0-9a-fA-F]{1,4})

comment		#.*$

%{

#include "mopher.h"
#include "acl_yacc.h"

#define acl_error parser_error

%}

%%

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
{valid}		return VALID;
{visa}		return VISA;
{eq}		return EQ;
{ne}		return NE;
{le}		return LE;
{ge}		return GE;
{and}		return AND;
{or}		return OR;

{op}		return acl_text[0];
{multiplier}	return acl_text[0];

{integer}	return parser_tok_int(INTEGER, &acl_lval.i, acl_text);
{float}		return parser_tok_float(FLOAT, &acl_lval.d, acl_text);
{string}	return parser_tok_encstr(STRING, &acl_lval.str, acl_text);
{addr}		return parser_tok_addr(ADDR, &acl_lval.ss, acl_text);
{id}		return parser_tok_str(ID, &acl_lval.str, acl_text);
{variable}	return parser_tok_str(VARIABLE, &acl_lval.str, acl_text + 1);

{comment}	;
[ \t]		;
\n		parser_line();

.		parser_error("unknown token '%s'", acl_text);

%%
