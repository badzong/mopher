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
call		call
delete		delete
reply		reply
xcode		xcode
message		message
pipe		pipe
isset		isset
in		in
null		null
is		is
return          return

eq		==
ne		!=
le		<=
ge		>=
and		&&
or		\|\|
nr		!~

op		[-+*/!()<>=,~%?:]
multiplier	[smhdwMy]
cbr		[{}]

id		[a-zA-Z_][a-zA-Z0-9_-]*
variable	\$[a-zA-Z][a-zA-Z0-9_-]+
integer		[+-]?[0-9]+
float		[+-]?[0-9]+\.[0-9]+
string		\"[^\"]*\"
addr		(([0-9]{1,3}\.){3}[0-9]{1,3}|([0-9a-fA-F]{0,4}:[0-9a-fA-F]{0,4}:[0-9a-fA-F:]{0,29}))
macro		\{[^}]+\}

comment		#.*$

%x incl

%{

#include "mopher.h"
#include "acl_yacc.h"

#define acl_error acl_parser_error

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
{call}		return CALL;
{delete}	return DELETE;
{reply}		return REPLY;
{xcode}		return XCODE;
{message}	return MSG;
{pipe}		return PIPE;
{isset}		return IS_SET;
{in}		return IN;
{is}		return IS;
{null}		return XNULL;   /* NULL */
{return}	return XRETURN; /* RETURN */

{op}		return acl_text[0];
{multiplier}	return acl_text[0];
{cbr}		return acl_text[0];

{integer}	return parser_tok_int(INTEGER, &acl_lval.i, acl_text);
{float}		return parser_tok_float(FLOAT, &acl_lval.d, acl_text);
{string}	return parser_tok_encstr(STRING, &acl_lval.str, acl_text);
{addr}		return parser_tok_addr(ADDR, &acl_lval.ss, acl_text);
{id}		return parser_tok_str(ID, &acl_lval.str, acl_text);
{variable}	return parser_tok_str(VARIABLE, &acl_lval.str, acl_text + 1);
{macro}		return parser_tok_str(MACRO, &acl_lval.str, acl_text);

{comment}	;
[ \t]		;
\n		parser_next_line(&acl_parser);

.		acl_parser_error("unknown token '%s' in %s on line %d", acl_text, parser_current_file(&acl_parser), parser_current_line(&acl_parser));

<incl>[ \t]*	/* eat the whitespace */
<incl>[^ \t\n]+	{ /* got the include file name */
	yyin = fopen(yytext, "r");
	if (!yyin)
	{
		acl_parser_error("Failed to open %s: %s", yytext, strerror(errno));
	}

	parser_stack(&acl_parser, YY_CURRENT_BUFFER, yytext);
	yy_switch_to_buffer(yy_create_buffer(yyin, YY_BUF_SIZE));
	BEGIN(INITIAL);
}
     
<<EOF>> {
	yy_delete_buffer(parser_current_yy_buffer(&acl_parser));
	if (parser_pop(&acl_parser) == 0)
	{
		yyterminate();
	}
	else
	{
		yy_switch_to_buffer(parser_current_yy_buffer(&acl_parser));
	}
}

%%
