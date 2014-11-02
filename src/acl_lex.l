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
level		level
set		set
deadline	deadline
visa		visa
delay		delay
attempts	attempts
add		add
header		header
value		value
insert		insert
change		change
index		index
from		from
esmtp		esmtp
jump		jump
rcpt		rcpt
body		body
size		size
delete		delete
reply		reply
xcode		xcode
message		message
pipe		pipe
isset		isset

eq		==
ne		!=
le		<=
ge		>=
and		&&
or		\|\|

op		[-+*/!()<>=,]
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
{add}		return ADD;
{header}	return HEADER;
{value}		return VALUE;
{insert}	return INSERT;
{change}	return CHANGE;
{index}		return INDEX;
{from}		return FROM;
{esmtp}		return ESMTP;
{rcpt}		return RCPT;
{jump}		return JUMP;
{body}		return BODY;
{size}		return SIZE;
{delete}	return DELETE;
{reply}		return REPLY;
{xcode}		return XCODE;
{message}	return MSG;
{pipe}		return PIPE;
{isset}		return IS_SET;

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

{comment}	;
[ \t]		;
\n		parser_line();

.		parser_error("unknown token '%s'", acl_text);

%%
