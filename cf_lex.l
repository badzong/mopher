%option nomain
%option noyywrap
%option nounput
%option noinput

id		[a-zA-Z][a-zA-Z0-9_-]+
integer		[+-]?[0-9]+
float		[+-]?[0-9]+\.[0-9]+
multiplier	[KkMmGgHhDdWw]
string		\"[^\"]*\"
ip4		[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}
ip6		[0-9a-fA-F:]{1,34}:[0-9a-fA-F]{1,4}
tt		[,=(){}\[\]]

comment		#.*$

%{

#include <stdio.h>
#include <arpa/inet.h>

#include "log.h"
#include "util.h"
#include "var.h"
#include "ll.h"
#include "cf.h"

#include "cf_yacc.h"

#undef YY_INPUT
#define YY_INPUT(b, r, n) (r = cf_yyinput(b, n))

extern int cf_line;

void
cf_error(const char *s)
{
	log_die(EX_CONFIG, "%s on line %d\n", s, cf_line);
}

%}
%%

{integer}	{
			VAR_INT_T i;
			var_t *v;

			i = atol(cf_text);
			if ((v = var_create(VT_INT, NULL, &i, VF_COPYDATA))
			    == NULL) {
				log_die(EX_CONFIG, "cf_lex.l: var_create failed");
			}

			cf_lval.v = v;
			return SCALAR;
		}

{float}		{
			VAR_FLOAT_T f;
			var_t *v;

			f = atof(cf_text);
			if ((v = var_create(VT_FLOAT, NULL, &f, VF_COPYDATA))
			    == NULL) {
				log_die(EX_CONFIG, "cf_lex.l: var_create failed");
			}

			cf_lval.v = v;
			return SCALAR;
		}

{string}	{
			char *str;
			var_t *v;

			if((str = util_strdupenc(cf_text, "\"\"")) == NULL) {
				log_die(EX_CONFIG, "cf_lex.l: util_strdupenc failed");
			}

			if ((v = var_create(VT_STRING, NULL, str, VF_REF))
			    == NULL) {
				log_die(EX_CONFIG, "cf_lex.l: var_create failed");
			}

			cf_lval.v = v;
			return SCALAR;
		}

{ip4}		|
{ip6}		{
			struct sockaddr_storage *ss;
			var_t *v;

			if((ss = util_strtoaddr(cf_text)) == NULL) {
				log_die(EX_CONFIG, "cf_lex.l: util_strtoaddr failed");
			}

			if ((v = var_create(VT_ADDR, NULL, ss, VF_REF))
			    == NULL) {
				log_die(EX_CONFIG, "cf_lex.l: var_create failed");
			}

			cf_lval.v = v;
			return SCALAR;
		}

{id}		{
			if((cf_lval.str = strdup(cf_text)) == NULL) {
				log_die(EX_CONFIG, "cf_lex.l: strdup");
			}

			return ID;
		}

{tt}		return yytext[0];
{comment}	;
[ \t]		;
\n		++cf_line;

.		{
			log_die(EX_CONFIG, "unknown token '%s' on line %d\n",
			    yytext, cf_line);
		}
%%
