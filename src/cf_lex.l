%option nomain
%option noyywrap
%option nounput
%option noinput

include		include
id		[a-zA-Z][a-zA-Z0-9_-]+
integer		[+-]?[0-9]+
float		[+-]?[0-9]+\.[0-9]+
string		\"[^\"]*\"
ip4		[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}
ip6		[0-9a-fA-F:]{1,34}:[0-9a-fA-F]{1,4}
tt		[,=(){}\[\]]

comment		#.*$

%x incl

%{

#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

#include "mopher.h"
#include "cf_yacc.h"

#define cf_error(...) parser_error(&cf_parser, __VA_ARGS__)

%}
%%

{include}	BEGIN(incl);

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
				log_sys_die(EX_CONFIG, "cf_lex.l: strdup");
			}

			return ID;
		}

{tt}		return yytext[0];
{comment}	;
[ \t]		;
\n		parser_next_line(&cf_parser);

.		cf_parser_error("unknown token '%s' in %s on line %d", yytext);

<incl>[ \t]*	/* eat the whitespace */
<incl>[^ \t\n]+	{ /* got the include file name */
	yyin = fopen(yytext, "r");
	if (!yyin)
	{
		cf_parser_error("Failed to open %s: %s", yytext, strerror(errno));
	}

	parser_stack(&cf_parser, YY_CURRENT_BUFFER, yytext);
	yy_switch_to_buffer(yy_create_buffer(yyin, YY_BUF_SIZE));
	BEGIN(INITIAL);
}
     
<<EOF>> {
	yy_delete_buffer(parser_current_yy_buffer(&cf_parser));
	if (parser_pop(&cf_parser) == 0)
	{
		yyterminate();
	}
	else
	{
		yy_switch_to_buffer(parser_current_yy_buffer(&cf_parser));
	}
}

%%
