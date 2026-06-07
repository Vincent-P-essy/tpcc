%{
#include "tree.h"
#include "tpcc.tab.h"
#include <stdlib.h>
#include <string.h>

int lineno = 1;
int colno  = 0;
%}
%x COMMENT
%option nounput
%option noinput
%option noyywrap

%%
\/\*                    { BEGIN COMMENT; }
<COMMENT>\n             { lineno++; colno = 0; }
<COMMENT>.              { colno++; }
<COMMENT>\*\/           { colno += 2; BEGIN INITIAL; }
\/\/.*                  { /* commentaire fin de ligne */ }

void                    { colno += 4; return VOID; }
if                      { colno += 2; return IF; }
else                    { colno += 4; return ELSE; }
while                   { colno += 5; return WHILE; }
return                  { colno += 6; return RETURN; }
struct                  { colno += 6; return STRUCT; }

==|!=                   { colno += 2; yylval.str = strdup(yytext); return EQ; }
\<|\<=|\>|\>=           { colno += yyleng; yylval.str = strdup(yytext); return ORDER; }
[+-]                    { colno++; yylval.str = strdup(yytext); return ADDSUB; }
[*/%]                   { colno++; yylval.str = strdup(yytext); return DIVSTAR; }
\|\|                    { colno += 2; return OR; }
&&                      { colno += 2; return AND; }

'[a-zA-Z0-9!"#$%&*+,\-./:;=?@^_`~()\<\>\[\]{}| ]'|'\\n'|'\\t'|'\\r'|'\\''|'\\\\' {
                          colno += yyleng;
                          yylval.str = strdup(yytext);
                          return CHARACTER; }

[0-9]+                  { colno += yyleng; yylval.num = atoi(yytext); return NUM; }
int|char                { colno += yyleng; yylval.str = strdup(yytext); return TYPE; }
[a-zA-Z_][a-zA-Z_0-9]* { colno += yyleng; yylval.str = strdup(yytext); return IDENT; }

=|!                     { colno++; return yytext[0]; }
;|,|\.                  { colno++; return yytext[0]; }
\(|\)                   { colno++; return yytext[0]; }
\{|\}                   { colno++; return yytext[0]; }

\n                      { lineno++; colno = 0; }
[ \t\r]+                { colno += yyleng; }
.                       { colno++;
                          fprintf(stderr, "Erreur lexicale ligne %d, colonne %d: caractère inattendu '%s'\n",
                                  lineno, colno, yytext);
                          return yytext[0]; }
%%
