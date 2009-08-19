%option nomain
%option noyywrap
%option nounput
%option noinput

default		default
jump		jump
pass		pass
block		block
discard		discard
delay		delay
continue	continue
visa		visa
valid		valid
any		any
and		and
or		or

eq		=
ne		!=
lt		<
gt		>
le		<=
ge		>=

ob		\(
cb		\)
comma		,

not		(not|!)
id		[a-zA-Z][a-zA-Z0-9_-]+
integer		[+-]?[0-9]+
float		[+-]?[0-9]+\.[0-9]+
multiplier	[KkMmGgHhDdWw]
string		\"[^\"]*\"
ip4		[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}
ip6		[0-9a-fA-F:]{1,34}:[0-9a-fA-F]{1,4}

comment		#.*$

%{

#include <stdio.h>
#include <arpa/inet.h>

#include "log.h"
#include "acl.h"

#include "acl_yacc.h"

int acl_line = 1;

void
acl_error(const char *s)
{
	log_die(EX_CONFIG, "%s on line %d\n", s, acl_line);
}

%}
%%
{default}	return DEFAULT;
{jump}		return JUMP;
{visa}		return VISA;
{valid}		return VALID;
{any}		return ANY;
{not}		return NOT;
{comma}		return COMMA;
{ob}		return OB;
{cb}		return CB;

{pass}		{
			acl_lval.at = AA_PASS;
			return PASS;
		}

{block}		{
			acl_lval.at = AA_BLOCK;
			return BLOCK;
		}

{continue}	{
			acl_lval.at = AA_CONTINUE;
			return CONTINUE;
		}

{discard}	{
			acl_lval.at = AA_DISCARD;
			return DISCARD;
		}

{delay}		{
			acl_lval.at = AA_DELAY;
			return DELAY;
		}

{and}		{
			acl_lval.ga = AG_AND;
			return AND;
		}

{or}		{
			acl_lval.ga = AG_OR;
			return OR;
		}

{integer}	{
			acl_lval.i = atol(acl_text);
			return INTEGER;
		}

{float}		{
			acl_lval.d = atof(acl_text);
			return FLOAT;
		}

{multiplier}	{
			acl_lval.c = acl_text[0];
			return MULTIPLIER;
		}

{string}	{
			if((acl_lval.str = strdup(acl_text + 1)) == NULL) {
				log_die(EX_CONFIG, "acl_lex.l: strdup");
			}
			acl_lval.str[strlen(acl_lval.str) - 1] = 0;
			return STRING;
		}

{ip4}		{
			struct sockaddr_in *sin;

			if((acl_lval.ss = (struct sockaddr_storage *)
			    malloc(sizeof(struct sockaddr_storage))) == NULL) {
				log_die(EX_CONFIG, "acl_lex.l: malloc");
			}

			sin = (struct sockaddr_in *) acl_lval.ss;

			if(inet_pton(AF_INET, acl_text, &sin->sin_addr) != 1) {
				log_die(EX_CONFIG,
				    "acl_lex.l: inet_pton failed");
			}

			acl_lval.ss->ss_family = AF_INET;

			return IP4;
		}

{ip6}		{
			struct sockaddr_in *sin;

			if((acl_lval.ss = (struct sockaddr_storage *)
			    malloc(sizeof(struct sockaddr_storage))) == NULL) {
				log_die(EX_CONFIG, "acl_lex.l: malloc");
			}

			sin = (struct sockaddr_in *) acl_lval.ss;

			if(inet_pton(AF_INET6, acl_text, &sin->sin_addr) != 1)
			{
				log_die(EX_CONFIG,
				    "acl_lex.l: inet_pton failed");
			}

			acl_lval.ss->ss_family = AF_INET6;

			return IP6;
		}

{id}		{
			if((acl_lval.str = strdup(acl_text)) == NULL) {
				log_die(EX_CONFIG, "acl_lex.l: strdup");
			}
			return ID;
		}

{eq}		{
			acl_lval.cm = AC_EQ;
			return EQ;
		}

{ne}		{
			acl_lval.cm = AC_NE;
			return NE;
		}

{lt}		{
			acl_lval.cm = AC_LT;
			return LT;
		}

{le}		{
			acl_lval.cm = AC_LE;
			return LE;
		}

{gt}		{
			acl_lval.cm = AC_GT;
			return GT;
		}

{ge}		{
			acl_lval.cm = AC_GE;
			return GE;
		}

{comment}	;
[ \t]		;
\n		++acl_line;

.		{
			log_die(EX_CONFIG, "unknown token '%s' on line %d\n",
			    yytext, acl_line);
		}
%%
